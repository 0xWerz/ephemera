#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EphemeraHeightField.generated.h"

/**
 * A world's terrain as data: heights (and a few maps that go with them) on the planet builder's
 * cube-sphere grid, made offline by designing the land and letting simulated water erode it
 * (unreal/Tools/terrain). The game, the planet builder and the collision all read the ground
 * from here, so they agree exactly.
 *
 * The grid: six faces, Resolution + 1 points along each edge, shared along the cube's edges. Face
 * order, axes and the tangent warp are those of EphemeraPlanetBuilder.cpp.
 */
UCLASS(BlueprintType)
class EPHEMERA_API UEphemeraHeightField : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Grid cells along one cube-face edge. */
	UPROPERTY(VisibleAnywhere, Category = "Height Field") int32 Resolution = 0;
	/** Heights are stored as 0..65535 between these two, in metres above the datum. */
	UPROPERTY(VisibleAnywhere, Category = "Height Field") float LowM = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Height Field") float HighM = 1.f;

	UPROPERTY() TArray<uint16> Heights;
	/** Where water runs: 0 dry ground .. 255 a river. */
	UPROPERTY() TArray<uint8> Flow;
	/** Fresh lava and cinder: 0..255. */
	UPROPERTY() TArray<uint8> Young;
	/** Glacier ice: 0..255. */
	UPROPERTY() TArray<uint8> Ice;
	/** What covers the ground: 0 bare sand and gravel .. 128 lichened lava .. 255 deep moss. */
	UPROPERTY() TArray<uint8> Cover;

	bool IsUsable() const { return Resolution > 0 && Heights.Num() == 6 * (Resolution + 1) * (Resolution + 1); }

	/** Height above the datum along a direction, metres (bilinear between grid points). */
	double HeightM(const FVector& UnitDirection) const;
	/** One of the maps along a direction, 0..1. */
	double Sample(const TArray<uint8>& Map, const FVector& UnitDirection) const;
	/** Distance between neighbouring grid points on a sphere of this radius, metres. */
	double SpacingM(double RadiusM) const { return Resolution > 0 ? RadiusM * UE_DOUBLE_HALF_PI / Resolution : 1.0; }

private:
	struct FCorner
	{
		int32 Index[4];
		double Weight[4];
	};
	FCorner Locate(const FVector& UnitDirection) const;
};
