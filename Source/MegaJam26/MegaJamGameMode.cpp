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
		FlowSubsystem->SetDistanceMeters(0.0f);
		FlowSubsystem->SetTargetDistanceMeters(TargetDistanceMeters);
		FlowSubsystem->SetParTimeSeconds(SceneParTimeSeconds);
		FlowSubsystem->SetFilmMeter(50.0f);
		FlowSubsystem->SetHazardTimeScale(1.0f);
		FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
	}

	EnsureTrackGeneratorSpawned();

	if (bStartOnTitleScreen)
	{
		RunTime = 0.0f;
		DistanceMeters = 0.0f;
		CurrentCheckpointDistanceMeters = 0.0f;
		ActiveSceneGenre = EMegaJamSceneGenre::Action;
		ParTimeBonusAwarded = 0;
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

	// Take Clock runs continuously while playing
	RunTime += DeltaSeconds;

	// Track distance traveled in meters along +X from SceneStartOriginX (0m to 3,600m)
	AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (FlowPawn)
	{
		const float PawnX = static_cast<float>(FlowPawn->GetActorLocation().X);
		DistanceMeters = FMath::Max(0.0f, (PawnX - SceneStartOriginX) / 100.0f);
	}

	// Scene 02 Transition at 1,800m: trigger mid-air Clapperboard Stunt Transition into Western Standoff
	if (DistanceMeters >= 1800.0f && DistanceMeters < 3600.0f && ActiveSceneGenre == EMegaJamSceneGenre::Action)
	{
		ActiveSceneGenre = EMegaJamSceneGenre::Western;
		CurrentCheckpointDistanceMeters = 1800.0f;
		if (FlowSubsystem)
		{
			FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
		}
		if (FlowPawn)
		{
			FlowPawn->TriggerClapperboardTransition(EMegaJamSceneGenre::Western);
		}
	}
	// Scene 03 Transition at 3,600m: trigger mid-air Clapperboard Stunt Transition into Noir (Spy Car Vertigo Dolly-Zoom)
	else if (DistanceMeters >= 3600.0f && DistanceMeters < 5400.0f && ActiveSceneGenre != EMegaJamSceneGenre::Noir)
	{
		ActiveSceneGenre = EMegaJamSceneGenre::Noir;
		CurrentCheckpointDistanceMeters = 3600.0f;
		if (FlowSubsystem)
		{
			FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
		}
		if (FlowPawn)
		{
			FlowPawn->TriggerClapperboardTransition(EMegaJamSceneGenre::Noir);
		}
	}
	// Scene 04 Transition at 5,400m: trigger mid-air Clapperboard Stunt Transition into Chase (Snowboard Comet Smash)
	else if (DistanceMeters >= 5400.0f && DistanceMeters < 7200.0f && ActiveSceneGenre != EMegaJamSceneGenre::Chase)
	{
		ActiveSceneGenre = EMegaJamSceneGenre::Chase;
		CurrentCheckpointDistanceMeters = 5400.0f;
		if (FlowSubsystem)
		{
			FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
		}
		if (FlowPawn)
		{
			FlowPawn->TriggerClapperboardTransition(EMegaJamSceneGenre::Chase);
		}
	}
	// Scene 05 Transition at 7,200m: Cliff-Jump into Hang Glider & Match-Cut Montage Finale
	else if (DistanceMeters >= 7200.0f && ActiveSceneGenre != EMegaJamSceneGenre::Montage)
	{
		ActiveSceneGenre = EMegaJamSceneGenre::Montage;
		CurrentCheckpointDistanceMeters = 7200.0f;
		if (FlowSubsystem)
		{
			FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
		}
		if (FlowPawn)
		{
			FlowPawn->TriggerClapperboardTransition(EMegaJamSceneGenre::Montage);
		}
	}

	// Multiplier decays back toward 1 unless refreshed by grazes.
	CurrentMultiplier = FMath::FInterpConstantTo(CurrentMultiplier, 1.0f, DeltaSeconds, MultiplierDecayPerSecond);

	// Score = base rate * multiplier * how fast we are going relative to terminal velocity.
	Score += ScorePerSecond * CurrentMultiplier * FlowRatio * DeltaSeconds;

	if (FlowSubsystem)
	{
		FlowSubsystem->SetCurrentMultiplier(CurrentMultiplier);
		FlowSubsystem->SetDistanceMeters(DistanceMeters);
	}

	if (TargetDistanceMeters > 0.0f && DistanceMeters >= TargetDistanceMeters)
	{
		DistanceMeters = TargetDistanceMeters;
		if (FlowSubsystem)
		{
			FlowSubsystem->SetDistanceMeters(DistanceMeters);
		}
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
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);

	if (RunState == EMegaJamRunState::TitleScreen || RunState == EMegaJamRunState::Completed)
	{
		StartProductionFromTitle();
		return;
	}

	// Step cleanly across all five acts:
	// < 1750.0f -> warp to 1,760m (Scene 01 -> 02 Western transition)
	// < 3550.0f -> warp to 3,560m (Scene 02 -> 03 Noir transition)
	// < 5350.0f -> warp to 5,360m (Scene 03 -> 04 Chase transition)
	// < 7150.0f -> warp to 7,160m (Scene 04 -> 05 Cliff-Jump Montage transition)
	// >= 7150.0f -> warp to 8,910m (Scene 05 Golden Final Cut Wrap Arch approach)
	FString WarpMsg;
	if (DistanceMeters < 1750.0f)
	{
		DistanceMeters = 1760.0f;
		ActiveSceneGenre = EMegaJamSceneGenre::Action;
		CurrentCheckpointDistanceMeters = 0.0f;
		WarpMsg = TEXT("DEV HOTKEY [F7]: WARPED TO 1,760m (SCENE 01 -> 02 TRANSITION)");
	}
	else if (DistanceMeters < 3550.0f)
	{
		DistanceMeters = 3560.0f;
		ActiveSceneGenre = EMegaJamSceneGenre::Western;
		CurrentCheckpointDistanceMeters = 1800.0f;
		WarpMsg = TEXT("DEV HOTKEY [F7]: WARPED TO 3,560m (SCENE 02 -> 03 NOIR TRANSITION)");
	}
	else if (DistanceMeters < 5350.0f)
	{
		DistanceMeters = 5360.0f;
		ActiveSceneGenre = EMegaJamSceneGenre::Noir;
		CurrentCheckpointDistanceMeters = 3600.0f;
		WarpMsg = TEXT("DEV HOTKEY [F7]: WARPED TO 5,360m (SCENE 03 -> 04 CHASE TRANSITION)");
	}
	else if (DistanceMeters < 7150.0f)
	{
		DistanceMeters = 7160.0f;
		ActiveSceneGenre = EMegaJamSceneGenre::Chase;
		CurrentCheckpointDistanceMeters = 5400.0f;
		WarpMsg = TEXT("DEV HOTKEY [F7]: WARPED TO 7,160m (SCENE 04 -> 05 CLIFF-JUMP TRANSITION)");
	}
	else
	{
		DistanceMeters = 8910.0f;
		ActiveSceneGenre = EMegaJamSceneGenre::Montage;
		CurrentCheckpointDistanceMeters = 7200.0f;
		WarpMsg = TEXT("DEV HOTKEY [F7]: WARPED TO 8,910m (FINAL CUT WRAP ARCH APPROACH)");
	}
	const float TargetWorldX = SceneStartOriginX + DistanceMeters * 100.0f;

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->SetActorLocation(FVector(TargetWorldX, 0.0f, 140.0f), false, nullptr, ETeleportType::TeleportPhysics);
		FlowPawn->ClearCrashStateForDebugSkip();
		FlowPawn->SyncSceneMusicTrack(ActiveSceneGenre);
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetDistanceMeters(DistanceMeters);
		FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
	}

	SetRunState(EMegaJamRunState::Playing);

	if (AMegaJamTrackGenerator* Generator = Cast<AMegaJamTrackGenerator>(
		UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
	{
		Generator->RebuildTrack();
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(202603, 2.5f, FColor::Green, WarpMsg, true, FVector2D(1.3f, 1.3f));
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
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);

	CurrentTakeNumber = 1;
	Score = 0.0f;
	ParTimeBonusAwarded = 0;
	CurrentMultiplier = 1.0f;
	TotalFramesCollected = 0;
	HighestSpliceChain = 1;
	CurrentCheckpointTime = 0.0f;
	CurrentCheckpointScene = EMegaJamFilmScene::Soundstage;
	CurrentCheckpointDistanceMeters = 0.0f;
	ActiveSceneGenre = EMegaJamSceneGenre::Action;
	RunTime = 0.0f;
	DistanceMeters = 0.0f;

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
		SceneStartOriginX = static_cast<float>(FlowPawn->GetActorLocation().X);
	}

	if (World)
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
			FlowSubsystem->SetFlowRatio(0.0f);
			FlowSubsystem->SetCurrentMultiplier(1.0f);
			FlowSubsystem->SetSpliceChain(1);
			FlowSubsystem->SetAspectHoldTimer(0.0f);
			FlowSubsystem->SetIsShotStale(false);
			FlowSubsystem->SetAirExposureRatio(0.0f);
			FlowSubsystem->SetIsOverexposed(false);
			FlowSubsystem->SetDropCutBannerTimer(0.0f);
			FlowSubsystem->SetOnBeatFlashTimer(0.0f);
			FlowSubsystem->SetDistanceMeters(0.0f);
			FlowSubsystem->SetTargetDistanceMeters(TargetDistanceMeters);
			FlowSubsystem->SetParTimeSeconds(SceneParTimeSeconds);
			FlowSubsystem->SetFilmMeter(50.0f);
			FlowSubsystem->SetHazardTimeScale(1.0f);
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

	// Reset cleanly to the current Scene's start distance (0m for Scene 01, 1800m for Scene 02) without incrementing CurrentTakeNumber
	if (CurrentCheckpointDistanceMeters <= 0.0f)
	{
		RunTime = 0.0f;
	}
	DistanceMeters = CurrentCheckpointDistanceMeters;
	ParTimeBonusAwarded = 0;
	CurrentMultiplier = 1.0f;

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
	}

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
		SceneStartOriginX = static_cast<float>(FlowPawn->GetActorLocation().X);
		if (CurrentCheckpointDistanceMeters > 0.0f)
		{
			FlowPawn->SetActorLocation(
				FVector(SceneStartOriginX + CurrentCheckpointDistanceMeters * 100.0f, 0.0f, 140.0f),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		FlowPawn->SyncSceneMusicTrack(ActiveSceneGenre);
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
		FlowSubsystem->SetOnBeatFlashTimer(0.0f);
		FlowSubsystem->SetDistanceMeters(DistanceMeters);
		FlowSubsystem->SetFilmMeter(50.0f);
		FlowSubsystem->SetHazardTimeScale(1.0f);
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
	DistanceMeters = 0.0f;
	ParTimeBonusAwarded = 0;
	Score = 0.0f;
	CurrentMultiplier = 1.0f;
	CurrentTakeNumber = 1;
	TotalFramesCollected = 0;
	HighestSpliceChain = 1;
	CurrentCheckpointTime = 0.0f;
	CurrentCheckpointScene = EMegaJamFilmScene::Soundstage;
	CurrentCheckpointDistanceMeters = 0.0f;
	ActiveSceneGenre = EMegaJamSceneGenre::Action;

	if (World)
	{
		if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
		{
			FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
		}
	}

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
		SceneStartOriginX = static_cast<float>(FlowPawn->GetActorLocation().X);
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
			FlowSubsystem->SetOnBeatFlashTimer(0.0f);
			FlowSubsystem->SetDistanceMeters(0.0f);
			FlowSubsystem->SetFilmMeter(50.0f);
			FlowSubsystem->SetHazardTimeScale(1.0f);
			FlowSubsystem->SetTotalFramesCollected(0);
			FlowSubsystem->SetHighestSpliceChain(1);
			FlowSubsystem->SetCurrentCheckpointTime(0.0f);
		}
	}

	SetRunState(EMegaJamRunState::TitleScreen);
}

