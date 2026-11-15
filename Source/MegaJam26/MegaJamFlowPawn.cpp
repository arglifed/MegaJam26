// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamFlowPawn.h"

#include "MegaJamCinemaHUD.h"
#include "MegaJamFlowSubsystem.h"
#include "MegaJamGameMode.h"
#include "MegaJamPlayerController.h"
#include "MegaJamTrackGenerator.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogMegaJamPawn, Log, All);

namespace
{
	FVector SanitizePawnScale3D(const FVector& InScale, const FVector& FallbackScale = FVector::OneVector)
	{
		if (InScale.ContainsNaN())
		{
			return FallbackScale;
		}
		FVector Safe = InScale;
		if (!FMath::IsFinite(Safe.X) || FMath::Abs(Safe.X) < 0.05f)
		{
			Safe.X = (Safe.X < 0.0f) ? -0.05f : FMath::Max(0.05f, static_cast<float>(FallbackScale.X));
		}
		if (!FMath::IsFinite(Safe.Y) || FMath::Abs(Safe.Y) < 0.05f)
		{
			Safe.Y = (Safe.Y < 0.0f) ? -0.05f : FMath::Max(0.05f, static_cast<float>(FallbackScale.Y));
		}
		if (!FMath::IsFinite(Safe.Z) || FMath::Abs(Safe.Z) < 0.05f)
		{
			Safe.Z = (Safe.Z < 0.0f) ? -0.05f : FMath::Max(0.05f, static_cast<float>(FallbackScale.Z));
		}
		return Safe;
	}

	void SanitizeActorHierarchyScales(AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}

		TInlineComponentArray<USceneComponent*> SceneComps(Actor);
		for (USceneComponent* Comp : SceneComps)
		{
			if (!Comp)
			{
				continue;
			}
			const FVector RelScale = Comp->GetRelativeScale3D();
			if (RelScale.ContainsNaN() || FMath::Abs(RelScale.X) < 0.01f || FMath::Abs(RelScale.Y) < 0.01f || FMath::Abs(RelScale.Z) < 0.01f)
			{
				Comp->SetRelativeScale3D(SanitizePawnScale3D(RelScale));
			}

			if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Comp))
			{
				for (int32 Idx = 0; Idx < ISM->PerInstanceSMData.Num(); ++Idx)
				{
					FMatrix& Mat = ISM->PerInstanceSMData[Idx].Transform;
					if (Mat.ContainsNaN() ||
						Mat.GetScaleVector().GetMin() < 0.01f ||
						FMath::Abs(Mat.Determinant()) < 1e-4f)
					{
						Mat = FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -50000.0), FVector::OneVector).ToMatrixWithScale();
					}
				}
			}
		}
	}

	void SanitizeWorldAndLegacyActors(AActor* SelfActor, bool bDisableLegacyLevelObstacles)
	{
		SanitizeActorHierarchyScales(SelfActor);

		UWorld* World = SelfActor ? SelfActor->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			SanitizeActorHierarchyScales(Actor);

			if (bDisableLegacyLevelObstacles && Actor != SelfActor)
			{
				if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(Actor))
				{
					const FString ActorName = SMA->GetName();
					const FVector Loc = SMA->GetActorLocation();
					const bool bIsLegacyCorridorActor =
						ActorName.Contains(TEXT("Obstacle"), ESearchCase::IgnoreCase) ||
						ActorName.Contains(TEXT("Floor"), ESearchCase::IgnoreCase) ||
						ActorName.Contains(TEXT("Wall"), ESearchCase::IgnoreCase) ||
						(FMath::Abs(Loc.Y) < 1500.0 && Loc.X >= -2000.0 && Loc.X <= 25000.0);

					if (bIsLegacyCorridorActor)
					{
						SMA->SetActorEnableCollision(false);
						SMA->SetActorHiddenInGame(true);
						if (UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent())
						{
							SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							SMC->SetVisibility(false);
						}
					}
				}
			}
		}
	}
}

AMegaJamFlowPawn::AMegaJamFlowPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Forgiving arcade hurtbox: 35 cm radius. Physical sweep collision is disabled so the Pawn never snags
	// on legacy hand-placed level geometry (0m-190m Floor/Obstacles in L_FlowTest); obstacle & wall math runs in StepPhysics().
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(35.0f);
	CollisionSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetRelativeScale3D(FVector::OneVector);
	RootComponent = CollisionSphere;

	GrazeSphere = CreateDefaultSubobject<USphereComponent>(TEXT("GrazeSphere"));
	GrazeSphere->SetupAttachment(CollisionSphere);
	GrazeSphere->InitSphereRadius(GrazeRadius);
	GrazeSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	GrazeSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrazeSphere->SetGenerateOverlapEvents(false);
	GrazeSphere->SetRelativeScale3D(FVector::OneVector);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(CollisionSphere);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetRelativeScale3D(FVector(0.55f));

	VehicleStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VehicleStaticMesh"));
	VehicleStaticMesh->SetupAttachment(CollisionSphere);
	VehicleStaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VehicleStaticMesh->SetRelativeScale3D(FVector::OneVector);
	VehicleStaticMesh->SetVisibility(false);

	DroppedBoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroppedBoardMesh"));
	DroppedBoardMesh->SetupAttachment(CollisionSphere);
	DroppedBoardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DroppedBoardMesh->SetCastShadow(false);
	DroppedBoardMesh->SetAbsolute(true, true, false);
	DroppedBoardMesh->SetRelativeScale3D(FVector::OneVector);
	DroppedBoardMesh->SetVisibility(false);

	ProceduralSpliceRingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProceduralSpliceRingMesh"));
	ProceduralSpliceRingMesh->SetupAttachment(CollisionSphere);
	ProceduralSpliceRingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProceduralSpliceRingMesh->SetCastShadow(false);
	ProceduralSpliceRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
	ProceduralSpliceRingMesh->SetRelativeRotation(FRotator::ZeroRotator);
	ProceduralSpliceRingMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 0.08f));
	ProceduralSpliceRingMesh->SetVisibility(false);

	ProceduralSlashArcMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProceduralSlashArcMesh"));
	ProceduralSlashArcMesh->SetupAttachment(CollisionSphere);
	ProceduralSlashArcMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProceduralSlashArcMesh->SetCastShadow(false);
	ProceduralSlashArcMesh->SetRelativeLocation(FVector(420.0f, 0.0f, 40.0f));
	ProceduralSlashArcMesh->SetRelativeRotation(FRotator(0.0f, -70.0f, -18.0f));
	ProceduralSlashArcMesh->SetRelativeScale3D(FVector(0.35f, 24.0f, 0.08f));
	ProceduralSlashArcMesh->SetVisibility(false);

	// Default visual: engine basic sphere (100 cm diameter, matches the 50 cm collision radius).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshFinder.Succeeded())
	{
		VisualMesh->SetStaticMesh(SphereMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMeshFinder.Succeeded())
	{
		ProceduralSpliceRingMesh->SetStaticMesh(CylinderMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		ProceduralSlashArcMesh->SetStaticMesh(CubeMeshFinder.Object);
	}

	// Chase camera. The pawn never rotates, so the boom simply trails behind along -X and looks down slightly over the hood.
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(CollisionSphere);
	SpringArm->TargetArmLength = 450.0f;
	SpringArm->TargetOffset = FVector(0.0f, 0.0f, 120.0f);
	SpringArm->SocketOffset = FVector(0.0f, 0.0f, 15.0f);
	SpringArm->SetRelativeRotation(FRotator(-11.0f, 0.0f, 0.0f));
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 8.0f;
	SpringArm->ProbeSize = 12.0f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(90.0f);

	ScoreAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("ScoreAudioComponent"));
	ScoreAudioComponent->SetupAttachment(CollisionSphere);
	ScoreAudioComponent->bAutoActivate = false;
	ScoreAudioComponent->bIsUISound = true;

	MusicAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("MusicAudioComponent"));
	MusicAudioComponent->SetupAttachment(CollisionSphere);
	MusicAudioComponent->bAutoActivate = false;
	MusicAudioComponent->bIsUISound = true;

	// Default sounds live in C++ so BP_FlowPawn inherits them without a binary edit. Override or clear per Blueprint.
	static ConstructorHelpers::FObjectFinder<USoundBase> MusicActionFinder(TEXT("/Game/MegaJam26/Audio/SW_Music_TooClever.SW_Music_TooClever"));
	static ConstructorHelpers::FObjectFinder<USoundBase> MusicWesternFinder(TEXT("/Game/MegaJam26/Audio/SW_Music_Brixxx.SW_Music_Brixxx"));
	static ConstructorHelpers::FObjectFinder<USoundBase> MusicNoirFinder(TEXT("/Game/MegaJam26/Audio/SW_Music_LucidDreaming.SW_Music_LucidDreaming"));
	static ConstructorHelpers::FObjectFinder<USoundBase> MusicChaseFinder(TEXT("/Game/MegaJam26/Audio/SW_Music_Outbreak.SW_Music_Outbreak"));
	static ConstructorHelpers::FObjectFinder<USoundBase> MusicMontageFinder(TEXT("/Game/MegaJam26/Audio/SW_Music_TheSnatcher.SW_Music_TheSnatcher"));

	static ConstructorHelpers::FObjectFinder<USoundBase> RunStartFinder(TEXT("/Game/MegaJam26/Audio/SW_SFX_ClapperboardHit.SW_SFX_ClapperboardHit"));
	static ConstructorHelpers::FObjectFinder<USoundBase> FilmFrameFinder(TEXT("/Game/MegaJam26/Audio/SW_SFX_FuturisticSell.SW_SFX_FuturisticSell"));
	static ConstructorHelpers::FObjectFinder<USoundBase> DollyBoostFinder(TEXT("/Game/MegaJam26/Audio/SW_SFX_PositiveRise.SW_SFX_PositiveRise"));
	static ConstructorHelpers::FObjectFinder<USoundBase> CrashFinder(TEXT("/Game/MegaJam26/Audio/SW_SFX_VocalCut.SW_SFX_VocalCut"));
	static ConstructorHelpers::FObjectFinder<USoundBase> WhooshFinder(TEXT("/Game/MegaJam26/Audio/SW_SFX_CinematicWhoosh.SW_SFX_CinematicWhoosh"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ScoreFinder(TEXT("/Game/MegaJam26/Audio/MS_DirectorScore.MS_DirectorScore"));

	MusicTrack = MusicActionFinder.Object;
	SceneMusicTracks.Add(EMegaJamSceneGenre::Action, MusicActionFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Western, MusicWesternFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Alpine, MusicNoirFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Noir, MusicNoirFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Chase, MusicChaseFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Montage, MusicMontageFinder.Object);

	SceneBPMs.Add(EMegaJamSceneGenre::Action, 120.0f);
	SceneBPMs.Add(EMegaJamSceneGenre::Western, 136.0f);
	SceneBPMs.Add(EMegaJamSceneGenre::Alpine, 125.0f);
	SceneBPMs.Add(EMegaJamSceneGenre::Noir, 125.0f);
	SceneBPMs.Add(EMegaJamSceneGenre::Chase, 110.0f);
	SceneBPMs.Add(EMegaJamSceneGenre::Montage, 120.0f);

	SceneVehicleYawOffsets.Add(EMegaJamSceneGenre::Action, 0.0f);
	SceneVehicleYawOffsets.Add(EMegaJamSceneGenre::Western, 0.0f);
	SceneVehicleYawOffsets.Add(EMegaJamSceneGenre::Alpine, 0.0f);
	SceneVehicleYawOffsets.Add(EMegaJamSceneGenre::Noir, 0.0f); // SM_Vehicle_SpyCar itself now faces +X (re-imported), so no yaw fix-up
	SceneVehicleYawOffsets.Add(EMegaJamSceneGenre::Chase, 0.0f);
	SceneVehicleYawOffsets.Add(EMegaJamSceneGenre::Montage, 0.0f);

	for (const TPair<EMegaJamSceneGenre, TObjectPtr<USoundBase>>& Pair : SceneMusicTracks)
	{
		FSceneAudioConfig AudioCfg;
		AudioCfg.MusicTrack = Pair.Value;
		AudioCfg.TrackBPM = SceneBPMs.Contains(Pair.Key) ? SceneBPMs[Pair.Key] : 120.0f;
		SceneAudioConfigs.Add(Pair.Key, AudioCfg);
	}

	RunStartSound = RunStartFinder.Object;
	FilmFramePickupSound = FilmFrameFinder.Object;
	DollyBoostStripSound = DollyBoostFinder.Object;
	CrashSound = CrashFinder.Object;
	// Issue #24: the cinematic whoosh is reserved for Director's Cut activation; splices/slashes and the victory keep PositiveRise.
	DirectorsCutEnterSound = WhooshFinder.Object ? WhooshFinder.Object : DollyBoostFinder.Object;
	DirectorsCutExitSound = RunStartFinder.Object;
	BeatSpliceSound = DollyBoostFinder.Object;
	VictorySound = DollyBoostFinder.Object;

	// The score owns music + SFX when present (Option A). Null-safe: without the asset the C++ audio path is used.
	ScoreMetaSound = ScoreFinder.Object;
}

void AMegaJamFlowPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SanitizeWorldAndLegacyActors(this, false);
	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (GrazeSphere)
	{
		GrazeSphere->SetSphereRadius(FMath::Max(10.0f, GrazeRadius));
		GrazeSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GrazeSphere->SetGenerateOverlapEvents(false);
	}
}

void AMegaJamFlowPawn::PreRegisterAllComponents()
{
	// Sanitize all component and world ISM scales BEFORE UE5 GPUScene creates scene proxies and calls TMatrix::InverseFast()
	SanitizeWorldAndLegacyActors(this, false);

	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (GrazeSphere)
	{
		GrazeSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		GrazeSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GrazeSphere->SetGenerateOverlapEvents(false);
	}
	if (SpringArm)
	{
		SpringArm->bDoCollisionTest = false;
		SpringArm->TargetOffset = FVector(0.0f, 0.0f, 120.0f);
		SpringArm->TargetArmLength = 450.0f;
	}

	Super::PreRegisterAllComponents();
}

void AMegaJamFlowPawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	SanitizeWorldAndLegacyActors(this, false);

	// Brutally override any Blueprint-saved defaults before Frame 1
	WideZBounds = FVector2D(2.0, 2.0);
	BoxZBounds.X = 2.0;

	SetActorRotation(FRotator::ZeroRotator);
	SetActorScale3D(FVector::OneVector);

	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (GrazeSphere)
	{
		GrazeSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		GrazeSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GrazeSphere->SetGenerateOverlapEvents(false);
	}

	if (SpringArm)
	{
		SpringArm->bDoCollisionTest = false;
		SpringArm->TargetOffset = FVector(0.0f, 0.0f, 120.0f);
		SpringArm->TargetArmLength = 450.0f;
	}
	if (Camera)
	{
		Camera->SetFieldOfView(90.0f);
	}

	// Enforce canonical per-scene BPMs (Scene 01 Action = 120.0f) in PostInitializeComponents so stale Blueprint defaults cannot override them
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Action) = 120.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Western) = 136.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Alpine) = 125.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Noir) = 125.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Chase) = 110.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Montage) = 120.0f;

	for (const TPair<EMegaJamSceneGenre, float>& BpmPair : SceneBPMs)
	{
		if (FSceneAudioConfig* AudioCfg = SceneAudioConfigs.Find(BpmPair.Key))
		{
			AudioCfg->TrackBPM = BpmPair.Value;
		}
	}
	CurrentTrackBPM = 120.0f;
}

void AMegaJamFlowPawn::BeginPlay()
{
	Super::BeginPlay();

	// Sanitize all component scales and disable/hide legacy L_FlowTest Floor/Wall/Obstacle StaticMeshActors (0m-190m)
	SanitizeWorldAndLegacyActors(this, true);

	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (GrazeSphere)
	{
		GrazeSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		GrazeSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GrazeSphere->SetGenerateOverlapEvents(false);
	}

	// Re-enforce canonical BPMs in BeginPlay before syncing audio
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Action) = 120.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Western) = 136.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Alpine) = 125.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Noir) = 125.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Chase) = 110.0f;
	SceneBPMs.FindOrAdd(EMegaJamSceneGenre::Montage) = 120.0f;
	for (const TPair<EMegaJamSceneGenre, float>& BpmPair : SceneBPMs)
	{
		if (FSceneAudioConfig* AudioCfg = SceneAudioConfigs.Find(BpmPair.Key))
		{
			AudioCfg->TrackBPM = BpmPair.Value;
		}
	}
	CurrentTrackBPM = 120.0f;

	// Issue #80 / #96 / #98: Enforce grounded contact with 2.0f Z-clearance and safe +X forward orientation
	WideZBounds = FVector2D(2.0, 2.0);
	BoxZBounds.X = 2.0;
	bIsGrounded = true;
	bHasProcessedJumpInput = false;

	SpawnTransform = GetActorTransform();
	FVector GroundedSpawnLoc = SpawnTransform.GetLocation();
	if (!GroundedSpawnLoc.ContainsNaN())
	{
		GroundedSpawnLoc.Z = FMath::Max(2.0, WideZBounds.X);
	}
	else
	{
		GroundedSpawnLoc = FVector(0.0, 0.0, 2.0);
	}
	const FVector InitialLookAheadTarget = GroundedSpawnLoc + FVector(10.0f, 0.0f, 0.0f);
	const FVector SafeForwardDir = (InitialLookAheadTarget - GroundedSpawnLoc).GetSafeNormal(1e-4f, FVector::ForwardVector);
	SpawnTransform.SetLocation(GroundedSpawnLoc);
	SpawnTransform.SetRotation(SafeForwardDir.Rotation().Quaternion());
	SpawnTransform.SetScale3D(FVector::OneVector);
	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);

	if (SpringArm)
	{
		SpringArm->TargetOffset = FVector(0.0f, 0.0f, 120.0f);
		SpringArm->TargetArmLength = 450.0f;
		SpringArm->bDoCollisionTest = false;
	}
	ApplyCameraPOV();

	if (Camera)
	{
		Camera->SetFieldOfView(90.0f);
		Camera->SetRelativeLocation(FVector::ZeroVector);
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
	}

	GrazeSphere->SetSphereRadius(GrazeRadius);
	GrazeSphere->OnComponentBeginOverlap.AddDynamic(this, &AMegaJamFlowPawn::OnGrazeBeginOverlap);

	if (ScoreAudioComponent && ScoreMetaSound)
	{
		ScoreAudioComponent->SetSound(ScoreMetaSound);
		// Instance parameters set before Play() are applied when the score starts, so the first track is ready on "On Play".
		PushScoreWaveParameters();
		ScoreAudioComponent->Play();
	}

	if (MusicAudioComponent)
	{
		MusicAudioComponent->OnAudioFinished.RemoveDynamic(this, &AMegaJamFlowPawn::OnMusicFinished);
		MusicAudioComponent->OnAudioFinished.AddDynamic(this, &AMegaJamFlowPawn::OnMusicFinished);
		const UMegaJamFlowSubsystem* FlowSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr;
		const EMegaJamSceneGenre InitialGenre = FlowSubsystem ? FlowSubsystem->ActiveGenre : EMegaJamSceneGenre::Action;
		SyncSceneMusicTrack(InitialGenre);
	}

	if (VisualMesh)
	{
		UMaterialInterface* BaseVFXMaterial = VisualMesh->GetMaterial(0);
		PawnVisualMID = VisualMesh->CreateAndSetMaterialInstanceDynamic(0);

		if (ProceduralSpliceRingMesh)
		{
			if (BaseVFXMaterial)
			{
				ProceduralSpliceRingMesh->SetMaterial(0, BaseVFXMaterial);
			}
			SpliceRingMID = ProceduralSpliceRingMesh->CreateAndSetMaterialInstanceDynamic(0);
			if (SpliceRingMID)
			{
				const FLinearColor ElectricCyan(6.0f, 35.0f, 60.0f, 1.0f);
				SpliceRingMID->SetVectorParameterValue(TEXT("Color"), ElectricCyan);
				SpliceRingMID->SetVectorParameterValue(TEXT("BaseColor"), ElectricCyan);
				SpliceRingMID->SetVectorParameterValue(TEXT("TakeColor"), ElectricCyan);
				SpliceRingMID->SetVectorParameterValue(TEXT("EmissiveColor"), ElectricCyan);
			}
		}

		if (ProceduralSlashArcMesh)
		{
			if (BaseVFXMaterial)
			{
				ProceduralSlashArcMesh->SetMaterial(0, BaseVFXMaterial);
			}
			SlashArcMID = ProceduralSlashArcMesh->CreateAndSetMaterialInstanceDynamic(0);
			if (SlashArcMID)
			{
				const FLinearColor GoldWhite(45.0f, 28.0f, 8.0f, 1.0f);
				SlashArcMID->SetVectorParameterValue(TEXT("Color"), GoldWhite);
				SlashArcMID->SetVectorParameterValue(TEXT("BaseColor"), GoldWhite);
				SlashArcMID->SetVectorParameterValue(TEXT("TakeColor"), GoldWhite);
				SlashArcMID->SetVectorParameterValue(TEXT("EmissiveColor"), GoldWhite);
			}
		}
	}

	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	UpdatePawnDynamicMaterial(0.0f);
}

void AMegaJamFlowPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HitstopRealTimeRemaining > 0.0f)
	{
		HitstopRealTimeRemaining = 0.0f;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}

	Super::EndPlay(EndPlayReason);
}

void AMegaJamFlowPawn::EnsureFallbackInputActions()
{
	if (!SteerAction && FPackageName::DoesPackageExist(TEXT("/Game/MegaJam26/Core/IA_Steer")))
	{
		SteerAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/MegaJam26/Core/IA_Steer.IA_Steer"));
	}
	if (!SteerAction)
	{
		SteerAction = NewObject<UInputAction>(this, TEXT("IA_Steer_Fallback"), RF_Transient);
	}
	SteerAction->ValueType = EInputActionValueType::Axis2D;

	if (!BoostAction && FPackageName::DoesPackageExist(TEXT("/Game/MegaJam26/Core/IA_Boost")))
	{
		BoostAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/MegaJam26/Core/IA_Boost.IA_Boost"));
	}
	if (!BoostAction)
	{
		BoostAction = NewObject<UInputAction>(this, TEXT("IA_Boost_Fallback"), RF_Transient);
		BoostAction->ValueType = EInputActionValueType::Boolean;
	}

	if (!BrakeAction && FPackageName::DoesPackageExist(TEXT("/Game/MegaJam26/Core/IA_Brake")))
	{
		BrakeAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/MegaJam26/Core/IA_Brake.IA_Brake"));
	}
	if (!BrakeAction)
	{
		BrakeAction = NewObject<UInputAction>(this, TEXT("IA_Brake_Fallback"), RF_Transient);
		BrakeAction->ValueType = EInputActionValueType::Boolean;
	}
}

void AMegaJamFlowPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	EnsureFallbackInputActions();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogMegaJamPawn, Warning, TEXT("InputComponent is not an EnhancedInputComponent."));
		return;
	}

	if (SteerAction)
	{
		EnhancedInput->BindAction(SteerAction, ETriggerEvent::Triggered, this, &AMegaJamFlowPawn::Input_Steer);
		EnhancedInput->BindAction(SteerAction, ETriggerEvent::Completed, this, &AMegaJamFlowPawn::Input_SteerReleased);
		EnhancedInput->BindAction(SteerAction, ETriggerEvent::Canceled, this, &AMegaJamFlowPawn::Input_SteerReleased);
	}
	else
	{
		UE_LOG(LogMegaJamPawn, Warning, TEXT("SteerAction is not assigned on %s."), *GetClass()->GetName());
	}

	// Boost = hard cut. One shot per press, so Started only.
	if (BoostAction)
	{
		EnhancedInput->BindAction(BoostAction, ETriggerEvent::Started, this, &AMegaJamFlowPawn::Input_Boost);
	}
	else
	{
		UE_LOG(LogMegaJamPawn, Warning, TEXT("BoostAction is not assigned on %s."), *GetClass()->GetName());
	}

	// Brake = aspect toggle. One shot per press, so Started only.
	if (BrakeAction)
	{
		EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Started, this, &AMegaJamFlowPawn::Input_Brake);
	}
	else
	{
		UE_LOG(LogMegaJamPawn, Warning, TEXT("BrakeAction is not assigned on %s."), *GetClass()->GetName());
	}

	// Direct bindings for SpaceBar / Gamepad_FaceButton_Right to ExecuteEditorSplice()
	for (const FKey& SpliceKey : { EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Right })
	{
		FInputKeyBinding SpliceBinding(FInputChord(SpliceKey), IE_Pressed);
		SpliceBinding.bConsumeInput = false;
		SpliceBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamFlowPawn::ExecuteEditorSplice);
		PlayerInputComponent->KeyBindings.Add(SpliceBinding);
	}

	// Direct bindings for Tab / R to CycleMontagePowerOnDemand() in Scene 05 Montage
	for (const FKey& CycleKey : { EKeys::Tab, EKeys::R })
	{
		FInputKeyBinding CycleBinding(FInputChord(CycleKey), IE_Pressed);
		CycleBinding.bConsumeInput = false;
		CycleBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamFlowPawn::CycleMontagePowerOnDemand);
		PlayerInputComponent->KeyBindings.Add(CycleBinding);
	}

	// Direct bindings for V / Gamepad_FaceButton_Top (Y) to CycleCameraPOV()
	for (const FKey& POVKey : { EKeys::V, EKeys::Gamepad_FaceButton_Top })
	{
		FInputKeyBinding POVBinding(FInputChord(POVKey), IE_Pressed);
		POVBinding.bConsumeInput = false;
		POVBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamFlowPawn::CycleCameraPOV);
		PlayerInputComponent->KeyBindings.Add(POVBinding);
	}
}

void AMegaJamFlowPawn::CycleCameraPOV()
{
	switch (ActiveCameraPOV)
	{
	case EMegaJamCameraPOV::ChaseStandard:
		ActiveCameraPOV = EMegaJamCameraPOV::ChaseFar;
		break;
	case EMegaJamCameraPOV::ChaseFar:
		ActiveCameraPOV = EMegaJamCameraPOV::BumperFirstPerson;
		break;
	case EMegaJamCameraPOV::BumperFirstPerson:
	default:
		ActiveCameraPOV = EMegaJamCameraPOV::ChaseStandard;
		break;
	}

	ApplyCameraPOV();

	if (GEngine)
	{
		const TCHAR* POVLabel = (ActiveCameraPOV == EMegaJamCameraPOV::ChaseStandard)
			? TEXT("CHASE STANDARD")
			: ((ActiveCameraPOV == EMegaJamCameraPOV::ChaseFar) ? TEXT("CHASE FAR") : TEXT("BUMPER FIRST-PERSON"));
		GEngine->AddOnScreenDebugMessage(202609, 1.8f, FColor::Cyan,
			FString::Printf(TEXT("CAMERA POV [V]: %s"), POVLabel),
			true, FVector2D(1.3f, 1.3f));
	}
}

void AMegaJamFlowPawn::ApplyCameraPOV()
{
	if (!SpringArm)
	{
		return;
	}

	SpringArm->TargetOffset = FVector(0.0f, 0.0f, 120.0f);
	SpringArm->bDoCollisionTest = false;

	switch (ActiveCameraPOV)
	{
	case EMegaJamCameraPOV::ChaseFar:
		SpringArm->TargetArmLength = 720.0f;
		SpringArm->SocketOffset = FVector(0.0f, 0.0f, 55.0f);
		SpringArm->SetRelativeRotation(FRotator(-14.0f, 0.0f, 0.0f));
		if (VisualMesh)
		{
			VisualMesh->SetRelativeScale3D(FVector(0.55f));
			VisualMesh->SetVisibility(true);
		}
		break;

	case EMegaJamCameraPOV::BumperFirstPerson:
		SpringArm->TargetArmLength = 0.0f;
		SpringArm->SocketOffset = FVector(135.0f, 0.0f, -75.0f);
		SpringArm->SetRelativeRotation(FRotator(-2.0f, 0.0f, 0.0f));
		if (VisualMesh)
		{
			VisualMesh->SetVisibility(false);
		}
		break;

	case EMegaJamCameraPOV::ChaseStandard:
	default:
		SpringArm->TargetArmLength = 450.0f;
		SpringArm->SocketOffset = FVector(0.0f, 0.0f, 15.0f);
		SpringArm->SetRelativeRotation(FRotator(-11.0f, 0.0f, 0.0f));
		if (VisualMesh)
		{
			VisualMesh->SetRelativeScale3D(FVector(0.55f));
			VisualMesh->SetVisibility(true);
		}
		break;
	}
}

EMegaJamSceneGenre AMegaJamFlowPawn::GetEffectivePowerGenre() const
{
	const UMegaJamFlowSubsystem* FlowSub = GetWorld() ? GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr;
	const EMegaJamSceneGenre BaseGenre = FlowSub ? FlowSub->ActiveGenre : ActiveMusicGenre;
	return (BaseGenre == EMegaJamSceneGenre::Montage) ? ActiveMontageSubGenre : BaseGenre;
}

void AMegaJamFlowPawn::CycleMontagePowerOnDemand()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return;
	}

	const AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && !GameMode->IsPlaying())
	{
		return;
	}

	const UMegaJamFlowSubsystem* FlowSub = World->GetSubsystem<UMegaJamFlowSubsystem>();
	const EMegaJamSceneGenre BaseGenre = FlowSub ? FlowSub->ActiveGenre : ActiveMusicGenre;
	if (BaseGenre != EMegaJamSceneGenre::Montage)
	{
		return;
	}

	EMegaJamSceneGenre NextSubGenre = EMegaJamSceneGenre::Action;
	switch (ActiveMontageSubGenre)
	{
	case EMegaJamSceneGenre::Action:
		NextSubGenre = EMegaJamSceneGenre::Western;
		break;
	case EMegaJamSceneGenre::Western:
		NextSubGenre = EMegaJamSceneGenre::Noir;
		break;
	case EMegaJamSceneGenre::Noir:
		NextSubGenre = EMegaJamSceneGenre::Chase;
		break;
	case EMegaJamSceneGenre::Chase:
	default:
		NextSubGenre = EMegaJamSceneGenre::Action;
		break;
	}

	ApplyMontageSubGenre(NextSubGenre, true);
}

void AMegaJamFlowPawn::ApplyMontageSubGenre(EMegaJamSceneGenre NewSubGenre, bool bIsManualCycle)
{
	UWorld* World = GetWorld();
	if (!World || bCrashed)
	{
		return;
	}

	ActiveMontageSubGenre = NewSubGenre;
	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	if (FlowSubsystem && bIsManualCycle)
	{
		FlowSubsystem->RecordSpecialMoveExecution(FlowSubsystem->ActiveGenre);
	}
	const bool bOnBeat = FlowSubsystem && FlowSubsystem->IsWithinBeatWindow(0.18f);

	// Match-Cut Power Switch: grant +15.0f Film Meter bonus and micro-flashbulb match-cut without forcing aspect or altitude locks
	FilmMeter = FMath::Clamp(FilmMeter + 15.0f, 0.0f, 100.0f);
	SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
	CurrentShotDuration = 0.0f;
	AspectHoldTimer = 0.0f;
	if (bIsShotStale)
	{
		bIsShotStale = false;
		BP_OnStaleShotStateChanged(false);
	}

	EmissiveBurstTimer = 0.28f;
	MatchCutFlashTimer = 1.1f;
	CurrentFOVPunch = CutFOVPunch;
	AddCameraTrauma(0.38f, 0.65f);
	TriggerMicroHitstop(0.03f, 0.25f);

	// Refresh post-process grading for the newly selected Montage power.
	// If the player is already locked into Director's Cut overdrive, align the widescreen aspect mode with the selected sub-genre;
	// if they are in 4:3 flight, preserve 4:3 without forcing an altitude or lane lock.
	if (IsInBulletTimeOverdrive())
	{
		if (NewSubGenre == EMegaJamSceneGenre::Western)
		{
			CurrentLane = 2;
			bHasProcessedLaneInput = false;
			SetAspectMode(EMegaJamAspectMode::UltraPanavision_276_1);
		}
		else
		{
			const float EntryY = static_cast<float>(GetActorLocation().Y);
			CurrentLane = FMath::Clamp(FMath::RoundToInt((EntryY / FMath::Max(1.0f, LaneWidthY)) + 2.0f), 0, 4);
			bHasProcessedLaneInput = false;
			SetAspectMode(EMegaJamAspectMode::Wide_21_9);
		}
	}
	else
	{
		SetAspectMode(CurrentAspect);
	}

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		GameMode->AddBonusScore((bOnBeat ? 350.0f : 200.0f) * static_cast<float>(SpliceChain), true);
		GameMode->NotifySpliceChainUpdated(SpliceChain);
	}
	if (FlowSubsystem)
	{
		FlowSubsystem->SetOnBeatFlashTimer(1.1f);
		FlowSubsystem->SetSpliceChain(SpliceChain);
		FlowSubsystem->SetFilmMeter(FilmMeter);
	}

	PlayCue(TEXT("OnBeatCut"), BeatSpliceSound, 0.95f, 1.28f, true);
	TriggerAudioEvent(TEXT("OnAspectSwitch"));

	if (GEngine)
	{
		const TCHAR* PowerName = (NewSubGenre == EMegaJamSceneGenre::Western)
			? TEXT("WESTERN BEAT-SLASH")
			: ((NewSubGenre == EMegaJamSceneGenre::Noir)
				? TEXT("NOIR VERTIGO 2x GRAZE")
				: ((NewSubGenre == EMegaJamSceneGenre::Chase) ? TEXT("CHASE +60% COMET SMASH") : TEXT("ACTION BEAT-SPLICE")));
		GEngine->AddOnScreenDebugMessage(202607, 1.8f, FColor::Cyan,
			FString::Printf(TEXT("★ MATCH-CUT MONTAGE [MANUAL TAB/R]: %s (+15%% FILM | CHAIN x%d)"),
				PowerName,
				SpliceChain),
			true, FVector2D(1.35f, 1.35f));
	}
}

void AMegaJamFlowPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float UnscaledDelta = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.0f, 0.1f);
	if (UMegaJamFlowSubsystem* FlowSub = GetWorld() ? GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr)
	{
		if (FlowSub->ActiveGenre != ActiveMusicGenre)
		{
			SyncSceneMusicTrack(FlowSub->ActiveGenre);
		}
	}
	UpdateDynamicMusicModulation(UnscaledDelta);

	if (UGameplayStatics::IsGamePaused(this))
	{
		return;
	}

	if (HitstopRealTimeRemaining > 0.0f)
	{
		HitstopRealTimeRemaining -= UnscaledDelta;
		if (HitstopRealTimeRemaining <= 0.0f)
		{
			HitstopRealTimeRemaining = 0.0f;
			UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
		}
	}

	CameraTrauma = FMath::Max(0.0f, CameraTrauma - 2.4f * UnscaledDelta);
	EmissiveBurstTimer = FMath::Max(0.0f, EmissiveBurstTimer - DeltaSeconds);
	CurrentFOVPunch = FMath::FInterpTo(CurrentFOVPunch, 0.0f, DeltaSeconds, CutFOVPunchDecaySpeed);
	ActiveSlashRoll = FMath::FInterpTo(ActiveSlashRoll, 0.0f, DeltaSeconds, 12.0f);
	ActiveTrickSpinTimer = FMath::Max(0.0f, ActiveTrickSpinTimer - UnscaledDelta);
	CometSmashTimer = FMath::Max(0.0f, CometSmashTimer - DeltaSeconds);
	if (DroppedBoardTimer > 0.0f)
	{
		DroppedBoardTimer = FMath::Max(0.0f, DroppedBoardTimer - DeltaSeconds);
		if (DroppedBoardMesh)
		{
			if (DroppedBoardTimer > 0.0f)
			{
				DroppedBoardVelocity.Z -= 3200.0f * DeltaSeconds;
				DroppedBoardWorldLocation += DroppedBoardVelocity * DeltaSeconds;
				DroppedBoardWorldRotation.Pitch += 340.0f * DeltaSeconds;
				DroppedBoardWorldRotation.Roll += 260.0f * DeltaSeconds;
				DroppedBoardWorldRotation.Yaw += 95.0f * DeltaSeconds;
				DroppedBoardMesh->SetWorldLocationAndRotation(DroppedBoardWorldLocation, DroppedBoardWorldRotation);
			}
			else
			{
				DroppedBoardMesh->SetWorldScale3D(FVector::OneVector);
				DroppedBoardMesh->SetVisibility(false);
			}
		}
	}
	if (!bIsDropCutSlamming)
	{
		SlamPitchPunch = FMath::FInterpTo(SlamPitchPunch, 0.0f, DeltaSeconds, 12.0f);
	}

	// ---- Procedural 3D Beat-Splice / Cryo-Shatter Shockwave Ring (Scene 01 & Scene 03) ----
	if (SpliceRingTimer > 0.0f)
	{
		SpliceRingTimer = FMath::Max(0.0f, SpliceRingTimer - UnscaledDelta);
		if (ProceduralSpliceRingMesh)
		{
			if (SpliceRingTimer > 0.0f)
			{
				const float Alpha = FMath::Clamp(1.0f - (SpliceRingTimer / 0.18f), 0.0f, 1.0f);
				const float RadialScale = FMath::Max(0.05f, FMath::Lerp(1.2f, 11.5f, Alpha));
				const float ThicknessZ = FMath::Max(0.05f, FMath::Lerp(0.08f, 0.05f, Alpha));
				ProceduralSpliceRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
				ProceduralSpliceRingMesh->SetRelativeRotation(FRotator::ZeroRotator);
				ProceduralSpliceRingMesh->SetRelativeScale3D(SanitizePawnScale3D(FVector(RadialScale, RadialScale, ThicknessZ)));
			}
			else
			{
				ProceduralSpliceRingMesh->SetRelativeScale3D(FVector::OneVector);
				ProceduralSpliceRingMesh->SetVisibility(false);
			}
		}
	}

	// ---- Procedural 3D Emissive Slash Blade (Scene 02 Western Beat-Slash) ----
	if (SlashArcTimer > 0.0f)
	{
		SlashArcTimer = FMath::Max(0.0f, SlashArcTimer - UnscaledDelta);
		if (ProceduralSlashArcMesh)
		{
			if (SlashArcTimer > 0.0f)
			{
				const float Alpha = FMath::Clamp(1.0f - (SlashArcTimer / 0.16f), 0.0f, 1.0f);
				const float SweepYaw = FMath::Lerp(-70.0f, 70.0f, Alpha);
				const float WidthY = FMath::Max(0.05f, bActiveSlashIsOnBeat ? 24.0f : 6.5f);
				const float ThicknessZ = FMath::Max(0.05f, FMath::Lerp(0.08f, 0.05f, Alpha));
				ProceduralSlashArcMesh->SetRelativeLocation(FVector(420.0f, 0.0f, 40.0f));
				ProceduralSlashArcMesh->SetRelativeRotation(FRotator(0.0f, SweepYaw, -18.0f));
				ProceduralSlashArcMesh->SetRelativeScale3D(SanitizePawnScale3D(FVector(0.35f, WidthY, ThicknessZ)));
			}
			else
			{
				ProceduralSlashArcMesh->SetRelativeScale3D(FVector::OneVector);
				ProceduralSlashArcMesh->SetVisibility(false);
			}
		}
	}

	if (Camera)
	{
		if (!FMath::IsFinite(Camera->FieldOfView) || Camera->FieldOfView <= 10.0f)
		{
			Camera->SetFieldOfView(90.0f);
		}
		if (!FMath::IsFinite(CurrentFOVPunch))
		{
			CurrentFOVPunch = 0.0f;
		}
		if (!FMath::IsFinite(CurrentCameraRoll))
		{
			CurrentCameraRoll = 0.0f;
		}
		if (!FMath::IsFinite(CameraTrauma))
		{
			CameraTrauma = 0.0f;
		}
		if (!FMath::IsFinite(SlamPitchPunch))
		{
			SlamPitchPunch = 0.0f;
		}
		if (!FMath::IsFinite(ActiveSlashRoll))
		{
			ActiveSlashRoll = 0.0f;
		}

		const EMegaJamSceneGenre CamGenre = GetEffectivePowerGenre();
		const bool bNoirVertigo = (CamGenre == EMegaJamSceneGenre::Noir && CurrentAspect == EMegaJamAspectMode::Wide_21_9);

		const float POVTargetArmLength = (ActiveCameraPOV == EMegaJamCameraPOV::ChaseStandard)
			? 450.0f
			: ((ActiveCameraPOV == EMegaJamCameraPOV::ChaseFar) ? 720.0f : 0.0f);

		if (SpringArm && !FMath::IsFinite(SpringArm->TargetArmLength))
		{
			SpringArm->TargetArmLength = POVTargetArmLength;
		}

		float BaseFOV = FMath::IsFinite(BoxFOV) ? BoxFOV : 90.0f;
		if (CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
		{
			BaseFOV = FMath::IsFinite(UltraPanavisionFOV) ? UltraPanavisionFOV : 112.0f;
		}
		else if (bNoirVertigo)
		{
			// Hitchcock Vertigo Dolly-Zoom: dynamic TrackBPM sine pulse widening FOV to 114.0f while pulling SpringArm inward
			const UMegaJamFlowSubsystem* VertigoSub = GetWorld() ? GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr;
			const float RawTimeSeconds = VertigoSub
				? VertigoSub->CurrentAudioTime
				: (GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.0f);
			const float TimeSeconds = FMath::IsFinite(RawTimeSeconds) ? RawTimeSeconds : 0.0f;
			const float SafeBPM = (FMath::IsFinite(CurrentTrackBPM) && CurrentTrackBPM > 1.0f) ? CurrentTrackBPM : 125.0f;
			const float VertigoAlpha = FMath::Clamp(0.5f + 0.5f * FMath::Sin(TimeSeconds * PI * (SafeBPM / 60.0f)), 0.0f, 1.0f);
			BaseFOV = FMath::Lerp(FMath::IsFinite(WideFOV) ? WideFOV : 108.0f, 114.0f, VertigoAlpha);
			if (SpringArm)
			{
				if (ActiveCameraPOV == EMegaJamCameraPOV::BumperFirstPerson)
				{
					SpringArm->TargetArmLength = 0.0f;
				}
				else
				{
					const float TargetArm = FMath::Lerp(POVTargetArmLength, POVTargetArmLength * 0.60f, VertigoAlpha);
					SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetArm, DeltaSeconds, 14.0f);
				}
			}
		}
		else if (CurrentAspect == EMegaJamAspectMode::Wide_21_9)
		{
			BaseFOV = FMath::IsFinite(WideFOV) ? WideFOV : 108.0f;
		}

		if (SpringArm && !bNoirVertigo)
		{
			SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, POVTargetArmLength, DeltaSeconds, 10.0f);
		}

		if (GrazeSphere)
		{
			const float BaseGraze = FMath::Max(GrazeRadius, AdrenalineGrazeRadius);
			const float TargetGrazeRadius = bNoirVertigo ? (BaseGraze * 2.0f) : BaseGraze;
			if (!FMath::IsNearlyEqual(GrazeSphere->GetUnscaledSphereRadius(), TargetGrazeRadius, 0.5f))
			{
				GrazeSphere->SetSphereRadius(TargetGrazeRadius);
			}
		}

		const float SmoothedBaseFOV = FMath::FInterpTo(Camera->FieldOfView - CurrentFOVPunch, BaseFOV, DeltaSeconds, AspectFOVInterpSpeed);
		const float FinalFOV = FMath::Clamp(SmoothedBaseFOV + CurrentFOVPunch, 60.0f, 140.0f);
		Camera->SetFieldOfView(FMath::IsFinite(FinalFOV) ? FinalFOV : 90.0f);

		const float LateralAlpha = (TerminalVelocity > KINDA_SMALL_NUMBER && !bCrashed && FMath::IsFinite(Velocity.Y))
			? FMath::Clamp(static_cast<float>(Velocity.Y) / TerminalVelocity, -1.0f, 1.0f)
			: 0.0f;
		const float TargetRoll = LateralAlpha * MaxStrafeRoll;
		CurrentCameraRoll = FMath::FInterpTo(CurrentCameraRoll, TargetRoll, DeltaSeconds, CameraRollInterpSpeed);
		if (!FMath::IsFinite(CurrentCameraRoll))
		{
			CurrentCameraRoll = 0.0f;
		}

		// Damped high-frequency sinusoidal Pitch, Yaw, Roll, and vertical Z offsets using Trauma^2 scaling
		const float Shake = CameraTrauma * CameraTrauma;
		const double RealTime = FPlatformTime::Seconds();
		const float ShakePitch = Shake * 3.8f * FMath::Sin(static_cast<float>(RealTime * 43.0));
		const float ShakeYaw   = Shake * 3.2f * FMath::Cos(static_cast<float>(RealTime * 37.0));
		const float ShakeRoll  = Shake * 5.0f * FMath::Sin(static_cast<float>(RealTime * 53.0));
		const float ShakeZ     = Shake * 18.0f * FMath::Sin(static_cast<float>(RealTime * 61.0));

		const bool bChaseSlope = (ActiveMusicGenre == EMegaJamSceneGenre::Chase || CamGenre == EMegaJamSceneGenre::Chase);
		const bool bAlpineSlope = (CamGenre == EMegaJamSceneGenre::Alpine && CurrentAspect == EMegaJamAspectMode::Wide_21_9);
		const float SafeDownhillPitch = FMath::IsFinite(SnowboardDownhillPitch) ? SnowboardDownhillPitch : -12.5f;
		const float DownhillPitchOffset = bChaseSlope ? SafeDownhillPitch : (bAlpineSlope ? -7.5f : 0.0f);

		Camera->SetRelativeRotation(FRotator(SlamPitchPunch + DownhillPitchOffset + ShakePitch, ShakeYaw, CurrentCameraRoll + ShakeRoll + ActiveSlashRoll));
		Camera->SetRelativeLocation(FVector(0.0f, 0.0f, ShakeZ));
	}

	UpdatePawnDynamicMaterial(DeltaSeconds);

	if (bCrashed)
	{
		PushAudioParameters();
		return;
	}

	// Hold still or run attract/victory cruise when not in active gameplay.
	const AMegaJamGameMode* GameMode = GetWorld()->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && !GameMode->IsPlaying())
	{
		const EMegaJamRunState State = GameMode->GetRunState();
		const float TimeSeconds = static_cast<float>(GetWorld()->GetTimeSeconds());

		if (State == EMegaJamRunState::TitleScreen)
		{
			bTriggeredVictorySting = false;
			Velocity = FVector(AttractCruiseSpeed, 0.0f, 0.0f);
			FlowRatio = 0.35f;

			const FVector Loc = GetActorLocation();
			const float TargetY = FMath::Sin(TimeSeconds * 0.85f) * 180.0f;
			const float NewY = FMath::FInterpTo(static_cast<float>(Loc.Y), TargetY, DeltaSeconds, 4.0f);
			const float TargetGroundZ = FMath::Max(2.0f, static_cast<float>(WideZBounds.X));
			const float NewZ = FMath::FInterpTo(static_cast<float>(Loc.Z), TargetGroundZ, DeltaSeconds, 6.0f);
			SetActorLocation(FVector(Loc.X + AttractCruiseSpeed * DeltaSeconds, NewY, NewZ), false);

			if (Camera)
			{
				CurrentCameraRoll = FMath::Sin(TimeSeconds * 1.1f) * 2.2f;
				Camera->SetRelativeRotation(FRotator(0.0f, 0.0f, CurrentCameraRoll));
			}
		}
		else if (State == EMegaJamRunState::Completed)
		{
			if (!bTriggeredVictorySting)
			{
				bTriggeredVictorySting = true;
				CurrentFOVPunch = CutFOVPunch;
				TriggerAudioEvent(TEXT("OnHardCut"));
				PlayCue(TEXT("OnCompleted"), VictorySound, 1.0f, 0.75f);
			}

			// Cinematic letterboxed freeze-frame for the Box-Office Wrap-Up Scoreboard
			Velocity = FVector::ZeroVector;
			FlowRatio = 1.0f;

			if (Camera)
			{
				CurrentCameraRoll = FMath::FInterpTo(CurrentCameraRoll, 0.0f, DeltaSeconds, 6.0f);
				Camera->SetRelativeRotation(FRotator(0.0f, 0.0f, CurrentCameraRoll));
			}
		}

		PushAudioParameters();
		return;
	}

	bTriggeredVictorySting = false;

	// ---- 4:3 Flight Charge vs. 21:9 Bullet-Time Overdrive + Anti-Camping Stale-Shot Decay ----
	AirExposureTimer = 0.0f;
	bIsOverexposed = false;
	CurrentShotDuration += DeltaSeconds;
	AspectHoldTimer = CurrentShotDuration;
	const bool bWasShotStale = bIsShotStale;
	bIsShotStale = (CurrentShotDuration > StaleShotThreshold);
	if (bIsShotStale != bWasShotStale)
	{
		BP_OnStaleShotStateChanged(bIsShotStale);
	}

	if (bIsShotStale)
	{
		// "Bored Director" penalty: drain FilmMeter at 25/sec and reset SpliceChain to x1 until aspect cut
		FilmMeter = FMath::Max(0.0f, FilmMeter - 25.0f * DeltaSeconds);
		SpliceChain = 1;
	}

	UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>();

	if (CurrentAspect == EMegaJamAspectMode::Wide_21_9 || CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
	{
		// 21:9 / 2.76:1 Director's Cut: drain FilmMeter at 15/sec and slow hazard animations to 0.45x
		FilmMeter = FMath::Max(0.0f, FilmMeter - OverdriveDrainPerSec * DeltaSeconds);
		FlowBonusOffset = FMath::Clamp(FlowBonusOffset + (WideDollyFlowRegenPerSec * 0.01f) * DeltaSeconds, 0.0f, 0.35f);
		if (FlowSubsystem)
		{
			FlowSubsystem->SetHazardTimeScale(0.45f);
		}

		if (FilmMeter <= 0.0f)
		{
			FilmMeter = 0.0f;
			CurrentShotDuration = 0.0f;
			AspectHoldTimer = 0.0f;
			if (bIsShotStale)
			{
				bIsShotStale = false;
				BP_OnStaleShotStateChanged(false);
			}
			SetAspectMode(EMegaJamAspectMode::Box_4_3);
			if (FlowSubsystem)
			{
				FlowSubsystem->SetHazardTimeScale(1.0f);
			}
			PlayCue(TEXT("OnDirectorsCutExit"), DirectorsCutExitSound, 0.50f, 0.85f);
			TriggerAudioEvent(TEXT("OnAspectSwitch"));
		}
	}
	else
	{
		if (FlowSubsystem)
		{
			FlowSubsystem->SetHazardTimeScale(1.0f);
		}
	}

	StepPhysics(DeltaSeconds);

	// ---- Scene 04 Chase & Scene 05 Montage Chase: Active Comet Smash Invulnerable Obstacle Cleaving ----
	if (CometSmashTimer > 0.0f && !bCrashed)
	{
		if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(GetWorld(), AMegaJamTrackGenerator::StaticClass())))
		{
			const FVector PawnLoc = GetActorLocation();
			const int32 SmashedCount = TrackGen->CleaveObstaclesInRange(
				static_cast<float>(PawnLoc.X) - 120.0f,
				static_cast<float>(PawnLoc.X) + 1125.0f,
				420.0f,
				static_cast<float>(PawnLoc.Y));
			if (SmashedCount > 0)
			{
				SpliceChain = FMath::Clamp(SpliceChain + SmashedCount, 1, MaxSpliceChain);
				FilmMeter = FMath::Clamp(FilmMeter + 10.0f * static_cast<float>(SmashedCount), 0.0f, 100.0f);
				EmissiveBurstTimer = 0.25f;
				MatchCutFlashTimer = 1.0f;
				AddCameraTrauma(0.35f, 0.65f);
				if (AMegaJamGameMode* GM = GetWorld()->GetAuthGameMode<AMegaJamGameMode>())
				{
					GM->AddBonusScore(300.0f * static_cast<float>(SpliceChain) * static_cast<float>(SmashedCount) * 2.0f, true);
					GM->NotifySpliceChainUpdated(SpliceChain);
				}
				if (FlowSubsystem)
				{
					FlowSubsystem->SetOnBeatFlashTimer(1.0f);
				}
			}
		}
	}

	// ---- Procedural C++ Motion for VehicleStaticMesh (Banking + Western Gallop + Noir Spy Car Drift + Chase Snowboard Carve + Montage Hang Glider) ----
	if (VehicleStaticMesh && VehicleStaticMesh->IsVisible())
	{
		const EMegaJamSceneGenre CurrentGenre = FlowSubsystem ? FlowSubsystem->ActiveGenre : ActiveMusicGenre;
		const bool bIsSnowboardGenre = (CurrentGenre == EMegaJamSceneGenre::Chase || CurrentGenre == EMegaJamSceneGenre::Alpine);
		const bool bIsNoirGenre = (CurrentGenre == EMegaJamSceneGenre::Noir);
		const bool bIsMontageGlider = (CurrentGenre == EMegaJamSceneGenre::Montage);

		float MaxRollDeg = 28.0f;
		float RollScale = 0.018f;
		if (bIsSnowboardGenre)
		{
			MaxRollDeg = 35.0f;
			RollScale = 0.024f;
		}
		else if (bIsMontageGlider)
		{
			MaxRollDeg = 34.0f;
			RollScale = 0.022f;
		}
		else if (bIsNoirGenre)
		{
			MaxRollDeg = 18.0f;
			RollScale = 0.015f;
		}

		const float VehicleRoll = FMath::Clamp(static_cast<float>(Velocity.Y) * RollScale, -MaxRollDeg, MaxRollDeg);
		float VehiclePitch = 0.0f;
		float VehicleYaw = 0.0f;
		float GallopZ = 0.0f;

		if (bIsNoirGenre)
		{
			// Counter-steer spy car drift proportional to lateral velocity
			VehicleYaw = FMath::Clamp(-static_cast<float>(Velocity.Y) * 0.032f, -32.0f, 32.0f);
		}
		else if (bIsSnowboardGenre)
		{
			VehicleYaw = 18.0f;
		}
		else if (bIsMontageGlider)
		{
			const float WorldSeconds = static_cast<float>(GetWorld()->GetTimeSeconds());
			VehiclePitch = FMath::Sin(WorldSeconds * 2.4f) * 3.5f;
			GallopZ = FMath::Sin(WorldSeconds * 3.2f) * 14.0f;
		}

		const bool bOnGround = bIsGrounded || (static_cast<float>(GetActorLocation().Z) <= WideZBounds.Y + 15.0f);
		if (CurrentGenre == EMegaJamSceneGenre::Western && bOnGround && !bCrashed)
		{
			// Rhythmic dynamic TrackBPM sine-wave pitch and vertical Z-hop so the static horse mesh visibly gallops to the beat
			const float TimeSeconds = FlowSubsystem
				? FlowSubsystem->CurrentAudioTime
				: static_cast<float>(GetWorld()->GetTimeSeconds());
			const float GallopWave = FMath::Sin(TimeSeconds * PI * (CurrentTrackBPM / 60.0f));
			VehiclePitch = GallopWave * 6.5f;
			GallopZ = FMath::Abs(GallopWave) * 28.0f;
		}
		else if ((bIsSnowboardGenre || bIsMontageGlider) && ActiveTrickSpinTimer > 0.0f)
		{
			// Smoothly spin VehicleStaticMesh through a full 360-degree Yaw rotation with a -16.0 deg nose-grab pitch
			const float SpinAlpha = FMath::Clamp(1.0f - (ActiveTrickSpinTimer / 0.42f), 0.0f, 1.0f);
			VehicleYaw = (bIsSnowboardGenre ? 18.0f : 0.0f) + SpinAlpha * 360.0f;
			VehiclePitch = FMath::Sin(SpinAlpha * PI) * -16.0f;
		}

		const float BaseYawOffset = SceneVehicleYawOffsets.Contains(CurrentGenre) ? SceneVehicleYawOffsets[CurrentGenre] : 0.0f;
		VehicleStaticMesh->SetRelativeRotation(FRotator(VehiclePitch, BaseYawOffset + VehicleYaw, VehicleRoll));
		VehicleStaticMesh->SetRelativeLocation(FVector(0.0f, 0.0f, GallopZ));
		if (VisualMesh)
		{
			const FVector SeatOffset = SceneSeatOffsets.Contains(CurrentGenre) ? SceneSeatOffsets[CurrentGenre] : FVector(0.0f, 0.0f, 85.0f);
			VisualMesh->SetRelativeLocation(SeatOffset + FVector(0.0f, 0.0f, GallopZ));
		}
	}

	ActiveDollyBoostBonus = FMath::FInterpTo(ActiveDollyBoostBonus, 0.0f, DeltaSeconds, 3.5f);
	MatchCutFlashTimer = FMath::Max(0.0f, MatchCutFlashTimer - DeltaSeconds);
	DropCutFlashTimer = FMath::Max(0.0f, DropCutFlashTimer - DeltaSeconds);
	DollyBoostFlashTimer = FMath::Max(0.0f, DollyBoostFlashTimer - DeltaSeconds);

	// Normalized speed + FlowBonusOffset. Computed after StepPhysics so a crash leaves FlowRatio at 0.
	const float RawSpeedFlow = TerminalVelocity > KINDA_SMALL_NUMBER
		? FMath::Clamp(Velocity.Size() / TerminalVelocity, 0.0f, 1.0f)
		: 0.0f;
	FlowRatio = FMath::Clamp(RawSpeedFlow + FlowBonusOffset, 0.0f, 1.0f);

	if (FlowSubsystem)
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
		FlowSubsystem->SetSpliceChain(SpliceChain);
		FlowSubsystem->SetAspectHoldTimer(CurrentShotDuration);
		FlowSubsystem->SetIsShotStale(bIsShotStale);
		FlowSubsystem->SetAirExposureRatio(0.0f);
		FlowSubsystem->SetIsOverexposed(false);
		FlowSubsystem->SetFilmMeter(FilmMeter);
	}

	PushAudioParameters();
}

