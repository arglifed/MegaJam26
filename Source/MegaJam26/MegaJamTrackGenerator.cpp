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

	// Floor and walls block the pawn without firing graze overlaps.
	ConfigureISM(FloorISM, TrackRoot, true, false);
	ConfigureISM(WallISM, TrackRoot, true, false);
	ConfigureISM(TakeA_ObstaclesISM, TrackRoot, true, true);
	ConfigureISM(TakeB_ObstaclesISM, TrackRoot, true, true);

	// Set pieces and collectible Film Frames are purely non-colliding.
	ConfigureISM(SetPieceISM, TrackRoot, false, false);
	ConfigureISM(PickupISM, TrackRoot, false, false);
	ConfigureISM(PickupTakeB_ISM, TrackRoot, false, false);

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

	// Soft default: only load the ghost material if the asset exists and nothing was assigned.
	if (!ObstacleMaterial && !DefaultObstacleMaterial.IsNull() && FPackageName::DoesPackageExist(DefaultObstacleMaterial.ToSoftObjectPath().GetLongPackageName()))
	{
		ObstacleMaterial = DefaultObstacleMaterial.LoadSynchronous();
	}

	// Per-pool dynamic material: TakeIndex lets the shader compare against MPC ActiveTake.
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

		// Collectible Film Frames use brighter gold-tinted Take A / cyan-tinted Take B materials so they pop in the corridor.
		UMaterialInstanceDynamic* PickupMatA = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		PickupMatA->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		PickupMatA->SetVectorParameterValue(TEXT("TakeColor"), FLinearColor(1.0f, 0.85f, 0.2f, 1.0f));
		PickupISM->SetMaterial(0, PickupMatA);

		UMaterialInstanceDynamic* PickupMatB = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		PickupMatB->SetScalarParameterValue(TEXT("TakeIndex"), 1.0f);
		PickupMatB->SetVectorParameterValue(TEXT("TakeColor"), FLinearColor(0.25f, 0.95f, 1.0f, 1.0f));
		PickupTakeB_ISM->SetMaterial(0, PickupMatB);

		// SetPieceISM uses a scene-reactive dynamic material instance.
		SetPieceMID = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		SetPieceMID->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		SetPieceMID->SetVectorParameterValue(TEXT("TakeColor"), CurrentSceneColor);
		SetPieceISM->SetMaterial(0, SetPieceMID);
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
	const int32 SceneIndex = SceneIndexAtTime(GetRunTimeSeconds());
	if (SceneIndex != LastBroadcastScene)
	{
		LastBroadcastScene = SceneIndex;
		CurrentScene = static_cast<EMegaJamFilmScene>(SceneIndex);

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

FLinearColor AMegaJamTrackGenerator::GetSceneAccentColor(EMegaJamFilmScene Scene) const
{
	switch (Scene)
	{
	case EMegaJamFilmScene::Soundstage:
		// Warm tungsten / cyan studio rig accent
		return FLinearColor(1.0f, 0.72f, 0.35f, 1.0f);
	case EMegaJamFilmScene::OverpassNight:
		// Neon midnight / magenta accent
		return FLinearColor(0.88f, 0.15f, 0.95f, 1.0f);
	case EMegaJamFilmScene::CuttingRoom:
		// Amber / sepia celluloid accent
		return FLinearColor(0.95f, 0.55f, 0.12f, 1.0f);
	case EMegaJamFilmScene::FinalCut:
	default:
		// Climax gold / crimson accent
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

		// Check collection proximity
		if (!BoundPawn->HasCrashed() && FVector::DistSquared(PawnLocation, Pickup.Location) <= RadiusSq)
		{
			if (PawnTake == Pickup.RequiredTake)
			{
				Pickup.bActive = false;
				TargetISM->UpdateInstanceTransform(Pickup.PoolIndex, MakeHiddenInstanceTransform(), true, false, true);
				BoundPawn->NotifyFilmFrameCollected();
				bPickupsDirty = true;
				continue;
			}
			// Mismatched Take frame safely phases through without breaking the chain!
		}

		// Continuous target-ring spin
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

	if (!BoundPawn)
	{
		bBuilt = false;
		return;
	}

	FirstChunkIndex = FMath::FloorToInt(static_cast<float>(BoundPawn->GetActorLocation().X) / ChunkLength) - ChunksBehind;
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

	// Force the scene hook to fire again for the opening scene.
	LastBroadcastScene = -1;
	bBuilt = true;
}

void AMegaJamTrackGenerator::HandleTakeCut(EMegaJamTakeState NewTake)
{
	ApplyTakeCollision(NewTake);
}

void AMegaJamTrackGenerator::HandleRunStateChanged(EMegaJamRunState NewState, EMegaJamRunState OldState)
{
	if (NewState == EMegaJamRunState::ActiveFlow)
	{
		RebuildTrack();
	}
}

void AMegaJamTrackGenerator::ApplyTakeCollision(EMegaJamTakeState Take)
{
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

	// Assign modular art meshes with automatic fallback to Cube / Cylinder.
	FloorISM->SetStaticMesh(FloorTileMesh ? FloorTileMesh.Get() : BlockMesh.Get());
	WallISM->SetStaticMesh(WallSegmentMesh ? WallSegmentMesh.Get() : BlockMesh.Get());
	TakeA_ObstaclesISM->SetStaticMesh(ObstacleTakeAMesh ? ObstacleTakeAMesh.Get() : BlockMesh.Get());
	TakeB_ObstaclesISM->SetStaticMesh(ObstacleTakeBMesh ? ObstacleTakeBMesh.Get() : BlockMesh.Get());
	SetPieceISM->SetStaticMesh(SoundstageTrussMesh ? SoundstageTrussMesh.Get() : BlockMesh.Get());

	UStaticMesh* ResolvedPickupMesh = FilmFramePickupMesh
		? FilmFramePickupMesh.Get()
		: (CylinderMesh ? CylinderMesh.Get() : BlockMesh.Get());
	PickupISM->SetStaticMesh(ResolvedPickupMesh);
	PickupTakeB_ISM->SetStaticMesh(ResolvedPickupMesh);

	UInstancedStaticMeshComponent* All[] = {
		FloorISM, WallISM, TakeA_ObstaclesISM, TakeB_ObstaclesISM, SetPieceISM, PickupISM, PickupTakeB_ISM
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

	// Floor tile (100 cm base cube scaled to chunk length & corridor width)
	FloorISM->UpdateInstanceTransform(Slot,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, 0.0f, -50.0f), FVector(ChunkLength / 100.0f, CorridorHalfWidth * 2.0f / 100.0f, 1.0f)),
		true, false, true);

	// Corridor walls
	const FVector WallScale(ChunkLength / 100.0f, 1.0f, 10.0f);
	WallISM->UpdateInstanceTransform(Slot * 2,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, -CorridorHalfWidth, 500.0f), WallScale), true, false, true);
	WallISM->UpdateInstanceTransform(Slot * 2 + 1,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, CorridorHalfWidth, 500.0f), WallScale), true, false, true);

	// Project scene and difficulty density for this chunk
	float Density = 0.0f;
	int32 ProjectedSceneIdx = 0;
	if (BoundPawn)
	{
		const float PawnX = static_cast<float>(BoundPawn->GetActorLocation().X);
		const float Speed = FMath::Max(static_cast<float>(BoundPawn->GetFlowVelocity().X), MinProjectionSpeed);
		const float ProjectedTime = GetRunTimeSeconds() + FMath::Max(0.0f, ChunkCenterX - PawnX) / Speed;
		ProjectedSceneIdx = SceneIndexAtTime(ProjectedTime);
		const float SceneScale = FMath::Lerp(0.55f, 1.0f, static_cast<float>(ProjectedSceneIdx) / 3.0f);
		const float FlowScale = FMath::Lerp(0.65f, 1.0f, BoundPawn->GetFlowRatio());
		Density = FMath::Clamp(MaxObstacleDensity * SceneScale * FlowScale, 0.0f, 1.0f);
	}

	const EMegaJamFilmScene ChunkScene = static_cast<EMegaJamFilmScene>(ProjectedSceneIdx);
	FRandomStream Stream(Seed + ChunkIndex * 7919);

	// Populate 4-Scene Set Dressing and Aspect-Locked Collectible Film Frames
	PopulateSetPiecesForChunk(Slot, ChunkIndex, ChunkStartX, ChunkCenterX, ChunkScene, Stream);
	PopulatePickupsForChunk(Slot, ChunkIndex, ChunkStartX, Stream);

	const float RowLength = ChunkLength / Rows;

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const int32 BaseObstacleIndex = (Slot * Rows + Row) * ObstaclesPerRow;
		for (int32 SubIdx = 0; SubIdx < ObstaclesPerRow; ++SubIdx)
		{
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + SubIdx, MakeHiddenInstanceTransform(), true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + SubIdx, MakeHiddenInstanceTransform(), true, false, true);
		}

		// Fixed random draws per row for deterministic seeding
		const float PatternRoll = Stream.FRand();
		const float PresentRoll = Stream.FRand();
		const float TakeRoll = Stream.FRand();
		const float LateralRoll = Stream.FRand();
		const float HeightRoll = Stream.FRand();
		const float AirTakeRoll = Stream.FRand();
		const float AirLateralRoll = Stream.FRand();
		const float AirZRoll = Stream.FRand();

		if (ChunkIndex < SafeChunks || PresentRoll >= Density)
		{
			continue;
		}

		const float RowCenterX = ChunkStartX + (Row + 0.5f) * RowLength;

		// Pattern 1 (Row == 1, 22% chance): OVERHEAD CEILING GATE (21:9 Required)
		// Spans Y: [-900, 900] and Z: [220, 900], leaving Z < 220 open as a low crawlspace that forces Wide_21_9!
		if (Row == 1 && PatternRoll < 0.22f)
		{
			const FTransform CeilingGateTransform(
				FRotator::ZeroRotator,
				FVector(RowCenterX, 0.0f, 560.0f),
				FVector(ObstacleSize / 100.0f, 18.0f, 6.8f));

			// Solid in both Takes so the player MUST use 21:9 widescreen to duck under Z = 220.
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CeilingGateTransform, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CeilingGateTransform, true, false, true);

			// Add a Take-specific obstacle in the low crawlspace so the player also splices Takes while in 21:9.
			const float LowLateral = (LateralRoll < 0.5f) ? -550.0f : 550.0f;
			const FTransform LowBlockTransform(
				FRotator::ZeroRotator,
				FVector(RowCenterX, LowLateral, 100.0f),
				FVector(ObstacleSize / 100.0f, 5.0f, 2.0f));
			UInstancedStaticMeshComponent* LowTarget = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			LowTarget->UpdateInstanceTransform(BaseObstacleIndex + 1, LowBlockTransform, true, false, true);
			continue;
		}

		// Pattern 2 (Row == 3, 22% chance): NARROW VERTICAL SHAFT (4:3 Required)
		// Outer walls at Y: [-900, -420] and Y: [420, 900] (Z: [0, 900]), forcing Box_4_3 mode to thread the center shaft,
		// plus alternating high/low hurdles inside the center shaft.
		if (Row == 3 && PatternRoll >= 0.22f && PatternRoll < 0.44f)
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

			// Alternating high/low hurdle inside the center shaft (Y: [-420, 420])
			const bool bLowHurdle = (HeightRoll < 0.55f);
			const float HurdleZ = bLowHurdle ? 140.0f : 650.0f;
			const float HurdleHeight = 280.0f;
			const FTransform ShaftHurdle(
				FRotator::ZeroRotator,
				FVector(RowCenterX, 0.0f, HurdleZ),
				FVector(ObstacleSize / 100.0f, 8.2f, HurdleHeight / 100.0f));

			// Solid in both Takes when low so the player must climb in 4:3, or Take-specific when high!
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 2, ShaftHurdle, true, false, true);
			if (bLowHurdle)
			{
				TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 2, ShaftHurdle, true, false, true);
			}
			continue;
		}

		// Pattern 3 (Standard + High-Altitude Air Obstacles):
		// 3a. Ground-level Take A / Take B obstacle
		const float Height = FMath::Lerp(static_cast<float>(ObstacleHeightRange.X), static_cast<float>(ObstacleHeightRange.Y), HeightRoll);
		const float Lateral = FMath::Lerp(-ObstacleLateralRange, ObstacleLateralRange, LateralRoll);
		const FTransform GroundObstacleTransform(
			FRotator::ZeroRotator,
			FVector(RowCenterX, Lateral, Height * 0.5f),
			FVector(ObstacleSize / 100.0f, ObstacleSize * 1.35f / 100.0f, Height / 100.0f));

		UInstancedStaticMeshComponent* GroundTarget = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
		GroundTarget->UpdateInstanceTransform(BaseObstacleIndex, GroundObstacleTransform, true, false, true);

		// 3b. High-Altitude Air Obstacles (Z: 300-850) so flying in 4:3 requires active dodging and Take splicing!
		const float AirY = FMath::Lerp(-420.0f, 420.0f, AirLateralRoll);
		const float AirZ = FMath::Lerp(static_cast<float>(AirObstacleZRange.X), static_cast<float>(AirObstacleZRange.Y), AirZRoll);
		const FTransform AirObstacleTransform(
			FRotator::ZeroRotator,
			FVector(RowCenterX, AirY, AirZ),
			FVector(ObstacleSize / 100.0f, 3.2f, 2.2f));

		UInstancedStaticMeshComponent* AirTarget = (AirTakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
		AirTarget->UpdateInstanceTransform(BaseObstacleIndex + 1, AirObstacleTransform, true, false, true);
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
		// Scene 1 (0-35s): Overhead studio lighting trusses at Z = 920 spanning the soundstage
		PlaceSetPiece(0, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.25f, 0.0f, 920.0f), FVector(1.4f, 21.0f, 0.7f)));
		PlaceSetPiece(1, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.75f, 0.0f, 920.0f), FVector(1.4f, 21.0f, 0.7f)));
		PlaceSetPiece(2, FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, -940.0f, 920.0f), FVector(ChunkLength / 100.0f, 0.6f, 0.6f)));
		PlaceSetPiece(3, FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, 940.0f, 920.0f), FVector(ChunkLength / 100.0f, 0.6f, 0.6f)));
		break;

	case EMegaJamFilmScene::OverpassNight:
		// Scene 2 (35-75s): Tall flanking skyscraper silhouettes at Y = +/-1350
		PlaceSetPiece(0, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.3f, -1350.0f, 900.0f * Variation), FVector(4.5f, 4.5f, 18.0f * Variation)));
		PlaceSetPiece(1, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.3f, 1350.0f, 950.0f / Variation), FVector(4.5f, 4.5f, 19.0f / Variation)));
		PlaceSetPiece(2, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.8f, -1350.0f, 1100.0f), FVector(5.0f, 4.5f, 22.0f)));
		PlaceSetPiece(3, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.8f, 1350.0f, 1050.0f), FVector(5.0f, 4.5f, 21.0f)));
		break;

	case EMegaJamFilmScene::CuttingRoom:
		// Scene 3 (75-115s): Flanking film-reel pillars rotating slowly in Tick()
		PlaceSetPiece(0, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.25f, -1120.0f, 520.0f), FVector(4.2f, 4.2f, 10.5f)), true, 50.0f);
		PlaceSetPiece(1, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.25f, 1120.0f, 520.0f), FVector(4.2f, 4.2f, 10.5f)), true, -50.0f);
		PlaceSetPiece(2, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.75f, -1120.0f, 520.0f), FVector(4.2f, 4.2f, 10.5f)), true, 65.0f);
		PlaceSetPiece(3, FTransform(FRotator::ZeroRotator, FVector(ChunkStartX + ChunkLength * 0.75f, 1120.0f, 520.0f), FVector(4.2f, 4.2f, 10.5f)), true, -65.0f);
		break;

	case EMegaJamFilmScene::FinalCut:
	default:
		// Scene 4 (115-150s): Concentric aperture arches framing the track ahead
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

	if (ChunkIndex < SafeChunks)
	{
		return;
	}

	// Pickup 0: Outer-Wing Frame at Y = +/-700, Z = 130 (Requires Wide_21_9 because Box_4_3 clamps Y to +/-450)
	{
		const int32 PoolIdx = BasePickupIdx;
		const float WingY = (Stream.FRand() < 0.5f) ? -700.0f : 700.0f;
		const EMegaJamTakeState Take = (Stream.FRand() < 0.5f) ? EMegaJamTakeState::StudioTake : EMegaJamTakeState::DirectorsTake;
		const FVector PickupPos(ChunkStartX + ChunkLength * 0.35f, WingY, 130.0f);
		const FVector RingScale(1.35f, 1.35f, 0.28f);

		PickupSlots[PoolIdx].Location = PickupPos;
		PickupSlots[PoolIdx].Scale = RingScale;
		PickupSlots[PoolIdx].RequiredTake = Take;
		PickupSlots[PoolIdx].bActive = true;

		UInstancedStaticMeshComponent* TargetISM = (Take == EMegaJamTakeState::StudioTake) ? PickupISM.Get() : PickupTakeB_ISM.Get();
		TargetISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator(90.0f, 0.0f, 0.0f), PickupPos, RingScale), true, false, true);
	}

	// Pickup 1: High-Altitude Frame at Y in [-350, 350], Z = 620 (Requires Box_4_3 because Wide_21_9 clamps Z to 200)
	{
		const int32 PoolIdx = BasePickupIdx + 1;
		const float HighY = Stream.FRandRange(-350.0f, 350.0f);
		const EMegaJamTakeState Take = (Stream.FRand() < 0.5f) ? EMegaJamTakeState::StudioTake : EMegaJamTakeState::DirectorsTake;
		const FVector PickupPos(ChunkStartX + ChunkLength * 0.80f, HighY, 620.0f);
		const FVector RingScale(1.35f, 1.35f, 0.28f);

		PickupSlots[PoolIdx].Location = PickupPos;
		PickupSlots[PoolIdx].Scale = RingScale;
		PickupSlots[PoolIdx].RequiredTake = Take;
		PickupSlots[PoolIdx].bActive = true;

		UInstancedStaticMeshComponent* TargetISM = (Take == EMegaJamTakeState::StudioTake) ? PickupISM.Get() : PickupTakeB_ISM.Get();
		TargetISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator(90.0f, 0.0f, 0.0f), PickupPos, RingScale), true, false, true);
	}
}
