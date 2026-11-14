// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamTrackGenerator.h"

#include "MegaJamFlowPawn.h"
#include "MegaJamFlowSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/PointLight.h"
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
	LaneRailsISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LaneRailsISM"));
	WrapArchISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WrapArchISM"));

	ConfigureISM(FloorISM, TrackRoot, true, false);
	ConfigureISM(WallISM, TrackRoot, true, false);
	ConfigureISM(TakeA_ObstaclesISM, TrackRoot, true, true);
	ConfigureISM(TakeB_ObstaclesISM, TrackRoot, true, true);
	TakeA_ObstaclesISM->NumCustomDataFloats = FMath::Max(TakeA_ObstaclesISM->NumCustomDataFloats, 1);
	TakeB_ObstaclesISM->NumCustomDataFloats = FMath::Max(TakeB_ObstaclesISM->NumCustomDataFloats, 1);

	ConfigureISM(SetPieceISM, TrackRoot, false, false);
	ConfigureISM(PickupISM, TrackRoot, false, false);
	ConfigureISM(PickupTakeB_ISM, TrackRoot, false, false);
	ConfigureISM(BoostStripISM, TrackRoot, false, false);
	ConfigureISM(LaneRailsISM, TrackRoot, false, false);
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
		ObstacleMatA = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		ObstacleMatA->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		ObstacleMatA->SetVectorParameterValue(TEXT("TakeColor"), TakeAColor);
		TakeA_ObstaclesISM->SetMaterial(0, ObstacleMatA);

		ObstacleMatB = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		ObstacleMatB->SetScalarParameterValue(TEXT("TakeIndex"), 1.0f);
		ObstacleMatB->SetVectorParameterValue(TEXT("TakeColor"), TakeBColor);
		TakeB_ObstaclesISM->SetMaterial(0, ObstacleMatB);

		PickupMatA = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		PickupMatA->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		PickupMatA->SetVectorParameterValue(TEXT("TakeColor"), FLinearColor(0.2f, 0.95f, 1.0f, 1.0f));
		PickupISM->SetMaterial(0, PickupMatA);

		PickupMatB = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
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

		LaneRailMID = UMaterialInstanceDynamic::Create(ObstacleMaterial, this);
		LaneRailMID->SetScalarParameterValue(TEXT("TakeIndex"), 0.0f);
		LaneRailMID->SetScalarParameterValue(TEXT("Opacity"), 0.35f);
		LaneRailMID->SetVectorParameterValue(TEXT("TakeColor"), FLinearColor(0.12f, 0.72f, 0.95f, 1.0f));
		LaneRailsISM->SetMaterial(0, LaneRailMID);
	}
	else
	{
		ObstacleMatA = TakeA_ObstaclesISM->CreateAndSetMaterialInstanceDynamic(0);
		ObstacleMatB = TakeB_ObstaclesISM->CreateAndSetMaterialInstanceDynamic(0);
		PickupMatA = PickupISM->CreateAndSetMaterialInstanceDynamic(0);
		PickupMatB = PickupTakeB_ISM->CreateAndSetMaterialInstanceDynamic(0);
		BoostStripMID = BoostStripISM->CreateAndSetMaterialInstanceDynamic(0);
		LaneRailMID = LaneRailsISM->CreateAndSetMaterialInstanceDynamic(0);
	}

	if (UWorld* World = GetWorld())
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->OnTakeSwapped.AddUniqueDynamic(this, &AMegaJamTrackGenerator::HandleTakeSwapped);
		}
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
	if (UWorld* World = GetWorld())
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->OnTakeSwapped.RemoveDynamic(this, &AMegaJamTrackGenerator::HandleTakeSwapped);
		}
	}
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
		LaneRailsISM->MarkRenderStateDirty();
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

	const float CurrentDistMeters = BoundGameMode
		? BoundGameMode->GetDistanceMeters()
		: FMath::Max(0.0f, static_cast<float>(BoundPawn->GetActorLocation().X) / 100.0f);

	if (CurrentDistMeters >= 8600.0f && !bWrapArchSpawned)
	{
		bWrapArchSpawned = true;
		// Golden Wrap Arch at 8,940m (894,000 cm) framing the 9,000m finish line
		const float ArchX = 894000.0f;
		const float HalfW = MEGAJAM_CORRIDOR_HALF_WIDTH;
		const float FullWidthScaleY = (HalfW * 2.0f) / 100.0f;

		WrapArchISM->UpdateInstanceTransform(0, FTransform(FRotator::ZeroRotator, FVector(ArchX, -(HalfW - 120.0f), 500.0f), FVector(2.8f, 2.4f, 10.0f)), true, false, true);
		WrapArchISM->UpdateInstanceTransform(1, FTransform(FRotator::ZeroRotator, FVector(ArchX, HalfW - 120.0f, 500.0f), FVector(2.8f, 2.4f, 10.0f)), true, false, true);
		WrapArchISM->UpdateInstanceTransform(2, FTransform(FRotator::ZeroRotator, FVector(ArchX, 0.0f, 960.0f), FVector(3.2f, FullWidthScaleY, 2.2f)), true, false, true);
		WrapArchISM->UpdateInstanceTransform(3, FTransform(FRotator(0.0f, 0.0f, 25.0f), FVector(ArchX - 250.0f, -(HalfW - 320.0f), 760.0f), FVector(2.0f, 1.8f, 5.5f)), true, false, true);
		WrapArchISM->UpdateInstanceTransform(4, FTransform(FRotator(0.0f, 0.0f, -25.0f), FVector(ArchX - 250.0f, HalfW - 320.0f, 760.0f), FVector(2.0f, 1.8f, 5.5f)), true, false, true);
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

	const float ActiveTakeVal = (BoundPawn && BoundPawn->GetCurrentTake() == EMegaJamTakeState::DirectorsTake) ? 1.0f : 0.0f;

	if (SetPieceMID)
	{
		SetPieceMID->SetScalarParameterValue(TEXT("TakeIndex"), ActiveTakeVal);
		SetPieceMID->SetVectorParameterValue(TEXT("TakeColor"), CurrentSceneColor);
	}

	// Dynamic Chain & Emissive Pulse for PickupISM / PickupTakeB_ISM and BoostStripISM:
	// Shift smoothly from Cool Cyan (x1-x3) -> Warm Gold (x4-x6) -> Electric Magenta-White (x7-x8 MAX CHAIN),
	// modulated by a rhythmic beat pulse scaled by SpeedRatio.
	const int32 Chain = BoundPawn ? BoundPawn->GetSpliceChain() : 1;
	const float FlowRatio = BoundPawn ? BoundPawn->GetFlowRatio() : 0.0f;
	const float SpeedRatio = (BoundPawn && BoundPawn->TerminalVelocity > KINDA_SMALL_NUMBER && !BoundPawn->HasCrashed())
		? FMath::Clamp(static_cast<float>(BoundPawn->GetFlowVelocity().X) / BoundPawn->TerminalVelocity, 0.0f, 1.0f)
		: 0.0f;
	const float TimeSec = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.0f;
	const float BeatPulse = 1.0f + SpeedRatio * (0.35f + 0.35f * FMath::Sin(TimeSec * (8.0f + SpeedRatio * 10.0f)));

	const FLinearColor CyanTint(0.15f, 0.95f, 1.0f, 1.0f);
	const FLinearColor GoldTint(1.0f, 0.85f, 0.18f, 1.0f);
	const FLinearColor MagentaWhiteTint(1.0f, 0.45f, 1.0f, 1.0f);

	FLinearColor ChainTint = CyanTint;
	if (Chain <= 3)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Chain - 1) / 2.0f, 0.0f, 1.0f);
		ChainTint = FMath::Lerp(CyanTint, FLinearColor(0.35f, 1.0f, 0.88f, 1.0f), Alpha);
	}
	else if (Chain <= 6)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Chain - 3) / 3.0f, 0.0f, 1.0f);
		ChainTint = FMath::Lerp(CyanTint, GoldTint, Alpha);
	}
	else
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Chain - 6) / 2.0f, 0.0f, 1.0f);
		ChainTint = FMath::Lerp(GoldTint, MagentaWhiteTint, Alpha);
	}

	const float EmissiveScale = (1.1f + FlowRatio * 1.4f) * BeatPulse;
	const FLinearColor PulsedChainColor = ChainTint * EmissiveScale;

	auto ApplyPulseToMID = [&](UMaterialInstanceDynamic* MID, float TakeIdxVal)
	{
		if (!MID)
		{
			return;
		}
		MID->SetScalarParameterValue(TEXT("TakeIndex"), TakeIdxVal);
		MID->SetVectorParameterValue(TEXT("TakeColor"), PulsedChainColor);
		MID->SetVectorParameterValue(TEXT("Color"), ChainTint);
		MID->SetVectorParameterValue(TEXT("BaseColor"), ChainTint);
		MID->SetVectorParameterValue(TEXT("EmissiveColor"), PulsedChainColor);
	};

	ApplyPulseToMID(BoostStripMID, ActiveTakeVal);
	ApplyPulseToMID(PickupMatA, 0.0f);
	ApplyPulseToMID(PickupMatB, 1.0f);

	if (LaneRailMID)
	{
		const bool bWide = BoundPawn && BoundPawn->IsInBulletTimeOverdrive();
		const float RailPulse = (bWide ? 0.55f : 0.22f) * (0.80f + 0.20f * FMath::Sin(TimeSec * 8.0f));
		const FLinearColor RailColor = FLinearColor(0.15f, 0.85f, 1.0f, 1.0f) * RailPulse;
		LaneRailMID->SetScalarParameterValue(TEXT("TakeIndex"), ActiveTakeVal);
		LaneRailMID->SetScalarParameterValue(TEXT("Opacity"), bWide ? 0.60f : 0.25f);
		LaneRailMID->SetVectorParameterValue(TEXT("TakeColor"), RailColor);
		LaneRailMID->SetVectorParameterValue(TEXT("Color"), RailColor);
		LaneRailMID->SetVectorParameterValue(TEXT("EmissiveColor"), RailColor);
	}
}

