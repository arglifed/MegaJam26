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
	BoostStripISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoostStripISM"));
	WrapArchISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WrapArchISM"));

	ConfigureISM(FloorISM, TrackRoot, true, false);
	ConfigureISM(WallISM, TrackRoot, true, false);
	ConfigureISM(TakeA_ObstaclesISM, TrackRoot, true, true);
	ConfigureISM(TakeB_ObstaclesISM, TrackRoot, true, true);

	ConfigureISM(SetPieceISM, TrackRoot, false, false);
	ConfigureISM(PickupISM, TrackRoot, false, false);
	ConfigureISM(PickupTakeB_ISM, TrackRoot, false, false);
	ConfigureISM(BoostStripISM, TrackRoot, false, false);
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

		BoostStripMID = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		BoostStripMID->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		BoostStripMID->SetVectorParameterValue(TEXT("TakeColor"), FLinearColor(0.15f, 0.95f, 1.0f, 1.0f));
		BoostStripISM->SetMaterial(0, BoostStripMID);
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
		BoostStripISM->MarkRenderStateDirty();
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
	if (BoostStripMID)
	{
		const float ActiveTakeVal = (BoundPawn && BoundPawn->GetCurrentTake() == EMegaJamTakeState::DirectorsTake) ? 1.0f : 0.0f;
		BoostStripMID->SetScalarParameterValue(TEXT("TakeIndex"), ActiveTakeVal);
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

	// 3. Outer-Wing Dolly Boost Strips (21:9 Required): collect when riding outer lanes in Wide_21_9
	if (bIsActivelyPlaying && !BoundPawn->HasCrashed() && BoundPawn->GetAspectMode() == EMegaJamAspectMode::Wide_21_9)
	{
		bool bStripsDirty = false;
		for (int32 Index = 0; Index < BoostStripSlots.Num(); ++Index)
		{
			FMegaJamDollyBoostStrip& Strip = BoostStripSlots[Index];
			if (!Strip.bActive)
			{
				continue;
			}

			if (FMath::Abs(PawnLocation.X - Strip.Location.X) <= 280.0f &&
				FMath::Abs(PawnLocation.Y - Strip.Location.Y) <= 185.0f &&
				PawnLocation.Z <= 240.0f)
			{
				Strip.bActive = false;
				BoostStripISM->UpdateInstanceTransform(Strip.PoolIndex, MakeHiddenInstanceTransform(), true, false, true);
				BoundPawn->NotifyDollyBoostStripCollected();
				bStripsDirty = true;
			}
		}

		if (bStripsDirty)
		{
			BoostStripISM->MarkRenderStateDirty();
		}
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
	BoostStripISM->MarkRenderStateDirty();

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
	const int32 TotalBoostStripSlots = Pool * BoostStripsPerChunk;

	FloorISM->SetStaticMesh(FloorTileMesh ? FloorTileMesh.Get() : BlockMesh.Get());
	WallISM->SetStaticMesh(WallSegmentMesh ? WallSegmentMesh.Get() : BlockMesh.Get());
	TakeA_ObstaclesISM->SetStaticMesh(ObstacleTakeAMesh ? ObstacleTakeAMesh.Get() : BlockMesh.Get());
	TakeB_ObstaclesISM->SetStaticMesh(ObstacleTakeBMesh ? ObstacleTakeBMesh.Get() : BlockMesh.Get());
	SetPieceISM->SetStaticMesh(SoundstageTrussMesh ? SoundstageTrussMesh.Get() : BlockMesh.Get());
	BoostStripISM->SetStaticMesh(BlockMesh.Get());
	WrapArchISM->SetStaticMesh(ApertureRingMesh ? ApertureRingMesh.Get() : BlockMesh.Get());

	UStaticMesh* ResolvedPickupMesh = FilmFramePickupMesh
		? FilmFramePickupMesh.Get()
		: (CylinderMesh ? CylinderMesh.Get() : BlockMesh.Get());
	PickupISM->SetStaticMesh(ResolvedPickupMesh);
	PickupTakeB_ISM->SetStaticMesh(ResolvedPickupMesh);

	UInstancedStaticMeshComponent* All[] = {
		FloorISM, WallISM, TakeA_ObstaclesISM, TakeB_ObstaclesISM, SetPieceISM, PickupISM, PickupTakeB_ISM, BoostStripISM, WrapArchISM
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

	BoostStripSlots.SetNum(TotalBoostStripSlots);
	for (int32 Index = 0; Index < TotalBoostStripSlots; ++Index)
	{
		BoostStripISM->AddInstance(MakeHiddenInstanceTransform(), true);
		BoostStripSlots[Index] = FMegaJamDollyBoostStrip();
		BoostStripSlots[Index].PoolIndex = Index;
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

bool AMegaJamTrackGenerator::IsHazardChunkForScene(int32 ChunkIndex, EMegaJamFilmScene ChunkScene) const
{
	const int32 Mod3 = ((ChunkIndex % 3) + 3) % 3;
	const int32 Mod2 = ((ChunkIndex % 2) + 2) % 2;

	switch (ChunkScene)
	{
	case EMegaJamFilmScene::Soundstage:
		// Act I (0-35s): 1 hazard chunk followed by 2 clean runway chunks
		return (Mod3 == 0);
	case EMegaJamFilmScene::OverpassNight:
		// Act II (35-75s): 1 hazard chunk followed by 1 clean runway chunk
		return (Mod2 == 0);
	case EMegaJamFilmScene::CuttingRoom:
	case EMegaJamFilmScene::FinalCut:
	default:
		// Acts III & IV (75-150s): 2 hazard chunks followed by 1 clean runway chunk
		return (Mod3 != 2);
	}
}

int32 AMegaJamTrackGenerator::GetHazardArchetypeForChunk(int32 ChunkIndex) const
{
	// Deterministic cycle across the 4 clean set-piece archetypes:
	// 0 = Color-Splice Gate
	// 1 = 21:9 Center-Median Split Blockade
	// 2 = 21:9 Overhead Ceiling Gate
	// 3 = 4:3 Vertical Shaft
	FRandomStream ArchetypeStream(Seed + ChunkIndex * 4999);
	const int32 BaseCycle = ((ChunkIndex % 4) + 4) % 4;
	return (BaseCycle + ArchetypeStream.RandHelper(2)) % 4;
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

	int32 ProjectedSceneIdx = 0;
	float ProjectedTime = GetRunTimeSeconds();
	const bool bTitleOrCompleted = BoundGameMode && !BoundGameMode->IsPlaying();

	if (BoundPawn)
	{
		const float PawnX = static_cast<float>(BoundPawn->GetActorLocation().X);
		const float Speed = FMath::Max(static_cast<float>(BoundPawn->GetFlowVelocity().X), MinProjectionSpeed);
		ProjectedTime = GetRunTimeSeconds() + FMath::Max(0.0f, ChunkCenterX - PawnX) / Speed;
		ProjectedSceneIdx = bTitleOrCompleted ? 0 : SceneIndexAtTime(ProjectedTime);
	}

	const EMegaJamFilmScene ChunkScene = static_cast<EMegaJamFilmScene>(ProjectedSceneIdx);
	FRandomStream Stream(Seed + ChunkIndex * 7919);

	const bool bInSafeOrFinishZone = (ChunkIndex < SafeUntilChunkIndex || ProjectedTime >= 147.5f || bTitleOrCompleted);
	const bool bIsHazardChunk = !bInSafeOrFinishZone && IsHazardChunkForScene(ChunkIndex, ChunkScene);
	const int32 HazardArchetype = GetHazardArchetypeForChunk(ChunkIndex);

	PopulateSetPiecesForChunk(Slot, ChunkIndex, ChunkStartX, ChunkCenterX, ChunkScene, Stream);
	PopulatePickupsForChunk(Slot, ChunkIndex, ChunkStartX, ChunkScene, bIsHazardChunk, HazardArchetype, Stream);
	PopulateBoostStripsForChunk(Slot, ChunkIndex, ChunkStartX, bIsHazardChunk, HazardArchetype, Stream);

	const float RowLength = ChunkLength / Rows;

	// Clear all obstacle slots in this chunk first
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const int32 BaseObstacleIndex = (Slot * Rows + Row) * ObstaclesPerRow;
		for (int32 SubIdx = 0; SubIdx < ObstaclesPerRow; ++SubIdx)
		{
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + SubIdx, MakeHiddenInstanceTransform(), true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + SubIdx, MakeHiddenInstanceTransform(), true, false, true);
		}
	}

	// On the Title Screen, place decorative side-lane Take A / B blocks so the attract camera glides cleanly down the center!
	if (bTitleOrCompleted)
	{
		for (int32 Row = 0; Row < Rows; Row += 2)
		{
			const int32 BaseObstacleIndex = (Slot * Rows + Row) * ObstaclesPerRow;
			const float RowCenterX = ChunkStartX + (Row + 0.5f) * RowLength;
			const float SideY = (Row == 0) ? -640.0f : 640.0f;
			const FTransform SideBlock(FRotator::ZeroRotator, FVector(RowCenterX, SideY, 120.0f), FVector(2.0f, 3.2f, 2.4f));
			UInstancedStaticMeshComponent* Target = (Row == 0) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			Target->UpdateInstanceTransform(BaseObstacleIndex, SideBlock, true, false, true);
		}
		return;
	}

	// Breathing Runway Chunks (and start/finish safe zones) contain zero lethal blocks!
	if (!bIsHazardChunk)
	{
		return;
	}

	// Strict 1-Archetype-Per-Hazard-Chunk rule: place the single telegraphed set-piece at the center of the chunk
	const int32 CenterRow = FMath::Clamp(Rows / 2, 0, Rows - 1);
	const int32 BaseObstacleIndex = (Slot * Rows + CenterRow) * ObstaclesPerRow;
	const float HazardX = ChunkCenterX;
	const float TakeRoll = Stream.FRand();
	const float HeightRoll = Stream.FRand();

	switch (HazardArchetype)
	{
	case 0:
	{
		// Archetype 1 — Color-Splice Gate:
		// A single pair of side-by-side Take A and Take B floor blocks (player matches color or Hard Cuts).
		const bool bLeftIsTakeA = (TakeRoll < 0.5f);
		const FTransform LeftGateBlock(
			FRotator::ZeroRotator,
			FVector(HazardX, -380.0f, 140.0f),
			FVector(ObstacleSize / 100.0f, 7.2f, 2.8f));
		const FTransform RightGateBlock(
			FRotator::ZeroRotator,
			FVector(HazardX, 380.0f, 140.0f),
			FVector(ObstacleSize / 100.0f, 7.2f, 2.8f));

		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, bLeftIsTakeA ? LeftGateBlock : RightGateBlock, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, bLeftIsTakeA ? RightGateBlock : LeftGateBlock, true, false, true);
		break;
	}

	case 1:
	{
		// Archetype 2 — 21:9 Center-Median Split Blockade:
		// Central monolith (Y: [-450, 450], Z: [0, 930]) with open outer wings (Y = +/-700) lined with cyan Dolly Boost Strips.
		// Zero extra obstacles on this chunk!
		const FTransform CentralMonolith(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 465.0f),
			FVector(ObstacleSize / 100.0f, 9.0f, 9.3f));
		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CentralMonolith, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CentralMonolith, true, false, true);
		break;
	}

	case 2:
	{
		// Archetype 3 — 21:9 Overhead Ceiling Gate:
		// Upper barrier (Y: [-900, 900], Z: [310, 930] -> center Z = 620, height 620) leaving the entire floor (Z < 310) open.
		const FTransform CeilingGateTransform(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 620.0f),
			FVector(ObstacleSize / 100.0f, 18.0f, 6.2f));
		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CeilingGateTransform, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CeilingGateTransform, true, false, true);
		break;
	}

	case 3:
	default:
	{
		// Archetype 4 — 4:3 Vertical Shaft:
		// Outer blockers (Y: [-900, -480] and [480, 900]) with a wide center opening containing a clean arc of 3 high Film Frames.
		// Zero interior hurdles in Acts I-II; at most 1 single low or high hurdle in Acts III-IV.
		const FTransform LeftOuterBlocker(
			FRotator::ZeroRotator,
			FVector(HazardX, -690.0f, 450.0f),
			FVector(ObstacleSize / 100.0f, 4.2f, 9.0f));
		const FTransform RightOuterBlocker(
			FRotator::ZeroRotator,
			FVector(HazardX, 690.0f, 450.0f),
			FVector(ObstacleSize / 100.0f, 4.2f, 9.0f));

		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, LeftOuterBlocker, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, LeftOuterBlocker, true, false, true);
		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, RightOuterBlocker, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, RightOuterBlocker, true, false, true);

		if (ChunkScene == EMegaJamFilmScene::CuttingRoom || ChunkScene == EMegaJamFilmScene::FinalCut)
		{
			const bool bLowHurdle = (HeightRoll < 0.65f);
			const float HurdleZ = bLowHurdle ? 125.0f : 680.0f;
			const FTransform ShaftHurdle(
				FRotator::ZeroRotator,
				FVector(HazardX, 0.0f, HurdleZ),
				FVector(ObstacleSize / 100.0f, 8.8f, 2.4f));
			UInstancedStaticMeshComponent* HurdleTarget = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			HurdleTarget->UpdateInstanceTransform(BaseObstacleIndex + 2, ShaftHurdle, true, false, true);
		}
		break;
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
	bool bIsHazardChunk,
	int32 HazardArchetype,
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
	const EMegaJamTakeState LineTake = (Stream.FRand() < 0.5f) ? EMegaJamTakeState::StudioTake : EMegaJamTakeState::DirectorsTake;
	UInstancedStaticMeshComponent* TargetISM = (LineTake == EMegaJamTakeState::StudioTake) ? PickupISM.Get() : PickupTakeB_ISM.Get();

	auto PlaceFrame = [&](int32 SubIdx, const FVector& Pos)
	{
		const int32 PoolIdx = BasePickupIdx + SubIdx;
		PickupSlots[PoolIdx].Location = Pos;
		PickupSlots[PoolIdx].Scale = RingScale;
		PickupSlots[PoolIdx].RequiredTake = LineTake;
		PickupSlots[PoolIdx].bActive = true;
		TargetISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator(90.0f, 0.0f, 0.0f), Pos, RingScale), true, false, true);
	};

	// Check if the NEXT chunk is a 21:9 Overhead Ceiling Gate (Archetype 2) so this runway chunk sets up a Drop-Cut Slam!
	const bool bNextIsCeilingGate = IsHazardChunkForScene(ChunkIndex + 1, ChunkScene) && (GetHazardArchetypeForChunk(ChunkIndex + 1) == 2);

	if ((bIsHazardChunk && HazardArchetype == 3) || (!bIsHazardChunk && bNextIsCeilingGate))
	{
		// 4:3 Vertical Shaft OR Runway preceding a 21:9 Ceiling Gate:
		// Spawn a clean arc of 3 high Film Frames (Z = 520 - 640) along Y = 0 to reward climbing in 4:3 and arming a Drop-Cut Slam!
		PlaceFrame(0, FVector(ChunkStartX + ChunkLength * 0.28f, 0.0f, 520.0f));
		PlaceFrame(1, FVector(ChunkStartX + ChunkLength * 0.50f, 0.0f, 640.0f));
		PlaceFrame(2, FVector(ChunkStartX + ChunkLength * 0.72f, 0.0f, 560.0f));
	}
	else if (bIsHazardChunk && HazardArchetype == 2)
	{
		// On a 21:9 Overhead Ceiling Gate chunk, place high approach frames before the gate (X = 0.20) and low dolly frames under the gate (Z = 120)
		PlaceFrame(0, FVector(ChunkStartX + ChunkLength * 0.18f, 0.0f, 560.0f));
		PlaceFrame(1, FVector(ChunkStartX + ChunkLength * 0.50f, 0.0f, 120.0f));
		PlaceFrame(2, FVector(ChunkStartX + ChunkLength * 0.78f, 0.0f, 120.0f));
	}
	else if (!bIsHazardChunk)
	{
		// Clean Breathing Runway Chunk: alternating high 4:3 arc or wide 21:9 outer-wing line
		if (ChunkIndex % 2 == 0)
		{
			const float WingY = ((ChunkIndex / 2) % 2 == 0) ? -680.0f : 680.0f;
			PlaceFrame(0, FVector(ChunkStartX + ChunkLength * 0.25f, WingY, 130.0f));
			PlaceFrame(1, FVector(ChunkStartX + ChunkLength * 0.50f, WingY, 130.0f));
			PlaceFrame(2, FVector(ChunkStartX + ChunkLength * 0.75f, WingY, 130.0f));
		}
		else
		{
			PlaceFrame(0, FVector(ChunkStartX + ChunkLength * 0.28f, 0.0f, 540.0f));
			PlaceFrame(1, FVector(ChunkStartX + ChunkLength * 0.52f, 0.0f, 620.0f));
			PlaceFrame(2, FVector(ChunkStartX + ChunkLength * 0.76f, 0.0f, 540.0f));
		}
	}
}

