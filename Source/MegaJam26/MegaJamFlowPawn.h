// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "MegaJamTypes.h"
#include "MegaJamFlowPawn.generated.h"

class UAudioComponent;
class UCameraComponent;
class UInputAction;
class USoundBase;
class USpringArmComponent;
class USphereComponent;
class UStaticMeshComponent;

/** Fired when the player hard-cuts between takes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTakeCut, EMegaJamTakeState, NewTake);

/** Fired when the framing mode toggles. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAspectChanged, EMegaJamAspectMode, NewAspect);

/**
 * Raw C++ physics pawn. No ACharacter / CharacterMovementComponent.
 * Velocity is integrated in Tick() and applied with a swept AddActorWorldOffset.
 * Binds Steer / Boost / Brake directly; the mapping context lives on the PlayerController.
 *
 * Director's Cut controls:
 *   IA_Boost (Started) = hard cut between StudioTake / DirectorsTake (or restart the run when crashed / completed).
 *   IA_Brake (Started) = toggle Wide_21_9 / Box_4_3 framing, which clamps the playable Y / Z corridor.
 */
UCLASS()
class MEGAJAM26_API AMegaJamFlowPawn : public APawn
{
	GENERATED_BODY()

public:
	AMegaJamFlowPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---- Components ----
	/** Collision root. Sweeps against obstacles. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	/** Near-miss detector. Larger than the collision sphere, overlap only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USphereComponent> GrazeSphere;

	/** Visual. No collision. Defaults to /Engine/BasicShapes/Sphere. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	/** Chase camera boom. Does not use controller rotation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UCameraComponent> Camera;

	// ---- Audio / MetaSound Bridge ----
	/** Audio component driving the dual-stem MetaSound score and parameter bridge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Audio")
	TObjectPtr<UAudioComponent> ScoreAudioComponent;

	/** Optional MetaSound asset to auto-play in BeginPlay. Safely no-ops if null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Audio")
	TObjectPtr<USoundBase> ScoreMetaSound;

	// ---- Physics tunables ----
	/** Forward acceleration (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float Acceleration = 4000.0f;

	/** Exponential velocity damping (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float Drag = 0.5f;

	/** Lateral / vertical steering acceleration (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float StrafeForce = 3000.0f;

	/** Maximum speed (cm/s). Maps to FlowRatio = 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float TerminalVelocity = 6000.0f;

	/** Radius of the near-miss detector (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float GrazeRadius = 200.0f;

	/** DEPRECATED: unused since IA_Boost became the hard cut. Kept so existing Blueprint defaults do not break. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float BoostMultiplier = 1.5f;

	/** DEPRECATED: unused since IA_Brake became the aspect toggle. Kept so existing Blueprint defaults do not break. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float BrakeDeceleration = 6000.0f;

	/** Closing speed into a blocking surface (cm/s) at or above which the pawn crashes. Below it the pawn slides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float CrashSpeedThreshold = 2500.0f;

	/** Minimum seconds between hard cuts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float CutCooldown = 0.25f;

	/** Forward speed added on every hard cut (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float CutSpeedImpulse = 600.0f;

	// ---- Match-Cut Chain & Stale-Shot Mechanics ----
	/** Maximum time (s) after grazing an obstacle or collecting a Film Frame in which a Hard Cut or Aspect Switch triggers a Match Cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	float MatchCutWindow = 1.2f;

	/** Maximum SpliceChain value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	int32 MaxSpliceChain = 8;

	/** Bonus score multiplied by SpliceChain when a Match Cut or Film Frame pickup triggers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	float MatchCutBonusBase = 150.0f;

	/** Seconds spent continuously in the same Aspect Mode before the shot becomes stale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	float StaleShotThreshold = 5.5f;

	/** Seconds between -1 SpliceChain decay ticks while bIsShotStale is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	float StaleDecayInterval = 2.0f;

	// ---- Aspect ratio corridor (world space; +X is forward, Y lateral, Z vertical) ----
	/** Wide_21_9: wide lateral bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	FVector2D WideYBounds = FVector2D(-900.0, 900.0);

	/** Wide_21_9: crushed vertical bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	FVector2D WideZBounds = FVector2D(60.0, 200.0);

	/** Box_4_3: narrow lateral bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	FVector2D BoxYBounds = FVector2D(-450.0, 450.0);

	/** Box_4_3: tall vertical bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	FVector2D BoxZBounds = FVector2D(60.0, 800.0);

	/** Lateral strafe force multiplier while in Wide_21_9. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float WideStrafeMultiplier = 1.35f;

	/** FInterpTo speed used to squeeze the pawn into new bounds after an aspect switch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float AspectClampInterpSpeed = 30.0f;

	/** Camera field of view in Wide_21_9 mode (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float WideFOV = 100.0f;

	/** Camera field of view in Box_4_3 mode (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float BoxFOV = 82.0f;

	/** Interpolation speed for camera FOV transitions between aspect modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float AspectFOVInterpSpeed = 12.0f;

	// ---- Camera Juice ----
	/** Maximum camera roll (degrees) induced by lateral strafe velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Camera")
	float MaxStrafeRoll = 4.5f;

	/** Interpolation speed for camera strafe roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Camera")
	float CameraRollInterpSpeed = 10.0f;

	/** Additive FOV punch (degrees) applied on a hard cut before decaying back to base FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Camera")
	float CutFOVPunch = 6.0f;

	/** Decay interpolation speed for the additive hard-cut FOV punch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Camera")
	float CutFOVPunchDecaySpeed = 10.0f;

	/** Steady forward cruise speed (cm/s) during the Attract-Mode Title Screen and Victory Cruise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Attract")
	float AttractCruiseSpeed = 1600.0f;

	// ---- Enhanced Input actions (assign in a Blueprint subclass) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	TObjectPtr<UInputAction> SteerAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	TObjectPtr<UInputAction> BoostAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	TObjectPtr<UInputAction> BrakeAction;

	/**
	 * If Steer / Boost / Brake actions are not assigned, creates transient UInputAction objects
	 * so the pawn is playable without any editor assets. Safe to call repeatedly.
	 */
	void EnsureFallbackInputActions();

