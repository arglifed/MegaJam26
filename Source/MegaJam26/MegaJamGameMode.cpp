// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamGameMode.h"

#include "MegaJamFlowPawn.h"
#include "MegaJamFlowSubsystem.h"
#include "MegaJamPlayerController.h"
#include "MegaJamTrackGenerator.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"

AMegaJamGameMode::AMegaJamGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	DefaultPawnClass = AMegaJamFlowPawn::StaticClass();
	PlayerControllerClass = AMegaJamPlayerController::StaticClass();
	TrackGeneratorClass = AMegaJamTrackGenerator::StaticClass();
}

void AMegaJamGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		if (!DefaultFlowMPC && FPackageName::DoesPackageExist(TEXT("/Game/MegaJam26/VFX/MPC_FlowState")))
		{
			DefaultFlowMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/MegaJam26/VFX/MPC_FlowState.MPC_FlowState"));
		}

		FlowSubsystem->SetFlowMPC(DefaultFlowMPC);
		FlowSubsystem->SetCurrentMultiplier(CurrentMultiplier);
		FlowSubsystem->SetTotalFramesCollected(0);
		FlowSubsystem->SetHighestSpliceChain(1);
		FlowSubsystem->SetCurrentCheckpointTime(0.0f);
	}

	EnsureTrackGeneratorSpawned();

	if (bStartOnTitleScreen)
	{
		RunTime = 0.0f;
		Score = 0.0f;
		CurrentMultiplier = 1.0f;
		CurrentTakeNumber = 1;
		TotalFramesCollected = 0;
		HighestSpliceChain = 1;
		CurrentCheckpointTime = 0.0f;
		CurrentCheckpointScene = EMegaJamFilmScene::Soundstage;
		SetRunState(EMegaJamRunState::TitleScreen);
	}
	else if (bAutoStartRun)
	{
		StartProductionFromTitle();
	}
}

void AMegaJamGameMode::EnsureTrackGeneratorSpawned()
{
	UWorld* World = GetWorld();
	if (World && bAutoSpawnTrackGenerator)
	{
		TArray<AActor*> Generators;
		UGameplayStatics::GetAllActorsOfClass(World, AMegaJamTrackGenerator::StaticClass(), Generators);
		if (Generators.IsEmpty())
		{
			UClass* GeneratorClass = TrackGeneratorClass ? TrackGeneratorClass.Get() : AMegaJamTrackGenerator::StaticClass();
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<AMegaJamTrackGenerator>(GeneratorClass, FTransform::Identity, SpawnParams);
		}
	}
}

void AMegaJamGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsPlaying())
	{
		return;
	}

	UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>();
	const float FlowRatio = FlowSubsystem ? FlowSubsystem->FlowRatio : 0.0f;

	RunTime += DeltaSeconds;
	EvaluateSceneCheckpoints();

	// Multiplier decays back toward 1 unless refreshed by grazes.
	CurrentMultiplier = FMath::FInterpConstantTo(CurrentMultiplier, 1.0f, DeltaSeconds, MultiplierDecayPerSecond);

	// Score = base rate * multiplier * how fast we are going relative to terminal velocity.
	Score += ScorePerSecond * CurrentMultiplier * FlowRatio * DeltaSeconds;

	if (FlowSubsystem)
	{
		FlowSubsystem->SetCurrentMultiplier(CurrentMultiplier);
	}

	if (RunDuration > 0.0f && RunTime >= RunDuration)
	{
		RunTime = RunDuration;
		CompleteRun();
	}
}

void AMegaJamGameMode::EvaluateSceneCheckpoints()
{
	if (RunTime >= 115.0f && CurrentCheckpointTime < 115.0f)
	{
		NotifyCheckpointReached(EMegaJamFilmScene::FinalCut, 115.0f);
	}
	else if (RunTime >= 75.0f && CurrentCheckpointTime < 75.0f)
	{
		NotifyCheckpointReached(EMegaJamFilmScene::CuttingRoom, 75.0f);
	}
	else if (RunTime >= 35.0f && CurrentCheckpointTime < 35.0f)
	{
		NotifyCheckpointReached(EMegaJamFilmScene::OverpassNight, 35.0f);
	}
}

