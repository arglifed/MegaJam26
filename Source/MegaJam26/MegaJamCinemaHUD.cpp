// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamCinemaHUD.h"

#include "MegaJamFlowPawn.h"
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
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UMegaJamCinemaHUD::RebuildWidget()
{
	if (!WidgetTree)
	{
		return Super::RebuildWidget();
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

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
	TopLeftText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
	TopLeftText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f)));
	TopLeftText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TopLeftText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	TopLeftText->SetText(FText::FromString(TEXT("REC ● TAKE 01  //  00:00:00")));
	if (UCanvasPanelSlot* TLSlot = RootCanvas->AddChildToCanvas(TopLeftText))
	{
		TLSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		TLSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		TLSlot->SetPosition(FVector2D(36.0f, 26.0f));
		TLSlot->SetAutoSize(true);
		TLSlot->SetZOrder(10);
	}

	// Dev God Mode Badge (only visible when bGodModeEnabled is true)
	DevBadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevBadgeText"));
	DevBadgeText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
	DevBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.72f, 0.12f, 1.0f)));
	DevBadgeText->SetShadowOffset(FVector2D(1.5f, 1.5f));
	DevBadgeText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	DevBadgeText->SetText(FText::FromString(TEXT("[DEV: GOD MODE | HITS: 0]")));
	DevBadgeText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* DevSlot = RootCanvas->AddChildToCanvas(DevBadgeText))
	{
		DevSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		DevSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		DevSlot->SetPosition(FVector2D(36.0f, 54.0f));
		DevSlot->SetAutoSize(true);
		DevSlot->SetZOrder(10);
	}

	// Top-Center Act Progress Readout & Progress Bar (visible during active gameplay)
	ActProgressBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ActProgressBorder"));
	ActProgressBorder->SetBrushColor(FLinearColor(0.04f, 0.04f, 0.04f, 0.78f));
	ActProgressBorder->SetPadding(FMargin(14.0f, 5.0f));

	UVerticalBox* ActBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ActBox"));
	ActProgressText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ActProgressText"));
	ActProgressText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	ActProgressText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.25f, 1.0f)));
	ActProgressText->SetJustification(ETextJustify::Center);
	ActProgressText->SetText(FText::FromString(TEXT("ACT I / IV  —  INT. SOUNDSTAGE (0%)")));
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
		ActSlot->SetPosition(FVector2D(0.0f, 78.0f));
		ActSlot->SetSize(FVector2D(280.0f, 44.0f));
		ActSlot->SetAutoSize(false);
		ActSlot->SetZOrder(12);
	}

	// Top-Right Active Take & Flow Multiplier readout
	TopRightText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TopRightText"));
	TopRightText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
	TopRightText->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.9f, 1.0f, 1.0f)));
	TopRightText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TopRightText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	TopRightText->SetJustification(ETextJustify::Right);
	TopRightText->SetText(FText::FromString(TEXT("STUDIO CUT // TAKE A  [21:9]  //  FLOW 1.0x (0%)")));
	if (UCanvasPanelSlot* TRSlot = RootCanvas->AddChildToCanvas(TopRightText))
	{
		TRSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
		TRSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		TRSlot->SetPosition(FVector2D(-36.0f, 18.0f));
		TRSlot->SetAutoSize(true);
		TRSlot->SetZOrder(10);
	}

	// Top-Right Match-Cut Chain & Stale Shot Badge
	ChainBadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChainBadgeText"));
	ChainBadgeText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 16));
	ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.92f, 1.0f)));
	ChainBadgeText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	ChainBadgeText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	ChainBadgeText->SetJustification(ETextJustify::Right);
	ChainBadgeText->SetText(FText::FromString(TEXT("SPLICE CHAIN x1")));
	if (UCanvasPanelSlot* ChainSlot = RootCanvas->AddChildToCanvas(ChainBadgeText))
	{
		ChainSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
		ChainSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		ChainSlot->SetPosition(FVector2D(-36.0f, 48.0f));
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
	AddTitleLine(TEXT("TitleControls1"), TEXT("[A / D] STRAFE   |   [SPACE / SHIFT] HARD CUT (TAKE A / B)"), 16, FLinearColor(0.25f, 0.92f, 1.0f, 1.0f), 6.0f);
	AddTitleLine(TEXT("TitleControls2"), TEXT("[LEFT CTRL / C] ASPECT RATIO (21:9 WIDE / 4:3 VERTICAL + W/S)"), 16, FLinearColor(1.0f, 0.55f, 0.2f, 1.0f), 16.0f);
	AddTitleLine(TEXT("TitleObj"), TEXT("SPLICE MATCH-CUTS & COLLECT FILM FRAMES TO BUILD YOUR CHAIN (x1–x8)"), 15, FLinearColor(0.92f, 0.92f, 0.92f, 1.0f), 24.0f);
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
	switch (Scene)
	{
	case EMegaJamFilmScene::Soundstage:
		return TEXT("SCENE 01 // INT. SOUNDSTAGE");
	case EMegaJamFilmScene::OverpassNight:
		return TEXT("SCENE 02 // EXT. OVERPASS - NIGHT");
	case EMegaJamFilmScene::CuttingRoom:
		return TEXT("SCENE 03 // INT. CUTTING ROOM");
	case EMegaJamFilmScene::FinalCut:
	default:
		return TEXT("SCENE 04 // THE FINAL CUT");
	}
}

