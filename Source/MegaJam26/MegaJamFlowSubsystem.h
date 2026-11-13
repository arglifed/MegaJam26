// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MegaJamFlowSubsystem.generated.h"

class UMaterialParameterCollection;

/** Fired every tick with the current flow values. Bind in Blueprint (HUD, MetaSound parameter drivers, etc). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnFlowStateUpdated, float, FlowRatio, float, CurrentMultiplier, float, AudioEnvelopeValue);

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

	/** True when AirExposureTimer exceeds 3.2s in Box_4_3 high altitude, draining Flow and speed. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	bool bIsOverexposed = false;

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