void AMegaJamGameMode::NotifyCheckpointReached(EMegaJamFilmScene NewScene, float CheckpointTime)
{
	if (CheckpointTime <= CurrentCheckpointTime && NewScene == CurrentCheckpointScene)
	{
		return;
	}

	CurrentCheckpointScene = NewScene;
	CurrentCheckpointTime = FMath::Max(0.0f, CheckpointTime);

	if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetCurrentCheckpointTime(CurrentCheckpointTime);
	}
}

void AMegaJamGameMode::DebugSkipToNextAct()
{
#if !UE_BUILD_SHIPPING
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(CrashRestartTimer);

	if (RunState == EMegaJamRunState::TitleScreen || RunState == EMegaJamRunState::Completed)
	{
		StartProductionFromTitle();
		return;
	}

	float TargetTime = 0.0f;
	EMegaJamFilmScene TargetScene = EMegaJamFilmScene::Soundstage;
	FString MilestoneName = TEXT("ACT I // 00:00 (SOUNDSTAGE)");

	if (RunTime < 35.0f)
	{
		TargetTime = 35.0f;
		TargetScene = EMegaJamFilmScene::OverpassNight;
		MilestoneName = TEXT("ACT II // 00:35 (OVERPASS NIGHT)");
	}
	else if (RunTime < 75.0f)
	{
		TargetTime = 75.0f;
		TargetScene = EMegaJamFilmScene::CuttingRoom;
		MilestoneName = TEXT("ACT III // 01:15 (CUTTING ROOM)");
	}
	else if (RunTime < 115.0f)
	{
		TargetTime = 115.0f;
		TargetScene = EMegaJamFilmScene::FinalCut;
		MilestoneName = TEXT("ACT IV // 01:55 (THE FINAL CUT)");
	}
	else if (RunTime < 147.0f)
	{
		TargetTime = 147.0f;
		TargetScene = EMegaJamFilmScene::FinalCut;
		MilestoneName = TEXT("PRE-WRAP GATE // 02:27 (FINAL ARCH)");
	}

	RunTime = TargetTime;
	CurrentCheckpointTime = (TargetTime >= 115.0f) ? 115.0f : TargetTime;
	CurrentCheckpointScene = TargetScene;

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ClearCrashStateForDebugSkip();
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetCurrentCheckpointTime(CurrentCheckpointTime);
	}

	SetRunState(EMegaJamRunState::Playing);

	if (AMegaJamTrackGenerator* Generator = Cast<AMegaJamTrackGenerator>(
		UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
	{
		Generator->RebuildTrack();
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(202603, 2.5f, FColor::Green,
			FString::Printf(TEXT("DEV HOTKEY [F7]: SKIPPED TO %s"), *MilestoneName), true, FVector2D(1.3f, 1.3f));
	}
#endif
}

void AMegaJamGameMode::StartProductionFromTitle()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(CrashRestartTimer);
	}

	CurrentTakeNumber = 1;
	Score = 0.0f;
	CurrentMultiplier = 1.0f;
	TotalFramesCollected = 0;
	HighestSpliceChain = 1;
	CurrentCheckpointTime = 0.0f;
	CurrentCheckpointScene = EMegaJamFilmScene::Soundstage;
	RunTime = 0.0f;

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
	}

	if (World)
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetFlowRatio(0.0f);
			FlowSubsystem->SetCurrentMultiplier(1.0f);
			FlowSubsystem->SetSpliceChain(1);
			FlowSubsystem->SetAspectHoldTimer(0.0f);
			FlowSubsystem->SetIsShotStale(false);
			FlowSubsystem->SetAirExposureRatio(0.0f);
			FlowSubsystem->SetIsOverexposed(false);
			FlowSubsystem->SetDropCutBannerTimer(0.0f);
			FlowSubsystem->SetTotalFramesCollected(0);
			FlowSubsystem->SetHighestSpliceChain(1);
			FlowSubsystem->SetCurrentCheckpointTime(0.0f);
		}
	}

	if (const AMegaJamFlowPawn* ClapperPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		ClapperPawn->PlayRunStartSound();
	}
	OnClapperboard(CurrentTakeNumber);
	SetRunState(EMegaJamRunState::PreRun);
	StartRun();
}

