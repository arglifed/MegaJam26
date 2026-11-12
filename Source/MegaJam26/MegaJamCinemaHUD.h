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
class UTextBlock;

/**
 * Pure C++ Cinema Letterbox HUD for SPLICE // DIRECTOR'S CUT.
 * Constructs its entire widget hierarchy via WidgetTree in RebuildWidget() so L_FlowTest works
 * out of the box with zero Widget Blueprint (.uasset) dependencies.
 *
 * Features:
 *   - Animated Top/Bottom letterbox matte bars in Wide_21_9 mode vs. Left/Right pillarbox matte bars in Box_4_3 mode.
 *   - Top-Left readout: REC ● TAKE XX and live 24fps SMPTE timecode MM:SS:FF.
 *   - Top-Right readout: STUDIO CUT // TAKE A vs DIRECTOR'S CUT // TAKE B, framing badge, FLOW multiplier,
 *     and Match-Cut CHAIN x1-x8 badge (flashing gold on MATCH CUT! and pulsing amber on STALE SHOT).
 *   - Top-Center 2.5-second Scene Slugline clapperboard flash on scene transitions.
 *   - Center-screen Clapperboard flash overlay (ACTION!, CUT! // REWINDING..., FINAL CUT // WRAP).
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

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

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
	TObjectPtr<UTextBlock> TopRightText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChainBadgeText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> SceneSluglineBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SceneSluglineText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> CenterBannerBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CenterBannerText;

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamTrackGenerator> BoundTrackGenerator;

	float CurrentLetterboxHeight = 88.0f;
	float CurrentPillarboxWidth = 0.0f;
	float ActionFlashTimer = 0.0f;
	float SceneSluglineTimer = 0.0f;
	int32 LastObservedTakeNumber = 0;
	int32 LastObservedSceneIndex = -1;
};
