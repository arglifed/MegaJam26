// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamFlowPawn.h"

#include "MegaJamFlowSubsystem.h"
#include "MegaJamGameMode.h"
#include "MegaJamTrackGenerator.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogMegaJamPawn, Log, All);

AMegaJamFlowPawn::AMegaJamFlowPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Forgiving arcade hurtbox: 35 cm collision radius (~30% smaller than the 50 cm visual mesh radius).
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(35.0f);
	CollisionSphere->SetCollisionProfileName(TEXT("Pawn"));
	CollisionSphere->SetSimulatePhysics(false);
	RootComponent = CollisionSphere;

	GrazeSphere = CreateDefaultSubobject<USphereComponent>(TEXT("GrazeSphere"));
	GrazeSphere->SetupAttachment(CollisionSphere);
	GrazeSphere->InitSphereRadius(GrazeRadius);
	GrazeSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	GrazeSphere->SetGenerateOverlapEvents(true);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(CollisionSphere);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	VehicleSkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VehicleSkeletalMesh"));
	VehicleSkeletalMesh->SetupAttachment(CollisionSphere);
	VehicleSkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VehicleSkeletalMesh->SetVisibility(false);

	// Default visual: engine basic sphere (100 cm diameter, matches the 50 cm collision radius).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshFinder.Succeeded())
	{
		VisualMesh->SetStaticMesh(SphereMeshFinder.Object);
	}

	// Chase camera. The pawn never rotates, so the boom simply trails behind along -X and looks down slightly.
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(CollisionSphere);
	SpringArm->TargetArmLength = 600.0f;
	SpringArm->SetRelativeRotation(FRotator(-10.0f, 0.0f, 0.0f));
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 8.0f;
	SpringArm->ProbeSize = 12.0f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(BoxFOV);

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

	MusicTrack = MusicActionFinder.Object;
	SceneMusicTracks.Add(EMegaJamSceneGenre::Action, MusicActionFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Western, MusicWesternFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Noir, MusicNoirFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Chase, MusicChaseFinder.Object);
	SceneMusicTracks.Add(EMegaJamSceneGenre::Montage, MusicMontageFinder.Object);

	RunStartSound = RunStartFinder.Object;
	FilmFramePickupSound = FilmFrameFinder.Object;
	DollyBoostStripSound = DollyBoostFinder.Object;
	CrashSound = CrashFinder.Object;
	DirectorsCutEnterSound = DollyBoostFinder.Object;
	DirectorsCutExitSound = RunStartFinder.Object;
}

void AMegaJamFlowPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	GrazeSphere->SetSphereRadius(GrazeRadius);
}