void AMegaJamFlowPawn::StepPhysics(float DeltaSeconds)
{
	const bool bUltraPanavision = (CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1);
	const bool bWide = (CurrentAspect == EMegaJamAspectMode::Wide_21_9 || bUltraPanavision);

	// Strafe. IA_Steer.X is always lateral (+Y / -Y).
	// Wide_21_9: 5-lane magnetic rail system (Y = -700, -350, 0, 350, 700) on horizontal cinema rail (Z = 140).
	// UltraPanavision_276_1: Center-lane standoff lock (CurrentLane = 2 -> Y = 0) with +30% speed surge.
	// Box_4_3:   full 2D analog free-flight across Y and Z.
	const float LateralAccel = StrafeForce * (bWide ? WideStrafeMultiplier : 1.0f);
	const float VerticalAccel = StrafeForce * VerticalStrafeMultiplier;

	const float CrossPlaneDamp = FMath::Exp(-LateralDamping * DeltaSeconds);

	const EMegaJamSceneGenre EffectiveGenre = GetEffectivePowerGenre();
	const bool bChaseWide = (CurrentAspect == EMegaJamAspectMode::Wide_21_9 && EffectiveGenre == EMegaJamSceneGenre::Chase);
	const bool bAlpineWide = (CurrentAspect == EMegaJamAspectMode::Wide_21_9 && EffectiveGenre == EMegaJamSceneGenre::Alpine);

	float TargetOverdriveSpeed = OverdriveSpeed;
	if (bChaseWide)
	{
		// Scene 04 Chase Director's Cut: +60% forward speed surge (1.60f multiplier)
		TargetOverdriveSpeed = FMath::Max(CruiseSpeed * 1.60f, OverdriveSpeed * 1.18f);
	}
	else if (bUltraPanavision)
	{
		TargetOverdriveSpeed = FMath::Max(CruiseSpeed * 1.30f, OverdriveSpeed);
	}
	else if (bAlpineWide)
	{
		TargetOverdriveSpeed = FMath::Max(CruiseSpeed * 1.35f, OverdriveSpeed * 1.05f);
	}

	const UMegaJamFlowSubsystem* FlowSub = GetWorld() ? GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr;
	const EMegaJamSceneGenre ActiveGenre = FlowSub ? FlowSub->ActiveGenre : ActiveMusicGenre;
	const bool bIsMontageScene = (ActiveGenre == EMegaJamSceneGenre::Montage);
	const float GroundZ = FMath::Max(2.0f, static_cast<float>(WideZBounds.X));

	if (bWide)
	{
		if (bUltraPanavision)
		{
			CurrentLane = 2;
		}
		const float CurrentY = static_cast<float>(GetActorLocation().Y);
		const float TargetLaneY = static_cast<float>(CurrentLane - 2) * LaneWidthY;
		const float NextY = FMath::FInterpTo(CurrentY, TargetLaneY, DeltaSeconds, 18.0f);
		Velocity.Y = (DeltaSeconds > KINDA_SMALL_NUMBER) ? ((NextY - CurrentY) / DeltaSeconds) : 0.0f;

		const float CurrentZ = static_cast<float>(GetActorLocation().Z);
		if (CurrentZ > GroundZ + 1.0f || Velocity.Z > 1.0f)
		{
			// Apply strict gravity (StuntGravityZ = -2800.0f) whenever airborne above FloorTopZ = 0.0f
			bIsGrounded = false;
			Velocity.Z += StuntGravityZ * DeltaSeconds;
		}
		else
		{
			bIsGrounded = true;
			Velocity.Z = 0.0f;
		}
		// Bullet-Time Overdrive / Ultra-Panavision Standoff: surge and sustain TargetOverdriveSpeed + ActiveDollyBoostBonus
		Velocity.X = FMath::FInterpTo(static_cast<float>(Velocity.X), TargetOverdriveSpeed + ActiveDollyBoostBonus, DeltaSeconds, 9.0f);
	}
	else
	{
		Velocity.Y *= CrossPlaneDamp;
		Velocity.Y += SteerInput.X * LateralAccel * DeltaSeconds;

		if (bIsMontageScene)
		{
			// Scene 05 (Montage, Hang Glider) retains full continuous 3D aerial pitch/altitude control over the canyon
			bIsGrounded = false;
			Velocity.Z *= CrossPlaneDamp;
			Velocity.Z += SteerInput.Y * VerticalAccel * DeltaSeconds;
		}
		else
		{
			// Scenes 01-04 (ActiveGenre != Montage): grounded riding on FloorTopZ = 0.0f + single ballistic stunt jump (Velocity.Z = 1150.0f) on W/Up
			const float CurrentZ = static_cast<float>(GetActorLocation().Z);
			if (SteerInput.Y <= 0.15f)
			{
				bHasProcessedJumpInput = false;
			}

			if (bIsGrounded && SteerInput.Y > 0.35f && !bHasProcessedJumpInput)
			{
				Velocity.Z = StuntJumpVelocityZ;
				bIsGrounded = false;
				bHasProcessedJumpInput = true;
			}
			else if (CurrentZ > GroundZ + 1.0f || Velocity.Z > 1.0f)
			{
				bIsGrounded = false;
				Velocity.Z += StuntGravityZ * DeltaSeconds;
			}
			else
			{
				bIsGrounded = true;
				Velocity.Z = 0.0f;
			}
		}

		// 4:3 Mode: accelerate toward CruiseSpeed (3800 u/s) and clamp to MaxFlightSpeed + ActiveDollyBoostBonus
		Velocity.X += Acceleration * DeltaSeconds;
		Velocity.X *= FMath::Exp(-Drag * DeltaSeconds);
		if (ActiveDollyBoostBonus > 1.0f)
		{
			Velocity.X = FMath::FInterpTo(static_cast<float>(Velocity.X), CruiseSpeed + ActiveDollyBoostBonus, DeltaSeconds, 9.0f);
		}
		else if (Velocity.X > CruiseSpeed)
		{
			Velocity.X = FMath::FInterpTo(static_cast<float>(Velocity.X), CruiseSpeed, DeltaSeconds, 3.5f);
		}
		Velocity.X = FMath::Clamp(static_cast<float>(Velocity.X), 0.0f, MaxFlightSpeed + ActiveDollyBoostBonus);
	}

	const float MaxAllowedSpeed = (bWide ? (TargetOverdriveSpeed + 250.0f) : MaxFlightSpeed) + ActiveDollyBoostBonus;
	if (Velocity.ContainsNaN())
	{
		Velocity = FVector::ForwardVector * CruiseSpeed;
	}
	if (Velocity.SizeSquared() > FMath::Square(MaxAllowedSpeed) && MaxAllowedSpeed > 0.0f)
	{
		Velocity = Velocity.GetSafeNormal(1e-4f, FVector::ForwardVector) * MaxAllowedSpeed;
	}

	// Ensure look-ahead orientation stays locked to +X with a safe minimum forward target offset (+10.0f X)
	const FVector CurrentPawnLoc = GetActorLocation();
	const FVector LookAheadTarget = CurrentPawnLoc + FVector(FMath::Max(10.0f, static_cast<float>(Velocity.X) * DeltaSeconds), 0.0f, 0.0f);
	const FVector SafeForward = (LookAheadTarget - CurrentPawnLoc).GetSafeNormal(1e-4f, FVector::ForwardVector);
	SetActorRotation(SafeForward.Rotation());

	// Un-swept mathematical move (bSweep = false) so the Pawn never snags on L_FlowTest's legacy hand-placed Floor/Obstacle_00..17 actors.
	AddActorWorldOffset(Velocity * DeltaSeconds, false);
	ApplyAspectClamp(DeltaSeconds);

	// Mathematical obstacle collision & near-miss graze detection against AMegaJamTrackGenerator
	if (UWorld* World = GetWorld())
	{
		if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
		{
			const bool bNoirVertigo = (EffectiveGenre == EMegaJamSceneGenre::Noir && CurrentAspect == EMegaJamAspectMode::Wide_21_9);
			const float BaseGraze = FMath::Max(GrazeRadius, AdrenalineGrazeRadius);
			const float TargetGrazeRadius = bNoirVertigo ? (BaseGraze * 2.0f) : BaseGraze;
			bool bGrazedObstacle = false;
			int32 GrazedInstanceIndex = INDEX_NONE;
			const FVector PostMoveLoc = GetActorLocation();

			const bool bHitObstacle = TrackGen->CheckObstacleCollisionAndGraze(
				PostMoveLoc,
				35.0f,
				TargetGrazeRadius,
				bGrazedObstacle,
				GrazedInstanceIndex);

			if (bHitObstacle)
			{
				if (CometSmashTimer > 0.0f)
				{
					TrackGen->CleaveObstaclesInRange(
						static_cast<float>(PostMoveLoc.X) - 120.0f,
						static_cast<float>(PostMoveLoc.X) + 1125.0f,
						420.0f,
						static_cast<float>(PostMoveLoc.Y));
				}
				else
				{
					HandleCrash(TrackGen);
					if (bCrashed)
					{
						return;
					}
				}
			}
			else if (bGrazedObstacle && GrazedInstanceIndex != INDEX_NONE)
			{
				OnGrazeBeginOverlap(GrazeSphere, TrackGen, GrazeSphere, GrazedInstanceIndex, false, FHitResult());
			}
		}
	}
}

void AMegaJamFlowPawn::ApplyAspectClamp(float DeltaSeconds)
{
	const UMegaJamFlowSubsystem* FlowSub = GetWorld() ? GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr;
	const EMegaJamSceneGenre ActiveGenre = FlowSub ? FlowSub->ActiveGenre : ActiveMusicGenre;
	const bool bIsMontageScene = (ActiveGenre == EMegaJamSceneGenre::Montage);
	const bool bWide = (CurrentAspect == EMegaJamAspectMode::Wide_21_9 || CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1);
	const FVector2D YBounds = bWide ? WideYBounds : BoxYBounds;
	const FVector2D ZBounds = bWide ? FVector2D(FMath::Max(2.0, WideZBounds.X), FMath::Max(2.0, WideZBounds.Y)) : FVector2D(FMath::Max(2.0, BoxZBounds.X), BoxZBounds.Y);
	const float GroundZ = FMath::Max(2.0f, static_cast<float>(WideZBounds.X));

	const FVector Location = GetActorLocation();
	const float ClampedY = FMath::Clamp(static_cast<float>(Location.Y), static_cast<float>(YBounds.X), static_cast<float>(YBounds.Y));
	const float ClampedZ = FMath::Clamp(static_cast<float>(Location.Z), static_cast<float>(ZBounds.X), static_cast<float>(ZBounds.Y));

	// Stop velocity from driving further out of the corridor.
	if ((Location.Y >= YBounds.Y && Velocity.Y > 0.0) || (Location.Y <= YBounds.X && Velocity.Y < 0.0))
	{
		Velocity.Y = 0.0;
	}

	const bool bBallisticAirborne = (!bIsMontageScene || bWide) && (Location.Z > GroundZ + 1.0f || Velocity.Z > 1.0f);
	if (!bBallisticAirborne && ((Location.Z >= ZBounds.Y && Velocity.Z > 0.0) || (Location.Z <= ZBounds.X && Velocity.Z < 0.0)))
	{
		Velocity.Z = 0.0;
	}

	// Enforce crisp ground touchdown at FloorTopZ = 0.0f in Scenes 01-04 (and 21:9 rail mode)
	if (!bIsMontageScene || bWide)
	{
		if (Location.Z <= GroundZ + 0.5f && Velocity.Z <= 0.0)
		{
			Velocity.Z = 0.0;
			bIsGrounded = true;
		}
	}

	if (FMath::IsNearlyEqual(ClampedY, static_cast<float>(Location.Y)) && FMath::IsNearlyEqual(ClampedZ, static_cast<float>(Location.Z)))
	{
		return;
	}

	// Fast interpolation toward the clamped position: a punchy squeeze, not a one-frame teleport.
	// While airborne in a ballistic stunt hop or Clapperboard launch, allow Z to follow StuntGravityZ cleanly down to GroundZ.
	const float NewY = FMath::FInterpTo(static_cast<float>(Location.Y), ClampedY, DeltaSeconds, AspectClampInterpSpeed);
	float NewZ = static_cast<float>(Location.Z);
	if (Location.Z < GroundZ)
	{
		NewZ = GroundZ;
		Velocity.Z = FMath::Max(0.0, Velocity.Z);
		bIsGrounded = true;
	}
	else if (bIsDropCutSlamming || bBallisticAirborne)
	{
		NewZ = FMath::Clamp(static_cast<float>(Location.Z), GroundZ, static_cast<float>(BoxZBounds.Y));
	}
	else
	{
		NewZ = FMath::FInterpTo(static_cast<float>(Location.Z), ClampedZ, DeltaSeconds, AspectClampInterpSpeed);
	}
	AddActorWorldOffset(FVector(0.0f, NewY - Location.Y, NewZ - Location.Z), false);
}

void AMegaJamFlowPawn::ExecuteEditorSplice()
{
	TriggerHardCut();
}

void AMegaJamFlowPawn::ExecuteNoirVertigoPulse()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World || !IsInBulletTimeOverdrive() || GetEffectivePowerGenre() != EMegaJamSceneGenre::Noir)
	{
		return;
	}

	AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && !GameMode->IsPlaying())
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double EffectiveCooldown = FMath::Max(static_cast<double>(EditorSpliceCooldown), static_cast<double>(CutCooldown));
	if (Now - LastEditorSpliceTime < EffectiveCooldown || Now - LastCutTime < EffectiveCooldown)
	{
		return;
	}
	LastEditorSpliceTime = Now;
	LastCutTime = Now;

	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	CurrentTake = (CurrentTake == EMegaJamTakeState::StudioTake) ? EMegaJamTakeState::DirectorsTake : EMegaJamTakeState::StudioTake;
	if (FlowSubsystem)
	{
		FlowSubsystem->SetTakeAActive(CurrentTake == EMegaJamTakeState::StudioTake);
		FlowSubsystem->RecordSpecialMoveExecution(FlowSubsystem->ActiveGenre);
	}

	Velocity += GetActorForwardVector().GetSafeNormal(1e-4f, FVector::ForwardVector) * CutSpeedImpulse;
	CurrentFOVPunch = CutFOVPunch;

	const bool bOnBeat = FlowSubsystem && FlowSubsystem->IsWithinBeatWindow(0.18f);
	const float BaseGraze = FMath::Max(GrazeRadius, AdrenalineGrazeRadius);
	const float DoubleGrazeRadius = BaseGraze * 2.0f;

	int32 CashedInCount = 0;
	if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
	{
		const FVector PawnLoc = GetActorLocation();
		const float ForwardSpan = bOnBeat ? 2750.0f : 1750.0f;
		CashedInCount = TrackGen->SpliceObstaclesInFront(PawnLoc, ForwardSpan, DoubleGrazeRadius);
	}

	// Pulse ProceduralSpliceRingMesh silver-white
	SpliceRingTimer = 0.18f;
	bSpliceRingIsHorizontal = true;
	if (SpliceRingMID)
	{
		const FLinearColor SilverWhite(35.0f, 38.0f, 45.0f, 1.0f);
		SpliceRingMID->SetVectorParameterValue(TEXT("Color"), SilverWhite);
		SpliceRingMID->SetVectorParameterValue(TEXT("BaseColor"), SilverWhite);
		SpliceRingMID->SetVectorParameterValue(TEXT("TakeColor"), SilverWhite);
		SpliceRingMID->SetVectorParameterValue(TEXT("EmissiveColor"), SilverWhite);
	}
	if (ProceduralSpliceRingMesh)
	{
		ProceduralSpliceRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
		ProceduralSpliceRingMesh->SetRelativeRotation(FRotator::ZeroRotator);
		ProceduralSpliceRingMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 0.08f));
		ProceduralSpliceRingMesh->SetVisibility(true);
	}

	if (bOnBeat || CashedInCount > 0)
	{
		BP_PlaySpliceVFX();
		SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
		FilmMeter = FMath::Clamp(FilmMeter + (bOnBeat ? 15.0f : 12.0f), 0.0f, 100.0f);
		EmissiveBurstTimer = 0.28f;
		MatchCutFlashTimer = 1.1f;

		if (GameMode)
		{
			const int32 EffectiveTargets = FMath::Max(1, CashedInCount);
			GameMode->AddBonusScore(260.0f * static_cast<float>(SpliceChain) * static_cast<float>(EffectiveTargets) * 2.0f, true);
			GameMode->NotifySpliceChainUpdated(SpliceChain);
		}
		if (FlowSubsystem)
		{
			FlowSubsystem->SetOnBeatFlashTimer(1.1f);
			FlowSubsystem->SetSpliceChain(SpliceChain);
			FlowSubsystem->SetFilmMeter(FilmMeter);
		}

		AddCameraTrauma(0.42f, 0.70f);
		TriggerMicroHitstop(0.035f, 0.20f);
		const float ChainPitch = FMath::Clamp(1.16f + 0.04f * static_cast<float>(SpliceChain), 1.16f, 1.48f);
		PlayCue(TEXT("OnBeatSplice"), BeatSpliceSound, 1.0f, ChainPitch, true);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(202605, 1.6f, FColor::White,
				FString::Printf(TEXT("★ NOIR VERTIGO PULSE! CASHED IN %d TARGET(S) -> FILM FRAMES! (CHAIN x%d)"),
					CashedInCount, SpliceChain),
				true, FVector2D(1.35f, 1.35f));
		}
	}
	else
	{
		AddCameraTrauma(0.18f, 0.35f);
		TryTriggerMatchCut();
	}

	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	TriggerAudioEvent(TEXT("OnHardCut"));
	OnTakeCut.Broadcast(CurrentTake);
	OnTakeCutEvent(CurrentTake);
}

