// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamFlowSubsystem.h"

#include "MegaJamFlowPawn.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"

void UMegaJamFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadBoxOfficeRecords();
}

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

	const float SafeDelta = FMath::Max(0.0f, DeltaTime);
	DropCutBannerTimer = FMath::Max(0.0f, DropCutBannerTimer - SafeDelta);
	OnBeatFlashTimer = FMath::Max(0.0f, OnBeatFlashTimer - SafeDelta);

	float PitchMult = 1.0f;
	if (AMegaJamFlowPawn* Pawn = GetMegaJamPawn())
	{
		PitchMult = FMath::IsFinite(Pawn->CurrentMusicPitch) ? FMath::Clamp(Pawn->CurrentMusicPitch, 0.01f, 4.0f) : 1.0f;
	}

	CurrentAudioTime += SafeDelta * PitchMult;
	BeatAccumulator += SafeDelta * PitchMult;

	const float SafeBPM = FMath::Max(1.0f, CurrentTrackBPM);
	const float BeatInterval = 60.0f / SafeBPM;
	BeatPhase = FMath::Fmod(BeatAccumulator, BeatInterval) / BeatInterval;

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

void UMegaJamFlowSubsystem::SetCurrentTrackBPM(float NewBPM)
{
	CurrentTrackBPM = FMath::Max(1.0f, NewBPM);
}

void UMegaJamFlowSubsystem::SetPrimaryAudioComponent(UAudioComponent* InAudioComp)
{
	PrimaryAudioComponent = InAudioComp;
}

AMegaJamFlowPawn* UMegaJamFlowSubsystem::GetMegaJamPawn() const
{
	UWorld* World = GetWorld();
	return World ? Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
}

bool UMegaJamFlowSubsystem::IsScoreDrivingAudio() const
{
	const AMegaJamFlowPawn* Pawn = GetMegaJamPawn();
	return Pawn && Pawn->IsScoreDrivingAudio();
}

void UMegaJamFlowSubsystem::SeekMusicToTime(float TimeSeconds)
{
	if (IsScoreDrivingAudio())
	{
		TimeSeconds = 0.0f;
	}

	const float SafeTime = FMath::Max(0.0f, TimeSeconds);
	CurrentAudioTime = SafeTime;
	BeatAccumulator = SafeTime;

	const float SafeBPM = FMath::Max(1.0f, CurrentTrackBPM);
	const float BeatInterval = 60.0f / SafeBPM;
	BeatPhase = FMath::Fmod(BeatAccumulator, BeatInterval) / BeatInterval;

	if (!IsScoreDrivingAudio() && PrimaryAudioComponent && PrimaryAudioComponent->Sound)
	{
		const float Duration = PrimaryAudioComponent->Sound->GetDuration();
		const float SeekOffset = (Duration > KINDA_SMALL_NUMBER && Duration < INDEFINITELY_LOOPING_DURATION)
			? FMath::Fmod(SafeTime, Duration)
			: SafeTime;
		PrimaryAudioComponent->Play(SeekOffset);
	}
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
	// S-Rank: Time <= SRankTime AND Moves >= SRankMoves
	// A-Rank: Time <= ARankTime AND Moves >= ARankMoves
	// B-Rank: Time <= BRankTime
	// C-Rank: Time > BRankTime (and <= CRankTime)
	// D-Rank: Time > CRankTime
	if (Time <= GradeThresholds.SRankTime && Moves >= GradeThresholds.SRankMoves)
	{
		Metrics.CalculatedGrade = TEXT("S");
	}
	else if (Time <= GradeThresholds.ARankTime && Moves >= GradeThresholds.ARankMoves)
	{
		Metrics.CalculatedGrade = TEXT("A");
	}
	else if (Time <= GradeThresholds.BRankTime)
	{
		Metrics.CalculatedGrade = TEXT("B");
	}
	else if (Time > GradeThresholds.CRankTime)
	{
		Metrics.CalculatedGrade = TEXT("D");
	}
	else
	{
		Metrics.CalculatedGrade = TEXT("C");
	}
}

