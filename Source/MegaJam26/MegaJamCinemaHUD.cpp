// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaJamCinemaHUD.h"

#include "MegaJamFlowPawn.h"
#include "MegaJamTrackGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
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

	// Top letterbox bar (anchored across top: Min(0,0) -> Max(1,0), Pivot(0,0))
	TopMatteBar = CreateMatteBar(TEXT("TopMatteBar"));
	if (UCanvasPanelSlot* TopSlot = RootCanvas->AddChildToCanvas(TopMatteBar))
	{
		TopSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
		TopSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		TopSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, LetterboxBarHeight));
		TopSlot->SetZOrder(1);
	}

	// Bottom letterbox bar (anchored across bottom: Min(0,1) -> Max(1,1), Pivot(0,1))
	BottomMatteBar = CreateMatteBar(TEXT("BottomMatteBar"));
	if (UCanvasPanelSlot* BottomSlot = RootCanvas->AddChildToCanvas(BottomMatteBar))
	{
		BottomSlot->SetAnchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f));
		BottomSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		BottomSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, LetterboxBarHeight));
		BottomSlot->SetZOrder(1);
	}

	// Left pillarbox bar (anchored along left: Min(0,0) -> Max(0,1), Pivot(0,0))
	LeftMatteBar = CreateMatteBar(TEXT("LeftMatteBar"));
	if (UCanvasPanelSlot* LeftSlot = RootCanvas->AddChildToCanvas(LeftMatteBar))
	{
		LeftSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 1.0f));
		LeftSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		LeftSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
		LeftSlot->SetZOrder(1);
	}

	// Right pillarbox bar (anchored along right: Min(1,0) -> Max(1,1), Pivot(1,0))
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

	// Top-Right Match-Cut Chain & Stale Shot Badge (directly below TopRightText)
	ChainBadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChainBadgeText"));
	ChainBadgeText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 16));
	ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.92f, 1.0f)));
	ChainBadgeText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	ChainBadgeText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	ChainBadgeText->SetJustification(ETextJustify::Right);
	ChainBadgeText->SetText(FText::FromString(TEXT("CHAIN x1")));
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
		SlugSlot->SetPosition(FVector2D(0.0f, 96.0f));
		SlugSlot->SetAutoSize(true);
		SlugSlot->SetZOrder(15);
	}

	// Center Clapperboard flash overlay
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

	return Super::RebuildWidget();
}

void UMegaJamCinemaHUD::NativeConstruct()
{
	Super::NativeConstruct();

	CurrentLetterboxHeight = LetterboxBarHeight;
	CurrentPillarboxWidth = 0.0f;
	ActionFlashTimer = ActionFlashDuration;
	SceneSluglineTimer = SceneSluglineFlashDuration;
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
	if (NewState == EMegaJamRunState::ActiveFlow)
	{
		ActionFlashTimer = ActionFlashDuration;
		SceneSluglineTimer = SceneSluglineFlashDuration;
		if (SceneSluglineText)
		{
			SceneSluglineText->SetText(FText::FromString(GetSceneSluglineText(EMegaJamFilmScene::Soundstage)));
		}
	}
}

void UMegaJamCinemaHUD::HandleFilmSceneChanged(EMegaJamFilmScene NewScene)
{
	SceneSluglineTimer = SceneSluglineFlashDuration;
	LastObservedSceneIndex = static_cast<int32>(NewScene);
	if (SceneSluglineText)
	{
		SceneSluglineText->SetText(FText::FromString(GetSceneSluglineText(NewScene)));
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

	const EMegaJamAspectMode Aspect = FlowPawn ? FlowPawn->GetAspectMode() : EMegaJamAspectMode::Wide_21_9;
	const EMegaJamTakeState TakeState = FlowPawn ? FlowPawn->GetCurrentTake() : EMegaJamTakeState::StudioTake;
	const float FlowRatio = FlowPawn ? FlowPawn->GetFlowRatio() : 0.0f;

	// Poll scene transitions from BoundTrackGenerator in case delegate fired before binding
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
		ActionFlashTimer = ActionFlashDuration;
	}

	// Update Top-Left readout: REC ● TAKE XX // MM:SS:FF
	if (TopLeftText)
	{
		const FString Timecode = GameMode ? GameMode->GetFormattedTimecode() : TEXT("00:00:00");
		const bool bRecBlink = FMath::Fmod(World->GetTimeSeconds(), 1.0) < 0.65;
		const TCHAR* RecDot = bRecBlink ? TEXT("●") : TEXT("○");
		TopLeftText->SetText(FText::FromString(FString::Printf(TEXT("REC %s TAKE %02d  //  %s"), RecDot, TakeNumber, *Timecode)));
	}

	// Update Top-Right readout: STUDIO CUT // TAKE A vs DIRECTOR'S CUT // TAKE B + FLOW + CHAIN
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

	// Update Chain Badge (flashing gold on MATCH CUT!, pulsing amber on STALE SHOT)
	if (ChainBadgeText)
	{
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
			ChainBadgeText->SetText(FText::FromString(FString::Printf(TEXT("SPLICE CHAIN x%d"), SpliceChain)));
			ChainBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.88f, 0.88f, 0.95f)));
		}
	}

	// Update Top-Center 2.5s Scene Slugline flash
	if (SceneSluglineBorder)
	{
		if (SceneSluglineTimer > 0.0f)
		{
			SceneSluglineTimer = FMath::Max(0.0f, SceneSluglineTimer - InDeltaTime);
			SceneSluglineBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			SceneSluglineBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Update Center Clapperboard flash overlay
	if (CenterBannerBorder && CenterBannerText)
	{
		const EMegaJamRunState RunState = GameMode ? GameMode->GetRunState() : EMegaJamRunState::ActiveFlow;
		if (RunState == EMegaJamRunState::Crashed)
		{
			CenterBannerBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			CenterBannerText->SetText(FText::FromString(TEXT("CUT! // REWINDING...")));
			CenterBannerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.2f, 0.2f, 1.0f)));
		}
		else if (RunState == EMegaJamRunState::Completed)
		{
			CenterBannerBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
			CenterBannerText->SetText(FText::FromString(FString::Printf(
				TEXT("FINAL CUT // IT'S A WRAP!\nTAKE %02d  //  SCORE %06d"),
				TakeNumber, GameMode ? FMath::RoundToInt(GameMode->GetScore()) : 0)));
			CenterBannerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.2f, 1.0f)));
		}
		else if (ActionFlashTimer > 0.0f)
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
