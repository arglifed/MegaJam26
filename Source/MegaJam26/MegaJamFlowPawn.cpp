// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamFlowPawn.h"

#include "MegaJamFlowSubsystem.h"
#include "MegaJamGameMode.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogMegaJamPawn, Log, All);

AMegaJamFlowPawn::AMegaJamFlowPawn()
{
	PrimaryActorTick.bCanEverTick = true;

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
	Camera->SetFieldOfView(WideFOV);

	ScoreAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("ScoreAudioComponent"));
	ScoreAudioComponent->SetupAttachment(CollisionSphere);
	ScoreAudioComponent->bAutoActivate = false;
	ScoreAudioComponent->bIsUISound = true;
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

	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
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
}

void AMegaJamFlowPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	CurrentFOVPunch = FMath::FInterpTo(CurrentFOVPunch, 0.0f, DeltaSeconds, CutFOVPunchDecaySpeed);

	if (Camera)
	{
		const float BaseFOV = (CurrentAspect == EMegaJamAspectMode::Wide_21_9) ? WideFOV : BoxFOV;
		const float SmoothedBaseFOV = FMath::FInterpTo(Camera->FieldOfView - CurrentFOVPunch, BaseFOV, DeltaSeconds, AspectFOVInterpSpeed);
		Camera->SetFieldOfView(SmoothedBaseFOV + CurrentFOVPunch);

		const float LateralAlpha = (TerminalVelocity > KINDA_SMALL_NUMBER && !bCrashed)
			? FMath::Clamp(static_cast<float>(Velocity.Y) / TerminalVelocity, -1.0f, 1.0f)
			: 0.0f;
		const float TargetRoll = LateralAlpha * MaxStrafeRoll;
		CurrentCameraRoll = FMath::FInterpTo(CurrentCameraRoll, TargetRoll, DeltaSeconds, CameraRollInterpSpeed);
		Camera->SetRelativeRotation(FRotator(0.0f, 0.0f, CurrentCameraRoll));
	}

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
	StepPhysics(DeltaSeconds);

	MatchCutFlashTimer = FMath::Max(0.0f, MatchCutFlashTimer - DeltaSeconds);
	DropCutFlashTimer = FMath::Max(0.0f, DropCutFlashTimer - DeltaSeconds);
	DollyBoostFlashTimer = FMath::Max(0.0f, DollyBoostFlashTimer - DeltaSeconds);

	AspectHoldTimer += DeltaSeconds;
	if (AspectHoldTimer > StaleShotThreshold)
	{
		bIsShotStale = true;
		StaleDecayAccum += DeltaSeconds;
		while (StaleDecayAccum >= StaleDecayInterval)
		{
			StaleDecayAccum -= StaleDecayInterval;
			SpliceChain = FMath::Max(1, SpliceChain - 1);
		}
	}
	else
	{
		bIsShotStale = false;
		StaleDecayAccum = 0.0f;
	}

	// ---- 21:9 Dolly Rail vs. 4:3 Crane Overexposure ----
	const float CurrentAltitude = static_cast<float>(GetActorLocation().Z);
	if (CurrentAspect == EMegaJamAspectMode::Wide_21_9)
	{
		// 21:9 Dolly Rail: cool down exposure 2.5x faster and grant passive Flow regeneration (+4.5% / sec).
		AirExposureTimer = FMath::Max(0.0f, AirExposureTimer - 2.5f * DeltaSeconds);
		bIsOverexposed = false;
		OverexposureDecayAccum = 0.0f;
		FlowBonusOffset = FMath::Clamp(FlowBonusOffset + (WideDollyFlowRegenPerSec * 0.01f) * DeltaSeconds, -0.5f, 0.35f);
	}
	else
	{
		if (CurrentAltitude > OverexposureAltitudeThreshold)
		{
			AirExposureTimer = FMath::Min(OverexposureMaxSeconds, AirExposureTimer + DeltaSeconds);
		}
		else if (CurrentAltitude < 220.0f)
		{
			AirExposureTimer = FMath::Max(0.0f, AirExposureTimer - 2.5f * DeltaSeconds);
		}

		bIsOverexposed = (AirExposureTimer > OverexposureStartSeconds);
		if (bIsOverexposed)
		{
			// Drain Flow (-14% / sec) and apply aerodynamic drag to forward speed.
			FlowBonusOffset = FMath::Clamp(FlowBonusOffset - (OverexposureFlowDrainPerSec * 0.01f) * DeltaSeconds, -0.5f, 0.35f);
			Velocity.X *= FMath::Exp(-1.35f * DeltaSeconds);

			if (AirExposureTimer >= OverexposureMaxSeconds)
			{
				OverexposureDecayAccum += DeltaSeconds;
				while (OverexposureDecayAccum >= 1.5f)
				{
					OverexposureDecayAccum -= 1.5f;
					SpliceChain = FMath::Max(1, SpliceChain - 1);
				}
			}
		}
		else
		{
			OverexposureDecayAccum = 0.0f;
		}
	}

	// Normalized speed + FlowBonusOffset. Computed after StepPhysics so a crash leaves FlowRatio at 0.
	const float RawSpeedFlow = TerminalVelocity > KINDA_SMALL_NUMBER
		? FMath::Clamp(Velocity.Size() / TerminalVelocity, 0.0f, 1.0f)
		: 0.0f;
	FlowRatio = FMath::Clamp(RawSpeedFlow + FlowBonusOffset, 0.0f, 1.0f);

	if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
		FlowSubsystem->SetSpliceChain(SpliceChain);
		FlowSubsystem->SetAspectHoldTimer(AspectHoldTimer);
		FlowSubsystem->SetIsShotStale(bIsShotStale);
		FlowSubsystem->SetAirExposureRatio(GetAirExposureRatio());
		FlowSubsystem->SetIsOverexposed(bIsOverexposed);
	}

	PushAudioParameters();
}

