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

	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnFlowStateUpdated OnFlowStateUpdated;

	// ---- MPC parameter names (must exist as scalar parameters in the MPC asset) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Shaders")
	FName FlowRatioParameterName = TEXT("FlowRatio");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Shaders")
	FName MultiplierParameterName = TEXT("Multiplier");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Shaders")
	FName AudioEnvelopeParameterName = TEXT("AudioEnvelope");

	UFUNCTION(BlueprintPure, Category="MegaJam|Shaders")
	UMaterialParameterCollection* GetFlowMPC() const { return FlowMPC; }

private:
	void PushToMaterialParameterCollection() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> FlowMPC;
};
