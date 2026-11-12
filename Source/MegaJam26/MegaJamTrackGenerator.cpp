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
	/** Parking spot for unused instances. Far below the floor and far from the pawn. */
	FTransform MakeHiddenInstanceTransform()
	{
		return FTransform(FRotator(0.0, 0.0, 0.0), FVector(0.0, 0.0, -100000.0), FVector(1.0, 1.0, 1.0));
	}

	void ConfigureISM(UInstancedStaticMeshComponent* ISM, USceneComponent* Parent, bool bGenerateOverlaps)
	{
		ISM->SetupAttachment(Parent);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetCollisionProfileName(TEXT("BlockAll"));
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

	// Floor and walls never generate overlaps, so the pawn's graze sphere only reacts to obstacles.
	ConfigureISM(FloorISM, TrackRoot, false);
	ConfigureISM(WallISM, TrackRoot, false);
	ConfigureISM(TakeA_ObstaclesISM, TrackRoot, true);
	ConfigureISM(TakeB_ObstaclesISM, TrackRoot, true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		BlockMesh = CubeFinder.Object;
	}

	// The ghost material is loaded softly in BeginPlay (no constructor lookup, so no error when it is missing).
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

	// The pawn may have appeared after BeginPlay.
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
		OnSceneChanged(CurrentScene);
	}

	// Recycle chunks that fell behind the pawn.
	const int32 DesiredFirst = FMath::FloorToInt(static_cast<float>(BoundPawn->GetActorLocation().X) / ChunkLength) - ChunksBehind;
	if (DesiredFirst < FirstChunkIndex)
	{
		// Pawn moved backwards (teleport). Re-lay everything.
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

	// The active take is solid. The inactive take is phased out (no collision, no overlap, so no crash and no graze).
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
	const int32 ObstacleSlots = Pool * FMath::Max(1, RowsPerChunk);

	UInstancedStaticMeshComponent* All[] = { FloorISM, WallISM, TakeA_ObstaclesISM, TakeB_ObstaclesISM };
	for (UInstancedStaticMeshComponent* ISM : All)
	{
		if (BlockMesh)
		{
			ISM->SetStaticMesh(BlockMesh);
		}
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

	// Cube mesh is 100 cm per unit scale.
	FloorISM->UpdateInstanceTransform(Slot,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, 0.0f, -50.0f), FVector(ChunkLength / 100.0f, CorridorHalfWidth * 2.0f / 100.0f, 1.0f)),
		true, false, true);

	// Walls: 100 cm thick, 1000 cm tall, standing on the floor plane.
	const FVector WallScale(ChunkLength / 100.0f, 1.0f, 10.0f);
	WallISM->UpdateInstanceTransform(Slot * 2,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, -CorridorHalfWidth, 500.0f), WallScale), true, false, true);
	WallISM->UpdateInstanceTransform(Slot * 2 + 1,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, CorridorHalfWidth, 500.0f), WallScale), true, false, true);

	// Difficulty scales with the film scene this chunk will be reached in, and with current speed.
	float Density = 0.0f;
	if (BoundPawn)
	{
		const float PawnX = static_cast<float>(BoundPawn->GetActorLocation().X);
		const float Speed = FMath::Max(static_cast<float>(BoundPawn->GetFlowVelocity().X), MinProjectionSpeed);
		const float ProjectedTime = GetRunTimeSeconds() + FMath::Max(0.0f, ChunkCenterX - PawnX) / Speed;
		const int32 SceneIndex = SceneIndexAtTime(ProjectedTime);
		const float SceneScale = static_cast<float>(SceneIndex + 1) / 4.0f;
		const float FlowScale = FMath::Lerp(0.6f, 1.0f, BoundPawn->GetFlowRatio());
		Density = FMath::Clamp(MaxObstacleDensity * SceneScale * FlowScale, 0.0f, 1.0f);
	}

	FRandomStream Stream(Seed + ChunkIndex * 7919);
	const float RowLength = ChunkLength / Rows;

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		// Always draw the same four values per row so the layout does not depend on density.
		const float PresentRoll = Stream.FRand();
		const float TakeRoll = Stream.FRand();
		const float LateralRoll = Stream.FRand();
		const float HeightRoll = Stream.FRand();

		const int32 InstanceIndex = Slot * Rows + Row;
		TakeA_ObstaclesISM->UpdateInstanceTransform(InstanceIndex, MakeHiddenInstanceTransform(), true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(InstanceIndex, MakeHiddenInstanceTransform(), true, false, true);

		if (ChunkIndex < SafeChunks || PresentRoll >= Density)
		{
			continue;
		}

		const float Height = FMath::Lerp(static_cast<float>(ObstacleHeightRange.X), static_cast<float>(ObstacleHeightRange.Y), HeightRoll);
		const float Lateral = FMath::Lerp(-ObstacleLateralRange, ObstacleLateralRange, LateralRoll);
		const float RowCenterX = ChunkStartX + (Row + 0.5f) * RowLength;

		const FTransform ObstacleTransform(
			FRotator::ZeroRotator,
			FVector(RowCenterX, Lateral, Height * 0.5f),
			FVector(ObstacleSize / 100.0f, ObstacleSize / 100.0f, Height / 100.0f));

		UInstancedStaticMeshComponent* Target = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
		Target->UpdateInstanceTransform(InstanceIndex, ObstacleTransform, true, false, true);
	}
}