void AMegaJamFlowPawn::StepPhysics(float DeltaSeconds)
{
	const bool bWide = CurrentAspect == EMegaJamAspectMode::Wide_21_9;

	// Forward thrust along +X (the pawn never rotates).
	Velocity += GetActorForwardVector() * Acceleration * DeltaSeconds;

	// Strafe. IA_Steer.X is always lateral (+Y / -Y).
	// Wide_21_9: stronger lateral force, vertical steering locked.
	// Box_4_3:   IA_Steer.Y steers vertically (+Z / -Z) with +30% vertical responsiveness.
	const float LateralAccel = StrafeForce * (bWide ? WideStrafeMultiplier : 1.0f);
	const float VerticalAccel = StrafeForce * VerticalStrafeMultiplier;

	// Tighter lateral/vertical velocity damping when steering is neutral or counter-steering
	const float CrossPlaneDamp = FMath::Exp(-LateralDamping * DeltaSeconds);
	Velocity.Y *= CrossPlaneDamp;
	Velocity.Y += SteerInput.X * LateralAccel * DeltaSeconds;

	if (bWide)
	{
		Velocity.Z = 0.0f;
	}
	else
	{
		Velocity.Z *= CrossPlaneDamp;
		Velocity.Z += SteerInput.Y * VerticalAccel * DeltaSeconds;
	}

	// Forward drag then terminal velocity clamp (allow brief overspeed from Drop-Cut Slam / Boost Strip to decay smoothly).
	Velocity.X *= FMath::Exp(-Drag * DeltaSeconds);
	const float MaxAllowedSpeed = (DropCutFlashTimer > 0.0f || DollyBoostFlashTimer > 0.0f)
		? (TerminalVelocity + 400.0f)
		: TerminalVelocity;
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
	const bool bWide = CurrentAspect == EMegaJamAspectMode::Wide_21_9;
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
	const float NewY = FMath::FInterpTo(static_cast<float>(Location.Y), ClampedY, DeltaSeconds, AspectClampInterpSpeed);
	const float NewZ = FMath::FInterpTo(static_cast<float>(Location.Z), ClampedZ, DeltaSeconds, AspectClampInterpSpeed);
	AddActorWorldOffset(FVector(0.0f, NewY - Location.Y, NewZ - Location.Z), true);
}