FString UMegaJamCinemaHUD::GetActProgressLabel(EMegaJamFilmScene Scene)
{
	switch (Scene)
	{
	case EMegaJamFilmScene::Soundstage:
		return TEXT("ACT I / IV  —  INT. SOUNDSTAGE");
	case EMegaJamFilmScene::OverpassNight:
		return TEXT("ACT II / IV  —  EXT. OVERPASS - NIGHT");
	case EMegaJamFilmScene::CuttingRoom:
		return TEXT("ACT III / IV  —  INT. CUTTING ROOM");
	case EMegaJamFilmScene::FinalCut:
	default:
		return TEXT("ACT IV / IV  —  THE FINAL CUT");
	}
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

	const AMegaJamGameMode* GameMode = World->GetAuthGameMode<AMegaJamGameMode>();
	const AMegaJamFlowPawn* FlowPawn = Cast<AMegaJamFlowPawn>(GetOwningPlayerPawn());

	const EMegaJamRunState RunState = GameMode ? GameMode->GetRunState() : EMegaJamRunState::Playing;
	const bool bIsPlaying = GameMode ? GameMode->IsPlaying() : true;
	const EMegaJamAspectMode Aspect = FlowPawn ? FlowPawn->GetAspectMode() : EMegaJamAspectMode::Wide_21_9;
	const EMegaJamTakeState TakeState = FlowPawn ? FlowPawn->GetCurrentTake() : EMegaJamTakeState::StudioTake;
	const float FlowRatio = FlowPawn ? FlowPawn->GetFlowRatio() : 0.0f;

	if (BoundTrackGenerator)
	{
		const int32 CurrentSceneIdx = static_cast<int32>(BoundTrackGenerator->GetCurrentScene());
		if (CurrentSceneIdx != LastObservedSceneIndex)
		{
			HandleFilmSceneChanged(BoundTrackGenerator->GetCurrentScene());
		}
	}

	// Smoothly interpolate letterbox vs pillarbox matte bars
	const float TargetLetterbox = (Aspect == EMegaJamAspectMode::Wide_21_9) ? LetterboxBarHeight : 0.0f;
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

			if (WrapStatsText)
			{
				WrapStatsText->SetText(FText::FromString(FString::Printf(
					TEXT("FINAL SCORE: %06d\nMAX MATCH-CUT CHAIN: x%d\nFILM FRAMES SPLICED: %d\nTOTAL TAKES USED: %02d"),
					FinalScore, MaxChain, FramesSpliced, TakeNumber)));
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

	// 3. Top-Center Act Progress Bar (visible during active gameplay)
	if (ActProgressBorder)
	{
		if (bIsPlaying)
		{
			ActProgressBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			const float RunTime = GameMode ? GameMode->GetRunTime() : 0.0f;
			const float Duration = (GameMode && GameMode->RunDuration > 0.0f) ? GameMode->RunDuration : 150.0f;
			const float ProgressRatio = FMath::Clamp(RunTime / Duration, 0.0f, 1.0f);
			const EMegaJamFilmScene CurrentScene = BoundTrackGenerator ? BoundTrackGenerator->GetCurrentScene() : EMegaJamFilmScene::Soundstage;

			if (ActProgressBar)
			{
				ActProgressBar->SetPercent(ProgressRatio);
			}
			if (ActProgressText)
			{
				ActProgressText->SetText(FText::FromString(FString::Printf(
					TEXT("%s  (%d%%)"),
					*GetActProgressLabel(CurrentScene),
					FMath::RoundToInt(ProgressRatio * 100.0f))));
			}
		}
		else
		{
			ActProgressBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 4. Top-Left readout: REC ● TAKE XX // MM:SS:FF
	if (TopLeftText)
	{
		if (RunState == EMegaJamRunState::TitleScreen)
		{
			TopLeftText->SetText(FText::FromString(TEXT("STANDBY ○ CAMERA READY  //  00:00:00")));
		}
		else
		{
			const FString Timecode = GameMode ? GameMode->GetFormattedTimecode() : TEXT("00:00:00");
			const bool bRecBlink = FMath::Fmod(World->GetTimeSeconds(), 1.0) < 0.65;
			const TCHAR* RecDot = bRecBlink ? TEXT("●") : TEXT("○");
			TopLeftText->SetText(FText::FromString(FString::Printf(TEXT("REC %s TAKE %02d  //  %s"), RecDot, TakeNumber, *Timecode)));
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
	const bool bShotStale = FlowPawn ? FlowPawn->IsShotStale() : false;
	const float MatchCutFlash = FlowPawn ? FlowPawn->GetMatchCutFlashTimer() : 0.0f;

	if (TopRightText)
	{
		const bool bDirectorsTake = (TakeState == EMegaJamTakeState::DirectorsTake);
		const TCHAR* TakeLabel = bDirectorsTake ? TEXT("DIRECTOR'S CUT // TAKE B") : TEXT("STUDIO CUT // TAKE A");
		const TCHAR* AspectLabel = (Aspect == EMegaJamAspectMode::Wide_21_9) ? TEXT("21:9") : TEXT("4:3");
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
			if (MatchCutFlash > 0.0f)
			{
				ChainBadgeText->SetText(FText::FromString(FString::Printf(
					TEXT("★ MATCH CUT! // CHAIN x%d (+%d PTS)"),
					SpliceChain, SpliceChain * 150)));
				ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.88f, 0.15f, 1.0f)));
			}
			else if (bShotStale)
			{
				const float Pulse = 0.55f + 0.45f * FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * 10.0f);
				ChainBadgeText->SetText(FText::FromString(FString::Printf(
					TEXT("⚠ STALE SHOT (SWITCH ASPECT) // CHAIN x%d"),
					SpliceChain)));
				ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.55f * Pulse, 0.05f, 1.0f)));
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

	// 7. Center Clapperboard flash overlay (Crashed / Action!)
	if (CenterBannerBorder && CenterBannerText)
	{
		if (RunState == EMegaJamRunState::Crashed)
		{
			const int32 CheckpointAct = FMath::Clamp(LastObservedSceneIndex + 1, 1, 4);
			CenterBannerBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			CenterBannerText->SetText(FText::FromString(FString::Printf(TEXT("CUT! // REWINDING TO ACT %d CHECKPOINT..."), CheckpointAct)));
			CenterBannerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.2f, 0.2f, 1.0f)));
		}
		else if (bIsPlaying && ActionFlashTimer > 0.0f)
		{
			ActionFlashTimer = FMath::Max(0.0f, ActionFlashTimer - InDeltaTime);
			const int32 ActiveSceneNum = FMath::Clamp(LastObservedSceneIndex + 1, 1, 4);
			CenterBannerBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			CenterBannerText->SetText(FText::FromString(FString::Printf(TEXT("SCENE %02d // TAKE %02d // ACTION!"), ActiveSceneNum, TakeNumber)));
			CenterBannerText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f)));
		}
		else
		{
			CenterBannerBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
