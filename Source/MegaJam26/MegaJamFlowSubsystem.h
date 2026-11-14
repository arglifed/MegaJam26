// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MegaJamTypes.h"
#include "MegaJamFlowSubsystem.generated.h"

class UMaterialParameterCollection;

/** Fired every tick with the current flow values. Bind in Blueprint (HUD, MetaSound parameter drivers, etc). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnFlowStateUpdated, float, FlowRatio, float, CurrentMultiplier, float, AudioEnvelopeValue);

/** Fired whenever Take A / Take B state swaps so AMegaJamTrackGenerator and obstacles can toggle collision and opacity. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTakeSwapped, bool, bNewTakeState);

/**
 * Central data bridge between gameplay physics, audio stems and shaders.
 *  - The Pawn writes FlowRatio.
 *  - The GameMode writes CurrentMultiplier.
 *  - Blueprint (Audio Synesthesia / MetaSound) writes AudioEnvelopeValue.
 * Every tick the values are written to the global Material Parameter Collection and broadcast.
 */
UCLASS()
class MEGAJAM26_API UMegaJamFlowSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ---- Subsystem / tickable overrides ----
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ---- Shared flow state ----
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetFlowRatio(float NewFlowRatio);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetCurrentMultiplier(float NewMultiplier);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetAudioEnvelopeValue(float NewEnvelopeValue);

	/** 0.0 = StudioTake, 1.0 = DirectorsTake. Written by the Pawn. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetActiveTake(float NewActiveTake);

	/** Sets bIsTakeA_Active (true = Studio Take A, false = Director's Take B) and broadcasts OnTakeSwapped. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetTakeAActive(bool bNewTakeState);

	/** 0.0 = Wide_21_9, 1.0 = Box_4_3. Written by the Pawn. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetAspectMode(float NewAspectMode);

	/** Current Match-Cut splice chain (1..8). Written by the Pawn. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetSpliceChain(int32 NewSpliceChain);

	/** Seconds spent continuously in the current Aspect Mode without switching. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetAspectHoldTimer(float NewHoldTimer);

	/** True when the player has stayed in one Aspect Mode beyond the stale threshold. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetIsShotStale(bool bNewShotStale);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetTotalFramesCollected(int32 NewTotalFrames);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetHighestSpliceChain(int32 NewHighestChain);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetCurrentCheckpointTime(float NewCheckpointTime);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetAirExposureRatio(float NewExposureRatio);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetIsOverexposed(bool bNewOverexposed);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetDropCutBannerTimer(float NewTimer);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetDistanceMeters(float NewDistanceMeters);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetTargetDistanceMeters(float NewTargetDistanceMeters);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetParTimeSeconds(float NewParTimeSeconds);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetFilmMeter(float NewFilmMeter);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetHazardTimeScale(float NewHazardTimeScale);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetOnBeatFlashTimer(float NewTimer);

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetActiveGenre(EMegaJamSceneGenre NewGenre);

	/** Returns true when the continuous 128 BPM beat clock is within +/- WindowSeconds of a beat boundary. */
	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	bool IsWithinBeatWindow(float WindowSeconds = 0.16f) const;

	/** Assign the global Material Parameter Collection that receives the flow values each tick. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Shaders")
	void SetFlowMPC(UMaterialParameterCollection* InFlowMPC);

	/** Normalized 0..1 (current velocity / terminal velocity). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float FlowRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float CurrentMultiplier = 1.0f;

	/** Written from Blueprint (Audio Synesthesia envelope follower or MetaSound output). */
	UPROPERTY(BlueprintReadWrite, Category="MegaJam|Flow")
	float AudioEnvelopeValue = 0.0f;

	/** 0.0 (StudioTake) or 1.0 (DirectorsTake). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float ActiveTake = 0.0f;

	/** True when Studio Take A is active; false when Director's Take B is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Flow")
	bool bIsTakeA_Active = true;

	/** Broadcast whenever bIsTakeA_Active swaps so track generator and obstacles toggle collision and opacity. */
	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnTakeSwapped OnTakeSwapped;

	/** 0.0 (Wide_21_9) or 1.0 (Box_4_3). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float AspectMode = 0.0f;

	/** Active Match-Cut chain multiplier (1 to 8). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	int32 SpliceChain = 1;

	/** Seconds spent continuously in the current EMegaJamAspectMode. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float AspectHoldTimer = 0.0f;

	/** True when AspectHoldTimer exceeds 5.5s and SpliceChain is decaying. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	bool bIsShotStale = false;

	/** Total Film Frame pickups spliced during the current production run. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	int32 TotalFramesCollected = 0;

	/** Highest Match-Cut SpliceChain achieved during the current production run. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	int32 HighestSpliceChain = 1;

	/** Start time (s) of the latest reached Act/Scene checkpoint. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float CurrentCheckpointTime = 0.0f;

	/** Normalized 0..1 aerial exposure meter while flying high in Box_4_3 mode. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float AirExposureRatio = 0.0f;

	/** True when AirExposureTimer exceeds OverexposureStartSeconds (2.0s) in Box_4_3 high altitude, draining Flow and speed. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	bool bIsOverexposed = false;

	/** Remaining duration (s) for the center-screen Drop-Cut Slam / Overdrive alert banner. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float DropCutBannerTimer = 0.0f;

	/** Distance traveled along the Scene track in meters (0m to 5400m across Scene 01, Scene 02 & Scene 03). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float DistanceMeters = 0.0f;

	/** Target finish distance for the active campaign in meters (5400m for Scene 01 + Scene 02 + Scene 03). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float TargetDistanceMeters = 5400.0f;

	/** Par time in seconds for the active campaign (135.0s for Scene 01 + Scene 02 + Scene 03). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float ParTimeSeconds = 135.0f;

	/** Film Meter (0.0f to 100.0f). Starts at 50.0f, charged in 4:3, spent in 21:9 Bullet-Time Overdrive. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float FilmMeter = 50.0f;

	/** Time dilation multiplier applied to track hazards/animations (1.0f in 4:3, 0.45f in 21:9 Overdrive). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float HazardTimeScale = 1.0f;

	/** Continuous 128.0 BPM beat phase in [0.0f, 1.0f). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float BeatPhase = 0.0f;

	/** Remaining duration (s) for the ★ ON-BEAT CUT! HUD flash. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float OnBeatFlashTimer = 0.0f;

	/** Active scene genre (Scene 01 = Action). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	EMegaJamSceneGenre ActiveGenre = EMegaJamSceneGenre::Action;

	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnFlowStateUpdated OnFlowStateUpdated;

	// ---- MPC parameter names (must exist as scalar parameters in the MPC asset) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Shaders")
	FName FlowRatioParameterName = TEXT("FlowRatio");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Shaders")
	FName MultiplierParameterName = TEXT("Multiplier");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Shaders")
	FName AudioEnvelopeParameterName = TEXT("AudioEnvelope");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Shaders")
	FName ActiveTakeParameterName = TEXT("ActiveTake");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Shaders")
	FName AspectModeParameterName = TEXT("AspectMode");

	UFUNCTION(BlueprintPure, Category="MegaJam|Shaders")
	UMaterialParameterCollection* GetFlowMPC() const { return FlowMPC; }

private:
	void PushToMaterialParameterCollection() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> FlowMPC;
};
