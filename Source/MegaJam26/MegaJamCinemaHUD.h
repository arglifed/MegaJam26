// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MegaJamGameMode.h"
#include "MegaJamTypes.h"
#include "MegaJamCinemaHUD.generated.h"

class AMegaJamTrackGenerator;
class UBorder;
class UCanvasPanel;
class UProgressBar;
class UTextBlock;
class UVerticalBox;

/**
 * Pure C++ Cinema Letterbox HUD for SPLICE // DIRECTOR'S CUT.
 * Constructs its entire widget hierarchy via WidgetTree in RebuildWidget() so L_FlowTest works
 * out of the box with zero Widget Blueprint (.uasset) dependencies.
 *
 * Features:
 *   - Clapperboard Title Screen overlay (Attract Mode) with controls legend, objective, and pulsing start prompt.
 *   - Animated Top/Bottom letterbox matte bars in Wide_21_9 mode vs. Left/Right pillarbox matte bars in Box_4_3 mode.
 *   - Top-Left readout: REC ● TAKE XX and live 24fps SMPTE timecode MM:SS:FF.
 *   - Top-Center Act Progress readout (ACT I / IV through ACT IV / IV) and progress bar filling toward 150s.
 *   - Top-Right readout: STUDIO CUT // TAKE A vs DIRECTOR'S CUT // TAKE B, framing badge, FLOW multiplier,
 *     and Match-Cut CHAIN x1-x8 badge (flashing gold on MATCH CUT! and pulsing amber on STALE SHOT).
 *   - Top-Center 2.5-second Scene Slugline clapperboard flash on scene transitions.
 *   - Final Cut Wrap-Up Scoreboard with 1-5 Critic Star Rating, cinema review quote, and replay prompt.
 */
UCLASS()
class MEGAJAM26_API UMegaJamCinemaHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Target height (px) of top and bottom letterbox bars when in Wide_21_9 mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|HUD")
	float LetterboxBarHeight = 88.0f;

	/** Target width (px) of left and right pillarbox bars when in Box_4_3 mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|HUD")
	float PillarboxBarWidth = 220.0f;

	/** Interpolation speed for letterbox/pillarbox matte transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|HUD")
	float MatteInterpSpeed = 14.0f;

	/** Duration (seconds) that the "ACTION!" clapperboard flash remains visible on run start/rewind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|HUD")
	float ActionFlashDuration = 0.85f;

	/** Duration (seconds) that the top-center scene slugline flash remains visible on scene change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|HUD")
	float SceneSluglineFlashDuration = 2.5f;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleRunStateChanged(EMegaJamRunState NewState, EMegaJamRunState OldState);

	UFUNCTION()
	void HandleFilmSceneChanged(EMegaJamFilmScene NewScene);

	void BindToTrackGenerator();
	static FString GetSceneSluglineText(EMegaJamFilmScene Scene);
	static FString GetActProgressLabel(EMegaJamFilmScene Scene);
	static void ComputeCriticRating(int32 FinalScore, int32 MaxChain, int32 FramesSpliced, int32 TakesUsed, FString& OutStars, FString& OutQuote);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

	/** Full-screen warm white/amber film-burn washout overlay driven by AirExposureRatio in Box_4_3 mode. */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> OverexposureWashoutOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> TopMatteBar;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> BottomMatteBar;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> LeftMatteBar;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> RightMatteBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TopLeftText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DevBadgeText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TopRightText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChainBadgeText;

	// ---- Top-Center Act Progress Readout ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> ActProgressBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ActProgressText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> ActProgressBar;

	// ---- Bottom-Center Film Meter & 128 BPM Beat Indicator ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> FilmMeterBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FilmMeterText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> FilmMeterBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BeatIndicatorText;

	// ---- Top-Center Scene Slugline Flash ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> SceneSluglineBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SceneSluglineText;

	// ---- Center Clapperboard Flash (ACTION! / CUT!) ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> CenterBannerBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CenterBannerText;

	// ---- Title Screen Card (RunState == TitleScreen) ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> TitleCardBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitlePromptText;

	// ---- Final Cut Wrap-Up Scoreboard (RunState == Completed) ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> WrapCardBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapStatsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapStarsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapQuoteText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapPromptText;

	// ---- Pause Menu Slate (IsPaused() == true) ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> PauseCardBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PauseOption0Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PauseOption1Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PauseOption2Text;

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamTrackGenerator> BoundTrackGenerator;

	float CurrentLetterboxHeight = 88.0f;
	float CurrentPillarboxWidth = 0.0f;
	float ActionFlashTimer = 0.0f;
	float SceneSluglineTimer = 0.0f;
	int32 LastObservedTakeNumber = 0;
	int32 LastObservedSceneIndex = -1;
};
