// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MegaJamTypes.generated.h"

class USoundBase;

/** Per-scene music track and dynamic tempo configuration. */
USTRUCT(BlueprintType)
struct FSceneAudioConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
	TObjectPtr<USoundBase> MusicTrack = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
	float TrackBPM = 128.0f;
};

/** Canonical corridor half-width (cm). Total corridor width is 2400.0f units. */
constexpr float MEGAJAM_CORRIDOR_HALF_WIDTH = 1200.0f;

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
	Box_4_3,
	UltraPanavision_276_1
};

/** The four film scenes, sequenced by run progress. */
UENUM(BlueprintType)
enum class EMegaJamFilmScene : uint8
{
	Soundstage,
	OverpassNight,
	CuttingRoom,
	FinalCut
};

/** 5-Scene Genre Roadmap for SPLICE // DIRECTOR'S CUT. */
UENUM(BlueprintType)
enum class EMegaJamSceneGenre : uint8
{
	Action,
	Western,
	Alpine,
	Noir,
	Chase,
	Montage
};

/** Per-scene performance tracking for the end-of-run Box-Office Critic Scoreboard. */
USTRUCT(BlueprintType)
struct FScenePerformanceMetrics
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Score")
	float TimeSpentSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Score")
	int32 SpecialMovesExecuted = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Score")
	int32 FilmFramesCollected = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Score")
	FString CalculatedGrade = TEXT("C");
};

/** Tunable grading thresholds for the per-scene Box-Office Critic Scoreboard. */
USTRUCT(BlueprintType)
struct FSceneGradeThresholds
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Grading")
	float SRankTime = 52.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Grading")
	int32 SRankMoves = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Grading")
	float ARankTime = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Grading")
	int32 ARankMoves = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Grading")
	float BRankTime = 68.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Grading")
	float CRankTime = 78.0f;
};


