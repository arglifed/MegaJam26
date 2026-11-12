// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamTrackGenerator.h"

#include "MegaJamFlowPawn.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Math/RandomStream.h"

namespace
{
	/** Parking spot for unused instances. Far below the floor and zero-scaled. */
	FTransform MakeHiddenInstanceTransform()
	{
		return FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -100000.0), FVector::ZeroVector);
	}

	void ConfigureISM(UInstancedStaticMeshComponent* ISM, USceneComponent* Parent, bool bBlockCollision, bool bGenerateOverlaps)
	{
		ISM->SetupAttachment(Parent);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetCollisionProfileName(bBlockCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
		if (!bBlockCollision)
		{
			ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		ISM->SetGenerateOverlapEvents(bGenerateOverlaps);
		ISM->SetCanEverAffectNavigation(false);
	}
}

AMegaJamTrackGenerator::AMegaJamTrackGenerator()
{
	PrimaryActorTick.bCanEverTick = true;

	TrackRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TrackRoot"));
	RootComponent = TrackRoot;

	FloorISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FloorISM"));
	WallISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WallISM"));
	TakeA_ObstaclesISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TakeA_ObstaclesISM"));
	TakeB_ObstaclesISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TakeB_ObstaclesISM"));
	SetPieceISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SetPieceISM"));
	PickupISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PickupISM"));
	PickupTakeB_ISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PickupTakeB_ISM"));
	WrapArchISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WrapArchISM"));

	ConfigureISM(FloorISM, TrackRoot, true, false);
	ConfigureISM(WallISM, TrackRoot, true, false);
	ConfigureISM(TakeA_ObstaclesISM, TrackRoot, true, true);
	ConfigureISM(TakeB_ObstaclesISM, TrackRoot, true, true);

	ConfigureISM(SetPieceISM, TrackRoot, false, false);
	ConfigureISM(PickupISM, TrackRoot, false, false);
	ConfigureISM(PickupTakeB_ISM, TrackRoot, false, false);
	ConfigureISM(WrapArchISM, TrackRoot, false, false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		BlockMesh = CubeFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		CylinderMesh = CylinderFinder.Object;
	}
}

void AMegaJamTrackGenerator::BeginPlay()
{
	Super::BeginPlay();

	CreatePools();

	if (!ObstacleMaterial && !DefaultObstacleMaterial.IsNull() && FPackageName::DoesPackageExist(DefaultObstacleMaterial.ToSoftObjectPath().GetLongPackageName()))
	{
		ObstacleMaterial = DefaultObstacleMaterial.LoadSynchronous();
	}

	if (ObstacleMaterial)
	{
		UMaterialInstanceDynamic* MaterialA = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		MaterialA->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		MaterialA->SetVectorParameterValue(TEXT("TakeColor"), TakeAColor);
		TakeA_ObstaclesISM->SetMaterial(0, MaterialA);

		UMaterialInstanceDynamic* MaterialB = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		MaterialB->SetScalarParameterValue(TEXT("TakeIndex"), 1.0f);
		MaterialB->SetVectorParameterValue(TEXT("TakeColor"), TakeBColor);
		TakeB_ObstaclesISM->SetMaterial(0, MaterialB);

		UMaterialInstanceDynamic* PickupMatA = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		PickupMatA->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		PickupMatA->SetVectorParameterValue(TEXT("TakeColor"), FLinearColor(1.0f, 0.85f, 0.2f, 1.0f));
		PickupISM->SetMaterial(0, PickupMatA);

		UMaterialInstanceDynamic* PickupMatB = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		PickupMatB->SetScalarParameterValue(TEXT("TakeIndex"), 1.0f);
		PickupMatB->SetVectorParameterValue(TEXT("TakeColor"), FLinearColor(0.25f, 0.95f, 1.0f, 1.0f));
		PickupTakeB_ISM->SetMaterial(0, PickupMatB);

		SetPieceMID = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		SetPieceMID->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		SetPieceMID->SetVectorParameterValue(TEXT("TakeColor"), CurrentSceneColor);
		SetPieceISM->SetMaterial(0, SetPieceMID);

		WrapArchMID = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		WrapArchMID->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		WrapArchMID->SetVectorParameterValue(TEXT("TakeColor"), FLinearColor(1.0f, 0.85f, 0.15f, 1.0f));
		WrapArchISM->SetMaterial(0, WrapArchMID);
	}

	if (AMegaJamGameMode* GameMode = GetWorld()->GetAuthGameMode<AMegaJamGameMode>())
	{
		BoundGameMode = GameMode;
		GameMode->OnRunStateChanged.AddUniqueDynamic(this, &AMegaJamTrackGenerator::HandleRunStateChanged);
	}

	BindToPawn();
	RebuildTrack();
}

void AMegaJamTrackGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundPawn)
	{
		BoundPawn->OnTakeCut.RemoveDynamic(this, &AMegaJamTrackGenerator::HandleTakeCut);
	}
	if (BoundGameMode)
	{
		BoundGameMode->OnRunStateChanged.RemoveDynamic(this, &AMegaJamTrackGenerator::HandleRunStateChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void AMegaJamTrackGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	BindToPawn();
	if (!BoundPawn)
	{
		return;
	}

	if (!bBuilt)
	{
		RebuildTrack();
		return;
	}

	// Film scene sequencing by run time.
	const float CurrentRunTime = GetRunTimeSeconds();
	const int32 SceneIndex = SceneIndexAtTime(CurrentRunTime);
	if (SceneIndex != LastBroadcastScene)
	{
		LastBroadcastScene = SceneIndex;
		CurrentScene = static_cast<EMegaJamFilmScene>(SceneIndex);

		// Notify GameMode when crossing into a new Act checkpoint (35s, 75s, 115s).
		if (BoundGameMode && BoundGameMode->IsPlaying() && SceneIndex > 0)
		{
			const float CheckpointStart = SceneEndTimes.IsValidIndex(SceneIndex - 1) ? SceneEndTimes[SceneIndex - 1] : 0.0f;
			BoundGameMode->NotifyCheckpointReached(CurrentScene, CheckpointStart);
		}

		// Update SetPieceISM mesh if a custom scene mesh is assigned.
		UStaticMesh* TargetSetMesh = BlockMesh;
		switch (CurrentScene)
		{
		case EMegaJamFilmScene::Soundstage:
			TargetSetMesh = SoundstageTrussMesh ? SoundstageTrussMesh.Get() : BlockMesh.Get();
			break;
		case EMegaJamFilmScene::OverpassNight:
			TargetSetMesh = SkyscraperMesh ? SkyscraperMesh.Get() : BlockMesh.Get();
			break;
		case EMegaJamFilmScene::CuttingRoom:
			TargetSetMesh = FilmReelMesh ? FilmReelMesh.Get() : (CylinderMesh ? CylinderMesh.Get() : BlockMesh.Get());
			break;
		case EMegaJamFilmScene::FinalCut:
			TargetSetMesh = ApertureRingMesh ? ApertureRingMesh.Get() : BlockMesh.Get();
			break;
		}
		if (TargetSetMesh && SetPieceISM->GetStaticMesh() != TargetSetMesh)
		{
			SetPieceISM->SetStaticMesh(TargetSetMesh);
		}

		OnFilmSceneChanged.Broadcast(CurrentScene);
		OnSceneChanged(CurrentScene);
	}

	UpdateSceneMaterials(DeltaSeconds);
	UpdateDynamicAnimationsAndPickups(DeltaSeconds);
	UpdateFinalCutWrapArch();

	// Recycle chunks that fell behind the pawn.
	const int32 DesiredFirst = FMath::FloorToInt(static_cast<float>(BoundPawn->GetActorLocation().X) / ChunkLength) - ChunksBehind;
	if (DesiredFirst < FirstChunkIndex)
	{
		RebuildTrack();
		return;
	}

	bool bRecycled = false;
	while (FirstChunkIndex < DesiredFirst)
	{
		PopulateChunk(FirstChunkIndex + PoolSize());
		++FirstChunkIndex;
		bRecycled = true;
	}

	if (bRecycled)
	{
		FloorISM->MarkRenderStateDirty();
		WallISM->MarkRenderStateDirty();
		TakeA_ObstaclesISM->MarkRenderStateDirty();
		TakeB_ObstaclesISM->MarkRenderStateDirty();
		SetPieceISM->MarkRenderStateDirty();
		PickupISM->MarkRenderStateDirty();
		PickupTakeB_ISM->MarkRenderStateDirty();
	}
}

void AMegaJamTrackGenerator::UpdateFinalCutWrapArch()
{
	if (!BoundPawn || !WrapArchISM)
	{
		return;
	}

	if (WrapArchMID)
	{
		const float ActiveTakeVal = (BoundPawn->GetCurrentTake() == EMegaJamTakeState::DirectorsTake) ? 1.0f : 0.0f;
		WrapArchMID->SetScalarParameterValue(TEXT("TakeIndex"), ActiveTakeVal);
	}

	const float RunTime = GetRunTimeSeconds();
	if (RunTime >= 148.0f && !bWrapArchSpawned)
	{
		bWrapArchSpawned = true;
		const float PawnX = static_cast<float>(BoundPawn->GetActorLocation().X);
		const float Speed = FMath::Max(static_cast<float>(BoundPawn->GetFlowVelocity().X), MinProjectionSpeed);
		const float ArchX = PawnX + FMath::Max(2800.0f, Speed * 2.0f);

		// Golden monument arch framing the 150s finish line
		WrapArchISM->UpdateInstanceTransform(0, FTransform(FRotator::ZeroRotator, FVector(ArchX, -880.0f, 500.0f), FVector(2.8f, 2.4f, 10.0f)), true, false, true);
		WrapArchISM->UpdateInstanceTransform(1, FTransform(FRotator::ZeroRotator, FVector(ArchX, 880.0f, 500.0f), FVector(2.8f, 2.4f, 10.0f)), true, false, true);
		WrapArchISM->UpdateInstanceTransform(2, FTransform(FRotator::ZeroRotator, FVector(ArchX, 0.0f, 960.0f), FVector(3.2f, 20.4f, 2.2f)), true, false, true);
		WrapArchISM->UpdateInstanceTransform(3, FTransform(FRotator(0.0f, 0.0f, 25.0f), FVector(ArchX - 250.0f, -680.0f, 760.0f), FVector(2.0f, 1.8f, 5.5f)), true, false, true);
		WrapArchISM->UpdateInstanceTransform(4, FTransform(FRotator(0.0f, 0.0f, -25.0f), FVector(ArchX - 250.0f, 680.0f, 760.0f), FVector(2.0f, 1.8f, 5.5f)), true, false, true);
		WrapArchISM->MarkRenderStateDirty();
	}
}

void AMegaJamTrackGenerator::HideFinalCutWrapArch()
{
	bWrapArchSpawned = false;
	if (!WrapArchISM)
	{
		return;
	}
	for (int32 Idx = 0; Idx < WrapArchParts; ++Idx)
	{
		WrapArchISM->UpdateInstanceTransform(Idx, MakeHiddenInstanceTransform(), true, false, true);
	}
	WrapArchISM->MarkRenderStateDirty();
}

FLinearColor AMegaJamTrackGenerator::GetSceneAccentColor(EMegaJamFilmScene Scene) const
{
	switch (Scene)
	{
	case EMegaJamFilmScene::Soundstage:
		return FLinearColor(1.0f, 0.72f, 0.35f, 1.0f);
	case EMegaJamFilmScene::OverpassNight:
		return FLinearColor(0.88f, 0.15f, 0.95f, 1.0f);
	case EMegaJamFilmScene::CuttingRoom:
		return FLinearColor(0.95f, 0.55f, 0.12f, 1.0f);
	case EMegaJamFilmScene::FinalCut:
	default:
		return FLinearColor(1.0f, 0.22f, 0.18f, 1.0f);
	}
}

void AMegaJamTrackGenerator::UpdateSceneMaterials(float DeltaSeconds)
{
	const FLinearColor TargetColor = GetSceneAccentColor(CurrentScene);
	CurrentSceneColor = FMath::CInterpTo(CurrentSceneColor, TargetColor, DeltaSeconds, 3.5f);

	if (SetPieceMID)
	{
		const float ActiveTakeVal = (BoundPawn && BoundPawn->GetCurrentTake() == EMegaJamTakeState::DirectorsTake) ? 1.0f : 0.0f;
		SetPieceMID->SetScalarParameterValue(TEXT("TakeIndex"), ActiveTakeVal);
		SetPieceMID->SetVectorParameterValue(TEXT("TakeColor"), CurrentSceneColor);
	}
}

void AMegaJamTrackGenerator::UpdateDynamicAnimationsAndPickups(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !BoundPawn)
	{
		return;
	}

	const float TimeSeconds = static_cast<float>(World->GetTimeSeconds());

	// 1. Rotate Scene 3 (Cutting Room) film-reel pillars in SetPieceISM
	bool bSetPiecesDirty = false;
	for (int32 Index = 0; Index < SetPieceSlots.Num(); ++Index)
	{
		const FMegaJamSetPieceSlot& Slot = SetPieceSlots[Index];
		if (Slot.bActive && Slot.bRotates)
		{
			FTransform AnimatedTransform = Slot.BaseTransform;
			const FRotator SpinRot(0.0f, TimeSeconds * Slot.RotationSpeedDeg, 0.0f);
			AnimatedTransform.SetRotation((Slot.BaseTransform.GetRotation().Rotator() + SpinRot).Quaternion());
			SetPieceISM->UpdateInstanceTransform(Index, AnimatedTransform, true, false, true);
			bSetPiecesDirty = true;
		}
	}
	if (bSetPiecesDirty)
	{
		SetPieceISM->MarkRenderStateDirty();
	}

	// 2. Rotate active Film Frame pickups and check pawn proximity for collection
	const FVector PawnLocation = BoundPawn->GetActorLocation();
	const float RadiusSq = FMath::Square(PickupCollectionRadius);
	const EMegaJamTakeState PawnTake = BoundPawn->GetCurrentTake();
	const bool bIsActivelyPlaying = BoundGameMode ? BoundGameMode->IsPlaying() : true;
	bool bPickupsDirty = false;

	for (int32 Index = 0; Index < PickupSlots.Num(); ++Index)
	{
		FMegaJamFilmFramePickup& Pickup = PickupSlots[Index];
		if (!Pickup.bActive)
		{
			continue;
		}

		UInstancedStaticMeshComponent* TargetISM = (Pickup.RequiredTake == EMegaJamTakeState::StudioTake)
			? PickupISM.Get()
			: PickupTakeB_ISM.Get();

		if (bIsActivelyPlaying && !BoundPawn->HasCrashed() && FVector::DistSquared(PawnLocation, Pickup.Location) <= RadiusSq)
		{
			if (PawnTake == Pickup.RequiredTake)
			{
				Pickup.bActive = false;
				TargetISM->UpdateInstanceTransform(Pickup.PoolIndex, MakeHiddenInstanceTransform(), true, false, true);
				BoundPawn->NotifyFilmFrameCollected();
				bPickupsDirty = true;
				continue;
			}
		}

		const float BobZ = FMath::Sin(TimeSeconds * 4.0f + static_cast<float>(Index)) * 14.0f;
		const FRotator RingRot(90.0f, TimeSeconds * 110.0f + static_cast<float>(Index * 45), 0.0f);
		const FTransform RingTransform(RingRot, Pickup.Location + FVector(0.0f, 0.0f, BobZ), Pickup.Scale);
		TargetISM->UpdateInstanceTransform(Pickup.PoolIndex, RingTransform, true, false, true);
		bPickupsDirty = true;
	}

	if (bPickupsDirty)
	{
		PickupISM->MarkRenderStateDirty();
		PickupTakeB_ISM->MarkRenderStateDirty();
	}
}