bool AMegaJamFlowPawn::TriggerHardCut()
{
	UWorld* World = GetWorld();
	if (bCrashed || !World)
	{
		return false;
	}

	const AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && !GameMode->IsPlaying())
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	if (Now - LastCutTime < CutCooldown)
	{
		return false;
	}
	LastCutTime = Now;

	CurrentTake = (CurrentTake == EMegaJamTakeState::StudioTake) ? EMegaJamTakeState::DirectorsTake : EMegaJamTakeState::StudioTake;

	// Brief forward impulse. StepPhysics re-applies the terminal velocity clamp.
	Velocity += GetActorForwardVector() * CutSpeedImpulse;
	CurrentFOVPunch = CutFOVPunch;

	TryTriggerMatchCut();

	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	TriggerAudioEvent(TEXT("OnHardCut"));
	OnTakeCut.Broadcast(CurrentTake);
	OnTakeCutEvent(CurrentTake);
	return true;
}

void AMegaJamFlowPawn::ToggleAspect()
{
	const EMegaJamAspectMode OldAspect = CurrentAspect;
	const float PreSwitchZ = static_cast<float>(GetActorLocation().Z);

	SetAspectMode(OldAspect == EMegaJamAspectMode::Wide_21_9 ? EMegaJamAspectMode::Box_4_3 : EMegaJamAspectMode::Wide_21_9);

	AspectHoldTimer = 0.0f;
	bIsShotStale = false;
	StaleDecayAccum = 0.0f;

	// ---- Drop-Cut Slam: switching from Box_4_3 -> Wide_21_9 while airborne (Z > 300) ----
	if (OldAspect == EMegaJamAspectMode::Box_4_3 && CurrentAspect == EMegaJamAspectMode::Wide_21_9 && PreSwitchZ > DropCutMinAltitude)
	{
		// Snap vertical downward velocity and slam onto the Z = 100.0f dolly plane
		Velocity.Z = -3200.0f;
		const FVector Loc = GetActorLocation();
		SetActorLocation(FVector(Loc.X, Loc.Y, 100.0f), true);

		// Grant +750.0f forward speed impulse (clamped to TerminalVelocity + 400.0f), +7.5 deg FOV punch, +20% Flow, +250 * SpliceChain score
		Velocity.X = FMath::Min(static_cast<float>(Velocity.X) + DropCutSpeedImpulse, TerminalVelocity + 400.0f);
		CurrentFOVPunch = DropCutFOVPunch;
		FlowBonusOffset = FMath::Clamp(FlowBonusOffset + 0.20f, -0.5f, 0.35f);
		FlowRatio = FMath::Clamp(FlowRatio + 0.20f, 0.0f, 1.0f);
		AirExposureTimer = 0.0f;
		bIsOverexposed = false;
		DropCutFlashTimer = 1.3f;

		TryTriggerMatchCut(true);

		if (UWorld* World = GetWorld())
		{
			if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
			{
				GameMode->AddBonusScore(250.0f * static_cast<float>(SpliceChain), true);
			}
		}

		PushTakeAndAspectToSubsystem();
		TriggerAudioEvent(TEXT("OnHardCut"));
	}
	else
	{
		TryTriggerMatchCut(false);
		PushTakeAndAspectToSubsystem();
		TriggerAudioEvent(TEXT("OnAspectSwitch"));
	}

	if (GEngine)
	{
		const bool bWide = (CurrentAspect == EMegaJamAspectMode::Wide_21_9);
		const FString Banner = bWide
			? TEXT("ASPECT: 21:9 WIDESCREEN")
			: TEXT("ASPECT: 4:3 ACADEMY BOX");
		const FColor BannerColor = bWide ? FColor::Cyan : FColor::Orange;
		GEngine->AddOnScreenDebugMessage(202601, 2.5f, BannerColor, Banner, true, FVector2D(1.5f, 1.5f));
	}
}

void AMegaJamFlowPawn::SetAspectMode(EMegaJamAspectMode NewAspect)
{
	CurrentAspect = NewAspect;

	PushTakeAndAspectToSubsystem();
	OnAspectChanged.Broadcast(CurrentAspect);
	OnAspectChangedEvent(CurrentAspect);
}

