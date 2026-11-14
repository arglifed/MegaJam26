// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamFlowSubsystem.h"

#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

bool UMegaJamFlowSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMegaJamFlowSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMegaJamFlowSubsystem, STATGROUP_Tickables);
}

void UMegaJamFlowSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	DropCutBannerTimer = FMath::Max(0.0f, DropCutBannerTimer - DeltaTime);
	OnBeatFlashTimer = FMath::Max(0.0f, OnBeatFlashTimer - DeltaTime);

	// Continuous 128.0 BPM beat phase in [0.0f, 1.0f)
	constexpr float BeatsPerSecond = 128.0f / 60.0f;
	BeatPhase = FMath::Fmod(BeatPhase + FMath::Max(0.0f, DeltaTime) * BeatsPerSecond, 1.0f);

	PushToMaterialParameterCollection();
	OnFlowStateUpdated.Broadcast(FlowRatio, CurrentMultiplier, AudioEnvelopeValue);
}

void UMegaJamFlowSubsystem::SetFlowRatio(float NewFlowRatio)
{
	FlowRatio = FMath::Clamp(NewFlowRatio, 0.0f, 1.0f);
}

void UMegaJamFlowSubsystem::SetCurrentMultiplier(float NewMultiplier)
{
	CurrentMultiplier = FMath::Max(NewMultiplier, 1.0f);
}

void UMegaJamFlowSubsystem::SetAudioEnvelopeValue(float NewEnvelopeValue)
{
	AudioEnvelopeValue = NewEnvelopeValue;
}

void UMegaJamFlowSubsystem::SetActiveTake(float NewActiveTake)
{
	ActiveTake = FMath::Clamp(NewActiveTake, 0.0f, 1.0f);
	const bool bNewTakeA = (ActiveTake < 0.5f);
	if (bIsTakeA_Active != bNewTakeA)
	{
		bIsTakeA_Active = bNewTakeA;
		OnTakeSwapped.Broadcast(bIsTakeA_Active);
	}
}

void UMegaJamFlowSubsystem::SetTakeAActive(bool bNewTakeState)
{
	const bool bChanged = (bIsTakeA_Active != bNewTakeState);
	bIsTakeA_Active = bNewTakeState;
	ActiveTake = bIsTakeA_Active ? 0.0f : 1.0f;
	if (bChanged)
	{
		OnTakeSwapped.Broadcast(bIsTakeA_Active);
	}
}

void UMegaJamFlowSubsystem::SetAspectMode(float NewAspectMode)
{
	AspectMode = FMath::Clamp(NewAspectMode, 0.0f, 1.0f);
}

void UMegaJamFlowSubsystem::SetSpliceChain(int32 NewSpliceChain)
{
	SpliceChain = FMath::Clamp(NewSpliceChain, 1, 8);
}

void UMegaJamFlowSubsystem::SetAspectHoldTimer(float NewHoldTimer)
{
	AspectHoldTimer = FMath::Max(0.0f, NewHoldTimer);
}

void UMegaJamFlowSubsystem::SetIsShotStale(bool bNewShotStale)
{
	bIsShotStale = bNewShotStale;
}

void UMegaJamFlowSubsystem::SetTotalFramesCollected(int32 NewTotalFrames)
{
	TotalFramesCollected = FMath::Max(0, NewTotalFrames);
}

void UMegaJamFlowSubsystem::SetHighestSpliceChain(int32 NewHighestChain)
{
	HighestSpliceChain = FMath::Clamp(NewHighestChain, 1, 8);
}

void UMegaJamFlowSubsystem::SetCurrentCheckpointTime(float NewCheckpointTime)
{
	CurrentCheckpointTime = FMath::Max(0.0f, NewCheckpointTime);
}

void UMegaJamFlowSubsystem::SetAirExposureRatio(float NewExposureRatio)
{
	AirExposureRatio = FMath::Clamp(NewExposureRatio, 0.0f, 1.0f);
}

void UMegaJamFlowSubsystem::SetIsOverexposed(bool bNewOverexposed)
{
	bIsOverexposed = bNewOverexposed;
}

void UMegaJamFlowSubsystem::SetDropCutBannerTimer(float NewTimer)
{
	DropCutBannerTimer = FMath::Max(0.0f, NewTimer);
}

void UMegaJamFlowSubsystem::SetDistanceMeters(float NewDistanceMeters)
{
	DistanceMeters = FMath::Max(0.0f, NewDistanceMeters);
}

void UMegaJamFlowSubsystem::SetTargetDistanceMeters(float NewTargetDistanceMeters)
{
	TargetDistanceMeters = FMath::Max(1.0f, NewTargetDistanceMeters);
}

void UMegaJamFlowSubsystem::SetParTimeSeconds(float NewParTimeSeconds)
{
	ParTimeSeconds = FMath::Max(1.0f, NewParTimeSeconds);
}

void UMegaJamFlowSubsystem::SetFilmMeter(float NewFilmMeter)
{
	FilmMeter = FMath::Clamp(NewFilmMeter, 0.0f, 100.0f);
}