void AMegaJamTrackGenerator::UpdateDynamicAnimationsAndPickups(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !BoundPawn)
	{
		return;
	}

	const UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	const float HazardScale = FlowSubsystem ? FlowSubsystem->HazardTimeScale : 1.0f;
	ScaledTrackTime += DeltaSeconds * HazardScale;

	// 1. Rotate set-piece pillars in SetPieceISM (scaled by HazardTimeScale so Bullet-Time Overdrive slows them to 0.45x)
	bool bSetPiecesDirty = false;
	for (int32 Index = 0; Index < SetPieceSlots.Num(); ++Index)
	{
		const FMegaJamSetPieceSlot& Slot = SetPieceSlots[Index];
		if (Slot.bActive && Slot.bRotates)
		{
			FTransform AnimatedTransform = Slot.BaseTransform;
			const FRotator SpinRot(0.0f, ScaledTrackTime * Slot.RotationSpeedDeg, 0.0f);
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
	// Double pickup magnet radius (380 cm) when 21:9 Bullet-Time Overdrive is active!
	const FVector PawnLocation = BoundPawn->GetActorLocation();
	const float EffectivePickupRadius = BoundPawn->IsInBulletTimeOverdrive()
		? 380.0f
		: PickupCollectionRadius;
	const float RadiusSq = FMath::Square(EffectivePickupRadius);
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

		if (Pickup.MagnetImmunityTimer > 0.0f)
		{
			Pickup.MagnetImmunityTimer = FMath::Max(0.0f, Pickup.MagnetImmunityTimer - DeltaSeconds);
		}

		if (Pickup.bHasBallisticVelocity)
		{
			Pickup.Location += Pickup.Velocity * DeltaSeconds;
			Pickup.Velocity.Z -= 2200.0f * DeltaSeconds;
			Pickup.Location.Y = FMath::Clamp(static_cast<float>(Pickup.Location.Y), -CorridorHalfWidth + 120.0f, CorridorHalfWidth - 120.0f);
			if (Pickup.Location.Z < 110.0f)
			{
				Pickup.Location.Z = 110.0f;
				Pickup.Velocity.Z = FMath::Abs(Pickup.Velocity.Z) * 0.45f;
			}
		}

		if (Pickup.MagnetImmunityTimer <= 0.0f)
		{
			if (Pickup.bHasBallisticVelocity)
			{
				// Gentle magnet pull once within 2.5x pickup radius so blasted frames swoop cleanly into the player
				const float MagnetRange = EffectivePickupRadius * 2.5f;
				if (bIsActivelyPlaying && !BoundPawn->HasCrashed() && FVector::DistSquared(PawnLocation, Pickup.Location) <= FMath::Square(MagnetRange))
				{
					Pickup.Location = FMath::VInterpTo(Pickup.Location, PawnLocation, DeltaSeconds, 11.0f);
				}
			}

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
		}

		const float BobZ = FMath::Sin(ScaledTrackTime * 4.0f + static_cast<float>(Index)) * 14.0f;
		const FRotator RingRot(90.0f, ScaledTrackTime * 110.0f + static_cast<float>(Index * 45), 0.0f);
		const FTransform RingTransform(RingRot, Pickup.Location + FVector(0.0f, 0.0f, BobZ), Pickup.Scale);
		TargetISM->UpdateInstanceTransform(Pickup.PoolIndex, RingTransform, true, false, true);
		bPickupsDirty = true;
	}

	if (bPickupsDirty)
	{
		PickupISM->MarkRenderStateDirty();
		PickupTakeB_ISM->MarkRenderStateDirty();
	}

	// 3. Outer-Wing Dolly Boost Strips: collect when riding outer lanes in Wide_21_9 or low 4:3
	if (bIsActivelyPlaying && !BoundPawn->HasCrashed())
	{
		bool bStripsDirty = false;
		for (int32 Index = 0; Index < BoostStripSlots.Num(); ++Index)
		{
			FMegaJamDollyBoostStrip& Strip = BoostStripSlots[Index];
			if (!Strip.bActive)
			{
				continue;
			}

			if (FMath::Abs(PawnLocation.X - Strip.Location.X) <= 300.0f &&
				FMath::Abs(PawnLocation.Y - Strip.Location.Y) <= 210.0f &&
				PawnLocation.Z <= 240.0f)
			{
				Strip.bActive = false;
				BoostStripISM->UpdateInstanceTransform(Strip.PoolIndex, MakeHiddenInstanceTransform(), true, false, true);
				BoundPawn->NotifyDollyBoostStripCollected();
				BoundPawn->TriggerDollyBoost();
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

	// Clear any leftover harvested burst Film Frames
	const int32 BaseBurstIdx = PoolSize() * PickupsPerChunk;
	for (int32 BurstIdx = BaseBurstIdx; BurstIdx < PickupSlots.Num(); ++BurstIdx)
	{
		PickupSlots[BurstIdx].bActive = false;
		PickupSlots[BurstIdx].bHasBallisticVelocity = false;
		PickupSlots[BurstIdx].MagnetImmunityTimer = 0.0f;
		PickupSlots[BurstIdx].Velocity = FVector::ZeroVector;
		PickupISM->UpdateInstanceTransform(BurstIdx, MakeHiddenInstanceTransform(), true, false, true);
		PickupTakeB_ISM->UpdateInstanceTransform(BurstIdx, MakeHiddenInstanceTransform(), true, false, true);
	}
	NextBurstPickupCursor = 0;

	FloorISM->MarkRenderStateDirty();
	WallISM->MarkRenderStateDirty();
	TakeA_ObstaclesISM->MarkRenderStateDirty();
	TakeB_ObstaclesISM->MarkRenderStateDirty();
	SetPieceISM->MarkRenderStateDirty();
	PickupISM->MarkRenderStateDirty();
	PickupTakeB_ISM->MarkRenderStateDirty();
	BoostStripISM->MarkRenderStateDirty();
	LaneRailsISM->MarkRenderStateDirty();

	ApplyTakeCollision(BoundPawn->GetCurrentTake());

	LastBroadcastScene = -1;
	bBuilt = true;
}

void AMegaJamTrackGenerator::HandleTakeCut(EMegaJamTakeState NewTake)
{
	ApplyTakeCollision(NewTake);
}

void AMegaJamTrackGenerator::HandleTakeSwapped(bool bNewTakeState)
{
	ApplyTakeCollision(bNewTakeState ? EMegaJamTakeState::StudioTake : EMegaJamTakeState::DirectorsTake);
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

	// Dynamically toggle solid vs ghosted/wireframe material parameters for Take A and Take B obstacles
	const float ActiveTakeVal = bStudio ? 0.0f : 1.0f;
	if (ObstacleMatA)
	{
		const FLinearColor ColorA = bStudio ? TakeAColor * 1.35f : TakeAColor * 0.22f;
		ObstacleMatA->SetScalarParameterValue(TEXT("ActiveTake"), ActiveTakeVal);
		ObstacleMatA->SetScalarParameterValue(TEXT("Opacity"), bStudio ? 1.0f : 0.22f);
		ObstacleMatA->SetVectorParameterValue(TEXT("TakeColor"), ColorA);
		ObstacleMatA->SetVectorParameterValue(TEXT("Color"), ColorA);
		ObstacleMatA->SetVectorParameterValue(TEXT("BaseColor"), ColorA);
		ObstacleMatA->SetVectorParameterValue(TEXT("EmissiveColor"), ColorA);
	}
	if (ObstacleMatB)
	{
		const FLinearColor ColorB = !bStudio ? TakeBColor * 1.35f : TakeBColor * 0.22f;
		ObstacleMatB->SetScalarParameterValue(TEXT("ActiveTake"), ActiveTakeVal);
		ObstacleMatB->SetScalarParameterValue(TEXT("Opacity"), !bStudio ? 1.0f : 0.22f);
		ObstacleMatB->SetVectorParameterValue(TEXT("TakeColor"), ColorB);
		ObstacleMatB->SetVectorParameterValue(TEXT("Color"), ColorB);
		ObstacleMatB->SetVectorParameterValue(TEXT("BaseColor"), ColorB);
		ObstacleMatB->SetVectorParameterValue(TEXT("EmissiveColor"), ColorB);
	}
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
	const int32 TotalPickupSlots = Pool * PickupsPerChunk + BurstPickupPoolSize;
	const int32 TotalBoostStripSlots = Pool * BoostStripsPerChunk;

	FloorISM->SetStaticMesh(FloorTileMesh ? FloorTileMesh.Get() : BlockMesh.Get());
	WallISM->SetStaticMesh(WallSegmentMesh ? WallSegmentMesh.Get() : BlockMesh.Get());
	TakeA_ObstaclesISM->SetStaticMesh(ObstacleTakeAMesh ? ObstacleTakeAMesh.Get() : BlockMesh.Get());
	TakeB_ObstaclesISM->SetStaticMesh(ObstacleTakeBMesh ? ObstacleTakeBMesh.Get() : BlockMesh.Get());
	SetPieceISM->SetStaticMesh(SoundstageTrussMesh ? SoundstageTrussMesh.Get() : BlockMesh.Get());
	BoostStripISM->SetStaticMesh(BlockMesh.Get());
	LaneRailsISM->SetStaticMesh(BlockMesh.Get());
	WrapArchISM->SetStaticMesh(ApertureRingMesh ? ApertureRingMesh.Get() : BlockMesh.Get());

	UStaticMesh* ResolvedPickupMesh = FilmFramePickupMesh
		? FilmFramePickupMesh.Get()
		: (CylinderMesh ? CylinderMesh.Get() : BlockMesh.Get());
	PickupISM->SetStaticMesh(ResolvedPickupMesh);
	PickupTakeB_ISM->SetStaticMesh(ResolvedPickupMesh);

	UInstancedStaticMeshComponent* All[] = {
		FloorISM, WallISM, TakeA_ObstaclesISM, TakeB_ObstaclesISM, SetPieceISM, PickupISM, PickupTakeB_ISM, BoostStripISM, LaneRailsISM, WrapArchISM
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

	TakeA_ObstaclesISM->NumCustomDataFloats = FMath::Max(TakeA_ObstaclesISM->NumCustomDataFloats, 1);
	TakeB_ObstaclesISM->NumCustomDataFloats = FMath::Max(TakeB_ObstaclesISM->NumCustomDataFloats, 1);

	for (int32 Index = 0; Index < ObstacleSlots; ++Index)
	{
		const int32 InstIdxA = TakeA_ObstaclesISM->AddInstance(MakeHiddenInstanceTransform(), true);
		TakeA_ObstaclesISM->SetCustomDataValue(InstIdxA, 0, 0.0f);
		const int32 InstIdxB = TakeB_ObstaclesISM->AddInstance(MakeHiddenInstanceTransform(), true);
		TakeB_ObstaclesISM->SetCustomDataValue(InstIdxB, 0, 0.0f);
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

	for (int32 Index = 0; Index < Pool * LaneRailsPerChunk; ++Index)
	{
		LaneRailsISM->AddInstance(MakeHiddenInstanceTransform(), true);
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
	// Scene 01 (Action Vertical Slice): strict Beat & Rest pacing —
	// every Action Gauntlet chunk (even) is followed by a clean 4:3 Flight Charge Runway chunk (odd).
	const int32 Mod2 = ((ChunkIndex % 2) + 2) % 2;
	return (Mod2 == 0);
}

int32 AMegaJamTrackGenerator::GetHazardArchetypeForChunk(int32 ChunkIndex) const
{
	// Deterministic cycle across the 4 clean Action Gauntlet set-piece archetypes:
	// 0 = Color-Splice Twin Gate (Take A / Take B side-by-side floor & air gates)
	// 1 = Action Split Pillars (Center pillar with wide Take A / Take B outer lanes & boost strips)
	// 2 = Overhead Laser Curtain (Upper barrier Z > 300 with open Z = 140 Overdrive rail)
	// 3 = Alternating Take Cross-Beams (High & Low Take A / Take B laser beams)
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
	const float ChunkDistMeters = FMath::Max(0.0f, ChunkStartX / 100.0f);

	// Scene 05 The Cliff Drop (DistanceMeters >= 7200.0f): drop the canyon floor beneath the track down to Z = -900.0f
	// while keeping the glowing holographic energy rails (LaneRailsISM at Z = 2.0f) suspended in mid-air!
	const float FloorZ = (ChunkDistMeters >= 7200.0f) ? -900.0f : -50.0f;
	FloorISM->UpdateInstanceTransform(Slot,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, 0.0f, FloorZ), FVector(ChunkLength / 100.0f, CorridorHalfWidth * 2.0f / 100.0f, 1.0f)),
		true, false, true);

	const FVector WallScale(ChunkLength / 100.0f, 1.0f, 10.0f);
	WallISM->UpdateInstanceTransform(Slot * 2,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, -CorridorHalfWidth, 500.0f), WallScale), true, false, true);
	WallISM->UpdateInstanceTransform(Slot * 2 + 1,
		FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, CorridorHalfWidth, 500.0f), WallScale), true, false, true);

	// 5 Visual Magnetic Floor Rails at exact lane Y-coordinates (-700, -350, 0, 350, 700)
	const FVector RailScale(ChunkLength / 100.0f, 0.14f, 0.04f);
	for (int32 LaneIdx = 0; LaneIdx < LaneRailsPerChunk; ++LaneIdx)
	{
		const float LaneY = static_cast<float>(LaneIdx - 2) * 350.0f;
		LaneRailsISM->UpdateInstanceTransform(
			Slot * LaneRailsPerChunk + LaneIdx,
			FTransform(FRotator::ZeroRotator, FVector(ChunkCenterX, LaneY, 2.0f), RailScale),
			true, false, true);
	}

	const bool bTitleOrCompleted = BoundGameMode && !BoundGameMode->IsPlaying();
	const float CurrentDistance = ChunkDistMeters;
	const int32 ChunkGenreIdx = FMath::Clamp(FMath::FloorToInt(CurrentDistance / 1800.0f), 0, 4);

	// Visual progression across Scene 01 (0m-1,800m Action), Scene 02 (1,800m-3,600m Western Standoff), Scene 03 (3,600m-5,400m Noir Spy Car), Scene 04 (5,400m-7,200m Chase Snowboard), and Scene 05 (7,200m-9,000m Final Cut Montage)
	EMegaJamFilmScene ChunkScene = EMegaJamFilmScene::Soundstage;
	if (ChunkDistMeters >= 5400.0f)
	{
		ChunkScene = EMegaJamFilmScene::FinalCut;
	}
	else if (ChunkDistMeters >= 3600.0f)
	{
		ChunkScene = EMegaJamFilmScene::OverpassNight;
	}
	else if (ChunkDistMeters >= 1800.0f)
	{
		ChunkScene = EMegaJamFilmScene::CuttingRoom;
	}
	else if (ChunkDistMeters >= 900.0f)
	{
		ChunkScene = EMegaJamFilmScene::OverpassNight;
	}

	FRandomStream Stream(Seed + ChunkIndex * 7919);

	// Keep chunks before SafeUntilChunkIndex, around the 1,800m, 3,600m, 5,400m, and 7,200m Clapperboard transitions, and beyond 8,880m obstacle-free
	const bool bInSceneTransitionZone =
		(ChunkDistMeters >= 1720.0f && ChunkDistMeters < 1920.0f) ||
		(ChunkDistMeters >= 3520.0f && ChunkDistMeters < 3720.0f) ||
		(ChunkDistMeters >= 5320.0f && ChunkDistMeters < 5520.0f) ||
		(ChunkDistMeters >= 7120.0f && ChunkDistMeters < 7320.0f);
	const bool bInSafeOrFinishZone = (ChunkIndex < SafeUntilChunkIndex || bInSceneTransitionZone || ChunkDistMeters >= 8880.0f || bTitleOrCompleted);
	const bool bIsHazardChunk = !bInSafeOrFinishZone && IsHazardChunkForScene(ChunkIndex, ChunkScene);
	const int32 HazardArchetype = GetHazardArchetypeForChunk(ChunkIndex);

	PopulateSetPiecesForChunk(Slot, ChunkIndex, ChunkStartX, ChunkCenterX, ChunkScene, Stream);
	PopulatePickupsForChunk(Slot, ChunkIndex, ChunkStartX, ChunkScene, bIsHazardChunk, HazardArchetype, Stream);
	PopulateBoostStripsForChunk(Slot, ChunkIndex, ChunkStartX, bIsHazardChunk, HazardArchetype, Stream);

	const float RowLength = ChunkLength / Rows;

	// Clear all obstacle slots in this chunk first and stamp Per-Instance Custom Data 0 with ChunkGenreIdx
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const int32 BaseObstacleIndex = (Slot * Rows + Row) * ObstaclesPerRow;
		for (int32 SubIdx = 0; SubIdx < ObstaclesPerRow; ++SubIdx)
		{
			const int32 InstIdx = BaseObstacleIndex + SubIdx;
			TakeA_ObstaclesISM->UpdateInstanceTransform(InstIdx, MakeHiddenInstanceTransform(), true, false, true);
			TakeA_ObstaclesISM->SetCustomDataValue(InstIdx, 0, (float)ChunkGenreIdx);
			TakeB_ObstaclesISM->UpdateInstanceTransform(InstIdx, MakeHiddenInstanceTransform(), true, false, true);
			TakeB_ObstaclesISM->SetCustomDataValue(InstIdx, 0, (float)ChunkGenreIdx);
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

	// Flight Charge Runway Chunks (and start/finish safe zones) contain zero lethal blocks!
	if (!bIsHazardChunk)
	{
		return;
	}

	// Action Gauntlets, Scene 02 Western Standoff Barricades, Scene 03 Noir Roadblocks, Scene 04 Chase Slalom/Boulders, and Scene 05 Match-Cut Montage!
	const int32 CenterRow = FMath::Clamp(Rows / 2, 0, Rows - 1);
	const int32 BaseObstacleIndex = (Slot * Rows + CenterRow) * ObstaclesPerRow;
	const float HazardX = ChunkCenterX;
	const float TakeRoll = Stream.FRand();

	// Determine effective obstacle genre for this chunk (in Scene 05 Montage 7,200m-8,940m, match the 400m sub-genre cycle!)
	const bool bIsChaseChunk = (ChunkDistMeters >= 5400.0f && ChunkDistMeters < 7200.0f) || (ChunkDistMeters >= 8400.0f);
	const bool bIsNoirChunk = (ChunkDistMeters >= 3600.0f && ChunkDistMeters < 5400.0f) || (ChunkDistMeters >= 8000.0f && ChunkDistMeters < 8400.0f);
	const bool bIsWesternChunk = (ChunkDistMeters >= 1800.0f && ChunkDistMeters < 3600.0f) || (ChunkDistMeters >= 7600.0f && ChunkDistMeters < 8000.0f);

	// Scene 04 Chase (5,400m-7,200m) & Montage Chase Segment (8,400m-8,940m): Snowboard Slalom Gates & Snowdrift/Boulder Clusters for +60% carving and Comet Smash!
	if (bIsChaseChunk)
	{
		if (HazardArchetype % 2 == 0)
		{
			// Snowdrift / Ice-Boulder Cluster across 3 lanes + low ridge (Comet-Smashable!)
			const float CenterBoulderY = (ChunkIndex % 4 == 0) ? -350.0f : ((ChunkIndex % 4 == 2) ? 350.0f : 0.0f);
			const FTransform IceBoulderCluster(
				FRotator(0.0f, 15.0f, 8.0f),
				FVector(HazardX, CenterBoulderY, 150.0f),
				FVector(ObstacleSize / 100.0f, 7.2f, 2.8f));
			const FTransform LowAvalancheRidge(
				FRotator::ZeroRotator,
				FVector(HazardX, 0.0f, 115.0f),
				FVector(ObstacleSize / 100.0f, 15.5f, 1.85f));
			const FTransform OuterSlalomPillar(
				FRotator(0.0f, -12.0f, 0.0f),
				FVector(HazardX, -CenterBoulderY, 320.0f),
				FVector(ObstacleSize / 100.0f, 3.4f, 6.4f));

			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, IceBoulderCluster, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, IceBoulderCluster, true, false, true);

			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, LowAvalancheRidge, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, LowAvalancheRidge, true, false, true);

			UInstancedStaticMeshComponent* SlalomTarget = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
			SlalomTarget->UpdateInstanceTransform(BaseObstacleIndex + 2, OuterSlalomPillar, true, false, true);
		}
		else
		{
			// Downhill Slalom Gates across Lanes 1, 2, 3 (-350, 0, +350) with outer Dolly Boost rails at +/-700
			const FTransform CenterIceMonolith(
				FRotator(0.0f, 22.0f, 0.0f),
				FVector(HazardX, 0.0f, 210.0f),
				FVector(ObstacleSize / 100.0f, 3.6f, 4.2f));
			const FTransform LeftSlalomGate(
				FRotator::ZeroRotator,
				FVector(HazardX, -350.0f, 165.0f),
				FVector(ObstacleSize / 100.0f, 3.5f, 3.1f));
			const FTransform RightSlalomGate(
				FRotator::ZeroRotator,
				FVector(HazardX, 350.0f, 165.0f),
				FVector(ObstacleSize / 100.0f, 3.5f, 3.1f));

			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CenterIceMonolith, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CenterIceMonolith, true, false, true);

			const bool bLeftIsTakeA = (TakeRoll < 0.5f);
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bLeftIsTakeA ? LeftSlalomGate : RightSlalomGate, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bLeftIsTakeA ? RightSlalomGate : LeftSlalomGate, true, false, true);
		}
		return;
	}

	// Scene 03 Noir (3,600m-5,400m) & Montage Noir Segment (8,000m-8,400m): Spy Car Roadblocks & Enemy Sedans across the 5 lanes (built for 2x Adrenaline Graze & Vertigo Pulse!)
	if (bIsNoirChunk)
	{
		if (HazardArchetype % 2 == 0)
		{
			// Noir Enemy Sedan Chicane across alternating lanes for tight 2x Adrenaline Graze passes
			const float SedanCenterY = (ChunkIndex % 4 == 0) ? -350.0f : ((ChunkIndex % 4 == 2) ? 350.0f : 0.0f);
			const FTransform LeadEnemySedan(
				FRotator(0.0f, 12.0f, 0.0f),
				FVector(HazardX, SedanCenterY, 135.0f),
				FVector(ObstacleSize / 100.0f, 4.4f, 2.5f));
			const FTransform FlankingRoadblockLeft(
				FRotator(0.0f, -8.0f, 0.0f),
				FVector(HazardX, -700.0f, 145.0f),
				FVector(ObstacleSize / 100.0f, 3.4f, 2.8f));
			const FTransform FlankingRoadblockRight(
				FRotator(0.0f, 8.0f, 0.0f),
				FVector(HazardX, 700.0f, 145.0f),
				FVector(ObstacleSize / 100.0f, 3.4f, 2.8f));

			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, LeadEnemySedan, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, LeadEnemySedan, true, false, true);

			const bool bLeftIsTakeA = (TakeRoll < 0.5f);
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bLeftIsTakeA ? FlankingRoadblockLeft : FlankingRoadblockRight, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bLeftIsTakeA ? FlankingRoadblockRight : FlankingRoadblockLeft, true, false, true);
		}
		else
		{
			// Noir Split Roadblock & Searchlight Arch across Lanes 1, 2, 3 (-350, 0, +350)
			const FTransform CenterRoadblock(
				FRotator::ZeroRotator,
				FVector(HazardX, 0.0f, 160.0f),
				FVector(ObstacleSize / 100.0f, 3.4f, 3.2f));
			const FTransform LeftSedanBlock(
				FRotator(0.0f, 14.0f, 0.0f),
				FVector(HazardX, -350.0f, 140.0f),
				FVector(ObstacleSize / 100.0f, 3.3f, 2.6f));
			const FTransform RightSedanBlock(
				FRotator(0.0f, -14.0f, 0.0f),
				FVector(HazardX, 350.0f, 140.0f),
				FVector(ObstacleSize / 100.0f, 3.3f, 2.6f));

			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CenterRoadblock, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CenterRoadblock, true, false, true);

			const bool bLeftIsTakeA = (TakeRoll < 0.5f);
			TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bLeftIsTakeA ? LeftSedanBlock : RightSedanBlock, true, false, true);
			TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bLeftIsTakeA ? RightSedanBlock : LeftSedanBlock, true, false, true);
		}
		return;
	}

	// Scene 02 Western (1,800m-3,600m) & Montage Western Segment (7,600m-8,000m): Generate full-width Western Standoff Barricades designed to be cleaved with 2.76:1 + Space!
	if (bIsWesternChunk)
	{
		const float BarricadeScaleY = (MEGAJAM_CORRIDOR_HALF_WIDTH * 2.0f) / 100.0f;
		const FTransform MainTimberBarricade(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 180.0f),
			FVector(ObstacleSize / 100.0f, BarricadeScaleY, 3.6f));
		const FTransform UpperRailCrossbeam(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 430.0f),
			FVector(ObstacleSize / 100.0f, BarricadeScaleY, 2.2f));
		const FTransform FlankingPostLeft(
			FRotator::ZeroRotator,
			FVector(HazardX, -700.0f, 360.0f),
			FVector(ObstacleSize / 100.0f, 2.8f, 7.2f));
		const FTransform FlankingPostRight(
			FRotator::ZeroRotator,
			FVector(HazardX, 700.0f, 360.0f),
			FVector(ObstacleSize / 100.0f, 2.8f, 7.2f));

		// Place the primary standoff barricade across both Take A and Take B so the center standoff lane must be cleaved with Quick-Draw Beat-Slash
		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, MainTimberBarricade, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, MainTimberBarricade, true, false, true);

		const bool bUpperIsTakeA = (TakeRoll < 0.5f);
		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bUpperIsTakeA ? UpperRailCrossbeam : FlankingPostLeft, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bUpperIsTakeA ? FlankingPostLeft : UpperRailCrossbeam, true, false, true);

		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 2, FlankingPostRight, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 2, FlankingPostRight, true, false, true);
		return;
	}

	switch (HazardArchetype)
	{
	case 0:
	{
		// Archetype 0 — Color-Splice Twin Gate:
		// Aligned to 5-lane centers: Left Gate on Lane 1 (Y = -350), Right Gate on Lane 3 (Y = +350), Center Gate on Lane 2 (Y = 0).
		const bool bLeftIsTakeA = (TakeRoll < 0.5f);
		const FTransform LeftGateBlock(
			FRotator::ZeroRotator,
			FVector(HazardX, -350.0f, 260.0f),
			FVector(ObstacleSize / 100.0f, 7.0f, 5.2f));
		const FTransform RightGateBlock(
			FRotator::ZeroRotator,
			FVector(HazardX, 350.0f, 260.0f),
			FVector(ObstacleSize / 100.0f, 7.0f, 5.2f));

		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, bLeftIsTakeA ? LeftGateBlock : RightGateBlock, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, bLeftIsTakeA ? RightGateBlock : LeftGateBlock, true, false, true);
		break;
	}

	case 1:
	{
		// Archetype 1 — Action Split Pillars:
		// Central pillar on Lane 2 (Y = 0.0f) and flanking Take-specific pillars on Lanes 1 & 3 (Y = -350.0f, +350.0f),
		// with outer Lanes 0 & 4 (Y = -700.0f, +700.0f) lined with cyan Dolly Boost Strips.
		const FTransform CentralPillar(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 430.0f),
			FVector(ObstacleSize / 100.0f, 3.2f, 8.6f));
		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CentralPillar, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CentralPillar, true, false, true);

		const bool bMidLeftIsTakeA = (TakeRoll < 0.5f);
		const FTransform MidLeftPillar(
			FRotator::ZeroRotator,
			FVector(HazardX, -350.0f, 260.0f),
			FVector(ObstacleSize / 100.0f, 3.2f, 5.2f));
		const FTransform MidRightPillar(
			FRotator::ZeroRotator,
			FVector(HazardX, 350.0f, 260.0f),
			FVector(ObstacleSize / 100.0f, 3.2f, 5.2f));
		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bMidLeftIsTakeA ? MidLeftPillar : MidRightPillar, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex + 1, bMidLeftIsTakeA ? MidRightPillar : MidLeftPillar, true, false, true);
		break;
	}

	case 2:
	{
		// Archetype 2 — Overhead Laser Curtain:
		// Upper barrier (Z: [310, 930]) leaving the entire Z = 140 Overdrive rail wide open underneath,
		// with a Take-specific floor laser bar that can be phased through via Hard Cut!
		const FTransform CeilingGateTransform(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 620.0f),
			FVector(ObstacleSize / 100.0f, 18.0f, 6.2f));
		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CeilingGateTransform, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, CeilingGateTransform, true, false, true);

		const FTransform FloorLaserBar(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 130.0f),
			FVector(ObstacleSize / 100.0f, 16.0f, 1.8f));
		UInstancedStaticMeshComponent* LaserTarget = (TakeRoll < 0.5f) ? TakeA_ObstaclesISM.Get() : TakeB_ObstaclesISM.Get();
		LaserTarget->UpdateInstanceTransform(BaseObstacleIndex + 1, FloorLaserBar, true, false, true);
		break;
	}

	case 3:
	default:
	{
		// Archetype 3 — Alternating Take Cross-Beams:
		// Low beam on one Take (Z = 140) and High beam on the opposite Take (Z = 520), so players can fly over/under in 4:3
		// or Hard-Cut straight through the Z = 140 rail at 4,500 u/s in 21:9 Overdrive!
		const bool bLowIsTakeA = (TakeRoll < 0.5f);
		const FTransform LowBeam(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 140.0f),
			FVector(ObstacleSize / 100.0f, 16.5f, 2.2f));
		const FTransform HighBeam(
			FRotator::ZeroRotator,
			FVector(HazardX, 0.0f, 520.0f),
			FVector(ObstacleSize / 100.0f, 16.5f, 3.6f));

		TakeA_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, bLowIsTakeA ? LowBeam : HighBeam, true, false, true);
		TakeB_ObstaclesISM->UpdateInstanceTransform(BaseObstacleIndex, bLowIsTakeA ? HighBeam : LowBeam, true, false, true);
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
		PickupSlots[PoolIdx].Velocity = FVector::ZeroVector;
		PickupSlots[PoolIdx].Scale = RingScale;
		PickupSlots[PoolIdx].RequiredTake = LineTake;
		PickupSlots[PoolIdx].bActive = true;
		PickupSlots[PoolIdx].bHasBallisticVelocity = false;
		PickupSlots[PoolIdx].MagnetImmunityTimer = 0.0f;
		TargetISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator(90.0f, 0.0f, 0.0f), Pos, RingScale), true, false, true);
	};

	if (!bIsHazardChunk)
	{
		// Flight Charge Runway: sweeping 5-frame 3D arc aligned with the 5 lane centers (-700, -350, 0, 350, 700)
		const float SideSign = ((ChunkIndex / 2) % 2 == 0) ? 1.0f : -1.0f;
		PlaceFrame(0, FVector(ChunkStartX + ChunkLength * 0.16f, -700.0f * SideSign, 240.0f));
		PlaceFrame(1, FVector(ChunkStartX + ChunkLength * 0.33f, -350.0f * SideSign, 480.0f));
		PlaceFrame(2, FVector(ChunkStartX + ChunkLength * 0.50f,    0.0f,            640.0f));
		PlaceFrame(3, FVector(ChunkStartX + ChunkLength * 0.67f,  350.0f * SideSign, 480.0f));
		PlaceFrame(4, FVector(ChunkStartX + ChunkLength * 0.84f,  700.0f * SideSign, 240.0f));
	}
	else
	{
		// Aerial Lens Hoops (10-frame ring):
		// Z = 520.0f in Scene 03 Alpine (reachable at the apex of a Trick-Ollie!), Z = 620.0f in Scenes 01/02
		const float ChunkDistMeters = FMath::Max(0.0f, ChunkStartX / 100.0f);
		const float HoopCenterX = ChunkStartX + ChunkLength * 0.50f;
		const float HoopCenterY = (HazardArchetype == 1)
			? ((ChunkIndex % 4 == 0) ? -700.0f : 700.0f)
			: ((HazardArchetype == 0) ? ((ChunkIndex % 4 == 0) ? -350.0f : 350.0f) : 0.0f);
		const float HoopCenterZ = (ChunkDistMeters >= 3600.0f) ? 520.0f : 620.0f;
		constexpr float HoopRadius = 185.0f;

		for (int32 RingIdx = 0; RingIdx < PickupsPerChunk; ++RingIdx)
		{
			const float AngleRad = (static_cast<float>(RingIdx) / static_cast<float>(PickupsPerChunk)) * 2.0f * PI;
			const float FrameY = HoopCenterY + FMath::Cos(AngleRad) * HoopRadius;
			const float FrameZ = HoopCenterZ + FMath::Sin(AngleRad) * HoopRadius;
			PlaceFrame(RingIdx, FVector(HoopCenterX, FrameY, FrameZ));
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

	// Spawn glowing cyan outer-wing Dolly Boost Strips (Y = +/-700, Z = 18) on Scene 03 Alpine chunks, 21:9 Center-Median Blockades, and Breathing Runway Chunks
	const float ChunkDistMeters = FMath::Max(0.0f, ChunkStartX / 100.0f);
	if (bIsHazardChunk && HazardArchetype != 1 && ChunkDistMeters < 3600.0f)
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

int32 AMegaJamTrackGenerator::SpliceObstaclesInFront(const FVector& PawnLocation, float ForwardRange, float SpliceRadius)
{
	if (!bPoolsCreated || !TakeA_ObstaclesISM || !TakeB_ObstaclesISM)
	{
		return 0;
	}

	const EMegaJamTakeState CurrentPawnTake = BoundPawn ? BoundPawn->GetCurrentTake() : EMegaJamTakeState::StudioTake;
	UInstancedStaticMeshComponent* TargetPickupISM = (CurrentPawnTake == EMegaJamTakeState::StudioTake)
		? PickupISM.Get()
		: PickupTakeB_ISM.Get();
	UInstancedStaticMeshComponent* OtherPickupISM = (CurrentPawnTake == EMegaJamTakeState::StudioTake)
		? PickupTakeB_ISM.Get()
		: PickupISM.Get();

	const int32 TotalObstacleInstances = TakeA_ObstaclesISM->GetInstanceCount();
	int32 ShreddedCount = 0;
	bool bObstaclesDirty = false;
	bool bPickupsDirty = false;

	auto TryCheckAndHideInstance = [&](UInstancedStaticMeshComponent* ISM, int32 InstanceIdx, FVector& OutHitCenter) -> bool
	{
		if (!ISM || InstanceIdx >= ISM->GetInstanceCount())
		{
			return false;
		}

		FTransform InstTransform;
		if (!ISM->GetInstanceTransform(InstanceIdx, InstTransform, true))
		{
			return false;
		}

		const FVector Scale = InstTransform.GetScale3D();
		if (Scale.SizeSquared() < 0.01f)
		{
			return false;
		}

		const FVector Loc = InstTransform.GetLocation();
		if (Loc.Z < -1000.0f)
		{
			return false;
		}

		// Cube primitive is 100x100x100 cm so half-extents are Scale * 50.0f
		const FVector HalfExtent = Scale.GetAbs() * 50.0f;
		if (Loc.X + HalfExtent.X < PawnLocation.X - 100.0f || Loc.X - HalfExtent.X > PawnLocation.X + ForwardRange)
		{
			return false;
		}

		const float ClosestY = FMath::Clamp(static_cast<float>(PawnLocation.Y), static_cast<float>(Loc.Y - HalfExtent.Y), static_cast<float>(Loc.Y + HalfExtent.Y));
		const float ClosestZ = FMath::Clamp(static_cast<float>(PawnLocation.Z), static_cast<float>(Loc.Z - HalfExtent.Z), static_cast<float>(Loc.Z + HalfExtent.Z));
		const float CrossDist = FVector2D::Distance(
			FVector2D(static_cast<float>(PawnLocation.Y), static_cast<float>(PawnLocation.Z)),
			FVector2D(ClosestY, ClosestZ));

		if (CrossDist > SpliceRadius)
		{
			return false;
		}

		ISM->UpdateInstanceTransform(InstanceIdx, MakeHiddenInstanceTransform(), true, false, true);
		OutHitCenter = FVector(Loc.X, ClosestY, FMath::Clamp(ClosestZ, 130.0f, 760.0f));
		return true;
	};

	const int32 BaseBurstIdx = PoolSize() * PickupsPerChunk;
	const FVector BurstRingScale(1.45f, 1.45f, 0.30f);

	for (int32 InstanceIdx = 0; InstanceIdx < TotalObstacleInstances; ++InstanceIdx)
	{
		FVector HitCenterA = FVector::ZeroVector;
		FVector HitCenterB = FVector::ZeroVector;
		const bool bHitA = TryCheckAndHideInstance(TakeA_ObstaclesISM, InstanceIdx, HitCenterA);
		const bool bHitB = TryCheckAndHideInstance(TakeB_ObstaclesISM, InstanceIdx, HitCenterB);

		if (!bHitA && !bHitB)
		{
			continue;
		}

		bObstaclesDirty = true;
		++ShreddedCount;

		const FVector ImpactLocation = bHitA ? HitCenterA : HitCenterB;

		// Flashbulb strobe: 150,000-lumen single-frame burst at ImpactLocation
		if (UWorld* World = GetWorld())
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APointLight* Flash = World->SpawnActor<APointLight>(ImpactLocation, FRotator::ZeroRotator, SpawnParams))
			{
				if (UPointLightComponent* LightComp = Flash->PointLightComponent)
				{
					LightComp->SetMobility(EComponentMobility::Movable);
					LightComp->SetIntensity(150000.0f);
					LightComp->SetAttenuationRadius(2500.0f);
				}
				Flash->SetLifeSpan(0.15f);
			}
		}

		// Piñata Radial Scatter: blast 4 Film Frames outward and upward before the pawn's magnet catches them
		const float PawnSpeed = BoundPawn ? static_cast<float>(BoundPawn->GetFlowVelocity().X) : 2900.0f;
		const int32 FramesToSpawn = 4;
		for (int32 FrameIdx = 0; FrameIdx < FramesToSpawn; ++FrameIdx)
		{
			if (BaseBurstIdx + BurstPickupPoolSize > PickupSlots.Num())
			{
				break;
			}

			const int32 PoolIdx = BaseBurstIdx + (NextBurstPickupCursor % BurstPickupPoolSize);
			++NextBurstPickupCursor;

			const FVector ScatterVel(
				PawnSpeed + FMath::RandRange(2000.0f, 4500.0f),
				FMath::RandRange(-3500.0f, 3500.0f),
				FMath::RandRange(2000.0f, 4000.0f));
			PickupSlots[PoolIdx].Location = ImpactLocation;
			PickupSlots[PoolIdx].Velocity = ScatterVel;
			PickupSlots[PoolIdx].Scale = BurstRingScale;
			PickupSlots[PoolIdx].RequiredTake = CurrentPawnTake;
			PickupSlots[PoolIdx].bActive = true;
			PickupSlots[PoolIdx].bHasBallisticVelocity = true;
			PickupSlots[PoolIdx].MagnetImmunityTimer = 0.25f;

			if (OtherPickupISM)
			{
				OtherPickupISM->UpdateInstanceTransform(PoolIdx, MakeHiddenInstanceTransform(), true, false, true);
			}
			if (TargetPickupISM)
			{
				TargetPickupISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator(90.0f, 0.0f, 0.0f), ImpactLocation, BurstRingScale), true, false, true);
			}
			bPickupsDirty = true;
		}
	}

	if (bObstaclesDirty)
	{
		TakeA_ObstaclesISM->MarkRenderStateDirty();
		TakeB_ObstaclesISM->MarkRenderStateDirty();
	}
	if (bPickupsDirty)
	{
		if (PickupISM)
		{
			PickupISM->MarkRenderStateDirty();
		}
		if (PickupTakeB_ISM)
		{
			PickupTakeB_ISM->MarkRenderStateDirty();
		}
	}

	return ShreddedCount;
}

