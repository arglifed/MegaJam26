// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamGameMode.h"

#include "MegaJamFlowPawn.h"
#include "MegaJamFlowSubsystem.h"
#include "MegaJamPlayerController.h"
#include "MegaJamTrackGenerator.h"
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
		// Fall back to the default MPC asset if one was not assigned in class defaults.
		if (!DefaultFlowMPC && FPackageName::DoesPackageExist(TEXT("/Game/MegaJam26/VFX/MPC_FlowState")))
		{
			DefaultFlowMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/MegaJam26/VFX/MPC_FlowState.MPC_FlowState"));
		}

		FlowSubsystem->SetFlowMPC(DefaultFlowMPC);
		FlowSubsystem->SetCurrentMultiplier(CurrentMultiplier);
	}

	if (bAutoStartRun)
	{
		StartRun();
	}
}

void AMegaJamGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (RunState != EMegaJamRunState::ActiveFlow)
	{
		return;
	}

	UMegaJamFlowSubsystem* FlowSubsystem = GetWorld()->GetSubsystem<UMegaJamFlowSubsystem>();
	const float FlowRatio = FlowSubsystem ? FlowSubsystem->FlowRatio : 0.0f;

	RunTime += DeltaSeconds;

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
		CompleteRun();
	}
}

void AMegaJamGameMode::StartRun()
{
	RunTime = 0.0f;
	Score = 0.0f;
	CurrentMultiplier = 1.0f;

	// Make L_FlowTest work out of the box: spawn a track at the origin if the level has none.
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

	SetRunState(EMegaJamRunState::ActiveFlow);
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

	if (AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FlowPawn->ResetForRun();
	}

	if (UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>())
	{
		FlowSubsystem->SetFlowRatio(0.0f);
		FlowSubsystem->SetCurrentMultiplier(1.0f);
	}

	OnClapperboard(CurrentTakeNumber);

	// Pass through PreRun so listeners (the track generator) always see a transition into ActiveFlow,
	// even when ResetRun is called mid-run.
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
	if (RunState == EMegaJamRunState::ActiveFlow)
	{
		SetRunState(EMegaJamRunState::Completed);
	}
}

void AMegaJamGameMode::NotifyCrash(AActor* HitActor)
{
	if (RunState == EMegaJamRunState::ActiveFlow)
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
	if (RunState != EMegaJamRunState::ActiveFlow)
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

void AMegaJamGameMode::SetRunState(EMegaJamRunState NewState)
{
	if (RunState == NewState)
	{
		return;
	}

	const EMegaJamRunState OldState = RunState;
	RunState = NewState;

	// Clapperboard rewind: a crash automatically restarts the run after CrashRestartDelay.
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
