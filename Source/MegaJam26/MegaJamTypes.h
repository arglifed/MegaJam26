// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MegaJamTypes.generated.h"

/** The two overlapping realities on the track. Obstacles of the inactive take are phased out. */
UENUM(BlueprintType)
enum class EMegaJamTakeState : uint8
{
	StudioTake,
	DirectorsTake
};

/** Framing mode. Clamps the pawn's playable Y / Z corridor. */
UENUM(BlueprintType)
enum class EMegaJamAspectMode : uint8
{
	Wide_21_9,
	Box_4_3
};

/** The four film scenes, sequenced by run time. */
UENUM(BlueprintType)
enum class EMegaJamFilmScene : uint8
{
	Soundstage,
	OverpassNight,
	CuttingRoom,
	FinalCut
};

