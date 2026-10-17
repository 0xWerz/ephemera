#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EphemeraUserSettings.generated.h"

/**
 * What the player has chosen in the settings menu. Kept in the user's own settings file, so it
 * survives a restart and is separate from the project's defaults.
 */
UCLASS(Config = GameUserSettings)
class EPHEMERA_API UEphemeraUserSettings : public UObject
{
	GENERATED_BODY()

public:
	/** A named sky (AEphemeraSkyRig::WeatherPresets): "clear", "fronts", "overcast" or "broken". */
	UPROPERTY(Config) FString Weather = TEXT("fronts");
	/** Vertical field of view, degrees. */
	UPROPERTY(Config) float FieldOfView = 70.f;
	/** Multiplies the mouse's turning rate. */
	UPROPERTY(Config) float MouseSensitivity = 1.f;
	/** Pushing the mouse or stick forward looks down instead of up. */
	UPROPERTY(Config) bool bInvertLook = false;
	/**
	 * How much the view moves with the body: the dip on landing, the sway of a stride, the lean
	 * into a turn, the widening at speed. 1 as designed, 0 a camera on rails (for anyone it makes
	 * queasy).
	 */
	UPROPERTY(Config) float CameraMotion = 1.f;
	/** 0..1: everything that is heard but the score, and the score. */
	UPROPERTY(Config) float Volume = 0.8f;
	UPROPERTY(Config) float MusicVolume = 0.7f;
	UPROPERTY(Config) bool bShowFps = true;
	/** Share of the window's pixels that are rendered, percent (the rest is upscaled). */
	UPROPERTY(Config) float ScreenPercentage = 100.f;
	/** Engine scalability level for everything at once: 0 low .. 3 epic. */
	UPROPERTY(Config) int32 Quality = 3;
	/**
	 * The ground's relief as real geometry (Nanite tessellation): 1 on, 0 off, -1 as the platform
	 * prefers (off on the Mac, where it is too slow to play with).
	 */
	UPROPERTY(Config) int32 RockGeometry = -1;
	UPROPERTY(Config) bool bVSync = false;
	/** The game waits for the graphics card instead of queueing frames behind it (FEphemeraFramePacer). */
	UPROPERTY(Config) bool bLowLatency = true;
	/** Frames per second the game is held to; 0 = no limit. */
	UPROPERTY(Config) int32 FrameLimit = 0;

	/** Which of this game's defaults a saved file was written under (BringUpToDate). */
	UPROPERTY(Config) int32 Version = 0;

	static UEphemeraUserSettings* Get() { return GetMutableDefault<UEphemeraUserSettings>(); }
	/** Applies a changed default to a file saved before the change. */
	void BringUpToDate();
	void Save() { SaveConfig(); }
	/** Pushes the rendering choices to the engine. Weather, view and mouse are applied by their owners. */
	void ApplyRendering() const;
};