void UMegaJamFlowSubsystem::SetHazardTimeScale(float NewHazardTimeScale)
{
	HazardTimeScale = FMath::Clamp(NewHazardTimeScale, 0.1f, 2.0f);
}

void UMegaJamFlowSubsystem::SetOnBeatFlashTimer(float NewTimer)
{
	OnBeatFlashTimer = FMath::Max(0.0f, NewTimer);
}

void UMegaJamFlowSubsystem::SetActiveGenre(EMegaJamSceneGenre NewGenre)
{
	ActiveGenre = NewGenre;
}

void UMegaJamFlowSubsystem::ResetSceneMetrics(float StartTimeSeconds)
{
	SceneMetrics.Empty();
	for (const EMegaJamSceneGenre Genre : {
		EMegaJamSceneGenre::Action,
		EMegaJamSceneGenre::Western,
		EMegaJamSceneGenre::Noir,
		EMegaJamSceneGenre::Chase,
		EMegaJamSceneGenre::Montage })
	{
		SceneMetrics.Add(Genre, FScenePerformanceMetrics());
	}
	CurrentSceneStartTime = FMath::Max(0.0f, StartTimeSeconds);
}

void UMegaJamFlowSubsystem::ResetSingleSceneMetrics(EMegaJamSceneGenre Genre, float StartTimeSeconds)
{
	SceneMetrics.FindOrAdd(Genre) = FScenePerformanceMetrics();
	CurrentSceneStartTime = FMath::Max(0.0f, StartTimeSeconds);
}

void UMegaJamFlowSubsystem::RecordSpecialMoveExecution(EMegaJamSceneGenre Genre)
{
	FScenePerformanceMetrics& Metrics = SceneMetrics.FindOrAdd(Genre);
	++Metrics.SpecialMovesExecuted;
}

void UMegaJamFlowSubsystem::RecordFramePickup(EMegaJamSceneGenre Genre)
{
	FScenePerformanceMetrics& Metrics = SceneMetrics.FindOrAdd(Genre);
	++Metrics.FilmFramesCollected;
}

void UMegaJamFlowSubsystem::FinalizeScenePerformance(EMegaJamSceneGenre OutgoingGenre, float CurrentTimeSeconds)
{
	FScenePerformanceMetrics& Metrics = SceneMetrics.FindOrAdd(OutgoingGenre);
	Metrics.TimeSpentSeconds = FMath::Max(0.0f, CurrentTimeSeconds - CurrentSceneStartTime);
	CurrentSceneStartTime = FMath::Max(CurrentSceneStartTime, CurrentTimeSeconds);

	const float Time = Metrics.TimeSpentSeconds;
	const int32 Moves = Metrics.SpecialMovesExecuted;

	// Critic Grading Logic:
	// S-Rank: Time <= 45.0s AND Moves >= 4
	// A-Rank: Time <= 50.0s AND Moves >= 2
	// B-Rank: Time <= 55.0s
	// C-Rank: Time > 55.0s (and <= 65.0s)
	// D-Rank: Time > 65.0s
	if (Time <= 45.0f && Moves >= 4)
	{
		Metrics.CalculatedGrade = TEXT("S");
	}
	else if (Time <= 50.0f && Moves >= 2)
	{
		Metrics.CalculatedGrade = TEXT("A");
	}
	else if (Time <= 55.0f)
	{
		Metrics.CalculatedGrade = TEXT("B");
	}
	else if (Time > 65.0f)
	{
		Metrics.CalculatedGrade = TEXT("D");
	}
	else
	{
		Metrics.CalculatedGrade = TEXT("C");
	}
}

bool UMegaJamFlowSubsystem::IsWithinBeatWindow(float WindowSeconds) const
{
	constexpr float BeatDurationSeconds = 60.0f / 128.0f; // 0.46875s per beat
	const float PhaseDistanceToNearestBeat = FMath::Min(BeatPhase, 1.0f - BeatPhase);
	const float SecondsToNearestBeat = PhaseDistanceToNearestBeat * BeatDurationSeconds;
	return SecondsToNearestBeat <= FMath::Max(0.0f, WindowSeconds);
}

void UMegaJamFlowSubsystem::SetFlowMPC(UMaterialParameterCollection* InFlowMPC)
{
	FlowMPC = InFlowMPC;
}

void UMegaJamFlowSubsystem::PushToMaterialParameterCollection() const
{
	UWorld* World = GetWorld();
	if (!FlowMPC || !World)
	{
		return;
	}

	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(FlowMPC);
	if (!Instance)
	{
		return;
	}

	Instance->SetScalarParameterValue(FlowRatioParameterName, FlowRatio);
	Instance->SetScalarParameterValue(MultiplierParameterName, CurrentMultiplier);
	Instance->SetScalarParameterValue(AudioEnvelopeParameterName, AudioEnvelopeValue);
	Instance->SetScalarParameterValue(ActiveTakeParameterName, ActiveTake);
	Instance->SetScalarParameterValue(AspectModeParameterName, AspectMode);
}