void AMegaJamFlowPawn::BeginPlay()
{
	Super::BeginPlay();

	SpawnTransform = GetActorTransform();

	if (Camera)
	{
		Camera->SetFieldOfView(CurrentAspect == EMegaJamAspectMode::Wide_21_9 ? WideFOV : BoxFOV);
	}

	GrazeSphere->SetSphereRadius(GrazeRadius);
	GrazeSphere->OnComponentBeginOverlap.AddDynamic(this, &AMegaJamFlowPawn::OnGrazeBeginOverlap);

	if (ScoreAudioComponent && ScoreMetaSound)
	{
		ScoreAudioComponent->SetSound(ScoreMetaSound);
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
		PawnVisualMID = VisualMesh->CreateAndSetMaterialInstanceDynamic(0);
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
	if (!bIsDropCutSlamming)
	{
		SlamPitchPunch = FMath::FInterpTo(SlamPitchPunch, 0.0f, DeltaSeconds, 12.0f);
	}

	if (Camera)
	{
		float BaseFOV = BoxFOV;
		if (CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
		{
			BaseFOV = UltraPanavisionFOV;
		}
		else if (CurrentAspect == EMegaJamAspectMode::Wide_21_9)
		{
			BaseFOV = WideFOV;
		}
		const float SmoothedBaseFOV = FMath::FInterpTo(Camera->FieldOfView - CurrentFOVPunch, BaseFOV, DeltaSeconds, AspectFOVInterpSpeed);
		Camera->SetFieldOfView(SmoothedBaseFOV + CurrentFOVPunch);

		const float LateralAlpha = (TerminalVelocity > KINDA_SMALL_NUMBER && !bCrashed)
			? FMath::Clamp(static_cast<float>(Velocity.Y) / TerminalVelocity, -1.0f, 1.0f)
			: 0.0f;
		const float TargetRoll = LateralAlpha * MaxStrafeRoll;
		CurrentCameraRoll = FMath::FInterpTo(CurrentCameraRoll, TargetRoll, DeltaSeconds, CameraRollInterpSpeed);

		// Damped high-frequency sinusoidal Pitch, Yaw, Roll, and vertical Z offsets using Trauma^2 scaling
		const float Shake = CameraTrauma * CameraTrauma;
		const double RealTime = FPlatformTime::Seconds();
		const float ShakePitch = Shake * 3.8f * FMath::Sin(static_cast<float>(RealTime * 43.0));
		const float ShakeYaw   = Shake * 3.2f * FMath::Cos(static_cast<float>(RealTime * 37.0));
		const float ShakeRoll  = Shake * 5.0f * FMath::Sin(static_cast<float>(RealTime * 53.0));
		const float ShakeZ     = Shake * 18.0f * FMath::Sin(static_cast<float>(RealTime * 61.0));

		Camera->SetRelativeRotation(FRotator(SlamPitchPunch + ShakePitch, ShakeYaw, CurrentCameraRoll + ShakeRoll));
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
			const float NewZ = FMath::FInterpTo(static_cast<float>(Loc.Z), 130.0f, DeltaSeconds, 4.0f);
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
			}

			const float VictorySpeed = AttractCruiseSpeed * 1.25f;
			Velocity = FVector(VictorySpeed, 0.0f, 0.0f);
			FlowRatio = 1.0f;

			const FVector Loc = GetActorLocation();
			const float NewY = FMath::FInterpTo(static_cast<float>(Loc.Y), 0.0f, DeltaSeconds, 5.0f);
			const float NewZ = FMath::FInterpTo(static_cast<float>(Loc.Z), 160.0f, DeltaSeconds, 5.0f);
			SetActorLocation(FVector(Loc.X + VictorySpeed * DeltaSeconds, NewY, NewZ), false);

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
	bIsShotStale = (CurrentShotDuration > StaleShotThreshold);

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
			bIsShotStale = false;
			SetAspectMode(EMegaJamAspectMode::Box_4_3);
			if (FlowSubsystem)
			{
				FlowSubsystem->SetHazardTimeScale(1.0f);
			}
			if (DirectorsCutExitSound)
			{
				UGameplayStatics::PlaySound2D(this, DirectorsCutExitSound, 0.50f, 0.85f);
			}
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

	const float TargetOverdriveSpeed = bUltraPanavision
		? FMath::Max(CruiseSpeed * 1.30f, OverdriveSpeed)
		: OverdriveSpeed;

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
		Velocity.Z = 0.0f;
		// Bullet-Time Overdrive / Ultra-Panavision Standoff: surge and sustain TargetOverdriveSpeed + ActiveDollyBoostBonus
		Velocity.X = FMath::FInterpTo(static_cast<float>(Velocity.X), TargetOverdriveSpeed + ActiveDollyBoostBonus, DeltaSeconds, 9.0f);
	}
	else
	{
		Velocity.Y *= CrossPlaneDamp;
		Velocity.Y += SteerInput.X * LateralAccel * DeltaSeconds;
		Velocity.Z *= CrossPlaneDamp;
		Velocity.Z += SteerInput.Y * VerticalAccel * DeltaSeconds;

		// 4:3 Flight & Charge Mode: accelerate toward CruiseSpeed (3800 u/s) and clamp to MaxFlightSpeed + ActiveDollyBoostBonus
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
	Velocity = Velocity.GetClampedToMaxSize(MaxAllowedSpeed);

	// Swept move. A blocking hit is resolved deterministically.
	FHitResult Hit;
	AddActorWorldOffset(Velocity * DeltaSeconds, true, &Hit);

	if (Hit.bBlockingHit)
	{
		// Speed component driving into the surface.
		const float ClosingSpeed = -FVector::DotProduct(Velocity, Hit.ImpactNormal);
		if (ClosingSpeed >= CrashSpeedThreshold)
		{
			HandleCrash(Hit.GetActor());
			if (bCrashed)
			{
				return;
			}
			// In God Mode (bCrashed remains false), phase past the obstacle so the pawn never snags.
			AddActorWorldOffset(FVector(240.0f, 0.0f, 0.0f), false);
		}
		else
		{
			// Slow contact: slide along the surface.
			Velocity = FVector::VectorPlaneProject(Velocity, Hit.ImpactNormal);
		}
	}

	ApplyAspectClamp(DeltaSeconds);
}

void AMegaJamFlowPawn::ApplyAspectClamp(float DeltaSeconds)
{
	const bool bWide = (CurrentAspect == EMegaJamAspectMode::Wide_21_9 || CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1);
	const FVector2D YBounds = bWide ? WideYBounds : BoxYBounds;
	const FVector2D ZBounds = bWide ? WideZBounds : BoxZBounds;

	const FVector Location = GetActorLocation();
	const float ClampedY = FMath::Clamp(static_cast<float>(Location.Y), static_cast<float>(YBounds.X), static_cast<float>(YBounds.Y));
	const float ClampedZ = FMath::Clamp(static_cast<float>(Location.Z), static_cast<float>(ZBounds.X), static_cast<float>(ZBounds.Y));

	// Stop velocity from driving further out of the corridor.
	if ((Location.Y >= YBounds.Y && Velocity.Y > 0.0) || (Location.Y <= YBounds.X && Velocity.Y < 0.0))
	{
		Velocity.Y = 0.0;
	}
	if ((Location.Z >= ZBounds.Y && Velocity.Z > 0.0) || (Location.Z <= ZBounds.X && Velocity.Z < 0.0))
	{
		Velocity.Z = 0.0;
	}

	if (FMath::IsNearlyEqual(ClampedY, static_cast<float>(Location.Y)) && FMath::IsNearlyEqual(ClampedZ, static_cast<float>(Location.Z)))
	{
		return;
	}

	// Fast interpolation toward the clamped position: a punchy squeeze, not a one-frame teleport.
	// Bypass soft vertical smoothing while bIsDropCutSlamming is driving Z down at 3600 u/s.
	const float NewY = FMath::FInterpTo(static_cast<float>(Location.Y), ClampedY, DeltaSeconds, AspectClampInterpSpeed);
	const float NewZ = bIsDropCutSlamming
		? static_cast<float>(Location.Z)
		: FMath::FInterpTo(static_cast<float>(Location.Z), ClampedZ, DeltaSeconds, AspectClampInterpSpeed);
	AddActorWorldOffset(FVector(0.0f, NewY - Location.Y, NewZ - Location.Z), true);
}

void AMegaJamFlowPawn::ExecuteEditorSplice()
{
	if (CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
	{
		ExecuteWesternBeatSlash();
		return;
	}
	TriggerHardCut();
}

void AMegaJamFlowPawn::ExecuteWesternBeatSlash()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
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
	}

	Velocity += GetActorForwardVector() * CutSpeedImpulse;
	CurrentFOVPunch = CutFOVPunch;

	const bool bOnBeat = FlowSubsystem && FlowSubsystem->IsWithinBeatWindow(0.18f);
	const float PawnX = static_cast<float>(GetActorLocation().X);
	const float ForwardMaxX = PawnX + (bOnBeat ? 3000.0f : 1800.0f);
	const float HalfWidthY = bOnBeat ? 1200.0f : 360.0f;

	int32 CleavedCount = 0;
	if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
	{
		CleavedCount = TrackGen->CleaveObstaclesInRange(PawnX - 120.0f, ForwardMaxX, HalfWidthY);
	}

	if (bOnBeat)
	{
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
		if (DirectorsCutEnterSound)
		{
			UGameplayStatics::PlaySound2D(this, DirectorsCutEnterSound, 1.0f, 1.45f);
		}

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
	if (CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
	{
		ExecuteWesternBeatSlash();
		return true;
	}

	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return false;
	}

	AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && !GameMode->IsPlaying())
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

	UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();

	// When pressed in 4:3 mode (or active flight), toggle Take A / Take B state
	if (CurrentAspect == EMegaJamAspectMode::Box_4_3 || CurrentAspect == EMegaJamAspectMode::Wide_21_9)
	{
		CurrentTake = (CurrentTake == EMegaJamTakeState::StudioTake) ? EMegaJamTakeState::DirectorsTake : EMegaJamTakeState::StudioTake;
		if (FlowSubsystem)
		{
			FlowSubsystem->SetTakeAActive(CurrentTake == EMegaJamTakeState::StudioTake);
		}
	}

	// Brief forward impulse. StepPhysics re-applies the terminal velocity clamp.
	Velocity += GetActorForwardVector() * CutSpeedImpulse;
	CurrentFOVPunch = CutFOVPunch;

	// Beat-Splice Harvesting: if pressed within the 128 BPM beat window (0.18s), shred active-take obstacles ahead into Film Frames!
	int32 ShreddedCount = 0;
	if (FlowSubsystem && FlowSubsystem->IsWithinBeatWindow(0.18f))
	{
		if (AMegaJamTrackGenerator* TrackGen = Cast<AMegaJamTrackGenerator>(UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
		{
			ShreddedCount = TrackGen->SpliceObstaclesInFront(GetActorLocation(), 1200.0f, 600.0f);
		}
	}

	if (ShreddedCount > 0)
	{
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
		if (DirectorsCutEnterSound)
		{
			const float ChainPitch = FMath::Clamp(1.15f + 0.04f * static_cast<float>(SpliceChain), 1.15f, 1.45f);
			UGameplayStatics::PlaySound2D(this, DirectorsCutEnterSound, 1.0f, ChainPitch);
		}

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

		const EMegaJamSceneGenre ActiveGenre = FlowSubsystem ? FlowSubsystem->ActiveGenre : ActiveMusicGenre;
		const bool bIsWesternGenre = (ActiveGenre == EMegaJamSceneGenre::Western);

		if (bIsWesternGenre)
		{
			// Scene 02 Western Standoff: lock to Center Lane (CurrentLane = 2) and snap to Ultra-Panavision 2.76:1
			CurrentLane = 2;
			bHasProcessedLaneInput = false;
			SetAspectMode(EMegaJamAspectMode::UltraPanavision_276_1);
		}
		else
		{
			// Scene 01 Action: lock onto the nearest of the 5 magnetic lanes (-700, -350, 0, 350, 700) on entry
			const float EntryY = static_cast<float>(GetActorLocation().Y);
			CurrentLane = FMath::Clamp(FMath::RoundToInt((EntryY / FMath::Max(1.0f, LaneWidthY)) + 2.0f), 0, 4);
			bHasProcessedLaneInput = false;
			SetAspectMode(EMegaJamAspectMode::Wide_21_9);
		}

		CurrentShotDuration = 0.0f;
		AspectHoldTimer = 0.0f;
		bIsShotStale = false;
		bIsDropCutSlamming = false;

		// Activate Director's Cut: +30% speed surge (or OverdriveSpeed), 0.45x HazardTimeScale, camera trauma + hitstop + white-hot burst
		Velocity.X = bIsWesternGenre ? FMath::Max(static_cast<float>(Velocity.X) * 1.30f, OverdriveSpeed) : OverdriveSpeed;
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

		// Check 128 BPM On-Beat Cut window (±0.16s) + Match-Cut window
		if (!TryTriggerOnBeatCut())
		{
			TryTriggerMatchCut(false);
		}

		PushTakeAndAspectToSubsystem();
		if (DirectorsCutEnterSound)
		{
			UGameplayStatics::PlaySound2D(this, DirectorsCutEnterSound, 1.05f, 1.25f);
		}
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
		bIsShotStale = false;
		bIsDropCutSlamming = false;

		if (FlowSubsystem)
		{
			FlowSubsystem->SetHazardTimeScale(1.0f);
		}

		PushTakeAndAspectToSubsystem();
		if (DirectorsCutExitSound)
		{
			UGameplayStatics::PlaySound2D(this, DirectorsCutExitSound, 0.50f, 0.85f);
		}
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
		if (CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1)
		{
			// Golden-Hour Sepia post-process grading for Scene 02 Ultra-Panavision 2.76:1
			Camera->PostProcessBlendWeight = 1.0f;
			Camera->PostProcessSettings.bOverride_ColorSaturation = true;
			Camera->PostProcessSettings.ColorSaturation = FVector4(0.62f, 0.62f, 0.62f, 1.0f);
			Camera->PostProcessSettings.bOverride_ColorGain = true;
			Camera->PostProcessSettings.ColorGain = FVector4(1.16f, 1.02f, 0.80f, 1.0f);
		}
		else
		{
			Camera->PostProcessSettings.bOverride_ColorSaturation = false;
			Camera->PostProcessSettings.bOverride_ColorGain = false;
		}
	}

	PushTakeAndAspectToSubsystem();
	OnAspectChanged.Broadcast(CurrentAspect);
	OnAspectChangedEvent(CurrentAspect);
}

void AMegaJamFlowPawn::TriggerClapperboardTransition(EMegaJamSceneGenre NextGenre)
{
	if (CurrentAspect != EMegaJamAspectMode::Box_4_3)
	{
		SetAspectMode(EMegaJamAspectMode::Box_4_3);
	}

	// Vertical launch impulse into the air + mid-air hang
	Velocity.Z = 1800.0f;
	TriggerMicroHitstop(0.25f, 0.0f);
	OnClapperboardSnapped.Broadcast();
	PlayRunStartSound();
	SyncSceneMusicTrack(NextGenre);

	if (UWorld* World = GetWorld())
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetActiveGenre(NextGenre);
		}
	}
}

void AMegaJamFlowPawn::ResetForRun()
{
	bCrashed = false;
	Velocity = FVector::ZeroVector;
	FlowRatio = 0.0f;
	FilmMeter = 50.0f;
	SteerInput = FVector2D::ZeroVector;
	CurrentLane = 2;
	bHasProcessedLaneInput = false;
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
	SpliceChain = 1;
	CurrentShotDuration = 0.0f;
	AspectHoldTimer = 0.0f;
	bIsShotStale = false;
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

	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Default starting mode: 4:3 Academy Frame (Flight & Charge Mode) with 50% Film Meter
	CurrentTake = EMegaJamTakeState::StudioTake;
	SetAspectMode(EMegaJamAspectMode::Box_4_3);
	if (Camera)
	{
		Camera->SetFieldOfView(BoxFOV);
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

	if (USoundBase* BeatSound = FilmFramePickupSound ? FilmFramePickupSound.Get() : DirectorsCutEnterSound.Get())
	{
		const float ChainPitch = FMath::Clamp(1.18f + 0.04f * static_cast<float>(SpliceChain), 1.18f, 1.46f);
		UGameplayStatics::PlaySound2D(this, BeatSound, 0.95f, ChainPitch);
	}

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

	// Check 128 BPM On-Beat Cut window (±0.16s) when scooping a Film Frame
	const bool bTriggeredOnBeat = TryTriggerOnBeatCut();

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		const float OverdriveMult = IsInBulletTimeOverdrive() ? 2.0f : 1.0f;
		GameMode->AddBonusScore(MatchCutBonusBase * static_cast<float>(SpliceChain) * OverdriveMult, true);
		GameMode->RegisterFilmFrameCollected(SpliceChain);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
		FlowSubsystem->SetSpliceChain(SpliceChain);
		FlowSubsystem->SetFilmMeter(FilmMeter);
	}

	if (!bTriggeredOnBeat)
	{
		PlayOneShot(FilmFramePickupSound);
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

	PlayOneShot(DollyBoostStripSound);
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

	if (Camera)
	{
		Camera->SetFieldOfView(Camera->FieldOfView + 25.0f);
	}

	AddCameraTrauma(0.4f, 0.6f);

	if (DirectorsCutEnterSound)
	{
		UGameplayStatics::PlaySound2D(this, DirectorsCutEnterSound, 1.0f, 1.6f);
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
	PlayOneShot(RunStartSound);
}

void AMegaJamFlowPawn::SyncSceneMusicTrack(EMegaJamSceneGenre TargetGenre)
{
	ActiveMusicGenre = TargetGenre;

	// Swap thematic vehicle skeletal mesh when mapped in SceneVehicleMeshes (Runner -> Horse -> Vintage Car)
	if (const TObjectPtr<USkeletalMesh>* FoundVehicleMesh = SceneVehicleMeshes.Find(TargetGenre))
	{
		if (USkeletalMesh* NewMesh = FoundVehicleMesh->Get())
		{
			if (VehicleSkeletalMesh)
			{
				VehicleSkeletalMesh->SetSkeletalMesh(NewMesh);
				VehicleSkeletalMesh->SetVisibility(true);
				if (VisualMesh)
				{
					VisualMesh->SetVisibility(false);
				}
			}
			else if (USkeletalMeshComponent* FallbackSkelComp = FindComponentByClass<USkeletalMeshComponent>())
			{
				FallbackSkelComp->SetSkeletalMesh(NewMesh);
			}
		}
	}

	if (!MusicAudioComponent)
	{
		return;
	}

	USoundBase* DesiredSound = nullptr;
	if (const TObjectPtr<USoundBase>* FoundTrack = SceneMusicTracks.Find(TargetGenre))
	{
		DesiredSound = FoundTrack->Get();
	}
	if (!DesiredSound)
	{
		DesiredSound = MusicTrack;
	}

	if (!DesiredSound)
	{
		return;
	}

	if (MusicAudioComponent->Sound != DesiredSound || !MusicAudioComponent->IsPlaying())
	{
		MusicAudioComponent->SetSound(DesiredSound);
		MusicAudioComponent->SetPitchMultiplier(CurrentMusicPitch);
		MusicAudioComponent->SetVolumeMultiplier(CurrentMusicVolume);
		MusicAudioComponent->Play();
	}
}

void AMegaJamFlowPawn::UpdateDynamicMusicModulation(float UnscaledDelta)
{
	if (!MusicAudioComponent)
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

	CurrentMusicPitch = FMath::FInterpTo(CurrentMusicPitch, TargetPitch, UnscaledDelta, 8.0f);
	CurrentMusicVolume = FMath::FInterpTo(CurrentMusicVolume, TargetVolume, UnscaledDelta, 8.0f);

	MusicAudioComponent->SetPitchMultiplier(CurrentMusicPitch);
	MusicAudioComponent->SetVolumeMultiplier(CurrentMusicVolume);
}

void AMegaJamFlowPawn::OnMusicFinished()
{
	// Loop whichever scene track is currently assigned on MusicAudioComponent.
	if (MusicAudioComponent && MusicAudioComponent->Sound)
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
	if (bCrashed)
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
	PlayOneShot(CrashSound);
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
	Velocity = FVector(TerminalVelocity * 0.7f, 0.0f, 0.0f);
	const FVector Loc = GetActorLocation();
	SetActorLocation(FVector(Loc.X, 0.0f, 130.0f), false, nullptr, ETeleportType::TeleportPhysics);
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
	FilmMeter = FMath::Clamp(FilmMeter + 10.0f, 0.0f, 100.0f);
	AddCameraTrauma(0.14f, 0.30f);

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
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
