#include "UI/SEphemeraHud.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Frame/EphemeraFrameSubsystem.h"
#include "Frame/EphemeraSkyRig.h"
#include "Misc/App.h"
#include "Player/EphemeraPlayer.h"
#include "Player/EphemeraUserSettings.h"
#include "ShaderCompiler.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

#define LOCTEXT_NAMESPACE "EphemeraHud"

namespace
{
	const FSlateColorBrush PanelBrush(FLinearColor(0.012f, 0.016f, 0.02f, 0.9f));
	const FSlateColorBrush NoticeBrush(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	const FSlateColorBrush LoadingBrush(FLinearColor(0.008f, 0.01f, 0.012f, 1.f));
	const FSlateColorBrush BarBrush(FLinearColor::White);
	const FLinearColor Ink(0.86f, 0.88f, 0.86f);
	const FLinearColor Dim(0.55f, 0.58f, 0.57f);
	const FLinearColor Chosen(0.32f, 0.62f, 0.52f);
	const FLinearColor Plain(0.16f, 0.18f, 0.19f);
	const FLinearColor Warn(0.95f, 0.5f, 0.28f);

	FSlateFontInfo Font(const int32 Size, const bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	TSharedRef<SWidget> Label(const FText& Text, const int32 Size = 11, const FLinearColor& Colour = Ink)
	{
		return SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Colour);
	}

	/** One line of the menu: what it is on the left, the control on the right. */
	TSharedRef<SWidget> Row(const FText& Name, const TSharedRef<SWidget>& Control)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.42f).VAlign(VAlign_Center)[Label(Name)]
			+ SHorizontalBox::Slot().FillWidth(0.58f).VAlign(VAlign_Center)[Control];
	}

	/** A row of buttons of which one is the current choice. */
	TSharedRef<SWidget> Choice(const TArray<FText>& Names, TFunction<int32()> Current, TFunction<void(int32)> Pick)
	{
		const TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			Box->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.ButtonColorAndOpacity_Lambda([Current, Index]() -> FSlateColor { return FSlateColor(Current() == Index ? Chosen : Plain); })
				.OnClicked_Lambda([Pick, Index] { Pick(Index); return FReply::Handled(); })
				[
					Label(Names[Index], 10)
				]
			];
		}
		return Box;
	}

	/** A slider with its value written beside it. */
	TSharedRef<SWidget> Slide(const float Low, const float High, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText(float)> Show)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([Get, Low, High] { return FMath::Clamp((Get() - Low) / (High - Low), 0.f, 1.f); })
				.OnValueChanged_Lambda([Set, Low, High](const float Alpha) { Set(FMath::Lerp(Low, High, Alpha)); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(64.f)
				[
					SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text_Lambda([Get, Show] { return Show(Get()); })
				]
			];
	}

	TSharedRef<SWidget> Check(TFunction<bool()> Get, TFunction<void(bool)> Set)
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([Get] { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Set](const ECheckBoxState State) { Set(State == ECheckBoxState::Checked); });
	}

	AEphemeraSkyRig* FindSky(const UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		const TActorIterator<AEphemeraSkyRig> First(World);
		return First ? *First : nullptr;
	}
}

