// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MegaJamGameMode.h"
#include "MegaJamTypes.h"
#include "MegaJamCinemaHUD.generated.h"

class AMegaJamFlowPawn;
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

	/** Target height (px) of top and bottom letterbox bars when in UltraPanavision_276_1 (2.76:1) mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|HUD")
	float UltraPanavisionBarHeight = 132.0f;

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

	/** Triggers a 0.14s unscaled-time full-width gold-white razor cut-line streak across the 2.76:1 viewport. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|HUD")
	void TriggerSlashCutFlash(bool bIsOnBeat);

	/** Triggers a 0.15s unscaled-time electric-cyan pulse on the 21:9 letterbox matte borders during a Scene 01 Beat-Splice. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|HUD")
	void TriggerSpliceShockwaveFlash();

	/** Populates and activates the end-of-run Box-Office Critic Scoreboard with per-scene grades and total gross. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|HUD")
	void DisplayBoxOfficeScoreboard(const TMap<EMegaJamSceneGenre, FScenePerformanceMetrics>& Metrics, int32 TotalGross);

	/** Renders/updates the Box-Office Scoreboard ledger when bIsShowingScoreboard is true. */
	void DrawHUD();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="MegaJam|HUD")
	bool bIsShowingScoreboard = false;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleRunStateChanged(EMegaJamRunState NewState, EMegaJamRunState OldState);

	UFUNCTION()
	void HandleFilmSceneChanged(EMegaJamFilmScene NewScene);

	UFUNCTION()
	void HandleClapperboardSnapped();

	void BindToTrackGenerator();
	void BindToPawn();
	static FString GetSceneSluglineText(EMegaJamFilmScene Scene);
	static FString GetActProgressLabel(EMegaJamFilmScene Scene);
	static void ComputeCriticRating(int32 FinalScore, int32 MaxChain, int32 FramesSpliced, int32 TakesUsed, FString& OutStars, FString& OutQuote);
	static FLinearColor GetCriticGradeColor(const FString& Grade);
	static FString FormatDollarsWithCommas(int32 Amount);

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

	// ---- Bottom-Center Film Meter & Dynamic BPM Beat Indicator ----
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
	TObjectPtr<UTextBlock> WrapLedgerRow0Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapLedgerRow1Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapLedgerRow2Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapLedgerRow3Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapLedgerRow4Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapGrossFooterText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapStatsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapStarsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapQuoteText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WrapPromptText;

	UPROPERTY(Transient)
	TMap<EMegaJamSceneGenre, FScenePerformanceMetrics> CachedScoreboardMetrics;

	int32 CachedTotalBoxOfficeGross = 10000000;

	// ---- Pause Menu Slate (IsPaused() == true) ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> PauseCardBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PauseOption0Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PauseOption1Text;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PauseOption2Text;

	// ---- Full-Screen Clapperboard Stunt Transition Blackout (0.4s) ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> ClapperboardBlackoutOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ClapperboardBlackoutText;

	// ---- Full-Width 2D Hitstop Razor Cut-Line Streak (Scene 02 Beat-Slash) ----
	UPROPERTY(Transient)
	TObjectPtr<UBorder> SlashCutLineBorder;

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamTrackGenerator> BoundTrackGenerator;

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamFlowPawn> BoundPawn;

	float CurrentLetterboxHeight = 88.0f;
	float CurrentPillarboxWidth = 0.0f;
	float ActionFlashTimer = 0.0f;
	float SceneSluglineTimer = 0.0f;
	float ClapperboardBlackoutTimer = 0.0f;
	float SlashCutFlashTimer = 0.0f;
	bool bSlashCutFlashOnBeat = false;
	float SpliceShockwaveFlashTimer = 0.0f;
	int32 LastObservedTakeNumber = 0;
	int32 LastObservedSceneIndex = -1;
};
