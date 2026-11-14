// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamCinemaHUD.h"

#include "MegaJamFlowPawn.h"
#include "MegaJamFlowSubsystem.h"
#include "MegaJamPlayerController.h"
#include "MegaJamTrackGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UMegaJamCinemaHUD::RebuildWidget()
{
	if (!WidgetTree)
	{
		return Super::RebuildWidget();
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	// Full-screen warm white/amber 35mm film-burn washout overlay (HitTestInvisible, ZOrder 0 behind mattes and text)
	OverexposureWashoutOverlay = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OverexposureWashoutOverlay"));
	OverexposureWashoutOverlay->SetBrushColor(FLinearColor(1.0f, 0.94f, 0.82f, 0.0f));
	OverexposureWashoutOverlay->SetPadding(FMargin(0.0f));
	OverexposureWashoutOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* WashoutSlot = RootCanvas->AddChildToCanvas(OverexposureWashoutOverlay))
	{
		WashoutSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		WashoutSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		WashoutSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
		WashoutSlot->SetZOrder(0);
	}

	const FLinearColor MatteBlack(0.0f, 0.0f, 0.0f, 0.96f);

	auto CreateMatteBar = [this, &MatteBlack](FName Name) -> UBorder*
	{
		UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Bar->SetBrushColor(MatteBlack);
		Bar->SetPadding(FMargin(0.0f));
		return Bar;
	};

	// Top letterbox bar
	TopMatteBar = CreateMatteBar(TEXT("TopMatteBar"));
	if (UCanvasPanelSlot* TopSlot = RootCanvas->AddChildToCanvas(TopMatteBar))
	{
		TopSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
		TopSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		TopSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, LetterboxBarHeight));
		TopSlot->SetZOrder(1);
	}

	// Bottom letterbox bar
	BottomMatteBar = CreateMatteBar(TEXT("BottomMatteBar"));
	if (UCanvasPanelSlot* BottomSlot = RootCanvas->AddChildToCanvas(BottomMatteBar))
	{
		BottomSlot->SetAnchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f));
		BottomSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		BottomSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, LetterboxBarHeight));
		BottomSlot->SetZOrder(1);
	}

	// Left pillarbox bar
	LeftMatteBar = CreateMatteBar(TEXT("LeftMatteBar"));
	if (UCanvasPanelSlot* LeftSlot = RootCanvas->AddChildToCanvas(LeftMatteBar))
	{
		LeftSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 1.0f));
		LeftSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		LeftSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
		LeftSlot->SetZOrder(1);
	}

	// Right pillarbox bar
	RightMatteBar = CreateMatteBar(TEXT("RightMatteBar"));
	if (UCanvasPanelSlot* RightSlot = RootCanvas->AddChildToCanvas(RightMatteBar))
	{
		RightSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
		RightSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		RightSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
		RightSlot->SetZOrder(1);
	}

	// Top-Left SMPTE & Take Counter readout
	TopLeftText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TopLeftText"));
	TopLeftText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
	TopLeftText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f)));
	TopLeftText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TopLeftText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	TopLeftText->SetText(FText::FromString(TEXT("REC ● TAKE 01  //  00:00:00")));
	if (UCanvasPanelSlot* TLSlot = RootCanvas->AddChildToCanvas(TopLeftText))
	{
		TLSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		TLSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		TLSlot->SetPosition(FVector2D(30.0f, 30.0f));
		TLSlot->SetAutoSize(true);
		TLSlot->SetZOrder(10);
	}

	// Dev God Mode Badge (only visible when bGodModeEnabled is true)
	DevBadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevBadgeText"));
	DevBadgeText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13));
	DevBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.72f, 0.12f, 1.0f)));
	DevBadgeText->SetShadowOffset(FVector2D(1.5f, 1.5f));
	DevBadgeText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	DevBadgeText->SetText(FText::FromString(TEXT("[DEV: GOD MODE | HITS: 0]")));
	DevBadgeText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* DevSlot = RootCanvas->AddChildToCanvas(DevBadgeText))
	{
		DevSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		DevSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		DevSlot->SetPosition(FVector2D(30.0f, 54.0f));
		DevSlot->SetAutoSize(true);
		DevSlot->SetZOrder(10);
	}

	// Top-Center Distance Progress Readout & Progress Bar (visible during active gameplay)
	ActProgressBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ActProgressBorder"));
	ActProgressBorder->SetBrushColor(FLinearColor(0.04f, 0.04f, 0.04f, 0.78f));
	ActProgressBorder->SetPadding(FMargin(14.0f, 5.0f));

	UVerticalBox* ActBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ActBox"));
	ActProgressText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ActProgressText"));
	ActProgressText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	ActProgressText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.25f, 1.0f)));
	ActProgressText->SetJustification(ETextJustify::Center);
	ActProgressText->SetText(FText::FromString(TEXT("DIST: 0000m / 1800m  (0%)")));
	if (UVerticalBoxSlot* TextSlot = ActBox->AddChildToVerticalBox(ActProgressText))
	{
		TextSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
		TextSlot->SetHorizontalAlignment(HAlign_Center);
	}

	ActProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ActProgressBar"));
	ActProgressBar->SetPercent(0.0f);
	ActProgressBar->SetFillColorAndOpacity(FLinearColor(1.0f, 0.82f, 0.18f, 1.0f));
	if (UVerticalBoxSlot* BarSlot = ActBox->AddChildToVerticalBox(ActProgressBar))
	{
		BarSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	ActProgressBorder->SetContent(ActBox);

	if (UCanvasPanelSlot* ActSlot = RootCanvas->AddChildToCanvas(ActProgressBorder))
	{
		ActSlot->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
		ActSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		ActSlot->SetPosition(FVector2D(0.0f, 85.0f));
		ActSlot->SetSize(FVector2D(300.0f, 44.0f));
		ActSlot->SetAutoSize(false);
		ActSlot->SetZOrder(12);
	}

	// Bottom-Center Film Meter & 128 BPM Beat Indicator
	FilmMeterBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FilmMeterBorder"));
	FilmMeterBorder->SetBrushColor(FLinearColor(0.02f, 0.025f, 0.035f, 0.88f));
	FilmMeterBorder->SetPadding(FMargin(18.0f, 7.0f));

	UVerticalBox* FilmBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FilmBox"));

	BeatIndicatorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BeatIndicatorText"));
	BeatIndicatorText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
	BeatIndicatorText->SetColorAndOpacity(FSlateColor(FLinearColor(0.25f, 0.92f, 1.0f, 1.0f)));
	BeatIndicatorText->SetJustification(ETextJustify::Center);
	BeatIndicatorText->SetText(FText::FromString(TEXT("♦ 128 BPM BEAT ♦")));
	if (UVerticalBoxSlot* BeatSlot = FilmBox->AddChildToVerticalBox(BeatIndicatorText))
	{
		BeatSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
		BeatSlot->SetHorizontalAlignment(HAlign_Center);
	}

	FilmMeterText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FilmMeterText"));
	FilmMeterText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13));
	FilmMeterText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.88f, 0.25f, 1.0f)));
	FilmMeterText->SetJustification(ETextJustify::Center);
	FilmMeterText->SetText(FText::FromString(TEXT("[CTRL / C] DIRECTOR'S CUT READY! (FILM: 50%)")));
	if (UVerticalBoxSlot* FMTextSlot = FilmBox->AddChildToVerticalBox(FilmMeterText))
	{
		FMTextSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		FMTextSlot->SetHorizontalAlignment(HAlign_Center);
	}

	FilmMeterBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("FilmMeterBar"));
	FilmMeterBar->SetPercent(0.5f);
	FilmMeterBar->SetFillColorAndOpacity(FLinearColor(0.2f, 0.95f, 1.0f, 1.0f));
	if (UVerticalBoxSlot* FMBarSlot = FilmBox->AddChildToVerticalBox(FilmMeterBar))
	{
		FMBarSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	FilmMeterBorder->SetContent(FilmBox);

	if (UCanvasPanelSlot* FMSlot = RootCanvas->AddChildToCanvas(FilmMeterBorder))
	{
		FMSlot->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
		FMSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		FMSlot->SetPosition(FVector2D(0.0f, -20.0f));
		FMSlot->SetSize(FVector2D(460.0f, 64.0f));
		FMSlot->SetAutoSize(false);
		FMSlot->SetZOrder(14);
	}

	// Top-Right Active Take & Flow Multiplier readout
	TopRightText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TopRightText"));
	TopRightText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
	TopRightText->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.9f, 1.0f, 1.0f)));
	TopRightText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TopRightText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	TopRightText->SetJustification(ETextJustify::Right);
	TopRightText->SetText(FText::FromString(TEXT("STUDIO CUT // TAKE A  [21:9]  //  FLOW 1.0x (0%)")));
	if (UCanvasPanelSlot* TRSlot = RootCanvas->AddChildToCanvas(TopRightText))
	{
		TRSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
		TRSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		TRSlot->SetPosition(FVector2D(-30.0f, 30.0f));
		TRSlot->SetAutoSize(true);
		TRSlot->SetZOrder(10);
	}

	// Top-Right Match-Cut Chain & Stale Shot Badge
	ChainBadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChainBadgeText"));
	ChainBadgeText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13));
	ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.92f, 1.0f)));
	ChainBadgeText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	ChainBadgeText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	ChainBadgeText->SetJustification(ETextJustify::Right);
	ChainBadgeText->SetText(FText::FromString(TEXT("SPLICE CHAIN x1")));
	if (UCanvasPanelSlot* ChainSlot = RootCanvas->AddChildToCanvas(ChainBadgeText))
	{
		ChainSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
		ChainSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		ChainSlot->SetPosition(FVector2D(-30.0f, 54.0f));
		ChainSlot->SetAutoSize(true);
		ChainSlot->SetZOrder(10);
	}

	// Top-Center 2.5s Scene Slugline Clapperboard Banner
	SceneSluglineBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SceneSluglineBorder"));
	SceneSluglineBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.88f));
	SceneSluglineBorder->SetPadding(FMargin(22.0f, 8.0f));
	SceneSluglineBorder->SetVisibility(ESlateVisibility::Collapsed);

	SceneSluglineText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SceneSluglineText"));
	SceneSluglineText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 20));
	SceneSluglineText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.25f, 1.0f)));
	SceneSluglineText->SetJustification(ETextJustify::Center);
	SceneSluglineBorder->SetContent(SceneSluglineText);

	if (UCanvasPanelSlot* SlugSlot = RootCanvas->AddChildToCanvas(SceneSluglineBorder))
	{
		SlugSlot->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
		SlugSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		SlugSlot->SetPosition(FVector2D(0.0f, 130.0f));
		SlugSlot->SetAutoSize(true);
		SlugSlot->SetZOrder(15);
	}

	// Center Clapperboard flash overlay (ACTION! / CUT!)
	CenterBannerBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CenterBannerBorder"));
	CenterBannerBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.85f));
	CenterBannerBorder->SetPadding(FMargin(28.0f, 14.0f));
	CenterBannerBorder->SetVisibility(ESlateVisibility::Collapsed);

	CenterBannerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CenterBannerText"));
	CenterBannerText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 28));
	CenterBannerText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	CenterBannerText->SetJustification(ETextJustify::Center);
	CenterBannerBorder->SetContent(CenterBannerText);

	if (UCanvasPanelSlot* CenterSlot = RootCanvas->AddChildToCanvas(CenterBannerBorder))
	{
		CenterSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CenterSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CenterSlot->SetPosition(FVector2D(0.0f, 0.0f));
		CenterSlot->SetAutoSize(true);
		CenterSlot->SetZOrder(20);
	}

	// ---- Clapperboard Title Screen Card (RunState == TitleScreen) ----
	TitleCardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TitleCardBorder"));
	TitleCardBorder->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.02f, 0.90f));
	TitleCardBorder->SetPadding(FMargin(44.0f, 32.0f));
	TitleCardBorder->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* TitleBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TitleBox"));

	auto AddTitleLine = [this, TitleBox](FName Name, const FString& Text, int32 Size, const FLinearColor& Color, float BottomPad) -> UTextBlock*
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetJustification(ETextJustify::Center);
		Block->SetText(FText::FromString(Text));
		if (UVerticalBoxSlot* BoxSlot = TitleBox->AddChildToVerticalBox(Block))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Center);
			BoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BottomPad));
		}
		return Block;
	};

	AddTitleLine(TEXT("TitleStudio"), TEXT("FOUR SWORDS PICTURES PRESENTS"), 16, FLinearColor(0.75f, 0.78f, 0.85f, 1.0f), 6.0f);
	AddTitleLine(TEXT("TitleMain"), TEXT("DIRECTOR'S CUT"), 44, FLinearColor(1.0f, 0.85f, 0.22f, 1.0f), 18.0f);
	AddTitleLine(TEXT("TitleControls1"), TEXT("[W / A / S / D] 4:3 FREE FLIGHT   |   [SPACE / SHIFT] HARD CUT / BEAT-SLASH"), 16, FLinearColor(0.25f, 0.92f, 1.0f, 1.0f), 6.0f);
	AddTitleLine(TEXT("TitleControls2"), TEXT("[LEFT CTRL / C] 21:9 & 2.76:1 DIRECTOR'S CUT (@ >=25% FILM)"), 16, FLinearColor(1.0f, 0.55f, 0.2f, 1.0f), 16.0f);
	AddTitleLine(TEXT("TitleObj"), TEXT("SCENE 01 (ACTION) + SCENE 02 (WESTERN) — 3,600m CAMPAIGN (PAR 01:30.00)!"), 15, FLinearColor(0.92f, 0.92f, 0.92f, 0.95f), 24.0f);
	TitlePromptText = AddTitleLine(TEXT("TitlePrompt"), TEXT("▶ PRESS [SPACE] TO ROLL CAMERA"), 22, FLinearColor(1.0f, 0.9f, 0.3f, 1.0f), 0.0f);

	TitleCardBorder->SetContent(TitleBox);
	if (UCanvasPanelSlot* TitleSlot = RootCanvas->AddChildToCanvas(TitleCardBorder))
	{
		TitleSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		TitleSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		TitleSlot->SetPosition(FVector2D(0.0f, 0.0f));
		TitleSlot->SetAutoSize(true);
		TitleSlot->SetZOrder(30);
	}

	// ---- Final Cut Wrap-Up Scoreboard Card (RunState == Completed) ----
	WrapCardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WrapCardBorder"));
	WrapCardBorder->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.02f, 0.92f));
	WrapCardBorder->SetPadding(FMargin(48.0f, 34.0f));
	WrapCardBorder->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* WrapBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WrapBox"));

	auto AddWrapLine = [this, WrapBox](FName Name, const FString& Text, int32 Size, const FLinearColor& Color, float BottomPad) -> UTextBlock*
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetJustification(ETextJustify::Center);
		Block->SetText(FText::FromString(Text));
		if (UVerticalBoxSlot* BoxSlot = WrapBox->AddChildToVerticalBox(Block))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Center);
			BoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BottomPad));
		}
		return Block;
	};

	AddWrapLine(TEXT("WrapHeader"), TEXT("FINAL CUT // THAT'S A WRAP!"), 34, FLinearColor(1.0f, 0.85f, 0.22f, 1.0f), 18.0f);
	WrapStatsText = AddWrapLine(TEXT("WrapStats"), TEXT("FINAL SCORE: 000000"), 18, FLinearColor(0.95f, 0.95f, 0.95f, 1.0f), 18.0f);
	WrapStarsText = AddWrapLine(TEXT("WrapStars"), TEXT("CRITIC RATING: ★★★★★"), 26, FLinearColor(1.0f, 0.88f, 0.15f, 1.0f), 8.0f);
	WrapQuoteText = AddWrapLine(TEXT("WrapQuote"), TEXT("\"A TOUR-DE-FORCE OF HIGH-SPEED EDITING!\""), 16, FLinearColor(0.25f, 0.92f, 1.0f, 1.0f), 24.0f);
	WrapPromptText = AddWrapLine(TEXT("WrapPrompt"), TEXT("▶ PRESS [SPACE] FOR NEW PRODUCTION"), 20, FLinearColor(1.0f, 0.9f, 0.3f, 1.0f), 0.0f);

	WrapCardBorder->SetContent(WrapBox);
	if (UCanvasPanelSlot* WrapSlot = RootCanvas->AddChildToCanvas(WrapCardBorder))
	{
		WrapSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		WrapSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		WrapSlot->SetPosition(FVector2D(0.0f, 0.0f));
		WrapSlot->SetAutoSize(true);
		WrapSlot->SetZOrder(30);
	}

	// ---- Cinema Slate Pause Menu Card (IsPaused() == true) ----
	PauseCardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PauseCardBorder"));
	PauseCardBorder->SetBrushColor(FLinearColor(0.012f, 0.012f, 0.018f, 0.94f));
	PauseCardBorder->SetPadding(FMargin(52.0f, 36.0f));
	PauseCardBorder->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* PauseBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseBox"));

	auto AddPauseLine = [this, PauseBox](FName Name, const FString& Text, int32 Size, const FLinearColor& Color, float BottomPad) -> UTextBlock*
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetJustification(ETextJustify::Center);
		Block->SetText(FText::FromString(Text));
		if (UVerticalBoxSlot* BoxSlot = PauseBox->AddChildToVerticalBox(Block))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Center);
			BoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BottomPad));
		}
		return Block;
	};

	AddPauseLine(TEXT("PauseHeader"), TEXT("PRODUCTION PAUSED // CUT"), 30, FLinearColor(1.0f, 0.85f, 0.22f, 1.0f), 22.0f);
	PauseOption0Text = AddPauseLine(TEXT("PauseOpt0"), TEXT("▶ RESUME SHOOTING"), 20, FLinearColor(0.2f, 0.95f, 1.0f, 1.0f), 12.0f);
	PauseOption1Text = AddPauseLine(TEXT("PauseOpt1"), TEXT("   RETAKE CURRENT ACT"), 20, FLinearColor(0.75f, 0.78f, 0.82f, 1.0f), 12.0f);
	PauseOption2Text = AddPauseLine(TEXT("PauseOpt2"), TEXT("   WRAP TO TITLE SLATE"), 20, FLinearColor(0.75f, 0.78f, 0.82f, 1.0f), 20.0f);
	AddPauseLine(TEXT("PauseHint"), TEXT("[W / S] SELECT   |   [SPACE / ENTER] CONFIRM   |   [ESC / P] RESUME"), 13, FLinearColor(0.65f, 0.68f, 0.72f, 1.0f), 0.0f);

	PauseCardBorder->SetContent(PauseBox);
	if (UCanvasPanelSlot* PauseSlot = RootCanvas->AddChildToCanvas(PauseCardBorder))
	{
		PauseSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		PauseSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PauseSlot->SetPosition(FVector2D(0.0f, 0.0f));
		PauseSlot->SetAutoSize(true);
		PauseSlot->SetZOrder(35);
	}

	// ---- Full-Screen Clapperboard Stunt Transition Blackout Slate (0.4s) ----
	ClapperboardBlackoutOverlay = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ClapperboardBlackoutOverlay"));
	ClapperboardBlackoutOverlay->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	ClapperboardBlackoutOverlay->SetPadding(FMargin(0.0f));
	ClapperboardBlackoutOverlay->SetHorizontalAlignment(HAlign_Center);
	ClapperboardBlackoutOverlay->SetVerticalAlignment(VAlign_Center);
	ClapperboardBlackoutOverlay->SetVisibility(ESlateVisibility::Collapsed);

	ClapperboardBlackoutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ClapperboardBlackoutText"));
	ClapperboardBlackoutText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 42));
	ClapperboardBlackoutText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ClapperboardBlackoutText->SetJustification(ETextJustify::Center);
	ClapperboardBlackoutText->SetText(FText::FromString(TEXT("SCENE 02 // ACTION!")));
	ClapperboardBlackoutOverlay->SetContent(ClapperboardBlackoutText);

	if (UCanvasPanelSlot* BlackoutSlot = RootCanvas->AddChildToCanvas(ClapperboardBlackoutOverlay))
	{
		BlackoutSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BlackoutSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		BlackoutSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
		BlackoutSlot->SetZOrder(40);
	}

	// ---- Full-Width 2D Hitstop Razor Cut-Line Streak (Scene 02 Western Beat-Slash) ----
	SlashCutLineBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlashCutLineBorder"));
	SlashCutLineBorder->SetBrushColor(FLinearColor(1.0f, 0.94f, 0.55f, 1.0f));
	SlashCutLineBorder->SetPadding(FMargin(0.0f));
	SlashCutLineBorder->SetRenderTransformAngle(-3.5f);
	SlashCutLineBorder->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* SlashSlot = RootCanvas->AddChildToCanvas(SlashCutLineBorder))
	{
		SlashSlot->SetAnchors(FAnchors(0.0f, 0.5f, 1.0f, 0.5f));
		SlashSlot->SetAlignment(FVector2D(0.0f, 0.5f));
		SlashSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
		SlashSlot->SetZOrder(18);
	}

	return Super::RebuildWidget();
}