void AMegaJamFlowPawn::ExecuteChaseCometSmash()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World || !IsInBulletTimeOverdrive() || GetEffectivePowerGenre() != EMegaJamSceneGenre::Chase)
	{
		return;
	}

	AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && !GameMode->IsPlaying())
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double EffectiveCooldown = FMath::Max(static_cast<double>(EditorSpliceCooldown), static_cast<double>(CutCooldown));
	if (Now - LastEditorSpliceTime < EffectiveCooldown || Now - LastCutTime < EffectiveCooldown)
	{
		return;
	}
	LastEditorSpliceTime = Now;
	LastCutTime = Now;

	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	CurrentTake = (CurrentTake == EMegaJamTakeState::StudioTake) ? EMegaJamTakeState::DirectorsTake : EMegaJamTakeState::StudioTake;
	if (FlowSubsystem)
	{
		FlowSubsystem->SetTakeAActive(CurrentTake == EMegaJamTakeState::StudioTake);
		FlowSubsystem->RecordSpecialMoveExecution(FlowSubsystem->ActiveGenre);
	}

	const bool bOnBeat = FlowSubsystem && FlowSubsystem->IsWithinBeatWindow(0.18f);
	CometSmashTimer = bOnBeat ? 1.10f : 0.65f;
	ActiveTrickSpinTimer = 0.42f;
	ActiveDollyBoostBonus = FMath::Max(ActiveDollyBoostBonus, bOnBeat ? 2600.0f : 1800.0f);
	Velocity.X += CutSpeedImpulse;
	CurrentFOVPunch = DropCutFOVPunch;

	BP_PlayCometSmashVFX();
	BP_PlayDollyBoostVFX();
	BP_PlayAvalancheTrickVFX(bOnBeat);

	int32 CleavedCount = 0;
	if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
	{
		const FVector PawnLoc = GetActorLocation();
		CleavedCount = TrackGen->CleaveObstaclesInRange(
			static_cast<float>(PawnLoc.X) - 120.0f,
			static_cast<float>(PawnLoc.X) + 1125.0f,
			420.0f,
			static_cast<float>(PawnLoc.Y));
	}

	SpliceRingTimer = 0.18f;
	bSpliceRingIsHorizontal = true;
	if (SpliceRingMID)
	{
		const FLinearColor CometAmber(55.0f, 28.0f, 6.0f, 1.0f);
		SpliceRingMID->SetVectorParameterValue(TEXT("Color"), CometAmber);
		SpliceRingMID->SetVectorParameterValue(TEXT("BaseColor"), CometAmber);
		SpliceRingMID->SetVectorParameterValue(TEXT("TakeColor"), CometAmber);
		SpliceRingMID->SetVectorParameterValue(TEXT("EmissiveColor"), CometAmber);
	}
	if (ProceduralSpliceRingMesh)
	{
		ProceduralSpliceRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
		ProceduralSpliceRingMesh->SetRelativeRotation(FRotator::ZeroRotator);
		ProceduralSpliceRingMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 0.08f));
		ProceduralSpliceRingMesh->SetVisibility(true);
	}

	if (bOnBeat || CleavedCount > 0)
	{
		SpliceChain = FMath::Clamp(SpliceChain + FMath::Max(1, CleavedCount), 1, MaxSpliceChain);
		FilmMeter = FMath::Clamp(FilmMeter + (bOnBeat ? 15.0f : 10.0f), 0.0f, 100.0f);
		EmissiveBurstTimer = 0.30f;
		MatchCutFlashTimer = 1.1f;

		if (GameMode)
		{
			const int32 EffectiveCleaved = FMath::Max(1, CleavedCount);
			GameMode->AddBonusScore(300.0f * static_cast<float>(SpliceChain) * static_cast<float>(EffectiveCleaved) * 2.0f, true);
			GameMode->NotifySpliceChainUpdated(SpliceChain);
		}
		if (FlowSubsystem)
		{
			FlowSubsystem->SetOnBeatFlashTimer(1.1f);
			FlowSubsystem->SetSpliceChain(SpliceChain);
			FlowSubsystem->SetFilmMeter(FilmMeter);
		}

		AddCameraTrauma(0.55f, 0.80f);
		TriggerMicroHitstop(0.035f, 0.22f);
		const float ChainPitch = FMath::Clamp(1.20f + 0.04f * static_cast<float>(SpliceChain), 1.20f, 1.52f);
		PlayCue(TEXT("OnBeatSplice"), BeatSpliceSound, 1.0f, ChainPitch, true);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(202605, 1.6f, FColor::Orange,
				FString::Printf(TEXT("★ COMET SMASH OVERDRIVE! (%.2fs INVULNERABLE | CLEAVED %d | CHAIN x%d)"),
					CometSmashTimer, CleavedCount, SpliceChain),
				true, FVector2D(1.4f, 1.4f));
		}
	}
	else
	{
		AddCameraTrauma(0.28f, 0.50f);
		TryTriggerMatchCut();
	}

	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	TriggerAudioEvent(TEXT("OnHardCut"));
	OnTakeCut.Broadcast(CurrentTake);
	OnTakeCutEvent(CurrentTake);
}

void AMegaJamFlowPawn::ExecuteAlpineTrickOllie()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World || !IsInBulletTimeOverdrive() || GetEffectivePowerGenre() != EMegaJamSceneGenre::Alpine)
	{
		return;
	}

	AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && !GameMode->IsPlaying())
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	constexpr double OllieCooldown = 0.26;
	if (Now - LastEditorSpliceTime < OllieCooldown || Now - LastCutTime < OllieCooldown)
	{
		return;
	}
	LastEditorSpliceTime = Now;
	LastCutTime = Now;

	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	CurrentTake = (CurrentTake == EMegaJamTakeState::StudioTake) ? EMegaJamTakeState::DirectorsTake : EMegaJamTakeState::StudioTake;
	if (FlowSubsystem)
	{
		FlowSubsystem->SetTakeAActive(CurrentTake == EMegaJamTakeState::StudioTake);
		FlowSubsystem->RecordSpecialMoveExecution(FlowSubsystem->ActiveGenre);
	}

	// Vertical jump impulse while maintaining 5-lane lateral rail lock in 21:9
	Velocity.Z = 1350.0f;
	bIsGrounded = false;
	Velocity.X += CutSpeedImpulse;
	CurrentFOVPunch = CutFOVPunch;
	ActiveTrickSpinTimer = 0.42f;

	const bool bOnBeat = FlowSubsystem && FlowSubsystem->IsWithinBeatWindow(0.18f);
	BP_PlayAvalancheTrickVFX(bOnBeat);

	const FVector PawnLoc = GetActorLocation();
	const float PawnX = static_cast<float>(PawnLoc.X);
	const float PawnY = static_cast<float>(PawnLoc.Y);

	int32 CleavedCount = 0;
	if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
	{
		if (bOnBeat)
		{
			CleavedCount = TrackGen->CleaveObstaclesInRange(PawnX - 150.0f, PawnX + 3000.0f, 550.0f, PawnY);
		}
		else
		{
			CleavedCount = TrackGen->CleaveObstaclesInRange(PawnX - 120.0f, PawnX + 1500.0f, 240.0f, PawnY);
		}
	}

	if (bOnBeat)
	{
		// On-Beat Cryo-Shatter: refund +10 FilmMeter, +1800 Dolly Boost, horizontal frost shockwave ring
		SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
		FilmMeter = FMath::Clamp(FilmMeter + 10.0f, 0.0f, 100.0f);
		ActiveDollyBoostBonus = FMath::Max(ActiveDollyBoostBonus, 1800.0f);
		EmissiveBurstTimer = 0.30f;
		MatchCutFlashTimer = 1.1f;

		SpliceRingTimer = 0.18f;
		bSpliceRingIsHorizontal = true;
		if (SpliceRingMID)
		{
			const FLinearColor FrostCyan(22.0f, 45.0f, 65.0f, 1.0f);
			SpliceRingMID->SetVectorParameterValue(TEXT("Color"), FrostCyan);
			SpliceRingMID->SetVectorParameterValue(TEXT("BaseColor"), FrostCyan);
			SpliceRingMID->SetVectorParameterValue(TEXT("TakeColor"), FrostCyan);
			SpliceRingMID->SetVectorParameterValue(TEXT("EmissiveColor"), FrostCyan);
		}
		if (ProceduralSpliceRingMesh)
		{
			ProceduralSpliceRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
			ProceduralSpliceRingMesh->SetRelativeRotation(FRotator::ZeroRotator);
			ProceduralSpliceRingMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 0.08f));
			ProceduralSpliceRingMesh->SetVisibility(true);
		}
		if (AMegaJamPlayerController* PC = Cast<AMegaJamPlayerController>(GetController()))
		{
			if (PC->CinemaHUDInstance)
			{
				PC->CinemaHUDInstance->TriggerSpliceShockwaveFlash();
			}
		}

		if (GameMode)
		{
			const int32 EffectiveCleaved = FMath::Max(1, CleavedCount);
			GameMode->AddBonusScore(280.0f * static_cast<float>(SpliceChain) * static_cast<float>(EffectiveCleaved) * 2.0f, true);
			GameMode->NotifySpliceChainUpdated(SpliceChain);
		}
		if (FlowSubsystem)
		{
			FlowSubsystem->SetOnBeatFlashTimer(1.1f);
			FlowSubsystem->SetSpliceChain(SpliceChain);
			FlowSubsystem->SetFilmMeter(FilmMeter);
		}

		AddCameraTrauma(0.50f);
		TriggerMicroHitstop(0.035f, 0.25f);
		const float ChainPitch = FMath::Clamp(1.18f + 0.04f * static_cast<float>(SpliceChain), 1.18f, 1.50f);
		PlayCue(TEXT("OnBeatSplice"), BeatSpliceSound, 1.0f, ChainPitch, true);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(202605, 1.6f, FColor::Cyan,
				FString::Printf(TEXT("★ AVALANCHE CRYO-SHATTER! SHATTERED %d BOULDER(S)! (+10%% FILM | CHAIN x%d)"),
					CleavedCount, SpliceChain),
				true, FVector2D(1.4f, 1.4f));
		}
	}
	else
	{
		AddCameraTrauma(0.22f);
		if (CleavedCount > 0 && GameMode)
		{
			GameMode->AddBonusScore(150.0f * static_cast<float>(SpliceChain) * static_cast<float>(CleavedCount), true);
		}
		TryTriggerMatchCut();
	}

	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	TriggerAudioEvent(TEXT("OnHardCut"));
	OnTakeCut.Broadcast(CurrentTake);
	OnTakeCutEvent(CurrentTake);
}

void AMegaJamFlowPawn::ExecuteWesternBeatSlash()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World || !IsInBulletTimeOverdrive() || GetEffectivePowerGenre() != EMegaJamSceneGenre::Western)
	{
		return;
	}

	AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && !GameMode->IsPlaying())
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double EffectiveCooldown = FMath::Max(static_cast<double>(EditorSpliceCooldown), static_cast<double>(CutCooldown));
	if (Now - LastEditorSpliceTime < EffectiveCooldown || Now - LastCutTime < EffectiveCooldown)
	{
		return;
	}
	LastEditorSpliceTime = Now;
	LastCutTime = Now;

	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();

	CurrentTake = (CurrentTake == EMegaJamTakeState::StudioTake) ? EMegaJamTakeState::DirectorsTake : EMegaJamTakeState::StudioTake;
	if (FlowSubsystem)
	{
		FlowSubsystem->SetTakeAActive(CurrentTake == EMegaJamTakeState::StudioTake);
		FlowSubsystem->RecordSpecialMoveExecution(FlowSubsystem->ActiveGenre);
	}

	Velocity += GetActorForwardVector().GetSafeNormal(1e-4f, FVector::ForwardVector) * CutSpeedImpulse;
	CurrentFOVPunch = CutFOVPunch;

	const bool bOnBeat = FlowSubsystem && FlowSubsystem->IsWithinBeatWindow(0.18f);
	const float PawnX = static_cast<float>(GetActorLocation().X);
	const float ForwardMaxX = PawnX + (bOnBeat ? 3750.0f : 2250.0f);
	const float HalfWidthY = bOnBeat ? MEGAJAM_CORRIDOR_HALF_WIDTH : 360.0f;

	int32 CleavedCount = 0;
	if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
	{
		CleavedCount = TrackGen->CleaveObstaclesInRange(PawnX - 120.0f, ForwardMaxX, HalfWidthY);
	}

	// Trigger Procedural 3D Emissive Slash Blade (0.16s sweep from -70 to +70 deg Yaw at -18 deg Roll)
	SlashArcTimer = 0.16f;
	bActiveSlashIsOnBeat = bOnBeat;
	if (SlashArcMID)
	{
		const FLinearColor BladeEmissive = bOnBeat
			? FLinearColor(45.0f, 28.0f, 8.0f, 1.0f)
			: FLinearColor(8.0f, 12.0f, 20.0f, 1.0f);
		SlashArcMID->SetVectorParameterValue(TEXT("Color"), BladeEmissive);
		SlashArcMID->SetVectorParameterValue(TEXT("BaseColor"), BladeEmissive);
		SlashArcMID->SetVectorParameterValue(TEXT("TakeColor"), BladeEmissive);
		SlashArcMID->SetVectorParameterValue(TEXT("EmissiveColor"), BladeEmissive);
	}
	if (ProceduralSlashArcMesh)
	{
		ProceduralSlashArcMesh->SetRelativeLocation(FVector(420.0f, 0.0f, 40.0f));
		ProceduralSlashArcMesh->SetRelativeRotation(FRotator(0.0f, -70.0f, -18.0f));
		ProceduralSlashArcMesh->SetRelativeScale3D(FVector(0.35f, bOnBeat ? 24.0f : 6.5f, 0.08f));
		ProceduralSlashArcMesh->SetVisibility(true);
	}
	if (AMegaJamPlayerController* PC = Cast<AMegaJamPlayerController>(GetController()))
	{
		if (PC->CinemaHUDInstance)
		{
			PC->CinemaHUDInstance->TriggerSlashCutFlash(bOnBeat);
		}
	}

	if (bOnBeat)
	{
		ActiveSlashRoll = -2.0f;
		BP_PlaySlashVFX(true);

		SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
		FilmMeter = FMath::Clamp(FilmMeter + 15.0f, 0.0f, 100.0f);
		EmissiveBurstTimer = 0.30f;
		MatchCutFlashTimer = 1.1f;

		if (GameMode)
		{
			const int32 EffectiveCleaved = FMath::Max(1, CleavedCount);
			GameMode->AddBonusScore(300.0f * static_cast<float>(SpliceChain) * static_cast<float>(EffectiveCleaved) * 2.0f, true);
			GameMode->NotifySpliceChainUpdated(SpliceChain);
		}
		if (FlowSubsystem)
		{
			FlowSubsystem->SetOnBeatFlashTimer(1.1f);
			FlowSubsystem->SetSpliceChain(SpliceChain);
			FlowSubsystem->SetFilmMeter(FilmMeter);
		}

		AddCameraTrauma(0.60f);
		TriggerMicroHitstop(0.04f, 0.2f);
		PlayCue(TEXT("OnBeatSplice"), BeatSpliceSound, 1.0f, 1.45f, true);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(202605, 1.6f, FColor::Yellow,
				FString::Printf(TEXT("★ QUICK-DRAW BEAT-SLASH! CLEAVED %d BARRICADE(S)! (CHAIN x%d)"),
					CleavedCount, SpliceChain),
				true, FVector2D(1.4f, 1.4f));
		}
	}
	else
	{
		ActiveSlashRoll = -2.0f;
		BP_PlaySlashVFX(false);
		AddCameraTrauma(0.30f);
		if (CleavedCount > 0 && GameMode)
		{
			GameMode->AddBonusScore(150.0f * static_cast<float>(SpliceChain) * static_cast<float>(CleavedCount), true);
		}
		TryTriggerMatchCut();
	}

	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	TriggerAudioEvent(TEXT("OnHardCut"));
	OnTakeCut.Broadcast(CurrentTake);
	OnTakeCutEvent(CurrentTake);
}

bool AMegaJamFlowPawn::TriggerHardCut()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return false;
	}

	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	const bool bInPowerUpMode = IsInBulletTimeOverdrive();
	const EMegaJamSceneGenre EffectiveGenre = GetEffectivePowerGenre();

	// Contextual Special Move Router: Special moves ONLY fire when locked into Director's Cut power-up mode (Ctrl / C)
	// and strictly match the active genre (or active Montage sub-genre in Scene 05).
	if (bInPowerUpMode)
	{
		if (EffectiveGenre == EMegaJamSceneGenre::Western)
		{
			ExecuteWesternBeatSlash();
			return true;
		}
		if (EffectiveGenre == EMegaJamSceneGenre::Noir)
		{
			ExecuteNoirVertigoPulse();
			return true;
		}
		if (EffectiveGenre == EMegaJamSceneGenre::Chase)
		{
			ExecuteChaseCometSmash();
			return true;
		}
		if (EffectiveGenre == EMegaJamSceneGenre::Alpine)
		{
			ExecuteAlpineTrickOllie();
			return true;
		}
	}

	AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && (!GameMode->IsPlaying() || GameMode->GetRunTime() < 0.05f))
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	const double EffectiveCooldown = FMath::Max(static_cast<double>(EditorSpliceCooldown), static_cast<double>(CutCooldown));
	if (Now - LastEditorSpliceTime < EffectiveCooldown || Now - LastCutTime < EffectiveCooldown)
	{
		return false;
	}
	LastEditorSpliceTime = Now;
	LastCutTime = Now;

	// Toggle Take A / Take B state
	CurrentTake = (CurrentTake == EMegaJamTakeState::StudioTake) ? EMegaJamTakeState::DirectorsTake : EMegaJamTakeState::StudioTake;
	if (FlowSubsystem)
	{
		FlowSubsystem->SetTakeAActive(CurrentTake == EMegaJamTakeState::StudioTake);
	}

	// Brief forward impulse. StepPhysics re-applies the terminal velocity clamp.
	Velocity += GetActorForwardVector().GetSafeNormal(1e-4f, FVector::ForwardVector) * CutSpeedImpulse;
	CurrentFOVPunch = CutFOVPunch;

	// Scene 01 Action (or Scene 05 Montage Action) Beat-Splice Harvesting:
	// Strictly gated to Director's Cut power-up mode (bInPowerUpMode) AND Action genre!
	// In 4:3 mode (or outside Action), SpaceBar only switches takes without firing the EMP shockwave or shredding obstacles.
	int32 ShreddedCount = 0;
	if (bInPowerUpMode && EffectiveGenre == EMegaJamSceneGenre::Action && FlowSubsystem && FlowSubsystem->IsWithinBeatWindow(0.18f))
	{
		if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
		{
			ShreddedCount = TrackGen->SpliceObstaclesInFront(GetActorLocation(), 1500.0f, 600.0f);
		}
	}

	if (ShreddedCount > 0)
	{
		if (FlowSubsystem)
		{
			FlowSubsystem->RecordSpecialMoveExecution(FlowSubsystem->ActiveGenre);
		}
		BP_PlaySpliceVFX();

		// Trigger Procedural 3D Beat-Splice Shockwave Ring (0.18s electric-cyan radial expansion underneath chassis)
		SpliceRingTimer = 0.18f;
		bSpliceRingIsHorizontal = true;
		if (SpliceRingMID)
		{
			const FLinearColor ElectricCyan(6.0f, 35.0f, 60.0f, 1.0f);
			SpliceRingMID->SetVectorParameterValue(TEXT("Color"), ElectricCyan);
			SpliceRingMID->SetVectorParameterValue(TEXT("BaseColor"), ElectricCyan);
			SpliceRingMID->SetVectorParameterValue(TEXT("TakeColor"), ElectricCyan);
			SpliceRingMID->SetVectorParameterValue(TEXT("EmissiveColor"), ElectricCyan);
		}
		if (ProceduralSpliceRingMesh)
		{
			ProceduralSpliceRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
			ProceduralSpliceRingMesh->SetRelativeRotation(FRotator::ZeroRotator);
			ProceduralSpliceRingMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 0.08f));
			ProceduralSpliceRingMesh->SetVisibility(true);
		}
		if (AMegaJamPlayerController* PC = Cast<AMegaJamPlayerController>(GetController()))
		{
			if (PC->CinemaHUDInstance)
			{
				PC->CinemaHUDInstance->TriggerSpliceShockwaveFlash();
			}
		}

		SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
		FilmMeter = FMath::Clamp(FilmMeter + 15.0f, 0.0f, 100.0f);
		EmissiveBurstTimer = 0.30f;
		MatchCutFlashTimer = 1.1f;

		if (GameMode)
		{
			const float OverdriveMult = IsInBulletTimeOverdrive() ? 2.0f : 1.0f;
			GameMode->AddBonusScore(250.0f * static_cast<float>(SpliceChain) * static_cast<float>(ShreddedCount) * OverdriveMult, true);
			GameMode->NotifySpliceChainUpdated(SpliceChain);
		}
		if (FlowSubsystem)
		{
			FlowSubsystem->SetOnBeatFlashTimer(1.1f);
			FlowSubsystem->SetSpliceChain(SpliceChain);
			FlowSubsystem->SetFilmMeter(FilmMeter);
		}

		TriggerMicroHitstop(0.06f, 0.10f);
		AddCameraTrauma(0.40f, 0.70f);
		const float ChainPitch = FMath::Clamp(1.15f + 0.04f * static_cast<float>(SpliceChain), 1.15f, 1.45f);
		PlayCue(TEXT("OnBeatSplice"), BeatSpliceSound, 1.0f, ChainPitch, true);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(202605, 1.6f, FColor::Yellow,
				FString::Printf(TEXT("★ BEAT-SPLICE HARVEST! SHREDDED %d OBSTACLE(S) -> FILM FRAMES! (+%d PTS | CHAIN x%d)"),
					ShreddedCount, 250 * SpliceChain * ShreddedCount, SpliceChain),
				true, FVector2D(1.35f, 1.35f));
		}
	}
	else
	{
		AddCameraTrauma(0.14f, 0.28f);
		TryTriggerMatchCut();
	}

	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	TriggerAudioEvent(TEXT("OnHardCut"));
	OnTakeCut.Broadcast(CurrentTake);
	OnTakeCutEvent(CurrentTake);
	return true;
}