void AMegaJamTrackGenerator::RebuildTrack()
{
	CreatePools();
	BindToPawn();
	HideFinalCutWrapArch();

	if (!BoundPawn)
	{
		bBuilt = false;
		return;
	}

	const int32 PawnChunkIndex = FMath::FloorToInt(static_cast<float>(BoundPawn->GetActorLocation().X) / ChunkLength);
	FirstChunkIndex = PawnChunkIndex - ChunksBehind;
	SafeUntilChunkIndex = PawnChunkIndex + FMath::Max(1, SafeChunks);

	for (int32 Offset = 0; Offset < PoolSize(); ++Offset)
	{
		PopulateChunk(FirstChunkIndex + Offset);
	}

	FloorISM->MarkRenderStateDirty();
	WallISM->MarkRenderStateDirty();
	TakeA_ObstaclesISM->MarkRenderStateDirty();
	TakeB_ObstaclesISM->MarkRenderStateDirty();
	SetPieceISM->MarkRenderStateDirty();
	PickupISM->MarkRenderStateDirty();
	PickupTakeB_ISM->MarkRenderStateDirty();

	ApplyTakeCollision(BoundPawn->GetCurrentTake());

	LastBroadcastScene = -1;
	bBuilt = true;
}

void AMegaJamTrackGenerator::HandleTakeCut(EMegaJamTakeState NewTake)
{
	ApplyTakeCollision(NewTake);
}