int32 AMegaJamTrackGenerator::CleaveObstaclesInRange(float ForwardMinX, float ForwardMaxX, float HalfWidthY, float CenterY)
{
	if (!bPoolsCreated || !TakeA_ObstaclesISM || !TakeB_ObstaclesISM)
	{
		return 0;
	}

	const EMegaJamTakeState CurrentPawnTake = BoundPawn ? BoundPawn->GetCurrentTake() : EMegaJamTakeState::StudioTake;
	UInstancedStaticMeshComponent* TargetPickupISM = (CurrentPawnTake == EMegaJamTakeState::StudioTake)
		? PickupISM.Get()
		: PickupTakeB_ISM.Get();
	UInstancedStaticMeshComponent* OtherPickupISM = (CurrentPawnTake == EMegaJamTakeState::StudioTake)
		? PickupTakeB_ISM.Get()
		: PickupISM.Get();

	const int32 TotalObstacleInstances = TakeA_ObstaclesISM->GetInstanceCount();
	int32 CleavedCount = 0;
	bool bObstaclesDirty = false;
	bool bPickupsDirty = false;

	auto TryCleaveInstance = [&](UInstancedStaticMeshComponent* ISM, int32 InstanceIdx, FVector& OutHitCenter) -> bool
	{
		if (!ISM || InstanceIdx >= ISM->GetInstanceCount())
		{
			return false;
		}

		FTransform InstTransform;
		if (!ISM->GetInstanceTransform(InstanceIdx, InstTransform, true))
		{
			return false;
		}

		const FVector Scale = InstTransform.GetScale3D();
		if (Scale.SizeSquared() < 0.01f)
		{
			return false;
		}

		const FVector Loc = InstTransform.GetLocation();
		if (Loc.Z < -1000.0f)
		{
			return false;
		}

		const FVector HalfExtent = Scale.GetAbs() * 50.0f;
		if (Loc.X + HalfExtent.X < ForwardMinX || Loc.X - HalfExtent.X > ForwardMaxX)
		{
			return false;
		}

		if (Loc.Y - HalfExtent.Y > CenterY + HalfWidthY || Loc.Y + HalfExtent.Y < CenterY - HalfWidthY)
		{
			return false;
		}

		ISM->UpdateInstanceTransform(InstanceIdx, MakeHiddenInstanceTransform(), true, false, true);
		OutHitCenter = FVector(Loc.X, FMath::Clamp(static_cast<float>(Loc.Y), CenterY - HalfWidthY, CenterY + HalfWidthY), FMath::Clamp(static_cast<float>(Loc.Z), 140.0f, 760.0f));
		return true;
	};

	const int32 BaseBurstIdx = PoolSize() * PickupsPerChunk;
	const FVector BurstRingScale(1.45f, 1.45f, 0.30f);

	for (int32 InstanceIdx = 0; InstanceIdx < TotalObstacleInstances; ++InstanceIdx)
	{
		FVector HitCenterA = FVector::ZeroVector;
		FVector HitCenterB = FVector::ZeroVector;
		const bool bHitA = TryCleaveInstance(TakeA_ObstaclesISM, InstanceIdx, HitCenterA);
		const bool bHitB = TryCleaveInstance(TakeB_ObstaclesISM, InstanceIdx, HitCenterB);

		if (!bHitA && !bHitB)
		{
			continue;
		}

		bObstaclesDirty = true;
		++CleavedCount;

		const FVector ImpactLocation = bHitA ? HitCenterA : HitCenterB;

		if (UWorld* World = GetWorld())
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APointLight* Flash = World->SpawnActor<APointLight>(ImpactLocation, FRotator::ZeroRotator, SpawnParams))
			{
				if (UPointLightComponent* LightComp = Flash->PointLightComponent)
				{
					LightComp->SetMobility(EComponentMobility::Movable);
					LightComp->SetIntensity(150000.0f);
					LightComp->SetAttenuationRadius(2500.0f);
				}
				Flash->SetLifeSpan(0.15f);
			}
		}

		const float PawnSpeed = BoundPawn ? static_cast<float>(BoundPawn->GetFlowVelocity().X) : 3800.0f;
		const int32 FramesToSpawn = 4;
		for (int32 FrameIdx = 0; FrameIdx < FramesToSpawn; ++FrameIdx)
		{
			if (BaseBurstIdx + BurstPickupPoolSize > PickupSlots.Num())
			{
				break;
			}

			const int32 PoolIdx = BaseBurstIdx + (NextBurstPickupCursor % BurstPickupPoolSize);
			++NextBurstPickupCursor;

			// "Parting the Red Sea": split half the frames violently left and half violently right so cleaved barricades part down the center lane
			const bool bSplitLeft = (FrameIdx % 2 == 0);
			const float LateralSplitY = bSplitLeft
				? FMath::RandRange(-8000.0f, -4000.0f)
				: FMath::RandRange(4000.0f, 8000.0f);
			const FVector SplitOrigin = ImpactLocation + FVector(0.0f, bSplitLeft ? -90.0f : 90.0f, 0.0f);
			const FVector ScatterVel(
				PawnSpeed + FMath::RandRange(1800.0f, 3800.0f),
				LateralSplitY,
				FMath::RandRange(600.0f, 1400.0f));
			PickupSlots[PoolIdx].Location = SplitOrigin;
			PickupSlots[PoolIdx].Velocity = ScatterVel;
			PickupSlots[PoolIdx].Scale = BurstRingScale;
			PickupSlots[PoolIdx].RequiredTake = CurrentPawnTake;
			PickupSlots[PoolIdx].bActive = true;
			PickupSlots[PoolIdx].bHasBallisticVelocity = true;
			PickupSlots[PoolIdx].MagnetImmunityTimer = 0.25f;

			if (OtherPickupISM)
			{
				OtherPickupISM->UpdateInstanceTransform(PoolIdx, MakeHiddenInstanceTransform(), true, false, true);
			}
			if (TargetPickupISM)
			{
				TargetPickupISM->UpdateInstanceTransform(PoolIdx, FTransform(FRotator(90.0f, 0.0f, 0.0f), SplitOrigin, BurstRingScale), true, false, true);
			}
			bPickupsDirty = true;
		}
	}

	if (bObstaclesDirty)
	{
		TakeA_ObstaclesISM->MarkRenderStateDirty();
		TakeB_ObstaclesISM->MarkRenderStateDirty();
	}
	if (bPickupsDirty)
	{
		if (PickupISM)
		{
			PickupISM->MarkRenderStateDirty();
		}
		if (PickupTakeB_ISM)
		{
			PickupTakeB_ISM->MarkRenderStateDirty();
		}
	}

	return CleavedCount;
}