void AMegaJamFlowPawn::ToggleAspect()
{
	UWorld* World = GetWorld();
	UMegaJamFlowSubsystem* FlowSubsystem = World ? World->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr;

	if (CurrentAspect == EMegaJamAspectMode::Box_4_3)
	{
		// Require at least 25% Film Meter to activate 21:9 / 2.76:1 Director's Cut
		if (FilmMeter < OverdriveMinActivationMeter)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(202601, 1.5f, FColor::Orange,
					FString::Printf(TEXT("NEED 25%% FILM METER FOR DIRECTOR'S CUT (%d%%)"), FMath::RoundToInt(FilmMeter)),
					true, FVector2D(1.3f, 1.3f));
			}
			return;
		}

		const EMegaJamSceneGenre EffectiveGenre = GetEffectivePowerGenre();
		const bool bIsWesternGenre = (EffectiveGenre == EMegaJamSceneGenre::Western);
		const bool bIsAlpineGenre = (EffectiveGenre == EMegaJamSceneGenre::Alpine);

		if (bIsWesternGenre)
		{
			// Scene 02 Western Standoff: lock to Center Lane (CurrentLane = 2) and snap to Ultra-Panavision 2.76:1
			CurrentLane = 2;
			bHasProcessedLaneInput = false;
			SetAspectMode(EMegaJamAspectMode::UltraPanavision_276_1);
		}
		else
		{
			// Scene 01 Action, Scene 03 Noir, Scene 04 Chase & Scene 05 Montage: lock onto nearest of the 5 magnetic lanes (-700, -350, 0, 350, 700) on entry
			const float EntryY = static_cast<float>(GetActorLocation().Y);
			CurrentLane = FMath::Clamp(FMath::RoundToInt((EntryY / FMath::Max(1.0f, LaneWidthY)) + 2.0f), 0, 4);
			bHasProcessedLaneInput = false;
			SetAspectMode(EMegaJamAspectMode::Wide_21_9);
		}

		CurrentShotDuration = 0.0f;
		AspectHoldTimer = 0.0f;
		if (bIsShotStale)
		{
			bIsShotStale = false;
			BP_OnStaleShotStateChanged(false);
		}
		bIsDropCutSlamming = false;

		// Activate Director's Cut: +60% speed surge (Chase), +35% speed surge (Alpine), +30% speed surge (Western), or OverdriveSpeed
		if (EffectiveGenre == EMegaJamSceneGenre::Chase)
		{
			Velocity.X = FMath::Max(static_cast<float>(Velocity.X) * 1.60f, OverdriveSpeed * 1.18f);
		}
		else if (bIsAlpineGenre)
		{
			Velocity.X = FMath::Max(static_cast<float>(Velocity.X) * 1.35f, OverdriveSpeed * 1.05f);
		}
		else if (bIsWesternGenre)
		{
			Velocity.X = FMath::Max(static_cast<float>(Velocity.X) * 1.30f, OverdriveSpeed);
		}
		else
		{
			Velocity.X = OverdriveSpeed;
		}
		Velocity.Z = 0.0f;
		CurrentFOVPunch = DropCutFOVPunch;
		SlamPitchPunch = -5.5f;
		EmissiveBurstTimer = 0.25f;
		DropCutFlashTimer = 1.4f;
		FlowBonusOffset = FMath::Clamp(FlowBonusOffset + 0.22f, 0.0f, 0.35f);
		FlowRatio = 1.0f;

		AddCameraTrauma(0.65f);
		TriggerMicroHitstop(0.03f, 0.15f);

		if (FlowSubsystem)
		{
			FlowSubsystem->SetHazardTimeScale(0.45f);
			FlowSubsystem->SetDropCutBannerTimer(1.4f);
		}

		// Check dynamic BPM On-Beat Cut window (±0.16s) + Match-Cut window
		if (!TryTriggerOnBeatCut())
		{
			TryTriggerMatchCut(false);
		}

		PushTakeAndAspectToSubsystem();
		// Cinematic whoosh at natural pitch: the only place DirectorsCutEnterSound plays (Issue #24).
		PlayCue(TEXT("OnDirectorsCutEnter"), DirectorsCutEnterSound, 1.05f, 1.0f);
		TriggerAudioEvent(TEXT("OnHardCut"));
		TriggerAudioEvent(TEXT("OnAspectSwitch"));

		if (GEngine)
		{
			const FString CutBanner = bIsWesternGenre
				? TEXT("2.76:1 ULTRA-PANAVISION // QUICK-DRAW STANDOFF!")
				: TEXT("21:9 DIRECTOR'S CUT // BULLET-TIME OVERDRIVE!");
			GEngine->AddOnScreenDebugMessage(202601, 2.5f, bIsWesternGenre ? FColor::Yellow : FColor::Cyan,
				CutBanner, true, FVector2D(1.5f, 1.5f));
		}
	}
	else
	{
		// Return to 4:3 Flight & Charge Mode
		bHasProcessedLaneInput = false;
		SetAspectMode(EMegaJamAspectMode::Box_4_3);
		CurrentShotDuration = 0.0f;
		AspectHoldTimer = 0.0f;
		if (bIsShotStale)
		{
			bIsShotStale = false;
			BP_OnStaleShotStateChanged(false);
		}
		bIsDropCutSlamming = false;

		if (FlowSubsystem)
		{
			FlowSubsystem->SetHazardTimeScale(1.0f);
		}

		PushTakeAndAspectToSubsystem();
		PlayCue(TEXT("OnDirectorsCutExit"), DirectorsCutExitSound, 0.50f, 0.85f);
		TriggerAudioEvent(TEXT("OnAspectSwitch"));

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(202601, 2.0f, FColor::Orange,
				TEXT("4:3 FLIGHT & CHARGE MODE"), true, FVector2D(1.4f, 1.4f));
		}
	}
}

void AMegaJamFlowPawn::SetAspectMode(EMegaJamAspectMode NewAspect)
{
	CurrentAspect = NewAspect;

	if (Camera)
	{
		const EMegaJamSceneGenre EffectiveGenre = GetEffectivePowerGenre();

		if (EffectiveGenre == EMegaJamSceneGenre::Noir)
		{
			// Stark black-and-white monochrome grading for Scene 03 Noir (Spy Car) & Montage Noir Segment
			Camera->PostProcessBlendWeight = 1.0f;
			Camera->PostProcessSettings.bOverride_ColorSaturation = true;
			Camera->PostProcessSettings.ColorSaturation = FVector4(0.05f, 0.05f, 0.08f, 1.0f);
			Camera->PostProcessSettings.bOverride_ColorContrast = true;
			Camera->PostProcessSettings.ColorContrast = FVector4(1.38f, 1.38f, 1.42f, 1.0f);
			Camera->PostProcessSettings.bOverride_ColorGain = false;
		}
		else if (CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
		{
			// Golden-Hour Sepia post-process grading for Scene 02 Ultra-Panavision 2.76:1
			Camera->PostProcessBlendWeight = 1.0f;
			Camera->PostProcessSettings.bOverride_ColorSaturation = true;
			Camera->PostProcessSettings.ColorSaturation = FVector4(0.62f, 0.62f, 0.62f, 1.0f);
			Camera->PostProcessSettings.bOverride_ColorContrast = false;
			Camera->PostProcessSettings.bOverride_ColorGain = true;
			Camera->PostProcessSettings.ColorGain = FVector4(1.16f, 1.02f, 0.80f, 1.0f);
		}
		else if (CurrentAspect == EMegaJamAspectMode::Wide_21_9 && EffectiveGenre == EMegaJamSceneGenre::Chase)
		{
			// Warm amber highway grading for Scene 04 Chase (Snowboard Comet Smash)
			Camera->PostProcessBlendWeight = 1.0f;
			Camera->PostProcessSettings.bOverride_ColorSaturation = true;
			Camera->PostProcessSettings.ColorSaturation = FVector4(1.08f, 1.02f, 0.88f, 1.0f);
			Camera->PostProcessSettings.bOverride_ColorContrast = true;
			Camera->PostProcessSettings.ColorContrast = FVector4(1.12f, 1.10f, 1.05f, 1.0f);
			Camera->PostProcessSettings.bOverride_ColorGain = true;
			Camera->PostProcessSettings.ColorGain = FVector4(1.22f, 1.04f, 0.76f, 1.0f);
		}
		else if (CurrentAspect == EMegaJamAspectMode::Wide_21_9 && EffectiveGenre == EMegaJamSceneGenre::Alpine)
		{
			// Cryo-Avalanche post-process grading for Alpine Director's Cut
			Camera->PostProcessBlendWeight = 1.0f;
			Camera->PostProcessSettings.bOverride_ColorSaturation = true;
			Camera->PostProcessSettings.ColorSaturation = FVector4(0.78f, 0.92f, 1.15f, 1.0f);
			Camera->PostProcessSettings.bOverride_ColorContrast = false;
			Camera->PostProcessSettings.bOverride_ColorGain = true;
			Camera->PostProcessSettings.ColorGain = FVector4(0.88f, 1.04f, 1.24f, 1.0f);
		}
		else
		{
			Camera->PostProcessSettings.bOverride_ColorSaturation = false;
			Camera->PostProcessSettings.bOverride_ColorContrast = false;
			Camera->PostProcessSettings.bOverride_ColorGain = false;
		}
	}

	PushTakeAndAspectToSubsystem();
	OnAspectChanged.Broadcast(CurrentAspect);
	OnAspectChangedEvent(CurrentAspect);
}

void AMegaJamFlowPawn::TriggerClapperboardTransition(EMegaJamSceneGenre NextGenre)
{
	const EMegaJamSceneGenre OutgoingGenre = ActiveMusicGenre;

	// Instantly refill FilmMeter so the player drops into the new scene fully fueled
	FilmMeter = 100.0f;
	ActiveMusicGenre = NextGenre;
	CurrentShotDuration = 0.0f;
	AspectHoldTimer = 0.0f;
	if (bIsShotStale)
	{
		bIsShotStale = false;
		BP_OnStaleShotStateChanged(false);
	}

	if (UWorld* World = GetWorld())
	{
		const AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
		const float CurrentRunTime = GameMode ? GameMode->GetRunTime() : static_cast<float>(World->GetTimeSeconds());
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->FinalizeScenePerformance(OutgoingGenre, CurrentRunTime);
			FlowSubsystem->SetActiveGenre(NextGenre);
			FlowSubsystem->SetFilmMeter(FilmMeter);
		}
	}

	if (NextGenre == EMegaJamSceneGenre::Montage)
	{
		// 7,200m Snowboard Kick-Off into Hang Glider: detach the Snowboard mesh and let it tumble downward for 2.0s
		if (DroppedBoardMesh)
		{
			UStaticMesh* BoardMesh = nullptr;
			if (const TObjectPtr<UStaticMesh>* FoundChaseMesh = SceneVehicleMeshes.Find(EMegaJamSceneGenre::Chase))
			{
				BoardMesh = FoundChaseMesh->Get();
			}
			if (!BoardMesh && VehicleStaticMesh)
			{
				BoardMesh = VehicleStaticMesh->GetStaticMesh();
			}
			if (BoardMesh)
			{
				DroppedBoardMesh->SetStaticMesh(BoardMesh);
				DroppedBoardMesh->SetWorldScale3D(SanitizePawnScale3D(VehicleStaticMesh ? VehicleStaticMesh->GetComponentScale() : FVector(1.0f)));
			}
			else if (ProceduralSlashArcMesh && ProceduralSlashArcMesh->GetStaticMesh())
			{
				DroppedBoardMesh->SetStaticMesh(ProceduralSlashArcMesh->GetStaticMesh());
				DroppedBoardMesh->SetWorldScale3D(SanitizePawnScale3D(FVector(1.6f, 0.45f, 0.08f)));
			}

			DroppedBoardWorldLocation = GetActorLocation() + FVector(0.0f, 0.0f, -20.0f);
			DroppedBoardWorldRotation = VehicleStaticMesh ? VehicleStaticMesh->GetComponentRotation() : GetActorRotation();
			DroppedBoardVelocity = FVector(static_cast<float>(Velocity.X) * 0.45f, static_cast<float>(Velocity.Y) * 0.5f, -240.0f);
			DroppedBoardTimer = 2.0f;
			DroppedBoardMesh->SetWorldLocationAndRotation(DroppedBoardWorldLocation, DroppedBoardWorldRotation);
			DroppedBoardMesh->SetVisibility(true);
		}

		// Scene 05 The Final Cut: Launch off the cliff into the Hang Glider in smooth 4:3 flight without forcing aspect/altitude locks
		ActiveMontageSubGenre = EMegaJamSceneGenre::Action;
		LastAutoMontageSegmentIndex = 0;
		bHasProcessedLaneInput = false;
		ActiveDollyBoostBonus = FMath::Max(ActiveDollyBoostBonus, 3200.0f);
		SetAspectMode(EMegaJamAspectMode::Box_4_3);
	}
	else if (NextGenre == EMegaJamSceneGenre::Western)
	{
		// Programmatically force Ultra-Panavision 2.76:1, lock Center Lane, and apply +2500 speed burst
		CurrentAspect = EMegaJamAspectMode::UltraPanavision_276_1;
		CurrentLane = 2;
		ActiveDollyBoostBonus = FMath::Max(ActiveDollyBoostBonus, 2500.0f);
		SetAspectMode(EMegaJamAspectMode::UltraPanavision_276_1);
	}
	else if (NextGenre == EMegaJamSceneGenre::Noir)
	{
		// Programmatically force 21:9 Director's Cut (Spy Car Vertigo Dolly-Zoom & 2x Adrenaline Graze) and +2600 speed burst
		CurrentAspect = EMegaJamAspectMode::Wide_21_9;
		const float EntryY = static_cast<float>(GetActorLocation().Y);
		CurrentLane = FMath::Clamp(FMath::RoundToInt((EntryY / FMath::Max(1.0f, LaneWidthY)) + 2.0f), 0, 4);
		bHasProcessedLaneInput = false;
		ActiveDollyBoostBonus = FMath::Max(ActiveDollyBoostBonus, 2600.0f);
		SetAspectMode(EMegaJamAspectMode::Wide_21_9);
	}
	else if (NextGenre == EMegaJamSceneGenre::Chase || NextGenre == EMegaJamSceneGenre::Alpine)
	{
		// Programmatically force 21:9 Director's Cut (Snowboard Downhill Carve & Comet Smash) and +3000 speed burst
		CurrentAspect = EMegaJamAspectMode::Wide_21_9;
		const float EntryY = static_cast<float>(GetActorLocation().Y);
		CurrentLane = FMath::Clamp(FMath::RoundToInt((EntryY / FMath::Max(1.0f, LaneWidthY)) + 2.0f), 0, 4);
		bHasProcessedLaneInput = false;
		ActiveDollyBoostBonus = FMath::Max(ActiveDollyBoostBonus, 3000.0f);
		SetAspectMode(EMegaJamAspectMode::Wide_21_9);
	}
	else if (CurrentAspect != EMegaJamAspectMode::Box_4_3)
	{
		SetAspectMode(EMegaJamAspectMode::Box_4_3);
	}

	// Vertical launch impulse into the air + mid-air hang (SnowboardDropInVelocityZ for 5,400m Scene 04 Mountain Drop-In, 1900.0f for 7,200m Cliff Drop into Hang Glider)
	Velocity.Z = (NextGenre == EMegaJamSceneGenre::Chase)
		? SnowboardDropInVelocityZ
		: ((NextGenre == EMegaJamSceneGenre::Montage) ? 1900.0f : 1800.0f);
	bIsGrounded = false;
	TriggerMicroHitstop(0.25f, 0.0f);
	OnClapperboardSnapped.Broadcast();
	PlayRunStartSound();
	SyncSceneMusicTrack(NextGenre);
}

void AMegaJamFlowPawn::HaltForWrapUpScoreboard()
{
	Velocity.X = 0.0f;
	Velocity.Y = 0.0f;
	Velocity.Z = 0.0f;
	CustomTimeDilation = 0.05f;
	if (CurrentAspect == EMegaJamAspectMode::Box_4_3)
	{
		SetAspectMode(EMegaJamAspectMode::Wide_21_9);
	}
}