void AMegaJamTrackGenerator::HandleRunStateChanged(EMegaJamRunState NewState, EMegaJamRunState OldState)
{
	if (NewState == EMegaJamRunState::Playing || NewState == EMegaJamRunState::ActiveFlow || NewState == EMegaJamRunState::TitleScreen)
	{
		RebuildTrack();
	}
	else if (NewState == EMegaJamRunState::Completed)
	{
		TakeA_ObstaclesISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TakeB_ObstaclesISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AMegaJamTrackGenerator::ApplyTakeCollision(EMegaJamTakeState Take)
{
	if (BoundGameMode && !BoundGameMode->IsPlaying())
	{
		TakeA_ObstaclesISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TakeB_ObstaclesISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	const bool bStudio = Take == EMegaJamTakeState::StudioTake;

	TakeA_ObstaclesISM->SetCollisionEnabled(bStudio ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	TakeB_ObstaclesISM->SetCollisionEnabled(bStudio ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
}

void AMegaJamTrackGenerator::BindToPawn()
{
	AMegaJamFlowPawn* Pawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (Pawn == BoundPawn)
	{
		return;
	}

	if (BoundPawn)
	{
		BoundPawn->OnTakeCut.RemoveDynamic(this, &AMegaJamTrackGenerator::HandleTakeCut);
	}

	BoundPawn = Pawn;
	if (BoundPawn)
	{
		BoundPawn->OnTakeCut.AddUniqueDynamic(this, &AMegaJamTrackGenerator::HandleTakeCut);
		if (bPoolsCreated)
		{
			ApplyTakeCollision(BoundPawn->GetCurrentTake());
		}
	}
}

void AMegaJamTrackGenerator::CreatePools()
{
	if (bPoolsCreated)
	{
		return;
	}

	const int32 Pool = PoolSize();
	const int32 Rows = FMath::Max(1, RowsPerChunk);
	const int32 ObstacleSlots = Pool * Rows * ObstaclesPerRow;
	const int32 TotalSetPieceSlots = Pool * SetPiecesPerChunk;
	const int32 TotalPickupSlots = Pool * PickupsPerChunk;

	FloorISM->SetStaticMesh(FloorTileMesh ? FloorTileMesh.Get() : BlockMesh.Get());
	WallISM->SetStaticMesh(WallSegmentMesh ? WallSegmentMesh.Get() : BlockMesh.Get());
	TakeA_ObstaclesISM->SetStaticMesh(ObstacleTakeAMesh ? ObstacleTakeAMesh.Get() : BlockMesh.Get());
	TakeB_ObstaclesISM->SetStaticMesh(ObstacleTakeBMesh ? ObstacleTakeBMesh.Get() : BlockMesh.Get());
	SetPieceISM->SetStaticMesh(SoundstageTrussMesh ? SoundstageTrussMesh.Get() : BlockMesh.Get());
	WrapArchISM->SetStaticMesh(ApertureRingMesh ? ApertureRingMesh.Get() : BlockMesh.Get());

	UStaticMesh* ResolvedPickupMesh = FilmFramePickupMesh
		? FilmFramePickupMesh.Get()
		: (CylinderMesh ? CylinderMesh.Get() : BlockMesh.Get());
	PickupISM->SetStaticMesh(ResolvedPickupMesh);
	PickupTakeB_ISM->SetStaticMesh(ResolvedPickupMesh);

	UInstancedStaticMeshComponent* All[] = {
		FloorISM, WallISM, TakeA_ObstaclesISM, TakeB_ObstaclesISM, SetPieceISM, PickupISM, PickupTakeB_ISM, WrapArchISM
	};
	for (UInstancedStaticMeshComponent* ISM : All)
	{
		ISM->ClearInstances();
	}

	for (int32 Index = 0; Index < Pool; ++Index)
	{
		FloorISM->AddInstance(MakeHiddenInstanceTransform(), true);
		WallISM->AddInstance(MakeHiddenInstanceTransform(), true);
		WallISM->AddInstance(MakeHiddenInstanceTransform(), true);
	}

	for (int32 Index = 0; Index < ObstacleSlots; ++Index)
	{
		TakeA_ObstaclesISM->AddInstance(MakeHiddenInstanceTransform(), true);
		TakeB_ObstaclesISM->AddInstance(MakeHiddenInstanceTransform(), true);
	}

	SetPieceSlots.SetNum(TotalSetPieceSlots);
	for (int32 Index = 0; Index < TotalSetPieceSlots; ++Index)
	{
		SetPieceISM->AddInstance(MakeHiddenInstanceTransform(), true);
		SetPieceSlots[Index] = FMegaJamSetPieceSlot();
	}

	PickupSlots.SetNum(TotalPickupSlots);
	for (int32 Index = 0; Index < TotalPickupSlots; ++Index)
	{
		PickupISM->AddInstance(MakeHiddenInstanceTransform(), true);
		PickupTakeB_ISM->AddInstance(MakeHiddenInstanceTransform(), true);
		PickupSlots[Index] = FMegaJamFilmFramePickup();
		PickupSlots[Index].PoolIndex = Index;
	}

	for (int32 Index = 0; Index < WrapArchParts; ++Index)
	{
		WrapArchISM->AddInstance(MakeHiddenInstanceTransform(), true);
	}

	bPoolsCreated = true;
}

int32 AMegaJamTrackGenerator::SlotOf(int32 ChunkIndex) const
{
	const int32 Pool = PoolSize();
	return ((ChunkIndex % Pool) + Pool) % Pool;
}

float AMegaJamTrackGenerator::GetRunTimeSeconds() const
{
	if (const AMegaJamGameMode* GameMode = GetWorld()->GetAuthGameMode<AMegaJamGameMode>())
	{
		return GameMode->GetRunTime();
	}
	return 0.0f;
}

int32 AMegaJamTrackGenerator::SceneIndexAtTime(float RunTimeSeconds) const
{
	for (int32 Index = 0; Index < SceneEndTimes.Num() && Index < 4; ++Index)
	{
		if (RunTimeSeconds < SceneEndTimes[Index])
		{
			return Index;
		}
	}
	return 3; // Final Cut
}

void AMegaJamTrackGenerator::PopulateChunk(int32 ChunkIndex)
{
	const int32 Slot = SlotOf(ChunkIndex);
	const int32 Rows = FMath::Max(1, RowsPerChunk);
	const float ChunkStartX = ChunkIndex * ChunkLength;
	const float ChunkCenterX = ChunkStartX + ChunkLength * 0.5f;

	FloorISM->UpdateInstanceTransform(Slot,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, 0.0f, -50.0f), FVector(ChunkLength / 100.0f, CorridorHalfWidth * 2.0f / 100.0f, 1.0f)),
		true, false, true);

	const FVector WallScale(ChunkLength / 100.0f, 1.0f, 10.0f);
	WallISM->UpdateInstanceTransform(Slot * 2,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, -CorridorHalfWidth, 500.0f), WallScale), true, false, true);
	WallISM->UpdateInstanceTransform(Slot * 2 + 1,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, CorridorHalfWidth, 500.0f), WallScale), true, false, true);

	float Density = 0.0f;
	int32 ProjectedSceneIdx = 0;
	float ProjectedTime = GetRunTimeSeconds();
	const bool bTitleOrCompleted = BoundGameMode && !BoundGameMode->IsPlaying();

	if (BoundPawn)
	{
		const float PawnX = static_cast<float>(BoundPawn->GetActorLocation().X);
		const float Speed = FMath::Max(static_cast<float>(BoundPawn->GetFlowVelocity().X), MinProjectionSpeed);
		ProjectedTime = GetRunTimeSeconds() + FMath::Max(0.0f, ChunkCenterX - PawnX) / Speed;
		ProjectedSceneIdx = bTitleOrCompleted ? 0 : SceneIndexAtTime(ProjectedTime);
		const float SceneScale = FMath::Lerp(0.55f, 1.0f, static_cast<float>(ProjectedSceneIdx) / 3.0f);
		const float FlowScale = FMath::Lerp(0.68f, 1.0f, BoundPawn->GetFlowRatio());
		Density = FMath::Clamp(MaxObstacleDensity * SceneScale * FlowScale, 0.0f, 1.0f);
	}

	const EMegaJamFilmScene ChunkScene = static_cast<EMegaJamFilmScene>(ProjectedSceneIdx);
	FRandomStream Stream(Seed + ChunkIndex * 7919);

	PopulateSetPiecesForChunk(Slot, ChunkIndex, ChunkStartX, ChunkCenterX, ChunkScene, Stream);
	PopulatePickupsForChunk(Slot, ChunkIndex, ChunkStartX, ChunkScene, Stream);

	const float RowLength = ChunkLength / Rows;

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const int32 BaseObstacleIndex = (Slot * Rows + Row) * ObstaclesPerRow;
		for (int32 SubIdx = 0; SubIdx < ObstaclesPerRow; ++SubIdx)
		{
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + SubIdx, MakeHiddenInstanceTransform(), true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + SubIdx, MakeHiddenInstanceTransform(), true, false, true);
		}

		const float PatternRoll = Stream.FRand();
		const float PresentRoll = Stream.FRand();
		const float TakeRoll = Stream.FRand();
		const float LateralRoll = Stream.FRand();
		const float HeightRoll = Stream.FRand();
		const float AirTakeRoll = Stream.FRand();
		const float AirLateralRoll = Stream.FRand();
		const float AirZRoll = Stream.FRand();

		// On the Title Screen, place decorative side-lane Take A / B blocks so the attract camera glides cleanly down the center!
		if (bTitleOrCompleted)
		{
			if (Row % 2 == 0)
			{
				const float RowCenterX = ChunkStartX + (Row + 0.5f) * RowLength;
				const float SideY = (Row == 0) ? -640.0f : 640.0f;
				const FTransform SideBlock(FRotator::ZeroRotator, FVector(RowCenterX, SideY, 120.0f), FVector(2.0f, 3.2f, 2.4f));
				UInstancedStaticMeshComponent* Target = (Row == 0) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
				Target->UpdateInstanceTransform(BaseObstacleIndex, SideBlock, true, false, true);
			}
			continue;
		}

		// Keep start/checkpoint runway and the 148s Final Cut finish stretch clear of obstacles!
		if (ChunkIndex < SafeUntilChunkIndex || ProjectedTime >= 147.5f || PresentRoll >= Density)
		{
			continue;
		}

		const float RowCenterX = ChunkStartX + (Row + 0.5f) * RowLength;

		// Helper lambdas for reusable gate & shaft patterns
		auto SpawnCeilingGate21_9 = [&]()
		{
			const FTransform CeilingGateTransform(
				FRotator::ZeroRotator,
				FVector(RowCenterX, 0.0f, 560.0f),
				FVector(ObstacleSize / 100.0f, 18.0f, 6.8f));
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CeilingGateTransform, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CeilingGateTransform, true, false, true);

			const float LowLateral = (LateralRoll < 0.5f) ? -520.0f : 520.0f;
			const FTransform LowBlockTransform(
				FRotator::ZeroRotator,
				FVector(RowCenterX, LowLateral, 100.0f),
				FVector(ObstacleSize / 100.0f, 5.0f, 2.0f));
			UInstancedStaticMeshComponent* LowTarget = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			LowTarget->UpdateInstanceTransform(BaseObstacleIndex + 1, LowBlockTransform, true, false, true);
		};

		auto SpawnVerticalShaft4_3 = [&]()
		{
			const FTransform LeftOuterBlocker(
				FRotator::ZeroRotator,
				FVector(RowCenterX, -660.0f, 450.0f),
				FVector(ObstacleSize / 100.0f, 4.8f, 9.0f));
			const FTransform RightOuterBlocker(
				FRotator::ZeroRotator,
				FVector(RowCenterX, 660.0f, 450.0f),
				FVector(ObstacleSize / 100.0f, 4.8f, 9.0f));

			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, LeftOuterBlocker, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, LeftOuterBlocker, true, false, true);
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, RightOuterBlocker, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, RightOuterBlocker, true, false, true);

			const bool bLowHurdle = (HeightRoll < 0.55f);
			const float HurdleZ = bLowHurdle ? 140.0f : 650.0f;
			const FTransform ShaftHurdle(
				FRotator::ZeroRotator,
				FVector(RowCenterX, 0.0f, HurdleZ),
				FVector(ObstacleSize / 100.0f, 8.2f, 2.8f));

			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 2, ShaftHurdle, true, false, true);
			if (bLowHurdle)
			{
				TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 2, ShaftHurdle, true, false, true);
			}
		};

		// ---- ACT-SPECIFIC LEVEL PACING ----
		switch (ChunkScene)
		{
		case EMegaJamFilmScene::Soundstage:
		{
			// Act I (Soundstage, 0-35s): Clean single-lane Take A/B blocks and spaced 21:9 / 4:3 intro gates
			if (Row == 2 && (ChunkIndex % 3 == 0))
			{
				if ((ChunkIndex / 3) % 2 == 0)
				{
					SpawnCeilingGate21_9();
				}
				else
				{
					SpawnVerticalShaft4_3();
				}
				break;
			}

			const float Height = FMath::Lerp(static_cast<float>(ObstacleHeightRange.X), static_cast<float>(ObstacleHeightRange.Y), HeightRoll);
			const float LaneY = (LateralRoll < 0.33f) ? -520.0f : ((LateralRoll < 0.66f) ? 0.0f : 520.0f);
			const FTransform BlockTransform(
				FRotator::ZeroRotator,
				FVector(RowCenterX, LaneY, Height * 0.5f),
				FVector(ObstacleSize / 100.0f, 3.4f, Height / 100.0f));
			UInstancedStaticMeshComponent* Target = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			Target->UpdateInstanceTransform(BaseObstacleIndex, BlockTransform, true, false, true);
			break;
		}

		case EMegaJamFilmScene::OverpassNight:
		{
			// Act II (OverpassNight, 35-75s): Faster staggered dual-lane highway blocks + elevated overpass crossbeams
			if (Row == 1 && PatternRoll < 0.28f)
			{
				SpawnCeilingGate21_9();
				break;
			}

			const float Height = FMath::Lerp(200.0f, 320.0f, HeightRoll);
			const float LeftLaneY = FMath::Lerp(-720.0f, -180.0f, LateralRoll);
			const float RightLaneY = FMath::Lerp(180.0f, 720.0f, AirLateralRoll);

			const FTransform LeftBlock(FRotator::ZeroRotator, FVector(RowCenterX, LeftLaneY, Height * 0.5f), FVector(ObstacleSize / 100.0f, 3.2f, Height / 100.0f));
			const FTransform RightBlock(FRotator::ZeroRotator, FVector(RowCenterX, RightLaneY, Height * 0.5f), FVector(ObstacleSize / 100.0f, 3.2f, Height / 100.0f));

			// Staggered dual-lane Take A & Take B blocks + elevated overpass crossbeam
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, (TakeRoll < 0.5f) ? LeftBlock : RightBlock, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, (TakeRoll < 0.5f) ? RightBlock : LeftBlock, true, false, true);

			const FTransform OverpassBeam(
				FRotator::ZeroRotator,
				FVector(RowCenterX, 0.0f, FMath::Lerp(380.0f, 680.0f, AirZRoll)),
				FVector(ObstacleSize / 100.0f, 9.0f, 1.8f));
			UInstancedStaticMeshComponent* BeamTarget = (AirTakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			BeamTarget->UpdateInstanceTransform(BaseObstacleIndex + 1, OverpassBeam, true, false, true);
			break;
		}

		case EMegaJamFilmScene::CuttingRoom:
		{
			// Act III (CuttingRoom, 75-115s): Rapid-splice rhythmic patterns (Take A -> Take B -> Take A) + narrow 4:3 vertical shafts
			if (Row == 3 && PatternRoll < 0.45f)
			{
				SpawnVerticalShaft4_3();
				break;
			}

			// Rhythmic alternating Take walls across the center lane forcing rapid A -> B -> A cuts
			const bool bRowTakeA = ((ChunkIndex * Rows + Row) % 2 == 0);
			UInstancedStaticMeshComponent* RhythmicTarget = bRowTakeA ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			const FTransform RhythmicBarrier(
				FRotator::ZeroRotator,
				FVector(RowCenterX, FMath::Lerp(-280.0f, 280.0f, LateralRoll), 180.0f),
				FVector(ObstacleSize / 100.0f, 7.8f, 3.6f));
			RhythmicTarget->UpdateInstanceTransform(BaseObstacleIndex, RhythmicBarrier, true, false, true);

			// High-altitude counter-take air block
			UInstancedStaticMeshComponent* AirTarget = bRowTakeA ? TakeB_ObstaclesISM.Get() : TakeA_ObstaclesISM.Get();
			const FTransform AirBlock(
				FRotator::ZeroRotator,
				FVector(RowCenterX, FMath::Lerp(-380.0f, 380.0f, AirLateralRoll), FMath::Lerp(420.0f, 760.0f, AirZRoll)),
				FVector(ObstacleSize / 100.0f, 4.5f, 2.5f));
			AirTarget->UpdateInstanceTransform(BaseObstacleIndex + 1, AirBlock, true, false, true);
			break;
		}

		case EMegaJamFilmScene::FinalCut:
		default:
		{
			// Act IV (FinalCut, 115-150s): High-intensity climax combining back-to-back 21:9 ceiling gates, 4:3 shafts, and air obstacles
			if (Row == 1 && PatternRoll < 0.45f)
			{
				SpawnCeilingGate21_9();
				break;
			}
			if (Row == 3 && PatternRoll < 0.45f)
			{
				SpawnVerticalShaft4_3();
				break;
			}

			const float Height = FMath::Lerp(220.0f, 340.0f, HeightRoll);
			const float Lateral = FMath::Lerp(-ObstacleLateralRange, ObstacleLateralRange, LateralRoll);
			const FTransform GroundObstacleTransform(
				FRotator::ZeroRotator,
				FVector(RowCenterX, Lateral, Height * 0.5f),
				FVector(ObstacleSize / 100.0f, 4.0f, Height / 100.0f));
			UInstancedStaticMeshComponent* GroundTarget = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			GroundTarget->UpdateInstanceTransform(BaseObstacleIndex, GroundObstacleTransform, true, false, true);

			const float AirY = FMath::Lerp(-420.0f, 420.0f, AirLateralRoll);
			const float AirZ = FMath::Lerp(static_cast<float>(AirObstacleZRange.X), static_cast<float>(AirObstacleZRange.Y), AirZRoll);
			const FTransform AirObstacleTransform(
				FRotator::ZeroRotator,
				FVector(RowCenterX, AirY, AirZ),
				FVector(ObstacleSize / 100.0f, 4.2f, 2.6f));
			UInstancedStaticMeshComponent* AirTarget = (AirTakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			AirTarget->UpdateInstanceTransform(BaseObstacleIndex + 1, AirObstacleTransform, true, false, true);
			break;
		}
		}
	}
}