	// ---- State ----
	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	float GetFlowRatio() const { return FlowRatio; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	FVector GetFlowVelocity() const { return Velocity; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	bool HasCrashed() const { return bCrashed; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	EMegaJamTakeState GetCurrentTake() const { return CurrentTake; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	EMegaJamAspectMode GetAspectMode() const { return CurrentAspect; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Chain")
	int32 GetSpliceChain() const { return SpliceChain; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Chain")
	float GetAspectHoldTimer() const { return AspectHoldTimer; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Chain")
	bool IsShotStale() const { return bIsShotStale; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Chain")
	float GetMatchCutFlashTimer() const { return MatchCutFlashTimer; }

	/** Called by AMegaJamTrackGenerator when the pawn collects an active-take Film Frame pickup. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Chain")
	void NotifyFilmFrameCollected();

	// ---- Director's Cut actions ----
	/** Toggles the take, adds CutSpeedImpulse and fires OnTakeCut. Returns false when on cooldown or not in a live run. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	bool TriggerHardCut();

	/** Toggles between Wide_21_9 and Box_4_3. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void ToggleAspect();

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetAspectMode(EMegaJamAspectMode NewAspect);

	/** Teleports back to the spawn transform and clears crash / velocity / take / aspect. Called by the GameMode on ResetRun. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	virtual void ResetForRun();

	// ---- Delegates ----
	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnTakeCut OnTakeCut;

	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnAspectChanged OnAspectChanged;

	// ---- Reactive events ----
	/** Fired when the near-miss detector first overlaps an obstacle. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnObstacleGrazed(AActor* GrazedActor);

	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnCrashed(AActor* HitActor);

	/** Blueprint hook (camera punch, audio sting). Also broadcast through OnTakeCut. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnTakeCutEvent(EMegaJamTakeState NewTake);

	/** Blueprint hook (letterbox widget). Also broadcast through OnAspectChanged. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnAspectChangedEvent(EMegaJamAspectMode NewAspect);

protected:
	virtual void BeginPlay() override;

	/** Integrates velocity and sweeps the pawn. Triggers HandleCrash on a fast blocking hit. */
	virtual void StepPhysics(float DeltaSeconds);

	virtual void HandleCrash(AActor* HitActor);

	/** Squeezes the pawn into the active aspect bounds and removes velocity driving out of them. */
	void ApplyAspectClamp(float DeltaSeconds);

	void PushTakeAndAspectToSubsystem() const;
	void PushAudioParameters() const;
	void TriggerAudioEvent(FName ParameterName) const;
	bool TryTriggerMatchCut();

	// ---- Input hooks ----
	virtual void Input_Steer(const FInputActionValue& Value);
	/** Started: hard cut, or restart the run when crashed / completed. */
	virtual void Input_Boost(const FInputActionValue& Value);
	/** Started: aspect toggle. */
	virtual void Input_Brake(const FInputActionValue& Value);

	virtual void Input_SteerReleased();

	UFUNCTION()
	void OnGrazeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float FlowRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	bool bCrashed = false;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	EMegaJamTakeState CurrentTake = EMegaJamTakeState::StudioTake;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	EMegaJamAspectMode CurrentAspect = EMegaJamAspectMode::Wide_21_9;

	/** Active Match-Cut chain multiplier (1 to 8). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Chain")
	int32 SpliceChain = 1;

	/** Seconds spent continuously in the current EMegaJamAspectMode without switching. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Chain")
	float AspectHoldTimer = 0.0f;

	/** True when AspectHoldTimer exceeds StaleShotThreshold (5.5s). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Chain")
	bool bIsShotStale = false;

	FVector2D SteerInput = FVector2D::ZeroVector;

	/** Current decaying additive FOV punch from TriggerHardCut. */
	float CurrentFOVPunch = 0.0f;

	/** Current interpolated camera roll angle (degrees). */
	float CurrentCameraRoll = 0.0f;

	/** World time of the last hard cut. */
	double LastCutTime = -1000.0;

	/** World time of the most recent obstacle graze or Film Frame pickup. */
	double LastGrazeOrPickupTime = -1000.0;

	/** Accumulator for decaying SpliceChain every StaleDecayInterval seconds while stale. */
	float StaleDecayAccum = 0.0f;

	/**Remaining HUD flash timer after a Match Cut or Film Frame pickup. */
	float MatchCutFlashTimer = 0.0f;

	/** Ensures the celebratory victory sting triggers once when entering Completed state. */
	bool bTriggeredVictorySting = false;

	/** Transform captured in BeginPlay, restored by ResetForRun. */
	FTransform SpawnTransform;

	/**
	 * Graze payout times keyed by (component, body index). Instanced meshes share one actor, so the key has to
	 * be per instance. A retrigger delay stops take-cut spamming from farming the multiplier.
	 */
	TMap<TTuple<TWeakObjectPtr<UPrimitiveComponent>, int32>, double> GrazeTimes;

	/** Seconds before the same obstacle instance can pay out another graze. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float GrazeRetriggerDelay = 1.5f;
};