void UMegaJamCinemaHUD::NativeConstruct()
{
	Super::NativeConstruct();

	CurrentLetterboxHeight = LetterboxBarHeight;
	CurrentPillarboxWidth = 0.0f;
	ActionFlashTimer = 0.0f;
	SceneSluglineTimer = 0.0f;
	LastObservedSceneIndex = 0;

	if (SceneSluglineText)
	{
		SceneSluglineText->SetText(FText::FromString(GetSceneSluglineText(EMegaJamFilmScene::Soundstage)));
	}

	if (UWorld* World = GetWorld())
	{
		if (AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
		{
			GameMode->OnRunStateChanged.RemoveDynamic(this, &UMegaJamCinemaHUD::HandleRunStateChanged);
			GameMode->OnRunStateChanged.AddDynamic(this, &UMegaJamCinemaHUD::HandleRunStateChanged);
			LastObservedTakeNumber = GameMode->GetCurrentTakeNumber();

			if (GameMode->IsPlaying())
			{
				ActionFlashTimer = ActionFlashDuration;
				SceneSluglineTimer = SceneSluglineFlashDuration;
			}
		}
	}

	BindToTrackGenerator();
	BindToPawn();
}

void UMegaJamCinemaHUD::BindToPawn()
{
	AMegaJamFlowPawn* CurrentPawn = Cast<AMegaJamFlowPawn>(GetOwningPlayerPawn());
	if (CurrentPawn == BoundPawn)
	{
		return;
	}

	if (BoundPawn)
	{
		BoundPawn->OnClapperboardSnapped.RemoveDynamic(this, &UMegaJamCinemaHUD::HandleClapperboardSnapped);
	}

	BoundPawn = CurrentPawn;
	if (BoundPawn)
	{
		BoundPawn->OnClapperboardSnapped.AddUniqueDynamic(this, &UMegaJamCinemaHUD::HandleClapperboardSnapped);
	}
}

void UMegaJamCinemaHUD::HandleClapperboardSnapped()
{
	ClapperboardBlackoutTimer = 0.40f;
	if (ClapperboardBlackoutText)
	{
		ClapperboardBlackoutText->SetText(FText::FromString(TEXT("SCENE 02 // ACTION!")));
	}
	if (ClapperboardBlackoutOverlay)
	{
		ClapperboardBlackoutOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (SceneSluglineText)
	{
		SceneSluglineText->SetText(FText::FromString(TEXT("SCENE 02 // WESTERN — HIGH-NOON STANDOFF")));
	}
	SceneSluglineTimer = SceneSluglineFlashDuration;
}

void UMegaJamCinemaHUD::TriggerSlashCutFlash(bool bIsOnBeat)
{
	SlashCutFlashTimer = 0.14f;
	bSlashCutFlashOnBeat = bIsOnBeat;
	if (SlashCutLineBorder)
	{
		SlashCutLineBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
		SlashCutLineBorder->SetRenderTransformAngle(-3.5f);
		const FLinearColor InitialColor = bIsOnBeat
			? FLinearColor(1.0f, 0.94f, 0.55f, 0.98f)
			: FLinearColor(0.82f, 0.90f, 1.0f, 0.72f);
		SlashCutLineBorder->SetBrushColor(InitialColor);
		if (UCanvasPanelSlot* SlashSlot = Cast<UCanvasPanelSlot>(SlashCutLineBorder->Slot))
		{
			SlashSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, bIsOnBeat ? 18.0f : 7.0f));
		}
	}
}