void UMegaJamFlowSubsystem::LoadBoxOfficeRecords()
{
	TopBoxOfficeRuns.Empty();
	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("DirectorCutRecords.json");
	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *FilePath) || JsonContent.IsEmpty())
	{
		return;
	}

	TArray<FString> Lines;
	JsonContent.ParseIntoArrayLines(Lines, true);
	for (const FString& RawLine : Lines)
	{
		const FString Line = RawLine.TrimStartAndEnd();
		if (!Line.StartsWith(TEXT("{")) || !Line.Contains(TEXT("\"gross\"")))
		{
			continue;
		}

		auto ParseIntField = [&Line](const TCHAR* Key) -> int32
		{
			const int32 KeyPos = Line.Find(Key);
			if (KeyPos == INDEX_NONE) { return 0; }
			const int32 ColonPos = Line.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, KeyPos);
			if (ColonPos == INDEX_NONE) { return 0; }
			return FCString::Atoi(*Line.Mid(ColonPos + 1).TrimStartAndEnd());
		};

		auto ParseFloatField = [&Line](const TCHAR* Key) -> float
		{
			const int32 KeyPos = Line.Find(Key);
			if (KeyPos == INDEX_NONE) { return 0.0f; }
			const int32 ColonPos = Line.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, KeyPos);
			if (ColonPos == INDEX_NONE) { return 0.0f; }
			return FCString::Atof(*Line.Mid(ColonPos + 1).TrimStartAndEnd());
		};

		auto ParseStringField = [&Line](const TCHAR* Key) -> FString
		{
			const int32 KeyPos = Line.Find(Key);
			if (KeyPos == INDEX_NONE) { return TEXT("C"); }
			const int32 ColonPos = Line.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, KeyPos);
			if (ColonPos == INDEX_NONE) { return TEXT("C"); }
			const int32 QuoteStart = Line.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, ColonPos + 1);
			if (QuoteStart == INDEX_NONE) { return TEXT("C"); }
			const int32 QuoteEnd = Line.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, QuoteStart + 1);
			if (QuoteEnd == INDEX_NONE || QuoteEnd <= QuoteStart + 1) { return TEXT("C"); }
			return Line.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		};

		FBoxOfficeRecordEntry Entry;
		Entry.BoxOfficeGross = ParseIntField(TEXT("\"gross\""));
		Entry.FinalScore = ParseIntField(TEXT("\"score\""));
		Entry.CompletionTimeSeconds = ParseFloatField(TEXT("\"time\""));
		Entry.OverallGrade = ParseStringField(TEXT("\"grade\""));
		if (Entry.BoxOfficeGross > 0)
		{
			TopBoxOfficeRuns.Add(Entry);
		}
	}

	TopBoxOfficeRuns.Sort([](const FBoxOfficeRecordEntry& A, const FBoxOfficeRecordEntry& B)
	{
		if (A.BoxOfficeGross != B.BoxOfficeGross)
		{
			return A.BoxOfficeGross > B.BoxOfficeGross;
		}
		return A.FinalScore > B.FinalScore;
	});
	if (TopBoxOfficeRuns.Num() > 3)
	{
		TopBoxOfficeRuns.SetNum(3);
	}

	if (TopBoxOfficeRuns.Num() > 0)
	{
		BestBoxOfficeGross = TopBoxOfficeRuns[0].BoxOfficeGross;
		BestOverallGrade = TopBoxOfficeRuns[0].OverallGrade;
		BestScore = TopBoxOfficeRuns[0].FinalScore;
		BestCompletionTimeSeconds = TopBoxOfficeRuns[0].CompletionTimeSeconds;
	}
}

void UMegaJamFlowSubsystem::SaveBoxOfficeRecords() const
{
	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("DirectorCutRecords.json");
	FString OutJson = TEXT("{\n  \"topRuns\": [\n");
	for (int32 Idx = 0; Idx < TopBoxOfficeRuns.Num(); ++Idx)
	{
		const FBoxOfficeRecordEntry& Entry = TopBoxOfficeRuns[Idx];
		const FString SafeGrade = Entry.OverallGrade.IsEmpty() ? TEXT("C") : Entry.OverallGrade;
		OutJson += FString::Printf(
			TEXT("    { \"gross\": %d, \"score\": %d, \"time\": %.2f, \"grade\": \"%s\" }%s\n"),
			Entry.BoxOfficeGross,
			Entry.FinalScore,
			Entry.CompletionTimeSeconds,
			*SafeGrade,
			(Idx + 1 < TopBoxOfficeRuns.Num()) ? TEXT(",") : TEXT(""));
	}
	OutJson += TEXT("  ]\n}\n");
	FFileHelper::SaveStringToFile(OutJson, *FilePath);
}

