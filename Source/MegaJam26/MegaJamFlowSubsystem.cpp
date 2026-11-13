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
