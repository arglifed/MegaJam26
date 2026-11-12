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

	GrazeSphere->SetSphereRadius(GrazeRadius);
	GrazeSphere->OnComponentBeginOverlap.AddDynamic(this, &AMegaJamFlowPawn::OnGrazeBeginOverlap);
	GrazeSphere->OnComponentEndOverlap.AddDynamic(this, &AMegaJamFlowPawn::OnGrazeEndOverlap);
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

	if (BoostAction)
	{
		EnhancedInput->BindAction(BoostAction, ETriggerEvent::Triggered, this, &AMegaJamFlowPawn::Input_Boost);
		EnhancedInput->BindAction(BoostAction, ETriggerEvent::Completed, this, &AMegaJamFlowPawn::Input_BoostReleased);
		EnhancedInput->BindAction(BoostAction, ETriggerEvent::Canceled, this, &AMegaJamFlowPawn::Input_BoostReleased);
	}
	else
	{
		UE_LOG(LogMegaJamPawn, Warning, TEXT("BoostAction is not assigned on %s."), *GetClass()->GetName());
	}

	if (BrakeAction)
	{
		EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Triggered, this, &AMegaJamFlowPawn::Input_Brake);
		EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Completed, this, &AMegaJamFlowPawn::Input_BrakeReleased);
		EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Canceled, this, &AMegaJamFlowPawn::Input_BrakeReleased);
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
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const FVector Up = GetActorUpVector();

	// Forward thrust (boosted when held).
	const float ThrustNow = Acceleration * (bBoostHeld ? BoostMultiplier : 1.0f);
	Velocity += Forward * ThrustNow * DeltaSeconds;

	// Strafe: X = right, Y = up.
	Velocity += (Right * SteerInput.X + Up * SteerInput.Y) * StrafeForce * DeltaSeconds;

	// Brake: remove speed without ever reversing direction.
	if (BrakeInput > 0.0f)
	{
		const float Speed = Velocity.Size();
		if (Speed > KINDA_SMALL_NUMBER)
		{
			const float NewSpeed = FMath::Max(0.0f, Speed - BrakeDeceleration * BrakeInput * DeltaSeconds);
			Velocity *= NewSpeed / Speed;
		}
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
	bBoostHeld = Value.Get<bool>();
}

void AMegaJamFlowPawn::Input_Brake(const FInputActionValue& Value)
{
	BrakeInput = FMath::Clamp(Value.Get<float>(), 0.0f, 1.0f);
}

void AMegaJamFlowPawn::Input_SteerReleased()
{
	SteerInput = FVector2D::ZeroVector;
}

void AMegaJamFlowPawn::Input_BoostReleased()
{
	bBoostHeld = false;
}

void AMegaJamFlowPawn::Input_BrakeReleased()
{
	BrakeInput = 0.0f;
}

void AMegaJamFlowPawn::OnGrazeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bCrashed || !OtherActor || OtherActor == this)
	{
		return;
	}

	bool bAlreadyInSet = false;
	GrazedActors.Add(OtherActor, &bAlreadyInSet);
	if (bAlreadyInSet)
	{
		return;
	}

	if (AMegaJamGameMode* GameMode = GetWorld()->GetAuthGameMode<AMegaJamGameMode>())
	{
		GameMode->RegisterGraze(OtherActor);
	}

	OnObstacleGrazed(OtherActor);
}

void AMegaJamFlowPawn::OnGrazeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	GrazedActors.Remove(OtherActor);
}