bool UMegaJamFlowSubsystem::RecordCompletedCampaignRun(int32 Gross, int32 FinalScore, float CompletionTimeSeconds)
{
	if (TopBoxOfficeRuns.Num() == 0 && BestBoxOfficeGross == 0)
	{
		LoadBoxOfficeRecords();
	}

	// Compute overall campaign grade from the 5 scene grades (S=4, A=3, B=2, C=1, D=0)
	int32 TotalGradePoints = 0;
	int32 SceneCount = 0;
	const EMegaJamSceneGenre CampaignGenres[] = {
		EMegaJamSceneGenre::Action,
		EMegaJamSceneGenre::Western,
		EMegaJamSceneGenre::Noir,
		EMegaJamSceneGenre::Chase,
		EMegaJamSceneGenre::Montage
	};
	for (EMegaJamSceneGenre Genre : CampaignGenres)
	{
		if (const FScenePerformanceMetrics* M = SceneMetrics.Find(Genre))
		{
			++SceneCount;
			if (M->CalculatedGrade == TEXT("S"))      { TotalGradePoints += 4; }
			else if (M->CalculatedGrade == TEXT("A")) { TotalGradePoints += 3; }
			else if (M->CalculatedGrade == TEXT("B")) { TotalGradePoints += 2; }
			else if (M->CalculatedGrade == TEXT("C")) { TotalGradePoints += 1; }
		}
	}
	const float AvgPoints = (SceneCount > 0) ? (static_cast<float>(TotalGradePoints) / static_cast<float>(SceneCount)) : 1.0f;
	FString OverallGrade = TEXT("C");
	if (AvgPoints >= 3.4f)      { OverallGrade = TEXT("S"); }
	else if (AvgPoints >= 2.5f) { OverallGrade = TEXT("A"); }
	else if (AvgPoints >= 1.6f) { OverallGrade = TEXT("B"); }
	else if (AvgPoints < 0.7f)  { OverallGrade = TEXT("D"); }

	bIsNewStudioRecord = (Gross > BestBoxOfficeGross);

	FBoxOfficeRecordEntry NewRun;
	NewRun.BoxOfficeGross = FMath::Max(0, Gross);
	NewRun.FinalScore = FMath::Max(0, FinalScore);
	NewRun.CompletionTimeSeconds = FMath::Max(0.0f, CompletionTimeSeconds);
	NewRun.OverallGrade = OverallGrade;

	TopBoxOfficeRuns.Add(NewRun);
	TopBoxOfficeRuns.Sort([](const FBoxOfficeRecordEntry& A, const FBoxOfficeRecordEntry& B)
	{
		if (A.BoxOfficeGross != B.BoxOfficeGross)
		{
			return A.BoxOfficeGross > B.BoxOfficeGross;
		}
		return A.FinalScore > B.FinalScore;
	});
	if (TopBoxOfficeRuns.Num() > 3)
	{
		TopBoxOfficeRuns.SetNum(3);
	}

	if (TopBoxOfficeRuns.Num() > 0)
	{
		BestBoxOfficeGross = TopBoxOfficeRuns[0].BoxOfficeGross;
		BestOverallGrade = TopBoxOfficeRuns[0].OverallGrade;
		BestScore = FMath::Max(BestScore, TopBoxOfficeRuns[0].FinalScore);
		BestCompletionTimeSeconds = TopBoxOfficeRuns[0].CompletionTimeSeconds;
	}

	SaveBoxOfficeRecords();
	return bIsNewStudioRecord;
}

bool UMegaJamFlowSubsystem::IsWithinBeatWindow(float WindowSeconds) const
{
	const float SafeBPM = FMath::Max(1.0f, CurrentTrackBPM);
	const float BeatInterval = 60.0f / SafeBPM;
	const float Remainder = FMath::Fmod(FMath::Max(0.0f, CurrentAudioTime), BeatInterval);
	const float SecondsToNearestBeat = FMath::Min(Remainder, BeatInterval - Remainder);
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