void AMegaJamGameMode::StartRun()
{
	if (CurrentCheckpointDistanceMeters <= 0.0f)
	{
		RunTime = 0.0f;
	}
	DistanceMeters = CurrentCheckpointDistanceMeters;
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
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	++CurrentTakeNumber;

	// Rewind to the current Scene's start distance (0m for Scene 01, 1800m for Scene 02)
	if (CurrentCheckpointDistanceMeters <= 0.0f)
	{
		RunTime = 0.0f;
	}
	DistanceMeters = CurrentCheckpointDistanceMeters;
	CurrentMultiplier = 1.0f;

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetActiveGenre(ActiveSceneGenre);
	}

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
		SceneStartOriginX = static_cast<float>(FlowPawn->GetActorLocation().X);
		if (CurrentCheckpointDistanceMeters > 0.0f)
		{
			FlowPawn->SetActorLocation(
				FVector(SceneStartOriginX + CurrentCheckpointDistanceMeters * 100.0f, 0.0f, 140.0f),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		FlowPawn->SyncSceneMusicTrack(ActiveSceneGenre);
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
		FlowSubsystem->SetOnBeatFlashTimer(0.0f);
		FlowSubsystem->SetDistanceMeters(DistanceMeters);
		FlowSubsystem->SetFilmMeter(50.0f);
		FlowSubsystem->SetHazardTimeScale(1.0f);
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

FString AMegaJamGameMode::GetFormattedTakeClock() const
{
	const float SafeTime = FMath::Max(0.0f, RunTime);
	const int32 TotalSeconds = FMath::FloorToInt(SafeTime);
	const int32 Hundredths = FMath::Clamp(FMath::FloorToInt((SafeTime - static_cast<float>(TotalSeconds)) * 100.0f), 0, 99);
	return FString::Printf(TEXT("%02d:%02d.%02d"), TotalSeconds / 60, TotalSeconds % 60, Hundredths);
}

void AMegaJamGameMode::CompleteRun()
{
	if (IsPlaying())
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
		const float SecondsUnderPar = FMath::Max(0.0f, SceneParTimeSeconds - RunTime);
		ParTimeBonusAwarded = FMath::RoundToInt(SecondsUnderPar * 500.0f);
		Score += static_cast<float>(ParTimeBonusAwarded);
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