void SEphemeraHud::Construct(const FArguments& InArgs)
{
	Player = InArgs._Player;
	SetVisibility(EVisibility::SelfHitTestInvisible);
	ChildSlot
	[
		SNew(SOverlay)
		// Frame rate, top right.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 14.f, 18.f, 0.f)
		[
			SNew(STextBlock)
			.Font(Font(11, true))
			.ColorAndOpacity(Ink)
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f))
			.Visibility_Lambda([] { return UEphemeraUserSettings::Get()->bShowFps ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.Text_Lambda([this]
			{
				return FText::Format(LOCTEXT("Fps", "{0} fps   {1} ms"), FText::AsNumber(FMath::RoundToInt32(1000.f / FMath::Max(FrameMs, 0.1f))),
					FText::AsNumber(FMath::RoundToInt32(FrameMs * 10.f) / 10.f));
			})
		]
		// What is still being prepared, top centre.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(&NoticeBrush)
			.Padding(FMargin(14.f, 6.f))
			.Visibility_Lambda([this] { return Preparing.IsEmpty() || bLoading ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
			[
				SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Ink).Text_Lambda([this] { return Preparing; })
			]
		]
		// The jets' tank, bottom centre: a thin line that is there only while it is not full.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 46.f)
		[
			SNew(SBox).WidthOverride(180.f).HeightOverride(3.f)
			.Visibility_Lambda([this] { return FuelShow > 0.01f && !bLoading ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBorder).BorderImage(&BarBrush)
					.BorderBackgroundColor_Lambda([this]() -> FSlateColor { return FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.45f * FuelShow)); })
				]
				+ SOverlay::Slot().HAlign(HAlign_Left)
				[
					SNew(SBox).WidthOverride_Lambda([this]() -> FOptionalSize { return FOptionalSize(180.f * FuelNow); })
					[
						SNew(SBorder).BorderImage(&BarBrush)
						.BorderBackgroundColor_Lambda([this]() -> FSlateColor
						{
							const FLinearColor& Colour = FuelNow < 0.2f ? Warn : Ink;
							return FSlateColor(FLinearColor(Colour.R, Colour.G, Colour.B, 0.85f * FuelShow));
						})
					]
				]
			]
		]
		// What a key just did, bottom centre, fading out.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 70.f)
		[
			SNew(SBorder)
			.BorderImage(&NoticeBrush)
			.Padding(FMargin(16.f, 7.f))
			.BorderBackgroundColor_Lambda([this]() -> FSlateColor { return FSlateColor(FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(NoteLeft / 0.5f, 0.f, 1.f))); })
			.Visibility_Lambda([this] { return NoteLeft > 0.f && !bLoading ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(STextBlock).Font(Font(11)).Text_Lambda([this] { return NoteText; })
				.ColorAndOpacity_Lambda([this]() -> FSlateColor { return FSlateColor(FLinearColor(Ink.R, Ink.G, Ink.B, FMath::Clamp(NoteLeft / 0.5f, 0.f, 1.f))); })
			]
		]
		// The loading screen: over the world and the notices, under the menu.
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.BorderImage(&LoadingBrush)
			.BorderBackgroundColor_Lambda([this]() -> FSlateColor { return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Veil)); })
			.Visibility_Lambda([this] { return Veil > 0.002f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("Game", "E P H E M E R A")).Font(Font(30))
					.ColorAndOpacity_Lambda([this]() -> FSlateColor { return FSlateColor(FLinearColor(Ink.R, Ink.G, Ink.B, Veil)); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 26.f, 0.f, 0.f)
				[
					// The bar: a dim track with the part done drawn over it.
					SNew(SBox).WidthOverride(460.f).HeightOverride(4.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SBorder).BorderImage(&BarBrush)
							.BorderBackgroundColor_Lambda([this]() -> FSlateColor { return FSlateColor(FLinearColor(Plain.R, Plain.G, Plain.B, Veil)); })
						]
						+ SOverlay::Slot().HAlign(HAlign_Left)
						[
							SNew(SBox).WidthOverride_Lambda([this]() -> FOptionalSize { return FOptionalSize(460.f * LoadingProgress); })
							[
								SNew(SBorder).BorderImage(&BarBrush)
								.BorderBackgroundColor_Lambda([this]() -> FSlateColor { return FSlateColor(FLinearColor(Chosen.R, Chosen.G, Chosen.B, Veil)); })
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(Font(11))
					.ColorAndOpacity_Lambda([this]() -> FSlateColor { return FSlateColor(FLinearColor(Dim.R, Dim.G, Dim.B, Veil)); })
					.Text_Lambda([this]
					{
						// Three dots that come and go, so a long wait is visibly alive.
						const int32 Dots = 1 + static_cast<int32>(FPlatformTime::Seconds() * 2.0) % 3;
						return FText::FromString(FString::Printf(TEXT("%s%s   %d%%"), *LoadingWhat.ToString(), *FString::ChrN(Dots, TEXT('.')),
							FMath::RoundToInt32(LoadingProgress * 100.f)));
					})
				]
			]
		]
		// The menu, centred.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(760.f)
			.Visibility_Lambda([this] { return bMenuOpen ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBorder).BorderImage(&PanelBrush).Padding(FMargin(26.f, 22.f))
				[
					BuildMenu()
				]
			]
		]
	];
}