void AMegaJamFlowPawn::ResetForRun()
{
	bCrashed = false;
	bIsGrounded = true;
	bHasProcessedJumpInput = false;
	CustomTimeDilation = 1.0f;
	Velocity = FVector(FMath::Max(1200.0f, CruiseSpeed), 0.0f, 0.0f);
	FlowRatio = 0.5f;
	FilmMeter = 50.0f;
	SteerInput = FVector2D::ZeroVector;
	CurrentLane = 2;
	bHasProcessedLaneInput = false;
	ActiveMontageSubGenre = EMegaJamSceneGenre::Action;
	LastAutoMontageSegmentIndex = -1;
	CurrentFOVPunch = 0.0f;
	SlamPitchPunch = 0.0f;
	CameraTrauma = 0.0f;
	EmissiveBurstTimer = 0.0f;
	if (HitstopRealTimeRemaining > 0.0f)
	{
		HitstopRealTimeRemaining = 0.0f;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}
	CurrentCameraRoll = 0.0f;
	ActiveSlashRoll = 0.0f;
	ActiveTrickSpinTimer = 0.0f;
	DroppedBoardTimer = 0.0f;
	if (DroppedBoardMesh)
	{
		DroppedBoardMesh->SetWorldScale3D(FVector::OneVector);
		DroppedBoardMesh->SetVisibility(false);
	}
	CometSmashTimer = 0.0f;
	bSpliceRingIsHorizontal = false;
	SpliceRingTimer = 0.0f;
	SlashArcTimer = 0.0f;
	if (ProceduralSpliceRingMesh)
	{
		ProceduralSpliceRingMesh->SetRelativeScale3D(FVector::OneVector);
		ProceduralSpliceRingMesh->SetVisibility(false);
	}
	if (ProceduralSlashArcMesh)
	{
		ProceduralSlashArcMesh->SetRelativeScale3D(FVector::OneVector);
		ProceduralSlashArcMesh->SetVisibility(false);
	}
	SpliceChain = 1;
	CurrentShotDuration = 0.0f;
	AspectHoldTimer = 0.0f;
	if (bIsShotStale)
	{
		bIsShotStale = false;
		BP_OnStaleShotStateChanged(false);
	}
	AirExposureTimer = 0.0f;
	bIsOverexposed = false;
	bIsDropCutSlamming = false;
	StaleDecayAccum = 0.0f;
	OverexposureDecayAccum = 0.0f;
	MatchCutFlashTimer = 0.0f;
	DropCutFlashTimer = 0.0f;
	DollyBoostFlashTimer = 0.0f;
	FlowBonusOffset = 0.0f;
	ActiveDollyBoostBonus = 0.0f;
	LastCutTime = -1000.0;
	LastEditorSpliceTime = -1000.0;
	LastGrazeOrPickupTime = -1000.0;
	GrazeTimes.Reset();

	FVector ResetLoc = SpawnTransform.GetLocation();
	if (ResetLoc.ContainsNaN())
	{
		ResetLoc = FVector(0.0, 0.0, 2.0);
	}
	ResetLoc.Z = FMath::Max(2.0, WideZBounds.X);
	const FVector ResetLookAhead = ResetLoc + FVector(10.0f, 0.0f, 0.0f);
	const FVector SafeResetDir = (ResetLookAhead - ResetLoc).GetSafeNormal(1e-4f, FVector::ForwardVector);
	SpawnTransform.SetLocation(ResetLoc);
	SpawnTransform.SetRotation(SafeResetDir.Rotation().Quaternion());
	SpawnTransform.SetScale3D(FVector::OneVector);
	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Default starting mode: 4:3 Academy Frame (Flight & Charge Mode) with 50% Film Meter
	CurrentTake = EMegaJamTakeState::StudioTake;
	SetAspectMode(EMegaJamAspectMode::Box_4_3);
	ApplyCameraPOV();
	if (Camera)
	{
		Camera->SetFieldOfView(90.0f);
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
		Camera->SetRelativeLocation(FVector::ZeroVector);
	}
	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	OnTakeCut.Broadcast(CurrentTake);
	OnTakeCutEvent(CurrentTake);
	OnAspectChanged.Broadcast(CurrentAspect);
	OnAspectChangedEvent(CurrentAspect);

	if (UWorld* World = GetWorld())
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetFlowRatio(0.0f);
			FlowSubsystem->SetFilmMeter(FilmMeter);
			FlowSubsystem->SetHazardTimeScale(1.0f);
			SyncSceneMusicTrack(FlowSubsystem->ActiveGenre);
			FlowSubsystem->SeekMusicToTime(0.0f);
		}
		else
		{
			SyncSceneMusicTrack(EMegaJamSceneGenre::Action);
		}
	}
}

void AMegaJamFlowPawn::PushTakeAndAspectToSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetActiveTake(CurrentTake == EMegaJamTakeState::DirectorsTake ? 1.0f : 0.0f);
			FlowSubsystem->SetAspectMode(CurrentAspect == EMegaJamAspectMode::Box_4_3 ? 1.0f : 0.0f);
			FlowSubsystem->SetSpliceChain(SpliceChain);
			FlowSubsystem->SetAspectHoldTimer(AspectHoldTimer);
			FlowSubsystem->SetIsShotStale(false);
			FlowSubsystem->SetAirExposureRatio(0.0f);
			FlowSubsystem->SetIsOverexposed(false);
			FlowSubsystem->SetFilmMeter(FilmMeter);
		}
	}
}

bool AMegaJamFlowPawn::TryTriggerOnBeatCut()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return false;
	}

	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	if (!FlowSubsystem || !FlowSubsystem->IsWithinBeatWindow(0.16f))
	{
		return false;
	}

	// ★ ON-BEAT CUT! (+15 Film Meter, +1 Chain up to x8, camera trauma, HUD flash, audio sting)
	FilmMeter = FMath::Clamp(FilmMeter + 15.0f, 0.0f, 100.0f);
	SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
	EmissiveBurstTimer = 0.25f;
	AddCameraTrauma(0.11f, 0.22f);

	FlowSubsystem->SetOnBeatFlashTimer(1.1f);
	FlowSubsystem->SetFilmMeter(FilmMeter);
	FlowSubsystem->SetSpliceChain(SpliceChain);

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		const float OverdriveMult = IsInBulletTimeOverdrive() ? 2.0f : 1.0f;
		GameMode->AddBonusScore(200.0f * static_cast<float>(SpliceChain) * OverdriveMult, true);
		GameMode->NotifySpliceChainUpdated(SpliceChain);
	}

	USoundBase* BeatSound = FilmFramePickupSound ? FilmFramePickupSound.Get() : BeatSpliceSound.Get();
	const float ChainPitch = FMath::Clamp(1.18f + 0.04f * static_cast<float>(SpliceChain), 1.18f, 1.46f);
	PlayCue(TEXT("OnBeatCut"), BeatSound, 0.95f, ChainPitch, true);

	TriggerAudioEvent(TEXT("OnHardCut"));
	return true;
}

bool AMegaJamFlowPawn::TryTriggerMatchCut(bool bForceMatchCut)
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	if (!bForceMatchCut && (Now - LastGrazeOrPickupTime > MatchCutWindow))
	{
		return false;
	}

	// Consume the window so a single graze cannot be farmed repeatedly.
	LastGrazeOrPickupTime = -1000.0;

	SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
	MatchCutFlashTimer = 1.1f;
	EmissiveBurstTimer = 0.25f;
	FilmMeter = FMath::Clamp(FilmMeter + 10.0f, 0.0f, 100.0f);

	// Refill Flow and forward momentum on a successful Match Cut.
	Velocity.X = FMath::Max(static_cast<float>(Velocity.X), IsInBulletTimeOverdrive() ? OverdriveSpeed : MaxFlightSpeed * 0.95f);
	FlowBonusOffset = FMath::Max(0.0f, FlowBonusOffset);
	FlowRatio = 1.0f;

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		const float OverdriveMult = IsInBulletTimeOverdrive() ? 2.0f : 1.0f;
		GameMode->AddBonusScore(MatchCutBonusBase * static_cast<float>(SpliceChain) * OverdriveMult, true);
		GameMode->NotifySpliceChainUpdated(SpliceChain);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
		FlowSubsystem->SetSpliceChain(SpliceChain);
		FlowSubsystem->SetFilmMeter(FilmMeter);
	}

	TriggerAudioEvent(TEXT("OnGraze"));
	return true;
}

void AMegaJamFlowPawn::NotifyFilmFrameCollected()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return;
	}

	LastGrazeOrPickupTime = World->GetTimeSeconds();
	FilmMeter = FMath::Clamp(FilmMeter + 14.0f, 0.0f, 100.0f);
	SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
	MatchCutFlashTimer = 0.9f;
	EmissiveBurstTimer = 0.25f;
	AddCameraTrauma(0.055f, 0.16f);

	const float MaxCap = IsInBulletTimeOverdrive() ? OverdriveSpeed : MaxFlightSpeed;
	Velocity.X = FMath::Min(static_cast<float>(Velocity.X) + CutSpeedImpulse * 0.75f, MaxCap);
	FlowBonusOffset = FMath::Clamp(FlowBonusOffset + 0.12f, 0.0f, 0.35f);
	FlowRatio = TerminalVelocity > KINDA_SMALL_NUMBER
		? FMath::Clamp(Velocity.Size() / TerminalVelocity + FlowBonusOffset, 0.0f, 1.0f)
		: 0.0f;

	// Check dynamic BPM On-Beat Cut window (±0.16s) when scooping a Film Frame
	const bool bTriggeredOnBeat = TryTriggerOnBeatCut();

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		const float OverdriveMult = IsInBulletTimeOverdrive() ? 2.0f : 1.0f;
		GameMode->AddBonusScore(MatchCutBonusBase * static_cast<float>(SpliceChain) * OverdriveMult, true);
		GameMode->RegisterFilmFrameCollected(SpliceChain);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->RecordFramePickup(FlowSubsystem->ActiveGenre);
		FlowSubsystem->SetFlowRatio(FlowRatio);
		FlowSubsystem->SetSpliceChain(SpliceChain);
		FlowSubsystem->SetFilmMeter(FilmMeter);
	}

	if (!bTriggeredOnBeat)
	{
		PlayCue(TEXT("OnFilmFrame"), FilmFramePickupSound);
	}
	TriggerAudioEvent(TEXT("OnGraze"));
}

void AMegaJamFlowPawn::NotifyDollyBoostStripCollected()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return;
	}

	LastGrazeOrPickupTime = World->GetTimeSeconds();
	FilmMeter = FMath::Clamp(FilmMeter + 10.0f, 0.0f, 100.0f);
	DollyBoostFlashTimer = 1.0f;
	AirExposureTimer = 0.0f;
	bIsOverexposed = false;
	AddCameraTrauma(0.14f, 0.30f);

	Velocity.X = FMath::Min(static_cast<float>(Velocity.X) + 550.0f, OverdriveSpeed + 250.0f);
	FlowBonusOffset = FMath::Clamp(FlowBonusOffset + 0.15f, 0.0f, 0.35f);
	FlowRatio = FMath::Clamp(FlowRatio + 0.15f, 0.0f, 1.0f);

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		const float OverdriveMult = IsInBulletTimeOverdrive() ? 2.0f : 1.0f;
		GameMode->AddBonusScore(100.0f * static_cast<float>(SpliceChain) * OverdriveMult, true);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
		FlowSubsystem->SetFilmMeter(FilmMeter);
	}

	// Score path: OnDollyBoost plays both layers (natural + the +1.6x surge layer TriggerDollyBoost adds in the C++ path).
	PlayCue(TEXT("OnDollyBoost"), DollyBoostStripSound);
	TriggerAudioEvent(TEXT("OnGraze"));
}

void AMegaJamFlowPawn::TriggerDollyBoost()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return;
	}

	ActiveDollyBoostBonus = 3500.0f;
	FilmMeter = FMath::Clamp(FilmMeter + 12.0f, 0.0f, 100.0f);
	BP_PlayDollyBoostVFX();

	if (Camera)
	{
		Camera->SetFieldOfView(Camera->FieldOfView + 25.0f);
	}

	AddCameraTrauma(0.4f, 0.6f);

	// High surge layer on top of the strip pickup sound. Fallback path only: in the score, OnDollyBoost already includes it.
	if (!IsScoreDrivingAudio() && DollyBoostStripSound)
	{
		UGameplayStatics::PlaySound2D(this, DollyBoostStripSound, 1.0f, 1.6f);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFilmMeter(FilmMeter);
	}
}

void AMegaJamFlowPawn::PlayOneShot(USoundBase* Sound) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

void AMegaJamFlowPawn::PlayRunStartSound() const
{
	PlayCue(TEXT("OnRunStart"), RunStartSound);
}

bool AMegaJamFlowPawn::IsScoreDrivingAudio() const
{
	return ScoreMetaSound && ScoreAudioComponent && ScoreAudioComponent->IsPlaying();
}

void AMegaJamFlowPawn::PlayCue(FName ScoreTrigger, USoundBase* FallbackSound, float Volume, float Pitch, bool bChainPitched) const
{
	if (IsScoreDrivingAudio())
	{
		if (bChainPitched)
		{
			// Pitch multiplier -> semitones for the score's Wave Player "Pitch Shift" (set right before the trigger, same frame).
			ScoreAudioComponent->SetFloatParameter(TEXT("ChainPitchSemitones"), 12.0f * FMath::Log2(FMath::Max(Pitch, 0.01f)));
		}
		ScoreAudioComponent->SetTriggerParameter(ScoreTrigger);
		return;
	}

	if (FallbackSound)
	{
		UGameplayStatics::PlaySound2D(this, FallbackSound, Volume, Pitch);
	}
}

void AMegaJamFlowPawn::PushScoreWaveParameters() const
{
	if (!ScoreAudioComponent)
	{
		return;
	}

	// MetaSound WaveAsset inputs take USoundWave. A non-wave override (e.g. a SoundCue) leaves that score cue silent.
	auto PushWave = [this](const TCHAR* InputName, USoundBase* Sound)
	{
		ScoreAudioComponent->SetWaveParameter(InputName, Cast<USoundWave>(Sound));
	};

	PushWave(TEXT("RunStartWave"), RunStartSound);
	PushWave(TEXT("FilmFrameWave"), FilmFramePickupSound);
	PushWave(TEXT("DollyBoostWave"), DollyBoostStripSound);
	PushWave(TEXT("CrashWave"), CrashSound);
	PushWave(TEXT("DirectorsCutEnterWave"), DirectorsCutEnterSound);
	PushWave(TEXT("DirectorsCutExitWave"), DirectorsCutExitSound);
	PushWave(TEXT("BeatSpliceWave"), BeatSpliceSound);
	PushWave(TEXT("BeatCutWave"), FilmFramePickupSound ? FilmFramePickupSound.Get() : BeatSpliceSound.Get());
	PushWave(TEXT("VictoryWave"), VictorySound);
}

bool AMegaJamFlowPawn::IsWithinBeatWindow(float Tolerance) const
{
	const float SafeBPM = FMath::Max(1.0f, CurrentTrackBPM);
	const float BeatInterval = 60.0f / SafeBPM;
	const UWorld* World = GetWorld();
	const UMegaJamFlowSubsystem* FlowSubsystem = World ? World->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr;
	const float CurrentAudioTime = FlowSubsystem
		? FlowSubsystem->CurrentAudioTime
		: (World ? static_cast<float>(World->GetTimeSeconds()) : 0.0f);
	const float Remainder = FMath::Fmod(FMath::Max(0.0f, CurrentAudioTime), BeatInterval);
	const float DistanceToNearestBeat = FMath::Min(Remainder, BeatInterval - Remainder);
	return DistanceToNearestBeat <= FMath::Max(0.0f, Tolerance);
}

void AMegaJamFlowPawn::SyncSceneMusicTrack(EMegaJamSceneGenre TargetGenre)
{
	const EMegaJamSceneGenre NewGenre = TargetGenre;
	ActiveMusicGenre = NewGenre;

	// Fetch and set CurrentTrackBPM from SceneAudioConfigs or SceneBPMs (fallback 120.0f)
	float ResolvedBPM = 120.0f;
	if (const FSceneAudioConfig* FoundCfg = SceneAudioConfigs.Find(TargetGenre))
	{
		if (FoundCfg->TrackBPM > 1.0f)
		{
			ResolvedBPM = FoundCfg->TrackBPM;
		}
	}
	if (const float* FoundBPM = SceneBPMs.Find(TargetGenre))
	{
		if (*FoundBPM > 1.0f && (!SceneAudioConfigs.Contains(TargetGenre) || FMath::IsNearlyEqual(ResolvedBPM, 120.0f)))
		{
			ResolvedBPM = *FoundBPM;
		}
	}
	// Strictly enforce 120 BPM for Scene 01 (Action) / Scene 05 (Montage) or if a stale 128 BPM value is serialized in BP_FlowPawn
	if (TargetGenre == EMegaJamSceneGenre::Action || TargetGenre == EMegaJamSceneGenre::Montage || FMath::IsNearlyEqual(ResolvedBPM, 128.0f, 0.5f))
	{
		ResolvedBPM = 120.0f;
		SceneBPMs.FindOrAdd(TargetGenre) = 120.0f;
		if (FSceneAudioConfig* AudioCfg = SceneAudioConfigs.Find(TargetGenre))
		{
			AudioCfg->TrackBPM = 120.0f;
		}
	}
	CurrentTrackBPM = FMath::Max(1.0f, ResolvedBPM);

	UMegaJamFlowSubsystem* FlowSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>() : nullptr;
	if (FlowSubsystem)
	{
		FlowSubsystem->SetCurrentTrackBPM(CurrentTrackBPM);
		FlowSubsystem->SetPrimaryAudioComponent(MusicAudioComponent);
	}

	// Swap thematic vehicle static mesh when mapped in SceneVehicleMeshes (Motorcycle -> Horse -> Snowboard -> Spy Car -> Stunt Car)
	UStaticMesh* NewMesh = nullptr;
	if (const TObjectPtr<UStaticMesh>* FoundVehicleMesh = SceneVehicleMeshes.Find(NewGenre))
	{
		NewMesh = FoundVehicleMesh->Get();
	}

	const FVector SeatOffset = SceneSeatOffsets.Contains(NewGenre) ? SceneSeatOffsets[NewGenre] : FVector(0.0f, 0.0f, 85.0f);
	if (VisualMesh)
	{
		VisualMesh->SetRelativeScale3D(FVector(0.55f));
		VisualMesh->SetVisibility(ActiveCameraPOV != EMegaJamCameraPOV::BumperFirstPerson);
		VisualMesh->SetRelativeLocation(SeatOffset);
	}

	if (NewMesh && VehicleStaticMesh)
	{
		const float BaseYawOffset = SceneVehicleYawOffsets.Contains(NewGenre) ? SceneVehicleYawOffsets[NewGenre] : 0.0f;
		VehicleStaticMesh->SetStaticMesh(NewMesh);
		VehicleStaticMesh->SetRelativeScale3D(SanitizePawnScale3D(VehicleStaticMesh->GetRelativeScale3D()));
		VehicleStaticMesh->SetRelativeRotation(FRotator(0.0f, BaseYawOffset, 0.0f));
		VehicleStaticMesh->SetVisibility(true);
	}
	else if (VehicleStaticMesh)
	{
		VehicleStaticMesh->SetRelativeScale3D(FVector::OneVector);
		VehicleStaticMesh->SetVisibility(false);
	}

	// Ensure camera post-process grading stays synced when genre changes via F7 debug skip or checkpoint rewind
	SetAspectMode(CurrentAspect);

	USoundBase* DesiredSound = nullptr;
	if (const FSceneAudioConfig* FoundCfg = SceneAudioConfigs.Find(TargetGenre))
	{
		if (FoundCfg->MusicTrack)
		{
			DesiredSound = FoundCfg->MusicTrack.Get();
		}
	}
	if (!DesiredSound)
	{
		if (const TObjectPtr<USoundBase>* FoundTrack = SceneMusicTracks.Find(TargetGenre))
		{
			DesiredSound = FoundTrack->Get();
		}
	}
	if (!DesiredSound)
	{
		DesiredSound = MusicTrack;
	}

	if (!DesiredSound)
	{
		return;
	}

	if (IsScoreDrivingAudio())
	{
		// Score path: the genre -> track mapping stays here in C++; the score just loops whatever MusicWave it is given.
		if (MusicAudioComponent && MusicAudioComponent->IsPlaying())
		{
			MusicAudioComponent->Stop();
		}
		ScoreAudioComponent->SetIntParameter(TEXT("SceneGenre"), static_cast<int32>(TargetGenre));
		if (ScoreActiveMusicWave != DesiredSound)
		{
			ScoreActiveMusicWave = DesiredSound;
			ScoreAudioComponent->SetWaveParameter(TEXT("MusicWave"), Cast<USoundWave>(DesiredSound));
			ScoreAudioComponent->SetTriggerParameter(TEXT("OnSceneChanged"));
			if (FlowSubsystem)
			{
				FlowSubsystem->SeekMusicToTime(0.0f);
			}
		}
		return;
	}

	if (!MusicAudioComponent)
	{
		return;
	}

	if (MusicAudioComponent->Sound != DesiredSound || !MusicAudioComponent->IsPlaying())
	{
		MusicAudioComponent->SetSound(DesiredSound);
		MusicAudioComponent->SetPitchMultiplier(CurrentMusicPitch);
		MusicAudioComponent->SetVolumeMultiplier(CurrentMusicVolume);
		MusicAudioComponent->Play(0.0f);
		if (FlowSubsystem)
		{
			FlowSubsystem->SeekMusicToTime(0.0f);
		}
	}
}

