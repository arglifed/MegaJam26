// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "MegaJamPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UMegaJamCinemaHUD;

/**
 * Owns the Enhanced Input mapping context and the pause action,
 * and switches UI focus between Game and Pause/Menu.
 * Steer / Boost / Brake are bound directly on AMegaJamFlowPawn.
 */
UCLASS()
class MEGAJAM26_API AMegaJamPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMegaJamPlayerController();

	// ---- Enhanced Input assets (assign in a Blueprint subclass or class defaults) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	int32 MappingContextPriority = 0;

	// ---- Cinema HUD ----
	/** Widget class spawned automatically in BeginPlay. Defaults to pure-C++ UMegaJamCinemaHUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|UI")
	TSubclassOf<UMegaJamCinemaHUD> CinemaHUDClass;

	/** Active instance of the Cinema Letterbox HUD. */
	UPROPERTY(Transient, BlueprintReadOnly, Category="MegaJam|UI")
	TObjectPtr<UMegaJamCinemaHUD> CinemaHUDInstance;

	// ---- UI focus ----
	/** Game only: hidden cursor, input goes to gameplay. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|UI")
	void SetGameFocus();

	/** Pause / menu: visible cursor, UI gets focus but input actions still fire. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|UI")
	void SetMenuFocus();

	UFUNCTION(BlueprintCallable, Category="MegaJam|UI")
	void SetPaused(bool bPause);

	/** Show / hide the pause widget here. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|UI")
	void OnPauseStateChanged(bool bPaused);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

	/** Pause action hook. Override for custom behavior. */
	virtual void Input_Pause(const FInputActionValue& Value);

	/** Handles SpaceBar / Enter / Gamepad_FaceButton_Bottom on TitleScreen and Completed screens. */
	void Input_StartOrConfirm();

	/** Dev hotkey [F6]: Toggles God Mode in non-shipping builds. */
	void Input_DebugToggleGodMode();

	/** Dev hotkey [F7]: Skips to the next Act milestone in non-shipping builds. */
	void Input_DebugSkipAct();

	/**
	 * Builds a transient mapping context with default bindings for the pawn's Steer/Boost/Brake actions and
	 * this controller's PauseAction. Only used when DefaultMappingContext is not assigned.
	 *   Steer : W/S/A/D and Left Stick
	 *   Boost : Space and Gamepad Face Button Bottom
	 *   Brake : Left Shift and Left Trigger
	 *   Pause : Escape and Gamepad Special Right
	 */
	UInputMappingContext* BuildFallbackMappingContext(class AMegaJamFlowPawn* FlowPawn);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FallbackMappingContext;
};
