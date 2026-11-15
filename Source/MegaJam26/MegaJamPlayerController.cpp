// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamPlayerController.h"

#include "MegaJamCinemaHUD.h"
#include "MegaJamFlowPawn.h"
#include "MegaJamGameMode.h"
#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogMegaJamPC, Log, All);

AMegaJamPlayerController::AMegaJamPlayerController()
{
	CinemaHUDClass = UMegaJamCinemaHUD::StaticClass();
}

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

	if (IsLocalController() && CinemaHUDClass && !CinemaHUDInstance)
	{
		CinemaHUDInstance = CreateWidget<UMegaJamCinemaHUD>(this, CinemaHUDClass);
		if (CinemaHUDInstance)
		{
			CinemaHUDInstance->AddToViewport(0);
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

	// Bind Escape, P, and Gamepad_Special_Right with .bExecuteWhenPaused = true to toggle pause during Playing state.
	for (const FKey& PauseKey : { EKeys::Escape, EKeys::P, EKeys::Gamepad_Special_Right })
	{
		FInputKeyBinding PauseBinding(FInputChord(PauseKey), IE_Pressed);
		PauseBinding.bConsumeInput = false;
		PauseBinding.bExecuteWhenPaused = true;
		PauseBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamPlayerController::Input_TogglePauseKey);
		InputComponent->KeyBindings.Add(PauseBinding);
	}

	// Bind W / Up / Gamepad_DPad_Up and S / Down / Gamepad_DPad_Down with .bExecuteWhenPaused = true for Pause Menu navigation.
	for (const FKey& UpKey : { EKeys::W, EKeys::Up, EKeys::Gamepad_DPad_Up })
	{
		FInputKeyBinding UpBinding(FInputChord(UpKey), IE_Pressed);
		UpBinding.bConsumeInput = false;
		UpBinding.bExecuteWhenPaused = true;
		UpBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamPlayerController::Input_PauseMenuUp);
		InputComponent->KeyBindings.Add(UpBinding);
	}

	for (const FKey& DownKey : { EKeys::S, EKeys::Down, EKeys::Gamepad_DPad_Down })
	{
		FInputKeyBinding DownBinding(FInputChord(DownKey), IE_Pressed);
		DownBinding.bConsumeInput = false;
		DownBinding.bExecuteWhenPaused = true;
		DownBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamPlayerController::Input_PauseMenuDown);
		InputComponent->KeyBindings.Add(DownBinding);
	}

	// Bind SpaceBar, Enter, Gamepad Cross, and USB Button 1 (non-consuming so gameplay IA_Boost still fires, and executing when paused)
	// to confirm Pause Menu selection, start from TitleScreen, or restart from Completed Wrap-Up screen.
	for (const FKey& StartKey : { EKeys::SpaceBar, EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom, FKey(TEXT("GenericUSBController_Button1")) })
	{
		FInputKeyBinding KeyBinding(FInputChord(StartKey), IE_Pressed);
		KeyBinding.bConsumeInput = false;
		KeyBinding.bExecuteWhenPaused = true;
		KeyBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamPlayerController::Input_StartOrConfirm);
		InputComponent->KeyBindings.Add(KeyBinding);
	}

#if !UE_BUILD_SHIPPING
	{
		FInputKeyBinding F6Binding(FInputChord(EKeys::F6), IE_Pressed);
		F6Binding.bConsumeInput = false;
		F6Binding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamPlayerController::Input_DebugToggleGodMode);
		InputComponent->KeyBindings.Add(F6Binding);

		FInputKeyBinding F7Binding(FInputChord(EKeys::F7), IE_Pressed);
		F7Binding.bConsumeInput = false;
		F7Binding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &AMegaJamPlayerController::Input_DebugSkipAct);
		InputComponent->KeyBindings.Add(F7Binding);
	}
#endif
}

void AMegaJamPlayerController::Input_DebugToggleGodMode()
{
#if !UE_BUILD_SHIPPING
	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(GetPawn()))
	{
		FlowPawn->ToggleGodMode();
	}
#endif
}

void AMegaJamPlayerController::Input_DebugSkipAct()
{
#if !UE_BUILD_SHIPPING
	if (UWorld* World = GetWorld())
	{
		if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
		{
			GameMode->DebugSkipToNextAct();
		}
	}
#endif
}

void AMegaJamPlayerController::Input_PauseMenuUp()
{
	if (IsPaused())
	{
		PauseMenuSelectionIndex = (PauseMenuSelectionIndex + 2) % 3;
	}
}

void AMegaJamPlayerController::Input_PauseMenuDown()
{
	if (IsPaused())
	{
		PauseMenuSelectionIndex = (PauseMenuSelectionIndex + 1) % 3;
	}
}

void AMegaJamPlayerController::Input_StartOrConfirm()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	if (IsPaused())
	{
		const int32 Choice = PauseMenuSelectionIndex;
		SetPaused(false);
		if (GameMode)
		{
			if (Choice == 1)
			{
				GameMode->RetakeCurrentAct();
			}
			else if (Choice == 2)
			{
				GameMode->WrapToTitleSlate();
			}
		}
		return;
	}

	if (GameMode)
	{
		const EMegaJamRunState State = GameMode->GetRunState();
		if (State == EMegaJamRunState::TitleScreen)
		{
			GameMode->StartProductionFromTitle();
		}
		else if (State == EMegaJamRunState::Completed)
		{
			GameMode->RestartFullProduction();
		}
	}
}

void AMegaJamPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

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

	// Always build and register our C++ supplemental/fallback mapping context so PS5 DualSense and DirectInput USB joystick
	// keys work seamlessly without modifying IMC_Flow.uasset.
	FallbackMappingContext = BuildFallbackMappingContext(FlowPawn);
	InputSubsystem->AddMappingContext(FallbackMappingContext, MappingContextPriority);
}

UInputMappingContext* AMegaJamPlayerController::BuildFallbackMappingContext(AMegaJamFlowPawn* FlowPawn)
{
	// Make sure the pawn has actions even if its input component was not set up yet.
	FlowPawn->EnsureFallbackInputActions();

	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Flow_Fallback"), RF_Transient);

	// ---- Steer (Axis2D): X = right, Y = up ----
	// D / Right / Gamepad_DPad_Right / GenericUSBController_Axis1 : +X (no modifiers)
	Context->MapKey(FlowPawn->SteerAction, EKeys::D);
	Context->MapKey(FlowPawn->SteerAction, EKeys::Right);
	Context->MapKey(FlowPawn->SteerAction, EKeys::Gamepad_DPad_Right);
	Context->MapKey(FlowPawn->SteerAction, EKeys::Gamepad_LeftX);
	Context->MapKey(FlowPawn->SteerAction, FKey(TEXT("GenericUSBController_Axis1")));

	// A / Left / Gamepad_DPad_Left : -X (Negate)
	for (const FKey& Key : { EKeys::A, EKeys::Left, EKeys::Gamepad_DPad_Left })
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(FlowPawn->SteerAction, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}

	// W / Up / Gamepad_DPad_Up / Gamepad_LeftY / GenericUSBController_Axis2 : +Y (Swizzle YXZ moves the key's X value onto Y)
	for (const FKey& Key : { EKeys::W, EKeys::Up, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftY, FKey(TEXT("GenericUSBController_Axis2")) })
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(FlowPawn->SteerAction, Key);
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
	}

	// S / Down / Gamepad_DPad_Down : -Y (Swizzle YXZ first, then Negate)
	for (const FKey& Key : { EKeys::S, EKeys::Down, EKeys::Gamepad_DPad_Down })
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(FlowPawn->SteerAction, Key);
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}

	// Left stick is already a 2D key.
	Context->MapKey(FlowPawn->SteerAction, EKeys::Gamepad_Left2D);

	// ---- Boost (Editor's Splice / Hard Cut, Bool) ----
	Context->MapKey(FlowPawn->BoostAction, EKeys::SpaceBar);
	Context->MapKey(FlowPawn->BoostAction, EKeys::LeftShift);
	Context->MapKey(FlowPawn->BoostAction, EKeys::Gamepad_FaceButton_Bottom);
	Context->MapKey(FlowPawn->BoostAction, EKeys::Gamepad_FaceButton_Right);
	Context->MapKey(FlowPawn->BoostAction, FKey(TEXT("GenericUSBController_Button1")));

	// ---- Brake (Aspect Toggle / Director's Cut, Bool) ----
	Context->MapKey(FlowPawn->BrakeAction, EKeys::LeftControl);
	Context->MapKey(FlowPawn->BrakeAction, EKeys::RightControl);
	Context->MapKey(FlowPawn->BrakeAction, EKeys::C);
	Context->MapKey(FlowPawn->BrakeAction, EKeys::Gamepad_FaceButton_Left);
	Context->MapKey(FlowPawn->BrakeAction, FKey(TEXT("GenericUSBController_Button2")));
	Context->MapKey(FlowPawn->BrakeAction, EKeys::Gamepad_LeftShoulder);
	Context->MapKey(FlowPawn->BrakeAction, EKeys::Gamepad_RightShoulder);

	// ---- Pause (Bool) ----
	Context->MapKey(PauseAction, EKeys::Escape);
	Context->MapKey(PauseAction, EKeys::P);
	Context->MapKey(PauseAction, EKeys::Gamepad_Special_Right);

	return Context;
}

void AMegaJamPlayerController::Input_Pause(const FInputActionValue& Value)
{
	Input_TogglePauseKey();
}

void AMegaJamPlayerController::Input_TogglePauseKey()
{
	const double NowReal = FPlatformTime::Seconds();
	if (NowReal - LastPauseToggleRealTime < 0.12)
	{
		return;
	}

	if (IsPaused())
	{
		LastPauseToggleRealTime = NowReal;
		SetPaused(false);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (const AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
		{
			if (GameMode->GetRunState() != EMegaJamRunState::Playing && !GameMode->IsPlaying())
			{
				return;
			}
		}
	}

	LastPauseToggleRealTime = NowReal;
	PauseMenuSelectionIndex = 0;
	SetPaused(true);
}

void AMegaJamPlayerController::SetPaused(bool bPause)
{
	if (!bPause)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}

	SetPause(bPause);

	if (bPause)
	{
		PauseMenuSelectionIndex = 0;
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
