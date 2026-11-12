// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamFlowPawn.h"

#include "MegaJamFlowSubsystem.h"
#include "MegaJamGameMode.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"

DEFINE_LOG_CATEGORY_STATIC(LogMegaJamPawn, Log, All);

AMegaJamFlowPawn::AMegaJamFlowPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(50.0f);
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

	GrazeSphere->SetSphereRadius(GrazeRadius);
	GrazeSphere->OnComponentBeginOverlap.AddDynamic(this, &AMegaJamFlowPawn::OnGrazeBeginOverlap);

	PushTakeAndAspectToSubsystem();
}

void AMegaJamFlowPawn::EnsureFallbackInputActions()
{
	if (!SteerAction)
	{
		SteerAction = NewObject<UInputAction>(this, TEXT("IA_Steer_Fallback"), RF_Transient);
		SteerAction->ValueType = EInputActionValueType::Axis2D;
	}
	if (!BoostAction)
	{
		BoostAction = NewObject<UInputAction>(this, TEXT("IA_Boost_Fallback"), RF_Transient);
		BoostAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!BrakeAction)
	{
		BrakeAction = NewObject<UInputAction>(this, TEXT("IA_Brake_Fallback"), RF_Transient);
		BrakeAction->ValueType = EInputActionValueType::Axis1D;
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

	if (bCrashed)
	{
		return;
	}

	// Hold still until the run is active (if a MegaJam game mode is in use).
	const AMegaJamGameMode* GameMode = GetWorld()->GetAuthGameMode<AMegaJamGameMode>();
	if (GameMode && GameMode->GetRunState() != EMegaJamRunState::ActiveFlow)
	{
		return;
	}

	StepPhysics(DeltaSeconds);

	// Normalized speed. Computed after StepPhysics so a crash leaves FlowRatio at 0.
	FlowRatio = TerminalVelocity > KINDA_SMALL_NUMBER
		? FMath::Clamp(Velocity.Size() / TerminalVelocity, 0.0f, 1.0f)
		: 0.0f;

	if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(FlowRatio);
	}
}

void AMegaJamFlowPawn::StepPhysics(float DeltaSeconds)
{
	const bool bWide = CurrentAspect == EMegaJamAspectMode::Wide_21_9;

	// Forward thrust along +X (the pawn never rotates).
	Velocity += GetActorForwardVector() * Acceleration * DeltaSeconds;

	// Strafe. IA_Steer.X is always lateral (+Y / -Y).
	// Wide_21_9: stronger lateral force, vertical steering locked.
	// Box_4_3:   IA_Steer.Y steers vertically (+Z / -Z).
	const float StrafeNow = StrafeForce * (bWide ? WideStrafeMultiplier : 1.0f);
	Velocity.Y += SteerInput.X * StrafeNow * DeltaSeconds;
	if (bWide)
	{
		Velocity.Z = 0.0f;
	}
	else
	{
		Velocity.Z += SteerInput.Y * StrafeNow * DeltaSeconds;
	}

	// Drag then terminal velocity clamp.
	Velocity *= FMath::Exp(-Drag * DeltaSeconds);
	Velocity = Velocity.GetClampedToMaxSize(TerminalVelocity);

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
			return;
		}

		// Slow contact: slide along the surface.
		Velocity = FVector::VectorPlaneProject(Velocity, Hit.ImpactNormal);
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
	if (GameMode && GameMode->GetRunState() != EMegaJamRunState::ActiveFlow)
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

	PushTakeAndAspectToSubsystem();
	OnTakeCut.Broadcast(CurrentTake);
	OnTakeCutEvent(CurrentTake);
	return true;
}

void AMegaJamFlowPawn::ToggleAspect()
{
	SetAspectMode(CurrentAspect == EMegaJamAspectMode::Wide_21_9 ? EMegaJamAspectMode::Box_4_3 : EMegaJamAspectMode::Wide_21_9);
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
	LastCutTime = -1000.0;
	GrazeTimes.Reset();

	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Back to the opening framing. Broadcast so listeners (track generator, HUD) resync.
	CurrentTake = EMegaJamTakeState::StudioTake;
	CurrentAspect = EMegaJamAspectMode::Wide_21_9;
	PushTakeAndAspectToSubsystem();
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
		}
	}
}

void AMegaJamFlowPawn::HandleCrash(AActor* HitActor)
{
	if (bCrashed)
	{
		return;
	}

	bCrashed = true;
	Velocity = FVector::ZeroVector;
	FlowRatio = 0.0f;

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
	// While crashed (or after the film completes) the same button rewinds to the next take immediately.
	if (AMegaJamGameMode* GameMode = GetWorld()->GetAuthGameMode<AMegaJamGameMode>())
	{
		const EMegaJamRunState State = GameMode->GetRunState();
		if (State == EMegaJamRunState::Crashed || State == EMegaJamRunState::Completed)
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

	ToggleAspect();
}

void AMegaJamFlowPawn::Input_SteerReleased()
{
	SteerInput = FVector2D::ZeroVector;
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

	if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
	{
		GameMode->RegisterGraze(OtherActor);
	}

	OnObstacleGrazed(OtherActor);
}
