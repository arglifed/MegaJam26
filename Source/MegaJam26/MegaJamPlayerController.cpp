// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamPlayerController.h"

#include "MegaJamFlowPawn.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogMegaJamPC, Log, All);

void AMegaJamPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!DefaultMappingContext && FPackageName::DoesPackageExist(TEXT("/Game/MegaJam26/Core/IMC_Flow")))
	{
		DefaultMappingContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/MegaJam26/Core/IMC_Flow.IMC_Flow"));
	}

	// Assigned asset path. The fallback context (if needed) is added in OnPossess, once the pawn's actions exist.
	if (DefaultMappingContext)
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				InputSubsystem->AddMappingContext(DefaultMappingContext, MappingContextPriority);
			}
		}
	}

	SetGameFocus();
}

void AMegaJamPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Fallback pause action so Escape works with no assets.
	if (!PauseAction && FPackageName::DoesPackageExist(TEXT("/Game/MegaJam26/Core/IA_Pause")))
	{
		PauseAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/MegaJam26/Core/IA_Pause.IA_Pause"));
	}
	if (!PauseAction)
	{
		PauseAction = NewObject<UInputAction>(this, TEXT("IA_Pause_Fallback"), RF_Transient);
		PauseAction->ValueType = EInputActionValueType::Boolean;
	}

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	// Must fire while the game is paused so the same key can unpause.
	PauseAction->bTriggerWhenPaused = true;
	EnhancedInput->BindAction(PauseAction, ETriggerEvent::Started, this, &AMegaJamPlayerController::Input_Pause);
}

void AMegaJamPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (DefaultMappingContext)
	{
		return;
	}

	AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(InPawn);
	if (!FlowPawn)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!InputSubsystem)
	{
		return;
	}

	if (FallbackMappingContext)
	{
		InputSubsystem->RemoveMappingContext(FallbackMappingContext);
	}

	UE_LOG(LogMegaJamPC, Log, TEXT("DefaultMappingContext not assigned. Building fallback WASD / gamepad mapping."));
	FallbackMappingContext = BuildFallbackMappingContext(FlowPawn);
	InputSubsystem->AddMappingContext(FallbackMappingContext, MappingContextPriority);
}

UInputMappingContext* AMegaJamPlayerController::BuildFallbackMappingContext(AMegaJamFlowPawn* FlowPawn)
{
	// Make sure the pawn has actions even if its input component was not set up yet.
	FlowPawn->EnsureFallbackInputActions();

	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Flow_Fallback"), RF_Transient);

	// ---- Steer (Axis2D): X = right, Y = up ----
	// D / Right : +X (no modifiers)
	Context->MapKey(FlowPawn->SteerAction, EKeys::D);
	Context->MapKey(FlowPawn->SteerAction, EKeys::Right);

	// A / Left : -X (Negate)
	for (const FKey& Key : { EKeys::A, EKeys::Left })
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(FlowPawn->SteerAction, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}

	// W / Up : +Y (Swizzle YXZ moves the key's X value onto Y)
	for (const FKey& Key : { EKeys::W, EKeys::Up })
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(FlowPawn->SteerAction, Key);
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
	}

	// S / Down : -Y (Swizzle YXZ first, then Negate)
	for (const FKey& Key : { EKeys::S, EKeys::Down })
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(FlowPawn->SteerAction, Key);
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}

	// Left stick is already a 2D key.
	Context->MapKey(FlowPawn->SteerAction, EKeys::Gamepad_Left2D);

	// ---- Boost (Hard Cut, Bool) ----
	Context->MapKey(FlowPawn->BoostAction, EKeys::SpaceBar);
	Context->MapKey(FlowPawn->BoostAction, EKeys::Gamepad_FaceButton_Bottom);

	// ---- Brake (Aspect Toggle, Bool) ----
	Context->MapKey(FlowPawn->BrakeAction, EKeys::LeftControl);
	Context->MapKey(FlowPawn->BrakeAction, EKeys::RightControl);
	Context->MapKey(FlowPawn->BrakeAction, EKeys::C);
	Context->MapKey(FlowPawn->BrakeAction, EKeys::Gamepad_FaceButton_Right);
	Context->MapKey(FlowPawn->BrakeAction, EKeys::LeftShift);

	// ---- Pause (Bool) ----
	Context->MapKey(PauseAction, EKeys::Escape);
	Context->MapKey(PauseAction, EKeys::Gamepad_Special_Right);

	return Context;
}

void AMegaJamPlayerController::Input_Pause(const FInputActionValue& Value)
{
	SetPaused(!IsPaused());
}

void AMegaJamPlayerController::SetPaused(bool bPause)
{
	SetPause(bPause);

	if (bPause)
	{
		SetMenuFocus();
	}
	else
	{
		SetGameFocus();
	}

	OnPauseStateChanged(bPause);
}

void AMegaJamPlayerController::SetGameFocus()
{
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

void AMegaJamPlayerController::SetMenuFocus()
{
	bShowMouseCursor = true;

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}