void AMegaJamTrackGenerator::PopulateSetPiecesForChunk(
	int32 Slot,
	int32 ChunkIndex,
	float ChunkStartX,
	float ChunkCenterX,
	EMegaJamFilmScene ChunkScene,
	FRandomStream& Stream)
{
	const int32 BaseSetIndex = Slot * SetPiecesPerChunk;
	for (int32 SubIdx = 0; SubIdx < SetPiecesPerChunk; ++SubIdx)
	{
		const int32 PoolIdx = BaseSetIndex + SubIdx;
		SetPieceSlots[PoolIdx].bActive = false;
		SetPieceSlots[PoolIdx].bRotates = false;
		SetPieceISM->UpdateInstanceTransform(PoolIdx, MakeHiddenInstanceTransform(), true, false, true);
	}

	auto PlaceSetPiece = [this, BaseSetIndex](int32 SubIdx, const FTransform& Transform, bool bRotates = false, float RotSpeed = 45.0f)
	{
		const int32 PoolIdx = BaseSetIndex + SubIdx;
		SetPieceSlots[PoolIdx].BaseTransform = Transform;
		SetPieceSlots[PoolIdx].bRotates = bRotates;
		SetPieceSlots[PoolIdx].RotationSpeedDeg = RotSpeed;
		SetPieceSlots[PoolIdx].bActive = true;
		SetPieceISM->UpdateInstanceTransform(PoolIdx, Transform, true, false, true);
	};

	const float Variation = Stream.FRandRange(0.85f, 1.25f);

	switch (ChunkScene)
	{
	case EMegaJamFilmScene::Soundstage:
		PlaceSetPiece(0, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.25f, 0.0f, 920.0f), FVector(1.4f, 21.0f, 0.7f)));
		PlaceSetPiece(1, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.75f, 0.0f, 920.0f), FVector(1.4f, 21.0f, 0.7f)));
		PlaceSetPiece(2, FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, -940.0f, 920.0f), FVector(ChunkLength / 100.0f, 0.6f, 0.6f)));
		PlaceSetPiece(3, FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, 940.0f, 920.0f), FVector(ChunkLength / 100.0f, 0.6f, 0.6f)));
		break;

	case EMegaJamFilmScene::OverpassNight:
		PlaceSetPiece(0, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.3f, -1350.0f, 900.0f * Variation), FVector(4.5f, 4.5f, 18.0f * Variation)));
		PlaceSetPiece(1, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.3f, 1350.0f, 950.0f / Variation), FVector(4.5f, 4.5f, 19.0f / Variation)));
		PlaceSetPiece(2, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.8f, -1350.0f, 1100.0f), FVector(5.0f, 4.5f, 22.0f)));
		PlaceSetPiece(3, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.8f, 1350.0f, 1050.0f), FVector(5.0f, 4.5f, 21.0f)));
		break;

	case EMegaJamFilmScene::CuttingRoom:
		PlaceSetPiece(0, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.25f, -1120.0f, 520.0f), FVector(4.2f, 4.2f, 10.5f)), true, 50.0f);
		PlaceSetPiece(1, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.25f, 1120.0f, 520.0f), FVector(4.2f, 4.2f, 10.5f)), true, -50.0f);
		PlaceSetPiece(2, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.75f, -1120.0f, 520.0f), FVector(4.2f, 4.2f, 10.5f)), true, 65.0f);
		PlaceSetPiece(3, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.75f, 1120.0f, 520.0f), FVector(4.2f, 4.2f, 10.5f)), true, -65.0f);
		break;

	case EMegaJamFilmScene::FinalCut:
	default:
		PlaceSetPiece(0, FTransform(FRotator(0.0f, 0.0f, 25.0f), FVector(ChunkCenterX, -880.0f, 550.0f), FVector(1.8f, 1.5f, 12.0f)));
		PlaceSetPiece(1, FTransform(FRotator(0.0f, 0.0f, -25.0f), FVector(ChunkCenterX, 880.0f, 550.0f), FVector(1.8f, 1.5f, 12.0f)));
		PlaceSetPiece(2, FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, 0.0f, 940.0f), FVector(2.0f, 19.5f, 1.4f)));
		PlaceSetPiece(3, FTransform(FRotator(45.0f, 0.0f, 0.0f), FVector(ChunkStartX + ChunkLength * 0.9f, 0.0f, 900.0f), FVector(1.5f, 18.0f, 1.5f)));
		break;
	}
}

