#pragma once

#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "EphemeraSeaSurface.generated.h"

class UEphemeraBodyDefinition;
class UEphemeraOceanSim;
class UMaterialInstanceDynamic;

/**
 * The near sea. The sea sphere (the body's OceanMesh) is a smooth ball whose material only moves its
 * normals: nothing on it rises or falls. This draws the water near the camera as geometry instead, and
 * its material raises it into the waves simulated on the GPU every frame (UEphemeraOceanSim): a swell and a
 * wind sea, crests drawn together and sharp, foam where they crowd. The far sea's material reads the same
 * simulation for its waves' slopes.
 *
 * The mesh (UEphemeraPlanetBuilder::BuildSeaGrid) is a set of flat square rings, each twice as coarse
 * as the one inside it, from half a metre at the centre to 16 m a kilometre out. The material lays it
 * on the sphere under the camera, on the lattice of one cube face (the face the camera is over, kept a
 * few degrees past its edge so crossing the edge does not flicker between two). Each ring is snapped
 * to its own lattice, so as the camera moves the vertices jump from lattice point to lattice point and
 * never slide over the waves; a sliding grid makes the waves under it crawl. The outer part of each
 * ring folds onto the next ring's lattice, so neighbours meet exactly.
 *
 * Where the grid lies, the far sphere is drawn with a hole cut in it (OceanFarMaterial reads SeaHole):
 * a square on the grid's own lattice face, so it is inside the grid wherever the camera is. Inside it
 * only the grid is drawn, and the grid's waves die away before its edge, so where the two overlap they
 * are the same flat water. The grid is drawn up to HideAboveM over the sea; above
 * that its waves are too small to see and the sphere is whole again.
 *
 * -EphemeraNoSeaGrid on the command line leaves all this out: the sea is the sphere alone, as it was.
 */
UCLASS()
class EPHEMERA_API UEphemeraSeaSurfaceComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UEphemeraSeaSurfaceComponent(const FObjectInitializer& ObjectInitializer);

	/** True when -EphemeraNoSeaGrid is on the command line. */
	static bool SwitchedOff();
	/** Whether a world gets a near sea: it has a sea and the grid's assets, and the switch is not on. */
	static bool Wanted(const UEphemeraBodyDefinition* Definition);

	/**
	 * Gets the grid ready for a world (nullptr: no grid; the component then draws nothing). InFarSea: the
	 * far sphere's material instance, which is told where to leave its hole.
	 */
	void Configure(const UEphemeraBodyDefinition* Definition, UMaterialInstanceDynamic* InFarSea);
	/** The way to the tide-raising body in the planet's own space (AEphemeraBody::SetTide). */
	void SetTide(const FVector& LocalDirection);
	/** True while the grid is drawn (near enough the sea, and set up). */
	bool IsDrawn() const { return bDrawn; }
	/**
	 * How high the drawn waves stand over the sea's still level (datum plus tide) where a line from the
	 * planet's centre along Up (planet space) meets them, in metres; 0 while the grid is not drawn. TideM:
	 * the tide there. The same sums the material does, from the same waves.
	 */
	double WaveHeightM(const FVector& Up, double TideM) const;

	/** How high over the sea the grid is still drawn, metres. */
	static constexpr double HideAboveM = 2000.0;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	void Show(bool bShow);
	/** Picks the lattice's face for the camera's direction (planet space) and its place on it. */
	void Place(const FVector& Direction);
	/** Where the waves move the water that rests at Up (metres, planet space). */
	FVector MoveM(const UEphemeraBodyDefinition& Definition, const FVector& Up, double TideM, double TimeS) const;
	/** How far Up lies from the camera along the lattice face's axes (the larger of the two, metres of
	 * arc), as the grid and the hole are measured; negative on the far side of the face. */
	double ReachM(const FVector& Up) const;
	/** The grid's spacing that far out as the material uses it (the ring's spacing, growing toward the next
	 * ring's over its outer part), metres; negative off the grid. */
	double SpacingAtM(const UEphemeraBodyDefinition& Definition, double Reach) const;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> NearSea;
	/** The waves themselves; both sea materials read its textures. */
	UPROPERTY() TObjectPtr<UEphemeraOceanSim> Ocean;
	/** The definition's bands, with the simulation's strongest waves in each (the game's sums use these). */
	UPROPERTY() TArray<FEphemeraSeaBand> Bands;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FarSea;
	/** The lattice's cube face (INDEX_NONE until the first placement). */
	int32 Face = INDEX_NONE;
	/** The camera's place on that face: metres of arc along its two axes. */
	FVector2D Arc = FVector2D::ZeroVector;
	/** How far the hole in the far sea reaches from the camera along the face's axes, metres of arc: a
	 * little inside the grid's coarsest ring, however that ring has been snapped. */
	double HoleM = 0.0;
	double RadiusM = 0.0;
	bool bDrawn = false;
};