void UMegaJamCinemaHUD::TriggerSpliceShockwaveFlash()
{
	SpliceShockwaveFlashTimer = 0.15f;
}

void UMegaJamCinemaHUD::BindToTrackGenerator()
{
	if (BoundTrackGenerator)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (AMegaJamTrackGenerator* Generator = Cast<AMegaJamTrackGenerator>(
			UGameplayStatics::GetActorOfClass(World, AMegaJamTrackGenerator::StaticClass())))
		{
			BoundTrackGenerator = Generator;
			Generator->OnFilmSceneChanged.RemoveDynamic(this, &UMegaJamCinemaHUD::HandleFilmSceneChanged);
			Generator->OnFilmSceneChanged.AddDynamic(this, &UMegaJamCinemaHUD::HandleFilmSceneChanged);
		}
	}
}

void UMegaJamCinemaHUD::HandleRunStateChanged(EMegaJamRunState NewState, EMegaJamRunState OldState)
{
	if (NewState == EMegaJamRunState::Playing || NewState == EMegaJamRunState::ActiveFlow)
	{
		ActionFlashTimer = ActionFlashDuration;
		SceneSluglineTimer = SceneSluglineFlashDuration;
		const EMegaJamFilmScene ActiveScene = BoundTrackGenerator ? BoundTrackGenerator->GetCurrentScene() : EMegaJamFilmScene::Soundstage;
		if (SceneSluglineText)
		{
			SceneSluglineText->SetText(FText::FromString(GetSceneSluglineText(ActiveScene)));
		}
	}
}

