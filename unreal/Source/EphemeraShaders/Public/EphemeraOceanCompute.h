#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

class FRHICommandListImmediate;

/** One cascade of the sea's waves on the GPU (Shaders/EphemeraOcean.usf). Textures are OCEAN_N (256) square. */
struct FEphemeraOceanCascadeRHI
{
	/** A(k) and A(-k), RGBA 32-bit float: the waves' amplitudes at time 0 (UEphemeraOceanSim). */
	FTextureRHIRef Spectrum;
	/** Written: dx, dz, height, Jacobian - 1. Half-float RGBA with a full mip chain, created for UAV use. */
	FTextureRHIRef Move;
	/** Written: slope x, slope z, slope x^2 + slope z^2, foam. As Move. */
	FTextureRHIRef Slope;
	/** Foam carried between frames: RGBA half float, UAV, no mips. */
	FTextureRHIRef Foam;
	float PatchM = 100.f;
	float Choppiness = 0.f;
};

struct FEphemeraOceanFrame
{
	float TimeS = 0.f;
	float DeltaS = 0.f;
	float Gravity = 9.81f;
	float FoamStart = 0.5f;
	float FoamFull = 0.2f;
	float FoamSeconds = 2.5f;
};

/** The texels across a cascade (the shaders' OCEAN_N). */
constexpr int32 EphemeraOceanTexels = 256;

/** Computes every cascade's textures for this moment. Render thread. */
EPHEMERASHADERS_API void EphemeraRenderOcean(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel,
	const TArray<FEphemeraOceanCascadeRHI>& Cascades, const FEphemeraOceanFrame& Frame);