void AMegaJamTrackGenerator::PopulatePickupsForChunk(
	int32 Slot,
	int32 ChunkIndex,
	float ChunkStartX,
	EMegaJamFilmScene ChunkScene,
	FRandomStream& Stream)
{
	const int32 BasePickupIdx = Slot * PickupsPerChunk;
	for (int32 SubIdx = 0; SubIdx < PickupsPerChunk; ++SubIdx)
	{
		const int32 PoolIdx = BasePickupIdx + SubIdx;
		PickupSlots[PoolIdx].bActive = false;
		PickupISM->UpdateInstanceTransform(PoolIdx, MakeHiddenInstanceTransform(), true, false, true);
		PickupTakeB_ISM->UpdateInstanceTransform(PoolIdx, MakeHiddenInstanceTransform(), true, false, true);
	}

	if (ChunkIndex < SafeUntilChunkIndex || (BoundGameMode && !BoundGameMode->IsPlaying()))
	{
		return;
	}

	const FVector RingScale(1.35f, 1.35f, 0.28f);

	// Pickup 0: Outer-Wing Frame (accessible in Act I at +/-580, full +/-700 slaloms in Acts II-IV)
	{
		const int32 PoolIdx = BasePickupIdx;
		const float WingOffset = (ChunkScene == EMegaJamFilmScene::Soundstage) ? 580.0f : 700.0f;
		const float WingY = ((ChunkIndex % 2 == 0) ? -1.0f : 1.0f) * WingOffset;
		const EMegaJamTakeState Take = (Stream.FRand() < 0.5f) ? EMegaJamTakeState::StudioTake : EMegaJamTakeState::DirectorsTake;
		const FVector PickupPos(ChunkStartX + ChunkLength * 0.35f, WingY, 130.0f);

		PickupSlots[PoolIdx].Location = PickupPos;
		PickupSlots[PoolIdx].Scale = RingScale;
		PickupSlots[PoolIdx].RequiredTake = Take;
		PickupSlots[PoolIdx].bActive = true;

		UInstancedStaticMeshComponent* TargetISM = (Take == EMegaJamTakeState::StudioTake) ? PickupISM.Get() : PickupTakeB_ISM.Get();
		TargetISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator(90.0f, 0.0f, 0.0f), PickupPos, RingScale), true, false, true);
	}

	// Pickup 1: High-Altitude Frame (Requires Box_4_3 because Wide_21_9 clamps Z to 200)
	{
		const int32 PoolIdx = BasePickupIdx + 1;
		const float HighY = Stream.FRandRange(-350.0f, 350.0f);
		const float HighZ = (ChunkScene == EMegaJamFilmScene::FinalCut) ? 660.0f : 620.0f;
		const EMegaJamTakeState Take = (Stream.FRand() < 0.5f) ? EMegaJamTakeState::StudioTake : EMegaJamTakeState::DirectorsTake;
		const FVector PickupPos(ChunkStartX + ChunkLength * 0.80f, HighY, HighZ);

		PickupSlots[PoolIdx].Location = PickupPos;
		PickupSlots[PoolIdx].Scale = RingScale;
		PickupSlots[PoolIdx].RequiredTake = Take;
		PickupSlots[PoolIdx].bActive = true;

		UInstancedStaticMeshComponent* TargetISM = (Take == EMegaJamTakeState::StudioTake) ? PickupISM.Get() : PickupTakeB_ISM.Get();
		TargetISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator(90.0f, 0.0f, 0.0f), PickupPos, RingScale), true, false, true);
	}
}