void AMegaJamFlowPawn::ResetForRun()
{
	bCrashed = false;
	Velocity = FVector::ZeroVector;
	FlowRatio = 0.0f;
	SteerInput = FVector2D::ZeroVector;
	CurrentFOVPunch = 0.0f;
	CurrentCameraRoll = 0.0f;
	SpliceChain = 1;
	AspectHoldTimer = 0.0f;
	bIsShotStale = false;
	AirExposureTimer = 0.0f;
	bIsOverexposed = false;
	StaleDecayAccum = 0.0f;
	OverexposureDecayAccum = 0.0f;
	MatchCutFlashTimer = 0.0f;
	DropCutFlashTimer = 0.0f;
	DollyBoostFlashTimer = 0.0f;
	FlowBonusOffset = 0.0f;
	LastCutTime = -1000.0;
	LastGrazeOrPickupTime = -1000.0;
	GrazeTimes.Reset();

	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Back to the opening framing. Broadcast so listeners (track generator, HUD) resync.
	CurrentTake = EMegaJamTakeState::StudioTake;
	CurrentAspect = EMegaJamAspectMode::Wide_21_9;
	if (Camera)
	{
		Camera->SetFieldOfView(WideFOV);
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
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
			FlowSubsystem->SetIsShotStale(bIsShotStale);
			FlowSubsystem->SetAirExposureRatio(GetAirExposureRatio());
			FlowSubsystem->SetIsOverexposed(bIsOverexposed);
		}
	}
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

	// Refill Flow and forward momentum on a successful Match Cut.
	Velocity.X = FMath::Max(static_cast<float>(Velocity.X), TerminalVelocity * 0.95f);
	FlowBonusOffset = FMath::Max(0.0f, FlowBonusOffset);
	FlowRatio = 1.0f;

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		GameMode->AddBonusScore(MatchCutBonusBase * static_cast<float>(SpliceChain), true);
		GameMode->NotifySpliceChainUpdated(SpliceChain);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
		FlowSubsystem->SetSpliceChain(SpliceChain);
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
	SpliceChain = FMath::Clamp(SpliceChain + 1, 1, MaxSpliceChain);
	MatchCutFlashTimer = 0.9f;

	Velocity.X = FMath::Min(static_cast<float>(Velocity.X) + CutSpeedImpulse * 0.75f, TerminalVelocity);
	FlowBonusOffset = FMath::Clamp(FlowBonusOffset + 0.12f, -0.5f, 0.35f);
	FlowRatio = TerminalVelocity > KINDA_SMALL_NUMBER
		? FMath::Clamp(Velocity.Size() / TerminalVelocity + FlowBonusOffset, 0.0f, 1.0f)
		: 0.0f;

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		GameMode->AddBonusScore(MatchCutBonusBase * static_cast<float>(SpliceChain), true);
		GameMode->RegisterFilmFrameCollected(SpliceChain);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
		FlowSubsystem->SetSpliceChain(SpliceChain);
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
	DollyBoostFlashTimer = 1.0f;
	AirExposureTimer = 0.0f;
	bIsOverexposed = false;

	Velocity.X = FMath::Min(static_cast<float>(Velocity.X) + 550.0f, TerminalVelocity + 300.0f);
	FlowBonusOffset = FMath::Clamp(FlowBonusOffset + 0.15f, -0.5f, 0.35f);
	FlowRatio = FMath::Clamp(FlowRatio + 0.15f, 0.0f, 1.0f);

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		GameMode->AddBonusScore(100.0f * static_cast<float>(SpliceChain), true);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
	}

	TriggerAudioEvent(TEXT("OnGraze"));
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
	Velocity = FVector::ZeroVector;
	FlowRatio = 0.0f;
	SpliceChain = 1;
	AspectHoldTimer = 0.0f;
	bIsShotStale = false;
	AirExposureTimer = 0.0f;
	bIsOverexposed = false;
	StaleDecayAccum = 0.0f;
	OverexposureDecayAccum = 0.0f;
	MatchCutFlashTimer = 0.0f;
	DropCutFlashTimer = 0.0f;
	DollyBoostFlashTimer = 0.0f;
	FlowBonusOffset = 0.0f;
	LastGrazeOrPickupTime = -1000.0;
	PushTakeAndAspectToSubsystem();
	PushAudioParameters();
	TriggerAudioEvent(TEXT("OnCrashed"));

	if (UWorld* World = GetWorld())
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetFlowRatio(0.0f);
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

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		GameMode->RegisterGraze(OtherActor);
	}

	TriggerAudioEvent(TEXT("OnGraze"));
	OnObstacleGrazed(OtherActor);
}