void AMegaJamFlowPawn::UpdateDynamicMusicModulation(float UnscaledDelta)
{
	const bool bScoreDriving = IsScoreDrivingAudio();
	if (!MusicAudioComponent && !bScoreDriving)
	{
		return;
	}

	UWorld* World = GetWorld();
	const AMegaJamGameMode* GameMode = World ? World->GetAuthGameMode<AMegaJamGameMode>() : nullptr;
	const EMegaJamRunState RunState = GameMode ? GameMode->GetRunState() : EMegaJamRunState::Playing;

	float TargetPitch = 1.0f;
	float TargetVolume = 0.90f;

	if (UGameplayStatics::IsGamePaused(this))
	{
		// Tactile reel-hold damping while PRODUCTION PAUSED // CUT is open
		TargetPitch = 0.76f;
		TargetVolume = 0.42f;
	}
	else if (RunState == EMegaJamRunState::TitleScreen)
	{
		TargetPitch = 0.96f;
		TargetVolume = 0.68f;
	}
	else if (CurrentAspect == EMegaJamAspectMode::Wide_21_9 || CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
	{
		if (ActiveMusicGenre == EMegaJamSceneGenre::Action)
		{
			// Bullet-Time Overdrive: distinct slow-mo tape drag without muddy distortion
			TargetPitch = 0.86f;
			TargetVolume = 1.05f;
		}
		else if (ActiveMusicGenre == EMegaJamSceneGenre::Chase)
		{
			// Fast-Forward Smash
			TargetPitch = 1.12f;
			TargetVolume = 1.05f;
		}
		else
		{
			// Western / Noir / Montage widescreen states
			TargetPitch = 0.94f;
			TargetVolume = 1.0f;
		}
	}
	else
	{
		// 4:3 Normal Flight with subtle chain escalation
		TargetPitch = 1.0f + FMath::Clamp(static_cast<float>(SpliceChain - 1) * 0.005f, 0.0f, 0.035f);
		TargetVolume = 0.90f;
	}

	if (!FMath::IsFinite(CurrentMusicPitch))
	{
		CurrentMusicPitch = 1.0f;
	}
	if (!FMath::IsFinite(CurrentMusicVolume))
	{
		CurrentMusicVolume = 0.90f;
	}

	CurrentMusicPitch = FMath::FInterpTo(CurrentMusicPitch, TargetPitch, UnscaledDelta, 8.0f);
	CurrentMusicVolume = FMath::FInterpTo(CurrentMusicVolume, TargetVolume, UnscaledDelta, 8.0f);

	if (bScoreDriving)
	{
		// Same modulation, delivered to the score: pitch multiplier -> semitones (Wave Player Pitch Shift), volume -> mixer gain.
		ScoreAudioComponent->SetFloatParameter(TEXT("MusicPitchSemitones"), 12.0f * FMath::Log2(FMath::Max(CurrentMusicPitch, 0.01f)));
		ScoreAudioComponent->SetFloatParameter(TEXT("MusicVolume"), CurrentMusicVolume);
		return;
	}

	MusicAudioComponent->SetPitchMultiplier(CurrentMusicPitch);
	MusicAudioComponent->SetVolumeMultiplier(CurrentMusicVolume);
}

void AMegaJamFlowPawn::OnMusicFinished()
{
	// Loop whichever scene track is currently assigned on MusicAudioComponent.
	// Not while the score owns music: SyncSceneMusicTrack's Stop() also lands here and must not restart the C++ track.
	if (MusicAudioComponent && MusicAudioComponent->Sound && !IsScoreDrivingAudio())
	{
		MusicAudioComponent->Play();
	}
}

void AMegaJamFlowPawn::AddCameraTrauma(float Amount, float MaxCap)
{
	const float ClampedCap = FMath::Clamp(MaxCap, 0.0f, 1.0f);
	if (CameraTrauma < ClampedCap)
	{
		CameraTrauma = FMath::Min(ClampedCap, CameraTrauma + FMath::Max(0.0f, Amount));
	}
}

void AMegaJamFlowPawn::TriggerMicroHitstop(float RealDurationSeconds, float Dilation)
{
	HitstopRealTimeRemaining = FMath::Max(HitstopRealTimeRemaining, RealDurationSeconds);
	UGameplayStatics::SetGlobalTimeDilation(this, FMath::Clamp(Dilation, 0.005f, 1.0f));
}

void AMegaJamFlowPawn::UpdatePawnDynamicMaterial(float DeltaSeconds)
{
	if (!PawnVisualMID)
	{
		return;
	}

	const float SpeedRatio = (TerminalVelocity > KINDA_SMALL_NUMBER && !bCrashed)
		? FMath::Clamp(static_cast<float>(Velocity.X) / TerminalVelocity, 0.0f, 1.0f)
		: 0.0f;
	const float RealTime = static_cast<float>(FPlatformTime::Seconds());
	const float BeatPulse = 1.0f + SpeedRatio * (0.35f + 0.35f * FMath::Sin(RealTime * (8.0f + SpeedRatio * 10.0f)));

	// Smooth chain color shift: Cool Cyan (x1-x3) -> Warm Gold (x4-x6) -> Electric Magenta-White (x7-x8 MAX CHAIN)
	const FLinearColor CyanTint(0.15f, 0.92f, 1.0f, 1.0f);
	const FLinearColor GoldTint(1.0f, 0.82f, 0.15f, 1.0f);
	const FLinearColor MagentaWhiteTint(1.0f, 0.42f, 0.98f, 1.0f);

	FLinearColor ChainColor = CyanTint;
	if (SpliceChain <= 3)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(SpliceChain - 1) / 2.0f, 0.0f, 1.0f);
		ChainColor = FMath::Lerp(CyanTint, FLinearColor(0.35f, 0.98f, 0.90f, 1.0f), Alpha);
	}
	else if (SpliceChain <= 6)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(SpliceChain - 3) / 3.0f, 0.0f, 1.0f);
		ChainColor = FMath::Lerp(CyanTint, GoldTint, Alpha);
	}
	else
	{
		const float Alpha = FMath::Clamp(static_cast<float>(SpliceChain - 6) / 2.0f, 0.0f, 1.0f);
		ChainColor = FMath::Lerp(GoldTint, MagentaWhiteTint, Alpha);
	}

	const float BurstAlpha = FMath::Clamp(EmissiveBurstTimer / 0.25f, 0.0f, 1.0f);
	const FLinearColor FinalTint = FMath::Lerp(ChainColor, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), BurstAlpha);
	const float EmissiveMultiplier = FMath::Lerp(1.2f + FlowRatio * 2.2f, 6.0f, BurstAlpha) * BeatPulse;
	const FLinearColor EmissiveVec = FinalTint * EmissiveMultiplier;

	PawnVisualMID->SetVectorParameterValue(TEXT("Color"), FinalTint);
	PawnVisualMID->SetVectorParameterValue(TEXT("BaseColor"), FinalTint);
	PawnVisualMID->SetVectorParameterValue(TEXT("TakeColor"), EmissiveVec);
	PawnVisualMID->SetVectorParameterValue(TEXT("EmissiveColor"), EmissiveVec);
}

void AMegaJamFlowPawn::PushAudioParameters() const
{
	if (!ScoreAudioComponent || !ScoreAudioComponent->IsPlaying())
	{
		return;
	}

	const float ActiveTakeValue = (CurrentTake == EMegaJamTakeState::DirectorsTake) ? 1.0f : 0.0f;
	const float SpeedRatio = (TerminalVelocity > KINDA_SMALL_NUMBER && !bCrashed)
		? FMath::Clamp(static_cast<float>(Velocity.X) / TerminalVelocity, 0.0f, 1.0f)
		: 0.0f;

	ScoreAudioComponent->SetFloatParameter(TEXT("ActiveTake"), ActiveTakeValue);
	ScoreAudioComponent->SetFloatParameter(TEXT("FlowRatio"), FlowRatio);
	ScoreAudioComponent->SetFloatParameter(TEXT("SpeedRatio"), SpeedRatio);
}

void AMegaJamFlowPawn::TriggerAudioEvent(FName ParameterName) const
{
	if (ScoreAudioComponent && ScoreAudioComponent->IsPlaying())
	{
		ScoreAudioComponent->SetTriggerParameter(ParameterName);
	}
}

void AMegaJamFlowPawn::HandleCrash(AActor* HitActor)
{
	if (bCrashed || CometSmashTimer > 0.0f)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (bGodModeEnabled)
	{
		if (UWorld* World = GetWorld())
		{
			const double Now = World->GetTimeSeconds();
			if (Now - LastGodModeHitTime >= 0.35)
			{
				LastGodModeHitTime = Now;
				++GodModeSimulatedHits;
				TriggerAudioEvent(TEXT("OnCrashed"));
			}
		}
		Velocity.X = FMath::Max(static_cast<float>(Velocity.X), TerminalVelocity * 0.65f);
		return;
	}
#endif

	bCrashed = true;
	ActiveDollyBoostBonus = 0.0f;
	ActiveMontageSubGenre = EMegaJamSceneGenre::Action;
	LastAutoMontageSegmentIndex = -1;
	if (HitstopRealTimeRemaining > 0.0f)
	{
		HitstopRealTimeRemaining = 0.0f;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}
	AddCameraTrauma(1.0f);
	Velocity = FVector::ZeroVector;
	FlowRatio = 0.0f;
	SpliceChain = 1;
	SetAspectMode(EMegaJamAspectMode::Box_4_3);
	AspectHoldTimer = 0.0f;
	bIsShotStale = false;
	AirExposureTimer = 0.0f;
	bIsOverexposed = false;
	bIsDropCutSlamming = false;
	SlamPitchPunch = 0.0f;
	ActiveSlashRoll = 0.0f;
	ActiveTrickSpinTimer = 0.0f;
	DroppedBoardTimer = 0.0f;
	if (DroppedBoardMesh)
	{
		DroppedBoardMesh->SetWorldScale3D(FVector::OneVector);
		DroppedBoardMesh->SetVisibility(false);
	}
	CometSmashTimer = 0.0f;
	bSpliceRingIsHorizontal = false;
	SpliceRingTimer = 0.0f;
	SlashArcTimer = 0.0f;
	if (ProceduralSpliceRingMesh)
	{
		ProceduralSpliceRingMesh->SetRelativeScale3D(FVector::OneVector);
		ProceduralSpliceRingMesh->SetVisibility(false);
	}
	if (ProceduralSlashArcMesh)
	{
		ProceduralSlashArcMesh->SetRelativeScale3D(FVector::OneVector);
		ProceduralSlashArcMesh->SetVisibility(false);
	}
	StaleDecayAccum = 0.0f;
	OverexposureDecayAccum = 0.0f;
	MatchCutFlashTimer = 0.0f;
	DropCutFlashTimer = 0.0f;
	DollyBoostFlashTimer = 0.0f;
	FlowBonusOffset = 0.0f;
	ActiveDollyBoostBonus = 0.0f;
	LastGrazeOrPickupTime = -1000.0;
	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	// The score plays its crash cue from the existing OnCrashed trigger, so only the fallback one-shot is gated here.
	if (!IsScoreDrivingAudio())
	{
		PlayOneShot(CrashSound);
	}
	TriggerAudioEvent(TEXT("OnCrashed"));

	if (UWorld* World = GetWorld())
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetFlowRatio(0.0f);
			FlowSubsystem->SetHazardTimeScale(1.0f);
		}
		if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
		{
			GameMode->NotifyCrash(HitActor);
		}
	}

	OnCrashed(HitActor);
}

void AMegaJamFlowPawn::Input_Steer(const FInputActionValue& Value)
{
	SteerInput = Value.Get<FVector2D>();

	if (CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
	{
		// Ultra-Panavision 2.76:1 locks strictly to Center Lane (CurrentLane = 2), ignoring lateral input
		CurrentLane = 2;
		bHasProcessedLaneInput = false;
		return;
	}

	if (CurrentAspect == EMegaJamAspectMode::Wide_21_9)
	{
		const float AxisValue = static_cast<float>(SteerInput.X);
		if (AxisValue > 0.5f && !bHasProcessedLaneInput)
		{
			const int32 NewLane = FMath::Min(4, CurrentLane + 1);
			bHasProcessedLaneInput = true;
			if (NewLane != CurrentLane)
			{
				CurrentLane = NewLane;
				TriggerAudioEvent(TEXT("OnGraze"));
			}
		}
		else if (AxisValue < -0.5f && !bHasProcessedLaneInput)
		{
			const int32 NewLane = FMath::Max(0, CurrentLane - 1);
			bHasProcessedLaneInput = true;
			if (NewLane != CurrentLane)
			{
				CurrentLane = NewLane;
				TriggerAudioEvent(TEXT("OnGraze"));
			}
		}
		else if (FMath::Abs(AxisValue) < 0.2f)
		{
			bHasProcessedLaneInput = false;
		}
	}
}

void AMegaJamFlowPawn::Input_Boost(const FInputActionValue& Value)
{
	if (AMegaJamGameMode* GameMode = GetWorld()->GetAuthGameMode<AMegaJamGameMode>())
	{
		const EMegaJamRunState State = GameMode->GetRunState();
		if (State == EMegaJamRunState::TitleScreen)
		{
			GameMode->StartProductionFromTitle();
			return;
		}
		if (State == EMegaJamRunState::Completed)
		{
			GameMode->RestartFullProduction();
			return;
		}
		if (State == EMegaJamRunState::Crashed)
		{
			GameMode->ResetRun();
			return;
		}
	}

	TriggerHardCut();
}

void AMegaJamFlowPawn::Input_Brake(const FInputActionValue& Value)
{
	if (bCrashed)
	{
		return;
	}

	if (const AMegaJamGameMode* GameMode = GetWorld()->GetAuthGameMode<AMegaJamGameMode>())
	{
		if (!GameMode->IsPlaying())
		{
			return;
		}
	}

	ToggleAspect();
}

void AMegaJamFlowPawn::Input_SteerReleased()
{
	SteerInput = FVector2D::ZeroVector;
	bHasProcessedLaneInput = false;
	bHasProcessedJumpInput = false;
}

void AMegaJamFlowPawn::ToggleGodMode()
{
#if !UE_BUILD_SHIPPING
	bGodModeEnabled = !bGodModeEnabled;
	if (bGodModeEnabled)
	{
		GodModeSimulatedHits = 0;
		bCrashed = false;
	}
	if (GEngine)
	{
		const FString Msg = bGodModeEnabled
			? TEXT("DEV HOTKEY [F6]: GOD MODE ENABLED")
			: TEXT("DEV HOTKEY [F6]: GOD MODE DISABLED");
		GEngine->AddOnScreenDebugMessage(202602, 2.5f, FColor::Yellow, Msg, true, FVector2D(1.3f, 1.3f));
	}
#endif
}

void AMegaJamFlowPawn::ClearCrashStateForDebugSkip()
{
	bCrashed = false;
	bIsGrounded = true;
	bHasProcessedJumpInput = false;
	Velocity = FVector(TerminalVelocity * 0.7f, 0.0f, 0.0f);
	const FVector Loc = GetActorLocation();
	SetActorLocation(FVector(Loc.X, 0.0f, FMath::Max(2.0f, static_cast<float>(WideZBounds.X))), false, nullptr, ETeleportType::TeleportPhysics);
}

void AMegaJamFlowPawn::OnGrazeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bCrashed || !OtherActor || OtherActor == this)
	{
		return;
	}

	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	const TTuple<TWeakObjectPtr<UPrimitiveComponent>, int32> Key(OtherComp, OtherBodyIndex);

	if (const double* LastTime = GrazeTimes.Find(Key))
	{
		if (Now - *LastTime < GrazeRetriggerDelay)
		{
			return;
		}
	}

	// Drop expired entries so the map stays small.
	for (auto It = GrazeTimes.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() >= GrazeRetriggerDelay)
		{
			It.RemoveCurrent();
		}
	}
	GrazeTimes.Add(Key, Now);
	LastGrazeOrPickupTime = Now;

	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	const EMegaJamSceneGenre EffectiveGenre = GetEffectivePowerGenre();
	const bool bNoirVertigoGraze = (EffectiveGenre == EMegaJamSceneGenre::Noir && CurrentAspect == EMegaJamAspectMode::Wide_21_9);

	FilmMeter = FMath::Clamp(FilmMeter + (bNoirVertigoGraze ? 12.0f : 10.0f), 0.0f, 100.0f);
	AddCameraTrauma(bNoirVertigoGraze ? 0.20f : 0.14f, 0.35f);

	if (bNoirVertigoGraze)
	{
		// Pulse ProceduralSpliceRingMesh silver-white on 2x Adrenaline Graze
		SpliceRingTimer = 0.16f;
		bSpliceRingIsHorizontal = true;
		if (SpliceRingMID)
		{
			const FLinearColor SilverWhite(32.0f, 35.0f, 42.0f, 1.0f);
			SpliceRingMID->SetVectorParameterValue(TEXT("Color"), SilverWhite);
			SpliceRingMID->SetVectorParameterValue(TEXT("BaseColor"), SilverWhite);
			SpliceRingMID->SetVectorParameterValue(TEXT("TakeColor"), SilverWhite);
			SpliceRingMID->SetVectorParameterValue(TEXT("EmissiveColor"), SilverWhite);
		}
		if (ProceduralSpliceRingMesh)
		{
			ProceduralSpliceRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
			ProceduralSpliceRingMesh->SetRelativeRotation(FRotator::ZeroRotator);
			ProceduralSpliceRingMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 0.08f));
			ProceduralSpliceRingMesh->SetVisibility(true);
		}
	}

	if (FlowSubsystem)
	{
		FlowSubsystem->SetFilmMeter(FilmMeter);
	}

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		GameMode->RegisterGraze(OtherActor);
		if (IsInBulletTimeOverdrive())
		{
			GameMode->AddBonusScore(120.0f * static_cast<float>(SpliceChain), true);
		}
	}

	TriggerAudioEvent(TEXT("OnGraze"));
	OnObstacleGrazed(OtherActor);
}
