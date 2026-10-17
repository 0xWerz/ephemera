#include "Player/EphemeraUserSettings.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Features/IModularFeatures.h"
#include "Frame/EphemeraFramePacer.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Performance/MaxTickRateHandlerModule.h"

namespace
{
	void SetVariable(const TCHAR* Name, const float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			// As if typed at the console: nothing the project or a quality level sets may undo a
			// choice the player has just made.
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}
}

void UEphemeraUserSettings::BringUpToDate()
{
	// 1: vertical sync is off unless asked for. It used to be on, and on a 60 Hz screen that held
	// every frame of a game running at 40 back for the screen's next refresh: it ran at 30 and
	// answered each key and each turn of the mouse some 50 ms later than it need have.
	if (Version < 1)
	{
		bVSync = false;
		Version = 1;
		Save();
	}
	// 2: "Rock detail as geometry" goes back to off. Until now nothing in the menu said what it costs (five
	// times the frame on the graphics card this is made for), it is remembered between sessions, and one who
	// had tried it met every later session at seven frames a second. The menu now says "(very slow)".
	if (Version < 2)
	{
		RockGeometry = -1;
		Version = 2;
		Save();
	}
	// 3: on a screen 1440 lines tall or more the picture is drawn at 77% and rebuilt to full size by the
	// engine's upscaler (TSR): at 1440p that is about 1080p drawn, within a hair of native to look at, and
	// two fifths more frames (measured 14 October: 40 fps drawn at 1080p against about 28 at native 1440p).
	// Only from the old default; the menu's "Native (100%)" puts it back and is kept.
	if (Version < 3)
	{
		const UGameUserSettings* Screen = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (Screen && ScreenPercentage >= 99.f && Screen->GetScreenResolution().Y >= 1440)
		{
			ScreenPercentage = 77.f;
		}
		Version = 3;
		Save();
	}
	// 4: and back to native. At 77% the distant mountains came out soft and blocky to the owner, who puts the
	// picture first ("long distance mountains dont look sharp, they look pixelated"); the choice is in the menu.
	if (Version < 4)
	{
		if (FMath::IsNearlyEqual(ScreenPercentage, 77.f))
		{
			ScreenPercentage = 100.f;
		}
		Version = 4;
		Save();
	}
}

void UEphemeraUserSettings::ApplyRendering() const
{
	if (GEngine)
	{
		if (UGameUserSettings* Engine = GEngine->GetGameUserSettings())
		{
			Engine->SetOverallScalabilityLevel(FMath::Clamp(Quality, 0, 3));
			Engine->ApplyNonResolutionSettings();
		}
	}
	// -EphemeraScale=<percent>: this run only, whatever the menu says (a stream to a browser draws
	// fewer pixels to be sent more often; the saved setting is the player's own and stays as it is).
	float Scale = ScreenPercentage;
	FParse::Value(FCommandLine::Get(), TEXT("EphemeraScale="), Scale);
	SetVariable(TEXT("r.ScreenPercentage"), FMath::Clamp(Scale, 40.f, 100.f));
	SetVariable(TEXT("r.VSync"), bVSync ? 1.f : 0.f);
	// -EphemeraMaxFps=<n>: this run only (a stream to a browser must not be drawn faster than it is
	// sent: NVIDIA's encoder aborts the whole game when fed above its configured rate).
	int32 Limit = FrameLimit;
	FParse::Value(FCommandLine::Get(), TEXT("EphemeraMaxFps="), Limit);
	SetVariable(TEXT("t.MaxFPS"), static_cast<float>(FMath::Max(Limit, 0)));
	FEphemeraFramePacer::Get().SetEnabled(bLowLatency && !FParse::Param(FCommandLine::Get(), TEXT("EphemeraNoPacing")) && !FParse::Param(FCommandLine::Get(), TEXT("EphemeraReflex")));
	// NVIDIA's Reflex does the same job as the frame pacer where its driver offers it. Two things
	// holding the game back at once have not been measured together, and the pacer is the one that
	// has (and that works on every card and with no window), so Reflex is only taken when asked
	// for: -EphemeraReflex, with the pacer then left off.
	if (FParse::Param(FCommandLine::Get(), TEXT("EphemeraReflex")))
	{
		for (IMaxTickRateHandlerModule* Pacer : IModularFeatures::Get().GetModularFeatureImplementations<IMaxTickRateHandlerModule>(IMaxTickRateHandlerModule::GetModularFeatureName()))
		{
			if (Pacer && Pacer->GetAvailable() && !Pacer->GetEnabled())
			{
				Pacer->SetEnabled(true);
				Pacer->SetFlags(1);
				UE_LOG(LogTemp, Display, TEXT("Ephemera: low-latency frame pacing is on (NVIDIA Reflex)"));
			}
		}
	}
	// -EphemeraLighting=rt|traced|classic: how bounce light and reflections are found, this run only, to
	// compare them on the same views (the user, 2026-10-14: games they know look better with ray tracing
	// off, and live lighting flickers). rt: Lumen on the graphics card's ray-tracing hardware (the build's
	// own setting). traced: Lumen on its simplified copy of the scene (distance fields), no hardware rays.
	// classic: no live bounce light, as raster games light a scene: the sky's light shadowed by the
	// scene's distance fields, ambient occlusion and reflections from what is on screen.
	FString Lighting;
	if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraLighting="), Lighting))
	{
		const bool bClassic = Lighting == TEXT("classic");
		SetVariable(TEXT("r.DynamicGlobalIlluminationMethod"), bClassic ? 0.f : 1.f);
		SetVariable(TEXT("r.ReflectionMethod"), bClassic ? 2.f : 1.f);
		SetVariable(TEXT("r.Lumen.HardwareRayTracing"), Lighting == TEXT("rt") ? 1.f : 0.f);
		SetVariable(TEXT("r.DistanceFieldAO"), 1.f);
		UE_LOG(LogTemp, Display, TEXT("Ephemera: lighting %s"), *Lighting);
	}
	// (-EphemeraNoRockGeometry: whatever the player last chose. The stream uses it: the option costs this game's
	// graphics card five times the frame, it is remembered between sessions, and on 14 October 2026 one visit to
	// the menu left every later visitor a game drawn at seven frames a second.)
	if (FParse::Param(FCommandLine::Get(), TEXT("EphemeraNoRockGeometry")))
	{
		SetVariable(TEXT("r.Nanite.Tessellation"), 0.f);
	}
	else if (RockGeometry >= 0)
	{
		SetVariable(TEXT("r.Nanite.Tessellation"), RockGeometry > 0 ? 1.f : 0.f);
	}
}