void AMegaJamTrackGenerator::PopulateBoostStripsForChunk(
	int32 Slot,
	int32 ChunkIndex,
	float ChunkStartX,
	bool bIsHazardChunk,
	int32 HazardArchetype,
	FRandomStream& Stream)
{
	const int32 BaseStripIdx = Slot * BoostStripsPerChunk;
	for (int32 SubIdx = 0; SubIdx < BoostStripsPerChunk; ++SubIdx)
	{
		const int32 PoolIdx = BaseStripIdx + SubIdx;
		BoostStripSlots[PoolIdx].bActive = false;
		BoostStripISM->UpdateInstanceTransform(PoolIdx, MakeHiddenInstanceTransform(), true, false, true);
	}

	if (ChunkIndex < SafeUntilChunkIndex || (BoundGameMode && !BoundGameMode->IsPlaying()))
	{
		return;
	}

	// Spawn glowing cyan outer-wing Dolly Boost Strips (Y = +/-700, Z = 18) on 21:9 Center-Median Blockades and Breathing Runway Chunks
	if (bIsHazardChunk && HazardArchetype != 1)
	{
		return;
	}

	const FVector StripScale(5.2f, 1.55f, 0.16f);
	const float StripX = bIsHazardChunk ? (ChunkStartX + ChunkLength * 0.50f) : (ChunkStartX + ChunkLength * 0.45f);

	// Left Outer Wing Dolly Boost Strip (Y = -700, Z = 18 -> requires Wide_21_9)
	{
		const int32 PoolIdx = BaseStripIdx;
		const FVector StripPos(StripX, -700.0f, 18.0f);
		BoostStripSlots[PoolIdx].Location = StripPos;
		BoostStripSlots[PoolIdx].bActive = true;
		BoostStripISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator::ZeroRotator, StripPos, StripScale), true, false, true);
	}

	// Right Outer Wing Dolly Boost Strip (Y = +700, Z = 18 -> requires Wide_21_9)
	{
		const int32 PoolIdx = BaseStripIdx + 1;
		const FVector StripPos(StripX, 700.0f, 18.0f);
		BoostStripSlots[PoolIdx].Location = StripPos;
		BoostStripSlots[PoolIdx].bActive = true;
		BoostStripISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator::ZeroRotator, StripPos, StripScale), true, false, true);
	}
}
