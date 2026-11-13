// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MegaJamTypes.h"
#include "MegaJamGameMode.generated.h"

class UMaterialParameterCollection;
class AMegaJamTrackGenerator;

UENUM(BlueprintType)
enum class EMegaJamRunState : uint8
{
	TitleScreen,
	PreRun,
	Playing,
	ActiveFlow,
	Crashed,
	Completed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRunStateChanged, EMegaJamRunState, NewState, EMegaJamRunState, OldState);

/** Owns run state, 4-Act scene checkpoints, run timer, and score / multiplier / session stats. */
UCLASS()
class MEGAJAM26_API AMegaJamGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMegaJamGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Returns true when the run state is actively playing (Playing or ActiveFlow). */
	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	bool IsPlaying() const { return RunState == EMegaJamRunState::Playing || RunState == EMegaJamRunState::ActiveFlow; }

	// ---- Run state & Arcade Loop ----
	/** Transitions from TitleScreen into active gameplay (Take 01, 0.0s, Scene 01). */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void StartProductionFromTitle();

	/** Resets the entire production back to Take 01, 0.0s, Scene 01 (called from the Final Cut Wrap-Up screen). */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void RestartFullProduction();

	/** Rewinds cleanly to CurrentCheckpointTime / CurrentCheckpointScene without incrementing CurrentTakeNumber (Pause Menu option). */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void RetakeCurrentAct();

	/** Returns to the Attract-Mode Clapperboard Title Screen (Pause Menu option). */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void WrapToTitleSlate();

	/** Starts or resumes the run from CurrentCheckpointTime. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void StartRun();

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void CompleteRun();

	/**
	 * Clapperboard rewind: bumps CurrentTakeNumber, resets the pawn, and rewinds RunTime to CurrentCheckpointTime.
	 * Called automatically CrashRestartDelay seconds after a crash, or immediately by IA_Boost.
	 */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void ResetRun();

	/** Updates the active Act/Scene checkpoint when the player crosses 35s, 75s, or 115s. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void NotifyCheckpointReached(EMegaJamFilmScene NewScene, float CheckpointTime);

	/** Dev hotkey (F7 in non-shipping builds): skips to the next Act milestone (0s -> 35s -> 75s -> 115s -> 147s). */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Dev")
	void DebugSkipToNextAct();

	/** Current run time as MM:SS:FF at 24 fps. */
	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	FString GetFormattedTimecode() const;

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	int32 GetCurrentTakeNumber() const { return CurrentTakeNumber; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Score")
	int32 GetTotalFramesCollected() const { return TotalFramesCollected; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Score")
	int32 GetHighestSpliceChain() const { return HighestSpliceChain; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	float GetCurrentCheckpointTime() const { return CurrentCheckpointTime; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	EMegaJamFilmScene GetCurrentCheckpointScene() const { return CurrentCheckpointScene; }

	/** Called by the Pawn when it crashes. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void NotifyCrash(AActor* HitActor);

	/** Called by the Pawn on a near-miss. Boosts the multiplier. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void RegisterGraze(AActor* GrazedActor);

	/** Awards an immediate bonus score (e.g. Match Cut or Film Frame pickup) and boosts multiplier. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void AddBonusScore(float BonusAmount, bool bBoostMultiplier = true);

	/** Records a collected Film Frame and updates HighestSpliceChain. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Score")
	void RegisterFilmFrameCollected(int32 CurrentChain);

	/** Updates HighestSpliceChain when the Pawn builds its Match-Cut chain. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Score")
	void NotifySpliceChainUpdated(int32 CurrentChain);

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

	/** Blueprint hook for the clapperboard (title card, audio sting). Fired by ResetRun / StartProductionFromTitle. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnClapperboard(int32 TakeNumber);

	// ---- Shaders ----
	/** Global MPC pushed to the Flow Subsystem in BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Shaders")
	TObjectPtr<UMaterialParameterCollection> DefaultFlowMPC;

	// ---- Tunables ----
	/** Default new level loads to the Attract-Mode Clapperboard Title Screen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	bool bStartOnTitleScreen = true;

	/** Legacy flag: when bStartOnTitleScreen is false, start the run immediately in BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	bool bAutoStartRun = true;

	/** Seconds until the run completes. Default 150 s = the four film acts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	float RunDuration = 150.0f;

	/** Seconds after a crash before the run rewinds automatically to CurrentCheckpointTime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MegaJam|Run")
	float CrashRestartDelay = 0.6f;

	/** Spawn a track generator at the origin when the level has none. */
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
	void EnsureTrackGeneratorSpawned();
	void EvaluateSceneCheckpoints();

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	EMegaJamRunState RunState = EMegaJamRunState::PreRun;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float RunTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Score")
	float Score = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Score")
	float CurrentMultiplier = 1.0f;

	/** Take counter for the film reel. Starts at 1, incremented on every crash rewind. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	int32 CurrentTakeNumber = 1;

	/** Total Film Frames spliced across the current production. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Score")
	int32 TotalFramesCollected = 0;

	/** Highest Match-Cut SpliceChain (1..8) reached during the current production. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Score")
	int32 HighestSpliceChain = 1;

	/** Saved checkpoint time (0s, 35s, 75s, 115s) that crash rewinds restore to. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float CurrentCheckpointTime = 0.0f;

	/** Saved checkpoint scene corresponding to CurrentCheckpointTime. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	EMegaJamFilmScene CurrentCheckpointScene = EMegaJamFilmScene::Soundstage;

	FTimerHandle CrashRestartTimer;
};