void AMegaJamGameMode::RestartFullProduction()
{
	StartProductionFromTitle();
}

void AMegaJamGameMode::RetakeCurrentAct()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(CrashRestartTimer);
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);

	// Rewind RunTime to CurrentCheckpointTime without incrementing CurrentTakeNumber
	RunTime = CurrentCheckpointTime;
	CurrentMultiplier = 1.0f;

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(0.0f);
		FlowSubsystem->SetCurrentMultiplier(1.0f);
		FlowSubsystem->SetSpliceChain(1);
		FlowSubsystem->SetAspectHoldTimer(0.0f);
		FlowSubsystem->SetIsShotStale(false);
		FlowSubsystem->SetAirExposureRatio(0.0f);
		FlowSubsystem->SetIsOverexposed(false);
		FlowSubsystem->SetDropCutBannerTimer(0.0f);
	}

	if (const AMegaJamFlowPawn* ClapperPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		ClapperPawn->PlayRunStartSound();
	}
	OnClapperboard(CurrentTakeNumber);
	SetRunState(EMegaJamRunState::PreRun);
	StartRun();
}

void AMegaJamGameMode::WrapToTitleSlate()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(CrashRestartTimer);
	}
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);

	RunTime = 0.0f;
	Score = 0.0f;
	CurrentMultiplier = 1.0f;
	CurrentTakeNumber = 1;
	TotalFramesCollected = 0;
	HighestSpliceChain = 1;
	CurrentCheckpointTime = 0.0f;
	CurrentCheckpointScene = EMegaJamFilmScene::Soundstage;

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
	}

	if (World)
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetFlowRatio(0.0f);
			FlowSubsystem->SetCurrentMultiplier(1.0f);
			FlowSubsystem->SetSpliceChain(1);
			FlowSubsystem->SetAspectHoldTimer(0.0f);
			FlowSubsystem->SetIsShotStale(false);
			FlowSubsystem->SetAirExposureRatio(0.0f);
			FlowSubsystem->SetIsOverexposed(false);
			FlowSubsystem->SetDropCutBannerTimer(0.0f);
			FlowSubsystem->SetTotalFramesCollected(0);
			FlowSubsystem->SetHighestSpliceChain(1);
			FlowSubsystem->SetCurrentCheckpointTime(0.0f);
		}
	}

	SetRunState(EMegaJamRunState::TitleScreen);
}

void AMegaJamGameMode::StartRun()
{
	RunTime = CurrentCheckpointTime;
	CurrentMultiplier = 1.0f;

	EnsureTrackGeneratorSpawned();

	SetRunState(EMegaJamRunState::Playing);
}

void AMegaJamGameMode::ResetRun()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(CrashRestartTimer);
	++CurrentTakeNumber;

	// Rewind RunTime to CurrentCheckpointTime so the player resumes at the start of their current Act!
	RunTime = CurrentCheckpointTime;
	CurrentMultiplier = 1.0f;

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(0.0f);
		FlowSubsystem->SetCurrentMultiplier(1.0f);
		FlowSubsystem->SetSpliceChain(1);
		FlowSubsystem->SetAspectHoldTimer(0.0f);
		FlowSubsystem->SetIsShotStale(false);
		FlowSubsystem->SetAirExposureRatio(0.0f);
		FlowSubsystem->SetIsOverexposed(false);
		FlowSubsystem->SetDropCutBannerTimer(0.0f);
	}

	if (const AMegaJamFlowPawn* ClapperPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		ClapperPawn->PlayRunStartSound();
	}
	OnClapperboard(CurrentTakeNumber);

	SetRunState(EMegaJamRunState::PreRun);
	StartRun();
}

