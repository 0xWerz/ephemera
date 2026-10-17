#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EphemeraCloudBuilder.generated.h"

/**
 * Generates the three volume textures the cloud material reads, as saved content:
 *
 *  T_CloudWeather  where on the planet the weather is. Looked up by direction from the planet's
 *                  centre (texture coordinate = direction * 0.5 + 0.5): a wet belt on the equator,
 *                  dry belts either side, storm tracks toward the poles with spiral cyclones in them.
 *  T_CloudShape    the billows: repeating Perlin-Worley noise.
 *  T_CloudDetail   the wisps that fray a cloud's edge: repeating Worley noise.
 *
 * Every texture is equalised: its values are spread evenly over 0..1, so in the material
 * "value > 1 - c" selects a fraction c of the sky or of the volume. Cloud cover is then a number
 * that means what it says, on any planet, without tuning thresholds to a noise texture's quirks.
 */
UCLASS()
class EPHEMERAEDITOR_API UEphemeraCloudBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Writes the textures under Folder (for example "/Game/Ephemera/Textures/Clouds"). Seed picks the weather. */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static bool BuildCloudTextures(const FString& Folder, int32 Seed);

	/**
	 * Makes a volume texture of moving water from a raw file written by Tools/terrain/water.py: Frames
	 * slices of Size x Size texels, four bytes each (blue, green, red, alpha). The third coordinate is
	 * time; it repeats, so the last slice runs on into the first. Saved as Folder/Name.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static class UVolumeTexture* ImportWaveVolume(const FString& Folder, const FString& Name, const FString& RawFile, int32 Size, int32 Frames);

	/**
	 * The same for the near sea's waves (Tools/terrain/ocean_fft.py): four half floats per texel (red,
	 * green, blue, alpha), kept as they are. They hold metres the sea's mesh is moved by, which eight
	 * bits cannot carry (a byte over a three-metre swell is a centimetre-high stair on every wave).
	 * No mips: the vertex shader reads the finest level only, and a volume's mips would average its
	 * moments together, not just its texels.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static class UVolumeTexture* ImportWaveVolumeHalf(const FString& Folder, const FString& Name, const FString& RawFile, int32 Size, int32 Frames);
};
