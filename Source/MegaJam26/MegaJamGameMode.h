// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MegaJamGameMode.generated.h"

class UMaterialParameterCollection;

UENUM(BlueprintType)
enum class EMegaJamRunState : uint8
{
	PreRun,
	ActiveFlow,
	Crashed,
	Completed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRunStateChanged, EMegaJamRunState, NewState, EMegaJamRunState, OldState);

/** Owns run state, run timer and score / multiplier math. */
UCLASS()
class MEGAJAM26_API AMegaJamGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMegaJamGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Run state ----
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void StartRun();

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void CompleteRun();

	/** Called by the Pawn when it crashes. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void NotifyCrash(AActor* HitActor);

	/** Called by the Pawn on a near-miss. Boosts the multiplier. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void RegisterGraze(AActor* GrazedActor);

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	EMegaJamRunState GetRunState() const { return RunState; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	float GetRunTime() const { return RunTime; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	float GetScore() const { return Score; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	float GetCurrentMultiplier() const { return CurrentMultiplier; }

	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnRunStateChanged OnRunStateChanged;

	/** Reactive hooks for Blueprint. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnRunStateChangedEvent(EMegaJamRunState NewState, EMegaJamRunState OldState);

	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnGrazeRegistered(AActor* GrazedActor, float NewMultiplier);

	// ---- Shaders ----
	/** Global MPC pushed to the Flow Subsystem in BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Shaders")
	TObjectPtr<UMaterialParameterCollection> DefaultFlowMPC;

	// ---- Tunables ----
	/** Start the run automatically in BeginPlay (jam convenience). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	bool bAutoStartRun = true;

	/** Seconds until the run completes. 0 = endless. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	float RunDuration = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Score")
	float ScorePerSecond = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Score")
	float MaxMultiplier = 8.0f;

	/** Multiplier added per graze. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Score")
	float GrazeMultiplierBoost = 0.5f;

	/** Multiplier lost per second back toward 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Score")
	float MultiplierDecayPerSecond = 0.25f;

protected:
	void SetRunState(EMegaJamRunState NewState);

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	EMegaJamRunState RunState = EMegaJamRunState::PreRun;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float RunTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Score")
	float Score = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Score")
	float CurrentMultiplier = 1.0f;
};