void UMegaJamCinemaHUD::HandleFilmSceneChanged(EMegaJamFilmScene NewScene)
{
	LastObservedSceneIndex = static_cast<int32>(NewScene);
	if (SceneSluglineText)
	{
		SceneSluglineText->SetText(FText::FromString(GetSceneSluglineText(NewScene)));
	}

	if (UWorld* World = GetWorld())
	{
		if (const AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>())
		{
			if (GameMode->IsPlaying())
			{
				SceneSluglineTimer = SceneSluglineFlashDuration;
			}
		}
	}
}

FString UMegaJamCinemaHUD::GetSceneSluglineText(EMegaJamFilmScene Scene)
{
	return TEXT("SCENE 01 // ACTION — BULLET-TIME OVERDRIVE");
}

FString UMegaJamCinemaHUD::GetActProgressLabel(EMegaJamFilmScene Scene)
{
	return TEXT("SCENE 01 // ACTION");
}

void UMegaJamCinemaHUD::ComputeCriticRating(int32 FinalScore, int32 MaxChain, int32 FramesSpliced, int32 TakesUsed, FString& OutStars, FString& OutQuote)
{
	int32 Stars = 1;
	if (FinalScore >= 18000)
	{
		Stars += 1;
	}
	if (FinalScore >= 36000 || FramesSpliced >= 10)
	{
		Stars += 1;
	}
	if (MaxChain >= 5 || FramesSpliced >= 16)
	{
		Stars += 1;
	}
	if (TakesUsed <= 3 && (FinalScore >= 42000 || MaxChain >= 6))
	{
		Stars += 1;
	}
	Stars = FMath::Clamp(Stars, 1, 5);

	switch (Stars)
	{
	case 5:
		OutStars = TEXT("CRITIC RATING: ★★★★★ (5/5)");
		OutQuote = TEXT("\"A MASTERPIECE OF KINETIC EDITING! GIVE THIS DIRECTOR THE PALME D'OR!\"");
		break;
	case 4:
		OutStars = TEXT("CRITIC RATING: ★★★★☆ (4/5)");
		OutQuote = TEXT("\"RAZOR-SHARP SPLICES AND WHITE-KNUCKLE ASPECT FRAMING!\"");
		break;
	case 3:
		OutStars = TEXT("CRITIC RATING: ★★★☆☆ (3/5)");
		OutQuote = TEXT("\"SOLID SUMMER BLOCKBUSTER — A FEW BUMPED TAKES, BUT THRILLING PACE!\"");
		break;
	case 2:
		OutStars = TEXT("CRITIC RATING: ★★☆☆☆ (2/5)");
		OutQuote = TEXT("\"THE STUDIO WANTS RESHOOTS — HIT THOSE MATCH-CUT WINDOWS!\"");
		break;
	default:
		OutStars = TEXT("CRITIC RATING: ★☆☆☆☆ (1/5)");
		OutQuote = TEXT("\"STRAIGHT TO VHS! THE CLAPPERBOARD LOADER WORKED OVERTIME TODAY!\"");
		break;
	}
}

void UMegaJamCinemaHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	BindToTrackGenerator();
	BindToPawn();

	const float UnscaledDelta = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.0f, 0.1f);
	if (ClapperboardBlackoutOverlay)
	{
		if (ClapperboardBlackoutTimer > 0.0f)
		{
			ClapperboardBlackoutTimer = FMath::Max(0.0f, ClapperboardBlackoutTimer - UnscaledDelta);
			ClapperboardBlackoutOverlay->SetVisibility(
				ClapperboardBlackoutTimer > 0.0f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		else
		{
			ClapperboardBlackoutOverlay->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	const AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	const AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(GetOwningPlayerPawn());
	const UMegaJamFlowSubsystem* FlowSubsystem = World->GetSubsystem<UMegaJamFlowSubsystem>();
	const EMegaJamSceneGenre ActiveGenre = FlowSubsystem
		? FlowSubsystem->ActiveGenre
		: (GameMode ? GameMode->GetActiveSceneGenre() : EMegaJamSceneGenre::Action);
	const bool bIsWestern = (ActiveGenre == EMegaJamSceneGenre::Western);
	const TCHAR* SceneTitleLabel = bIsWestern ? TEXT("SCENE 02 // WESTERN") : TEXT("SCENE 01 // ACTION");

	const EMegaJamRunState RunState = GameMode ? GameMode->GetRunState() : EMegaJamRunState::Playing;
	const bool bIsPlaying = GameMode ? GameMode->IsPlaying() : true;
	const EMegaJamAspectMode Aspect = FlowPawn ? FlowPawn->GetAspectMode() : EMegaJamAspectMode::Box_4_3;
	const EMegaJamTakeState TakeState = FlowPawn ? FlowPawn->GetCurrentTake() : EMegaJamTakeState::StudioTake;
	const float FlowRatio = FlowPawn ? FlowPawn->GetFlowRatio() : 0.0f;
	const float FilmMeter = FlowPawn ? FlowPawn->GetFilmMeter() : (FlowSubsystem ? FlowSubsystem->FilmMeter : 50.0f);
	const float DropCutBannerTimer = FlowSubsystem ? FlowSubsystem->DropCutBannerTimer : 0.0f;
	const float OnBeatFlashTimer = FlowSubsystem ? FlowSubsystem->OnBeatFlashTimer : 0.0f;
	const bool bOnBeatWindow = FlowSubsystem ? FlowSubsystem->IsWithinBeatWindow(0.16f) : false;

	if (OverexposureWashoutOverlay)
	{
		OverexposureWashoutOverlay->SetBrushColor(FLinearColor(1.0f, 0.94f, 0.82f, 0.0f));
	}

	if (BoundTrackGenerator)
	{
		const int32 CurrentSceneIdx = static_cast<int32>(BoundTrackGenerator->GetCurrentScene());
		if (CurrentSceneIdx != LastObservedSceneIndex)
		{
			HandleFilmSceneChanged(BoundTrackGenerator->GetCurrentScene());
		}
	}

	// Smoothly interpolate letterbox (21:9 or 2.76:1 Ultra-Panavision) vs pillarbox (4:3) matte bars
	float TargetLetterbox = 0.0f;
	if (Aspect == EMegaJamAspectMode::UltraPanavision_276_1)
	{
		TargetLetterbox = UltraPanavisionBarHeight;
	}
	else if (Aspect == EMegaJamAspectMode::Wide_21_9)
	{
		TargetLetterbox = LetterboxBarHeight;
	}
	const float TargetPillarbox = (Aspect == EMegaJamAspectMode::Box_4_3) ? PillarboxBarWidth : 0.0f;

	CurrentLetterboxHeight = FMath::FInterpTo(CurrentLetterboxHeight, TargetLetterbox, InDeltaTime, MatteInterpSpeed);
	CurrentPillarboxWidth = FMath::FInterpTo(CurrentPillarboxWidth, TargetPillarbox, InDeltaTime, MatteInterpSpeed);

	if (TopMatteBar)
	{
		if (UCanvasPanelSlot* TopSlot = Cast<UCanvasPanelSlot>(TopMatteBar->Slot))
		{
			TopSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, CurrentLetterboxHeight));
		}
	}
	if (BottomMatteBar)
	{
		if (UCanvasPanelSlot* BottomSlot = Cast<UCanvasPanelSlot>(BottomMatteBar->Slot))
		{
			BottomSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, CurrentLetterboxHeight));
		}
	}
	if (LeftMatteBar)
	{
		if (UCanvasPanelSlot* LeftSlot = Cast<UCanvasPanelSlot>(LeftMatteBar->Slot))
		{
			LeftSlot->SetOffsets(FMargin(0.0f, 0.0f, CurrentPillarboxWidth, 0.0f));
		}
	}
	if (RightMatteBar)
	{
		if (UCanvasPanelSlot* RightSlot = Cast<UCanvasPanelSlot>(RightMatteBar->Slot))
		{
			RightSlot->SetOffsets(FMargin(0.0f, 0.0f, CurrentPillarboxWidth, 0.0f));
		}
	}

	// ---- 2D HUD Hitstop Razor Cut-Line (Scene 02 Beat-Slash, unscaled 0.14s) ----
	if (SlashCutLineBorder)
	{
		if (SlashCutFlashTimer > 0.0f)
		{
			SlashCutFlashTimer = FMath::Max(0.0f, SlashCutFlashTimer - UnscaledDelta);
			if (SlashCutFlashTimer > 0.0f)
			{
				const float Norm = FMath::Clamp(SlashCutFlashTimer / 0.14f, 0.0f, 1.0f);
				const float MaxThickness = bSlashCutFlashOnBeat ? 18.0f : 7.0f;
				const float Thickness = FMath::Max(1.0f, MaxThickness * FMath::Pow(Norm, 1.6f));
				const FLinearColor StreakColor = bSlashCutFlashOnBeat
					? FLinearColor(1.0f, 0.94f, 0.55f, 0.98f * Norm)
					: FLinearColor(0.82f, 0.90f, 1.0f, 0.72f * Norm);
				SlashCutLineBorder->SetBrushColor(StreakColor);
				SlashCutLineBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
				if (UCanvasPanelSlot* SlashSlot = Cast<UCanvasPanelSlot>(SlashCutLineBorder->Slot))
				{
					SlashSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, Thickness));
				}
			}
			else
			{
				SlashCutLineBorder->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		else
		{
			SlashCutLineBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ---- 21:9 Letterbox Electric-Cyan Border Flash (Scene 01 Beat-Splice, unscaled 0.15s) ----
	const FLinearColor MatteBlack(0.0f, 0.0f, 0.0f, 0.96f);
	FLinearColor ActiveMatteColor = MatteBlack;
	if (SpliceShockwaveFlashTimer > 0.0f)
	{
		SpliceShockwaveFlashTimer = FMath::Max(0.0f, SpliceShockwaveFlashTimer - UnscaledDelta);
		const float FlashAlpha = FMath::Clamp(SpliceShockwaveFlashTimer / 0.15f, 0.0f, 1.0f);
		const FLinearColor ElectricCyanBorder(0.08f, 0.88f, 1.0f, 0.98f);
		ActiveMatteColor = FMath::Lerp(MatteBlack, ElectricCyanBorder, FlashAlpha);
	}
	else if (SlashCutFlashTimer > 0.0f && bSlashCutFlashOnBeat)
	{
		const float SlashAlpha = FMath::Clamp(SlashCutFlashTimer / 0.14f, 0.0f, 1.0f) * 0.45f;
		ActiveMatteColor = FMath::Lerp(MatteBlack, FLinearColor(1.0f, 0.82f, 0.22f, 0.98f), SlashAlpha);
	}

	if (TopMatteBar)
	{
		TopMatteBar->SetBrushColor(ActiveMatteColor);
	}
	if (BottomMatteBar)
	{
		BottomMatteBar->SetBrushColor(ActiveMatteColor);
	}
	if (LeftMatteBar)
	{
		LeftMatteBar->SetBrushColor(ActiveMatteColor);
	}
	if (RightMatteBar)
	{
		RightMatteBar->SetBrushColor(ActiveMatteColor);
	}

	const int32 TakeNumber = GameMode ? GameMode->GetCurrentTakeNumber() : 1;
	if (TakeNumber != LastObservedTakeNumber)
	{
		LastObservedTakeNumber = TakeNumber;
		if (bIsPlaying)
		{
			ActionFlashTimer = ActionFlashDuration;
		}
	}

	const float PulseAlpha = 0.45f + 0.55f * FMath::Abs(FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * 3.2f));

	// 1. Title Screen Overlay (RunState == TitleScreen)
	if (TitleCardBorder)
	{
		if (RunState == EMegaJamRunState::TitleScreen)
		{
			TitleCardBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (TitlePromptText)
			{
				TitlePromptText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.9f, 0.25f, PulseAlpha)));
			}
		}
		else
		{
			TitleCardBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 2. Final Cut Wrap-Up Scoreboard (RunState == Completed)
	if (WrapCardBorder)
	{
		if (RunState == EMegaJamRunState::Completed)
		{
			WrapCardBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			const int32 FinalScore = GameMode ? FMath::RoundToInt(GameMode->GetScore()) : 0;
			const int32 MaxChain = GameMode ? GameMode->GetHighestSpliceChain() : 1;
			const int32 FramesSpliced = GameMode ? GameMode->GetTotalFramesCollected() : 0;
			const int32 ParBonus = GameMode ? GameMode->GetParTimeBonusAwarded() : 0;
			const FString TakeClock = GameMode ? GameMode->GetFormattedTakeClock() : TEXT("00:00.00");

			if (WrapStatsText)
			{
				WrapStatsText->SetText(FText::FromString(FString::Printf(
					TEXT("FINAL SCORE: %06d\nTAKE TIME: %s  (PAR: 01:30.00)\nPAR-TIME SPEED BONUS: +%d PTS\nMAX MATCH-CUT CHAIN: x%d   |   FRAMES SPLICED: %d   |   TAKES: %02d"),
					FinalScore, *TakeClock, ParBonus, MaxChain, FramesSpliced, TakeNumber)));
			}

			FString StarsStr;
			FString QuoteStr;
			ComputeCriticRating(FinalScore, MaxChain, FramesSpliced, TakeNumber, StarsStr, QuoteStr);
			if (WrapStarsText)
			{
				WrapStarsText->SetText(FText::FromString(StarsStr));
			}
			if (WrapQuoteText)
			{
				WrapQuoteText->SetText(FText::FromString(QuoteStr));
			}
			if (WrapPromptText)
			{
				WrapPromptText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.9f, 0.25f, PulseAlpha)));
			}
		}
		else
		{
			WrapCardBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 2.5 Cinema Slate Pause Menu (PC->IsPaused() == true)
	const AMegaJamPlayerController* MegaJamPC = Cast<AMegaJamPlayerController>(GetOwningPlayer());
	const bool bIsPaused = MegaJamPC ? MegaJamPC->IsPaused() : false;
	if (PauseCardBorder)
	{
		if (bIsPaused)
		{
			PauseCardBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			const int32 SelectedIdx = MegaJamPC ? MegaJamPC->GetPauseMenuSelectionIndex() : 0;
			const FLinearColor ActiveColor(0.2f, 0.95f, 1.0f, 1.0f);
			const FLinearColor InactiveColor(0.75f, 0.78f, 0.82f, 1.0f);

			if (PauseOption0Text)
			{
				PauseOption0Text->SetText(FText::FromString(SelectedIdx == 0 ? TEXT("▶ RESUME SHOOTING") : TEXT("   RESUME SHOOTING")));
				PauseOption0Text->SetColorAndOpacity(FSlateColor(SelectedIdx == 0 ? ActiveColor : InactiveColor));
			}
			if (PauseOption1Text)
			{
				PauseOption1Text->SetText(FText::FromString(SelectedIdx == 1 ? TEXT("▶ RETAKE CURRENT ACT") : TEXT("   RETAKE CURRENT ACT")));
				PauseOption1Text->SetColorAndOpacity(FSlateColor(SelectedIdx == 1 ? ActiveColor : InactiveColor));
			}
			if (PauseOption2Text)
			{
				PauseOption2Text->SetText(FText::FromString(SelectedIdx == 2 ? TEXT("▶ WRAP TO TITLE SLATE") : TEXT("   WRAP TO TITLE SLATE")));
				PauseOption2Text->SetColorAndOpacity(FSlateColor(SelectedIdx == 2 ? ActiveColor : InactiveColor));
			}
		}
		else
		{
			PauseCardBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 3. Top-Center Distance Progress Bar (visible during active gameplay)
	if (ActProgressBorder)
	{
		if (bIsPlaying)
		{
			ActProgressBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			const float DistMeters = GameMode ? GameMode->GetDistanceMeters() : 0.0f;
			const float TargetDist = (GameMode && GameMode->TargetDistanceMeters > 0.0f) ? GameMode->TargetDistanceMeters : 1800.0f;
			const float ProgressRatio = FMath::Clamp(DistMeters / TargetDist, 0.0f, 1.0f);

			if (ActProgressBar)
			{
				ActProgressBar->SetPercent(ProgressRatio);
			}
			if (ActProgressText)
			{
				ActProgressText->SetText(FText::FromString(FString::Printf(
					TEXT("DIST: %04dm / %04dm  (%d%%)"),
					FMath::RoundToInt(DistMeters),
					FMath::RoundToInt(TargetDist),
					FMath::RoundToInt(ProgressRatio * 100.0f))));
			}
		}
		else
		{
			ActProgressBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 3.5 Bottom-Center Film Meter & 128 BPM Beat Indicator
	if (FilmMeterBorder)
	{
		if (bIsPlaying)
		{
			FilmMeterBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			const float FilmNorm = FMath::Clamp(FilmMeter / 100.0f, 0.0f, 1.0f);
			const int32 FilmPct = FMath::RoundToInt(FilmMeter);
			const bool bInOverdrive = (Aspect == EMegaJamAspectMode::Wide_21_9 || Aspect == EMegaJamAspectMode::UltraPanavision_276_1);
			const bool bInPanavision = (Aspect == EMegaJamAspectMode::UltraPanavision_276_1);

			if (BeatIndicatorText)
			{
				if (OnBeatFlashTimer > 0.0f)
				{
					BeatIndicatorText->SetText(FText::FromString(
						bInPanavision ? TEXT("★ QUICK-DRAW BEAT-SLASH! +15 FILM METER ★") : TEXT("★ ON-BEAT CUT! +15 FILM METER ★")));
					BeatIndicatorText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.18f, 1.0f)));
				}
				else if (bOnBeatWindow)
				{
					BeatIndicatorText->SetText(FText::FromString(
						bInPanavision ? TEXT("♦ 128 BPM BEAT — SLASH NOW! ♦") : TEXT("♦ 128 BPM BEAT — CUT NOW! ♦")));
					BeatIndicatorText->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 1.0f, 0.95f, 1.0f)));
				}
				else
				{
					BeatIndicatorText->SetText(FText::FromString(TEXT("· 128 BPM BEAT ·")));
					BeatIndicatorText->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.62f, 0.68f, 0.85f)));
				}
			}

			if (FilmMeterBar)
			{
				FilmMeterBar->SetPercent(FilmNorm);
				if (bInPanavision)
				{
					FilmMeterBar->SetFillColorAndOpacity(FLinearColor(1.0f, 0.78f, 0.22f, 1.0f));
				}
				else if (bInOverdrive)
				{
					FilmMeterBar->SetFillColorAndOpacity(FLinearColor(0.15f, 0.95f, 1.0f, 1.0f));
				}
				else if (FilmMeter >= 25.0f)
				{
					FilmMeterBar->SetFillColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.20f, 1.0f));
				}
				else
				{
					FilmMeterBar->SetFillColorAndOpacity(FLinearColor(0.85f, 0.42f, 0.15f, 1.0f));
				}
			}

			if (FilmMeterText)
			{
				if (bInPanavision)
				{
					FilmMeterText->SetText(FText::FromString(FString::Printf(
						TEXT("2.76:1 ULTRA-PANAVISION // [SPACE] QUICK-DRAW SLASH (%d%%)"), FilmPct)));
					FilmMeterText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.28f, 1.0f)));
				}
				else if (bInOverdrive)
				{
					FilmMeterText->SetText(FText::FromString(FString::Printf(
						TEXT("21:9 DIRECTOR'S CUT // BULLET-TIME OVERDRIVE (%d%%)"), FilmPct)));
					FilmMeterText->SetColorAndOpacity(FSlateColor(FLinearColor(0.15f, 0.95f, 1.0f, 1.0f)));
				}
				else if (FilmMeter >= 25.0f)
				{
					const FString ReadyMsg = bIsWestern
						? FString::Printf(TEXT("[CTRL / C] 2.76:1 STANDOFF READY! (FILM: %d%%)"), FilmPct)
						: FString::Printf(TEXT("[CTRL / C] DIRECTOR'S CUT READY! (FILM: %d%%)"), FilmPct);
					FilmMeterText->SetText(FText::FromString(ReadyMsg));
					FilmMeterText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.90f, 0.25f, PulseAlpha)));
				}
				else
				{
					FilmMeterText->SetText(FText::FromString(FString::Printf(
						TEXT("4:3 FLIGHT CHARGE — SCOOP FRAMES & GRAZE (%d%% / 25%%)"), FilmPct)));
					FilmMeterText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.72f, 0.35f, 1.0f)));
				}
			}
		}
		else
		{
			FilmMeterBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 4. Top-Left readout: SCENE 01/02 | TAKE XX // TIME: MM:SS.ms (PAR: 01:30.00)
	if (TopLeftText)
	{
		if (RunState == EMegaJamRunState::TitleScreen)
		{
			TopLeftText->SetText(FText::FromString(TEXT("SCENE 01 // ACTION  |  STANDBY ○ CAMERA READY")));
		}
		else
		{
			const FString TakeClock = GameMode ? GameMode->GetFormattedTakeClock() : TEXT("00:00.00");
			const bool bRecBlink = FMath::Fmod(World->GetTimeSeconds(), 1.0) < 0.65;
			const TCHAR* RecDot = bRecBlink ? TEXT("●") : TEXT("○");
			TopLeftText->SetText(FText::FromString(FString::Printf(
				TEXT("REC %s %s  |  TAKE %02d  //  TIME: %s  (PAR: 01:30.00)"),
				RecDot, SceneTitleLabel, TakeNumber, *TakeClock)));
		}
	}

	if (DevBadgeText)
	{
		if (FlowPawn && FlowPawn->IsGodModeEnabled())
		{
			DevBadgeText->SetVisibility(ESlateVisibility::HitTestInvisible);
			DevBadgeText->SetText(FText::FromString(FString::Printf(
				TEXT("[DEV: GOD MODE | HITS: %d]"),
				FlowPawn->GetGodModeSimulatedHits())));
		}
		else
		{
			DevBadgeText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 5. Top-Right readout & Chain Badge
	const int32 SpliceChain = FlowPawn ? FlowPawn->GetSpliceChain() : 1;
	const float MatchCutFlash = FlowPawn ? FlowPawn->GetMatchCutFlashTimer() : 0.0f;
	const float DropCutFlash = FlowPawn ? FlowPawn->GetDropCutFlashTimer() : 0.0f;
	const float DollyBoostFlash = FlowPawn ? FlowPawn->GetDollyBoostFlashTimer() : 0.0f;

	if (TopRightText)
	{
		const bool bDirectorsTake = (TakeState == EMegaJamTakeState::DirectorsTake);
		const TCHAR* TakeLabel = bDirectorsTake ? TEXT("DIRECTOR'S CUT // TAKE B") : TEXT("STUDIO CUT // TAKE A");
		const TCHAR* AspectLabel = (Aspect == EMegaJamAspectMode::UltraPanavision_276_1)
			? TEXT("2.76:1 PANAVISION")
			: ((Aspect == EMegaJamAspectMode::Wide_21_9) ? TEXT("21:9 OVERDRIVE") : TEXT("4:3 FLIGHT"));
		const float Multiplier = GameMode ? GameMode->GetCurrentMultiplier() : 1.0f;
		const int32 ScoreInt = GameMode ? FMath::RoundToInt(GameMode->GetScore()) : 0;
		const int32 FlowPct = FMath::RoundToInt(FlowRatio * 100.0f);

		TopRightText->SetText(FText::FromString(FString::Printf(
			TEXT("%s  [%s]  //  FLOW %.1fx (%d%%)  //  CHAIN x%d  //  %06d"),
			TakeLabel, AspectLabel, Multiplier, FlowPct, SpliceChain, ScoreInt)));

		const FLinearColor ReadoutColor = bDirectorsTake
			? FLinearColor(1.0f, 0.35f, 0.15f, 1.0f)
			: FLinearColor(0.2f, 0.9f, 1.0f, 1.0f);
		TopRightText->SetColorAndOpacity(FSlateColor(ReadoutColor));
	}

	if (ChainBadgeText)
	{
		if (!bIsPlaying)
		{
			ChainBadgeText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			ChainBadgeText->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (OnBeatFlashTimer > 0.0f)
			{
				const FString BeatBadge = (Aspect == EMegaJamAspectMode::UltraPanavision_276_1)
					? FString::Printf(TEXT("★ QUICK-DRAW BEAT-SLASH! (+15 FILM // CHAIN x%d)"), SpliceChain)
					: FString::Printf(TEXT("★ ON-BEAT CUT! (+15 FILM // CHAIN x%d)"), SpliceChain);
				ChainBadgeText->SetText(FText::FromString(BeatBadge));
				ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.18f, 1.0f)));
			}
			else if (DropCutFlash > 0.0f)
			{
				const FString DropBadge = (Aspect == EMegaJamAspectMode::UltraPanavision_276_1)
					? FString::Printf(TEXT("★ 2.76:1 ULTRA-PANAVISION! (+30%% SPEED // STANDOFF LOCK // CHAIN x%d)"), SpliceChain)
					: FString::Printf(TEXT("★ 21:9 BULLET-TIME OVERDRIVE! (+35%% SPEED // 2x SCORE // CHAIN x%d)"), SpliceChain);
				ChainBadgeText->SetText(FText::FromString(DropBadge));
				ChainBadgeText->SetColorAndOpacity(FSlateColor(
					(Aspect == EMegaJamAspectMode::UltraPanavision_276_1)
						? FLinearColor(1.0f, 0.85f, 0.25f, 1.0f)
						: FLinearColor(0.15f, 0.95f, 1.0f, 1.0f)));
			}
			else if (DollyBoostFlash > 0.0f)
			{
				ChainBadgeText->SetText(FText::FromString(FString::Printf(
					TEXT("⚡ DOLLY BOOST RAIL! (+10 FILM // CHAIN x%d)"),
					SpliceChain)));
				ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(0.25f, 1.0f, 0.85f, 1.0f)));
			}
			else if (MatchCutFlash > 0.0f)
			{
				ChainBadgeText->SetText(FText::FromString(FString::Printf(
					TEXT("★ MATCH CUT! // CHAIN x%d (+%d PTS)"),
					SpliceChain, SpliceChain * 150)));
				ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.88f, 0.15f, 1.0f)));
			}
			else
			{
				const int32 FramesSpliced = GameMode ? GameMode->GetTotalFramesCollected() : 0;
				ChainBadgeText->SetText(FText::FromString(FString::Printf(TEXT("SPLICE CHAIN x%d  //  FRAMES: %d"), SpliceChain, FramesSpliced)));
				ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.88f, 0.88f, 0.95f)));
			}
		}
	}

	// 6. Top-Center 2.5s Scene Slugline flash
	if (SceneSluglineBorder)
	{
		if (bIsPlaying && SceneSluglineTimer > 0.0f)
		{
			SceneSluglineTimer = FMath::Max(0.0f, SceneSluglineTimer - InDeltaTime);
			SceneSluglineBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			SceneSluglineBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 7. Center Clapperboard flash overlay (Crashed / Action! / 21:9 Overdrive / 2.76:1 Standoff)
	if (CenterBannerBorder && CenterBannerText)
	{
		if (RunState == EMegaJamRunState::Crashed)
		{
			CenterBannerBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			CenterBannerText->SetText(FText::FromString(
				bIsWestern ? TEXT("CUT! // REWINDING SCENE 02 TO 1,800m...") : TEXT("CUT! // REWINDING SCENE 01 TO 0m...")));
			CenterBannerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.2f, 0.2f, 1.0f)));
		}
		else if (bIsPlaying && ActionFlashTimer > 0.0f)
		{
			ActionFlashTimer = FMath::Max(0.0f, ActionFlashTimer - InDeltaTime);
			CenterBannerBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			CenterBannerText->SetText(FText::FromString(FString::Printf(TEXT("%s // TAKE %02d // ROLL CAMERA!"), SceneTitleLabel, TakeNumber)));
			CenterBannerText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f)));
		}
		else if (bIsPlaying && DropCutBannerTimer > 0.0f)
		{
			CenterBannerBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			CenterBannerText->SetText(FText::FromString(
				(Aspect == EMegaJamAspectMode::UltraPanavision_276_1)
					? TEXT("▶ 2.76:1 ULTRA-PANAVISION // [SPACE] QUICK-DRAW BEAT-SLASH!")
					: TEXT("▶ 21:9 DIRECTOR'S CUT // BULLET-TIME OVERDRIVE!")));
			CenterBannerText->SetColorAndOpacity(FSlateColor(
				(Aspect == EMegaJamAspectMode::UltraPanavision_276_1)
					? FLinearColor(1.0f, 0.85f, 0.25f, 1.0f)
					: FLinearColor(0.15f, 0.95f, 1.0f, 1.0f)));
		}
		else
		{
			CenterBannerBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
