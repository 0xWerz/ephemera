#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EphemeraOceanSim.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;
class UTextureRenderTarget2D;
struct FEphemeraSeaBand;

/**
 * The sea's waves, simulated on the GPU every frame (Shaders/EphemeraOcean.usf), as the best real-time seas
 * are (Tessendorf 2001; four cascades as in Dupuy and Bruneton's "Real-time Animation and Rendering of Ocean
 * Whitecaps", 2012): each band of the definition's SeaBands is a cascade, a 256-texel patch of PatchM metres.
 * Their sizes share no factor, and each holds only the waves its patch draws well (none longer than a sixth
 * of the next smaller patch, so no cascade shows its repeat), so together they never visibly repeat.
 *
 * The sea is one spectrum: a long swell from far off plus the wind sea this world's own wind raises
 * (JONSWAP, each scaled to its significant height). Each wave's amplitude is drawn once here, on the CPU,
 * from a fixed seed; the GPU moves them on (deep water: omega = sqrt(g k)) and sums them. Every band's
 * strongest waves also go into its Waves, so the game's own sums of the water's height
 * (UEphemeraSeaSurfaceComponent) match what is drawn.
 *
 * Its textures, per cascade, for the sea's materials (CascNMove, CascNSlope): the sideways move and height
 * of the water (metres) and its slopes, slope squared and foam, each with every mip.
 */
UCLASS()
class EPHEMERA_API UEphemeraOceanSim : public UObject
{
	GENERATED_BODY()

public:
	/** Makes the cascades for these bands (PatchM, Choppiness) and fills each band's Waves. */
	void Build(TArray<FEphemeraSeaBand>& Bands, double Gravity);
	/** Hands the textures to a sea material instance. */
	void Bind(UMaterialInstanceDynamic* Material) const;
	/** Computes the waves for this moment (seconds of world time, as the materials' clock). */
	void Simulate(double TimeS, float DeltaS);
	bool IsBuilt() const { return Cascades.Num() > 0; }

	/** How many cascades; the materials are built for this many (Tools/build_slice.py SEA_CASCADES). */
	static constexpr int32 MaxCascades = 4;

private:
	struct FCascade
	{
		float PatchM = 100.f;
		float Choppiness = 0.f;
	};
	TArray<FCascade> Cascades;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Spectra;
	UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Moves;
	UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Slopes;
	UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Foams;
	double GravityMs2 = 9.81;
};
