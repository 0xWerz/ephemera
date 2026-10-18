#pragma once

#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "EphemeraGroundSurface.generated.h"

class UEphemeraBodyDefinition;
class UMaterialInstanceDynamic;

/**
 * The near ground. The terrain chunks are a mesh with a point every eight metres or so: drawn as they are,
 * a slope is a few flat triangles with a picture of moss on them. Their material holds the land's relief
 * (ledges, blocks, hummocks, the lie of the ground) but can only shade it unless the engine splits the
 * whole land into pixel-sized triangles, which cost this game's graphics card five times the frame.
 *
 * So the ground near the camera is drawn as the near sea is (UEphemeraSeaSurfaceComponent): a set of flat
 * square rings (UEphemeraPlanetBuilder::BuildSeaGrid), each twice as coarse as the one inside it, from a
 * quarter of a metre at the centre out to about a kilometre, laid by its material on the land under the
 * camera. Each vertex is put where the terrain's heights say (the same cubic the game reads them through,
 * so feet and ground agree) and raised by the relief the chunks' material would only shade; that fades
 * out over the coarser rings, so by the grid's edge it is the plain land. The rings are snapped to their
 * own lattices on one cube face, so the land never slides under its vertices, and each folds onto the
 * next at its edge, so they meet with no step.
 *
 * Where the grid lies the chunks are drawn with a hole cut in them (TerrainFarMaterial reads GroundHole).
 * -EphemeraNoGroundGrid leaves all this out.
 */
UCLASS()
class EPHEMERA_API UEphemeraGroundSurfaceComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UEphemeraGroundSurfaceComponent(const FObjectInitializer& ObjectInitializer);

	static bool SwitchedOff();
	/** Whether a world gets a near ground: it has the grid's assets, and the switch is not on. */
	static bool Wanted(const UEphemeraBodyDefinition* Definition);

	/** Gets the grid ready for a world (nullptr: none). InFarTerrain: the chunks' material instance, told where its hole is. */
	void Configure(const UEphemeraBodyDefinition* Definition, UMaterialInstanceDynamic* InFarTerrain);
	bool IsDrawn() const { return bDrawn; }

	/** How high over the land the grid is still drawn, metres. */
	static constexpr double HideAboveM = 1500.0;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	void Show(bool bShow);
	void Place(const FVector& Direction);

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> NearGround;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FarTerrain;
	int32 Face = INDEX_NONE;
	FVector2D Arc = FVector2D::ZeroVector;
	double HoleM = 0.0;
	double RadiusM = 0.0;
	bool bDrawn = false;
};
