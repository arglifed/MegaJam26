// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MegaJamGameMode.generated.h"

class UMaterialParameterCollection;
class AMegaJamTrackGenerator;

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

	/**
	 * Clapperboard rewind: bumps CurrentTakeNumber, resets the pawn and restarts the run.
	 * Called automatically CrashRestartDelay seconds after a crash, or immediately by IA_Boost.
	 */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void ResetRun();

	/** Current run time as MM:SS:FF at 24 fps. */
	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	FString GetFormattedTimecode() const;

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	int32 GetCurrentTakeNumber() const { return CurrentTakeNumber; }

	/** Called by the Pawn when it crashes. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void NotifyCrash(AActor* HitActor);

	/** Called by the Pawn on a near-miss. Boosts the multiplier. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void RegisterGraze(AActor* GrazedActor);

	/** Awards an immediate bonus score (e.g. Match Cut or Film Frame pickup) and boosts multiplier. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void AddBonusScore(float BonusAmount, bool bBoostMultiplier = true);

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

	/** Blueprint hook for the clapperboard (title card, audio sting). Fired by ResetRun. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnClapperboard(int32 TakeNumber);

	// ---- Shaders ----
	/** Global MPC pushed to the Flow Subsystem in BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Shaders")
	TObjectPtr<UMaterialParameterCollection> DefaultFlowMPC;

	// ---- Tunables ----
	/** Start the run automatically in BeginPlay (jam convenience). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	bool bAutoStartRun = true;

	/** Seconds until the run completes. 0 = endless. Default 150 s = the four film scenes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	float RunDuration = 150.0f;

	/** Seconds after a crash before the run rewinds automatically. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	float CrashRestartDelay = 0.6f;

	/** Spawn a track generator at the origin in StartRun when the level has none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	bool bAutoSpawnTrackGenerator = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	TSubclassOf<AMegaJamTrackGenerator> TrackGeneratorClass;

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

	/** Take counter for the film reel. Starts at 1, incremented by ResetRun. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	int32 CurrentTakeNumber = 1;

	FTimerHandle CrashRestartTimer;
};