TSharedRef<SWidget> SEphemeraHud::BuildMenu()
{
	UEphemeraUserSettings* S = UEphemeraUserSettings::Get();
	const TWeakObjectPtr<AEphemeraPlayer> Who = Player;
	auto Rendering = [S] { S->ApplyRendering(); S->Save(); };
	const TArray<FName> Skies = {TEXT("clear"), TEXT("fronts"), TEXT("overcast")};
	const TArray<int32> Limits = {0, 30, 60, 120};
	const TArray<float> Speeds = {0.f, 1.f, 10.f, 60.f};
	auto Frames = [Who]() -> UEphemeraFrameSubsystem*
	{
		return Who.IsValid() && Who->GetWorld() ? Who->GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>() : nullptr;
	};
	const FMargin Gap(0.f, 0.f, 0.f, 9.f);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)[Label(LOCTEXT("Title", "Settings"), 18)]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Weather", "Weather"), Choice({LOCTEXT("Clear", "Clear"), LOCTEXT("Fronts", "Fronts"), LOCTEXT("Overcast", "Overcast")},
				[S, Skies] { return Skies.IndexOfByKey(FName(*S->Weather)); },
				[S, Skies, Who](const int32 Index)
				{
					S->Weather = Skies[Index].ToString();
					S->Save();
					if (AEphemeraSkyRig* Sky = FindSky(Who.IsValid() ? Who->GetWorld() : nullptr))
					{
						Sky->SetWeather(Skies[Index]);
					}
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Time", "Passage of time"), Choice({LOCTEXT("Stopped", "Paused"), LOCTEXT("Real", "Normal"), LOCTEXT("Ten", "x10"), LOCTEXT("Sixty", "x60")},
				[Frames, Speeds]
				{
					const UEphemeraFrameSubsystem* F = Frames();
					return F ? Speeds.IndexOfByPredicate([F](const float Speed) { return FMath::IsNearlyEqual(Speed, static_cast<float>(F->TimeScale)); }) : 1;
				},
				[Frames, Speeds](const int32 Index)
				{
					if (UEphemeraFrameSubsystem* F = Frames())
					{
						F->TimeScale = Speeds[Index];
					}
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Quality", "Graphics quality"), Choice({LOCTEXT("Low", "Low"), LOCTEXT("Medium", "Medium"), LOCTEXT("High", "High"), LOCTEXT("Epic", "Highest")},
				[S] { return FMath::Clamp(S->Quality, 0, 3); },
				[S, Rendering](const int32 Index) { S->Quality = Index; Rendering(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			// The picture is drawn at a share of the screen's size and rebuilt to full size by the engine's
			// temporal upscaler (TSR). At 77% on a 1440p screen it is drawn at about 1080p, which TSR
			// rebuilds to within a hair of native, for about two fifths more frames.
			Row(LOCTEXT("Scale", "Render resolution"), Choice({LOCTEXT("Native", "Native (100%)"), LOCTEXT("ScaleQuality", "Quality (77%)"),
					LOCTEXT("ScaleBalanced", "Balanced (67%)"), LOCTEXT("ScalePerformance", "Performance (58%)")},
				[S]
				{
					const float Shares[] = {100.f, 77.f, 67.f, 58.f};
					int32 Best = 0;
					for (int32 Index = 1; Index < 4; ++Index)
					{
						if (FMath::Abs(Shares[Index] - S->ScreenPercentage) < FMath::Abs(Shares[Best] - S->ScreenPercentage))
						{
							Best = Index;
						}
					}
					return Best;
				},
				[S, Rendering](const int32 Index)
				{
					const float Shares[] = {100.f, 77.f, 67.f, 58.f};
					S->ScreenPercentage = Shares[FMath::Clamp(Index, 0, 3)];
					Rendering();
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Rock", "Rock detail as geometry (very slow)"), SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Check([] { return FEphemeraTerrain::ReliefIsGeometry(); },
						[S, Rendering, Who](const bool bOn)
						{
							S->RockGeometry = bOn ? 1 : 0;
							Rendering();
							if (Who.IsValid())
							{
								Who->ReliefChanged();
							}
						})
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					Label(LOCTEXT("RockNote", "sharper cliffs and stones; heavy, very slow on a Mac"), 9, Dim)
				])
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Fov", "Field of view"), Slide(50.f, 100.f, [S] { return S->FieldOfView; },
				[S, Who](const float Value)
				{
					S->FieldOfView = FMath::RoundToFloat(Value);
					S->Save();
					if (Who.IsValid())
					{
						Who->ApplyUserSettings();
					}
				},
				[](const float Value) { return FText::AsNumber(FMath::RoundToInt32(Value)); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Mouse", "Mouse sensitivity"), Slide(0.2f, 3.f, [S] { return S->MouseSensitivity; },
				[S, Who](const float Value)
				{
					S->MouseSensitivity = FMath::RoundToFloat(Value * 20.f) / 20.f;
					S->Save();
					if (Who.IsValid())
					{
						Who->ApplyUserSettings();
					}
				},
				[](const float Value) { return FText::AsNumber(FMath::RoundToInt32(Value * 100.f) / 100.f); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Invert", "Invert looking up and down"), Check([S] { return S->bInvertLook; }, [S](const bool bOn) { S->bInvertLook = bOn; S->Save(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Motion", "Camera motion"), SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
				[
					Slide(0.f, 1.5f, [S] { return S->CameraMotion; },
						[S](const float Value) { S->CameraMotion = FMath::RoundToFloat(Value * 20.f) / 20.f; S->Save(); },
						[](const float Value) { return FText::Format(LOCTEXT("Percent", "{0}%"), FText::AsNumber(FMath::RoundToInt32(Value * 100.f))); })
				]
				+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
				[
					Label(LOCTEXT("MotionNote", "sway, landing dip, lean; 0 for none"), 9, Dim)
				])
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Volume", "Sound volume"), Slide(0.f, 1.f, [S] { return S->Volume; },
				[S](const float Value) { S->Volume = FMath::RoundToFloat(Value * 20.f) / 20.f; S->Save(); },
				[](const float Value) { return FText::Format(LOCTEXT("Percent", "{0}%"), FText::AsNumber(FMath::RoundToInt32(Value * 100.f))); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Music", "Music volume"), Slide(0.f, 1.f, [S] { return S->MusicVolume; },
				[S](const float Value) { S->MusicVolume = FMath::RoundToFloat(Value * 20.f) / 20.f; S->Save(); },
				[](const float Value) { return FText::Format(LOCTEXT("Percent", "{0}%"), FText::AsNumber(FMath::RoundToInt32(Value * 100.f))); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Limit", "Frame rate limit"), Choice({LOCTEXT("NoLimit", "None"), LOCTEXT("Thirty", "30"), LOCTEXT("SixtyFps", "60"), LOCTEXT("OneTwenty", "120")},
				[S, Limits] { return FMath::Max(Limits.IndexOfByKey(S->FrameLimit), 0); },
				[S, Limits, Rendering](const int32 Index) { S->FrameLimit = Limits[Index]; Rendering(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("VSync", "Vertical sync"), Check([S] { return S->bVSync; }, [S, Rendering](const bool bOn) { S->bVSync = bOn; Rendering(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("Latency", "Low latency"), SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Check([S] { return S->bLowLatency; }, [S, Rendering](const bool bOn) { S->bLowLatency = bOn; Rendering(); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					Label(LOCTEXT("LatencyNote", "no frames wait in a queue: keys and mouse are answered sooner"), 9, Dim)
				])
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Gap)
		[
			Row(LOCTEXT("ShowFps", "Show frame rate"), Check([S] { return S->bShowFps; }, [S](const bool bOn) { S->bShowFps = bOn; S->Save(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 12.f)
		[
			Label(LOCTEXT("Keys", "W A S D  move      Shift  run, boost      Space  jump, hold to fly      Ctrl  down      F  lamp\nV  free flight      mouse wheel  its speed      G  level horizon      Esc or F1  this menu      F3  frame rate\nMusic by Scott Buckley (scottbuckley.com.au), CC BY 4.0"), 9, Dim)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SButton).HAlign(HAlign_Center).ButtonColorAndOpacity(Chosen)
				.OnClicked_Lambda([Who] { if (Who.IsValid()) { Who->ToggleMenu(); } return FReply::Handled(); })
				[
					Label(LOCTEXT("Resume", "Back to the game"))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SButton).HAlign(HAlign_Center).ButtonColorAndOpacity(Plain)
				.OnClicked_Lambda([] { FPlatformMisc::RequestExit(false); return FReply::Handled(); })
				[
					Label(LOCTEXT("Quit", "Quit"))
				]
			]
		];
}

void SEphemeraHud::SetLoading(const bool bOn, const FText& What, const float Progress)
{
	bLoading = bOn;
	if (bOn)
	{
		Veil = 1.f;
	}
	LoadingProgress = bOn ? FMath::Max(LoadingProgress, FMath::Clamp(Progress, 0.f, 1.f)) : 1.f;
	if (!What.IsEmpty())
	{
		LoadingWhat = What;
	}
}

void SEphemeraHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	NoteLeft = FMath::Max(0.f, NoteLeft - static_cast<float>(FApp::GetDeltaTime()));
	if (Player.IsValid())
	{
		FuelNow = Player->GetFuel();
		const bool bShow = !Player->IsFreeFlight() && (FuelNow < 0.995f || Player->JetsLit());
		FuelShow = FMath::FInterpTo(FuelShow, bShow ? 1.f : 0.f, static_cast<float>(FApp::GetDeltaTime()), bShow ? 14.f : 3.f);
	}
	if (!bLoading && Veil > 0.f)
	{
		Veil = FMath::Max(0.f, Veil - static_cast<float>(FApp::GetDeltaTime()) / 0.8f);
	}
	// Real time, not game time: the readout must be right while time is stopped or sped up.
	FrameMs = FMath::Lerp(FrameMs, static_cast<float>(FApp::GetDeltaTime() * 1000.0), 0.08f);
	int32 Shaders = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	int32 Assets = 0;
#if WITH_EDITOR
	Assets = FAssetCompilingManager::Get().GetNumRemainingAssets();
#endif
	if (Shaders + Assets > 0)
	{
		Preparing = FText::Format(LOCTEXT("Preparing", "Still preparing: {0} shaders, {1} meshes and textures. Things will change on screen until this is done."),
			FText::AsNumber(Shaders), FText::AsNumber(Assets));
	}
	else
	{
		Preparing = FText::GetEmpty();
	}
}

FReply SEphemeraHud::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bMenuOpen && (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F1) && Player.IsValid())
	{
		Player->ToggleMenu();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