FString AMegaJamGameMode::GetFormattedTimecode() const
{
	constexpr int32 FramesPerSecond = 24;
	const int32 TotalFrames = FMath::Max(0, FMath::FloorToInt(RunTime * FramesPerSecond));
	const int32 Frames = TotalFrames % FramesPerSecond;
	const int32 TotalSeconds = TotalFrames / FramesPerSecond;
	return FString::Printf(TEXT("%02d:%02d:%02d"), TotalSeconds / 60, TotalSeconds % 60, Frames);
}

void AMegaJamGameMode::CompleteRun()
{
	if (IsPlaying())
	{
		SetRunState(EMegaJamRunState::Completed);
	}
}

void AMegaJamGameMode::NotifyCrash(AActor* HitActor)
{
	if (IsPlaying())
	{
		CurrentMultiplier = 1.0f;
		if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetCurrentMultiplier(CurrentMultiplier);
			FlowSubsystem->SetFlowRatio(0.0f);
		}
		SetRunState(EMegaJamRunState::Crashed);
	}
}

void AMegaJamGameMode::RegisterGraze(AActor* GrazedActor)
{
	if (!IsPlaying())
	{
		return;
	}

	CurrentMultiplier = FMath::Min(CurrentMultiplier + GrazeMultiplierBoost, MaxMultiplier);
	if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetCurrentMultiplier(CurrentMultiplier);
	}
	OnGrazeRegistered(GrazedActor, CurrentMultiplier);
}

void AMegaJamGameMode::AddBonusScore(float BonusAmount, bool bBoostMultiplier)
{
	if (!IsPlaying())
	{
		return;
	}

	Score += FMath::Max(0.0f, BonusAmount);
	if (bBoostMultiplier)
	{
		CurrentMultiplier = FMath::Min(CurrentMultiplier + GrazeMultiplierBoost, MaxMultiplier);
		if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetCurrentMultiplier(CurrentMultiplier);
		}
	}
}

void AMegaJamGameMode::RegisterFilmFrameCollected(int32 CurrentChain)
{
	if (!IsPlaying())
	{
		return;
	}

	++TotalFramesCollected;
	HighestSpliceChain = FMath::Max(HighestSpliceChain, FMath::Clamp(CurrentChain, 1, 8));

	if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetTotalFramesCollected(TotalFramesCollected);
		FlowSubsystem->SetHighestSpliceChain(HighestSpliceChain);
	}
}

void AMegaJamGameMode::NotifySpliceChainUpdated(int32 CurrentChain)
{
	if (!IsPlaying())
	{
		return;
	}

	HighestSpliceChain = FMath::Max(HighestSpliceChain, FMath::Clamp(CurrentChain, 1, 8));
	if (UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetHighestSpliceChain(HighestSpliceChain);
	}
}

void AMegaJamGameMode::SetRunState(EMegaJamRunState NewState)
{
	if (RunState == NewState)
	{
		return;
	}

	const EMegaJamRunState OldState = RunState;
	RunState = NewState;

	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		if (NewState == EMegaJamRunState::Crashed)
		{
			TimerManager.SetTimer(CrashRestartTimer, this, &AMegaJamGameMode::ResetRun, FMath::Max(CrashRestartDelay, KINDA_SMALL_NUMBER), false);
		}
		else
		{
			TimerManager.ClearTimer(CrashRestartTimer);
		}
	}

	OnRunStateChanged.Broadcast(NewState, OldState);
	OnRunStateChangedEvent(NewState, OldState);
}
