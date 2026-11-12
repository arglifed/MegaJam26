// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamGameMode.h"

#include "MegaJamFlowPawn.h"
#include "MegaJamFlowSubsystem.h"
#include "MegaJamPlayerController.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"

AMegaJamGameMode::AMegaJamGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	DefaultPawnClass = AMegaJamFlowPawn::StaticClass();
	PlayerControllerClass = AMegaJamPlayerController::StaticClass();
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
	SetRunState(EMegaJamRunState::ActiveFlow);
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
	OnRunStateChanged.Broadcast(NewState, OldState);
	OnRunStateChangedEvent(NewState, OldState);
}
