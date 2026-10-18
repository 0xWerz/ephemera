#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EphemeraBodyDefinition.generated.h"

class UStaticMesh;
class UMaterialInterface;

/** One octave group of terrain noise. Frequency is in cycles around the whole planet, so it reads the same at any radius. */
USTRUCT(BlueprintType)
struct FEphemeraNoiseLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise") double Frequency = 3.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise") double AmplitudeM = 20.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise") int32 Octaves = 4;
	/** 0 = rolling, 1 = sharp ridges (mountain spines). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise") double Ridged = 0.0;
};

UENUM(BlueprintType)
enum class EEphemeraTerrainFeature : uint8
{
	/** Flattens the ground toward HeightM inside RadiusM (a place to build on). */
	Plateau,
	/** A bowl DepthM deep with a raised rim. */
	Crater,
};

/** An authored landmark, placed by direction from the planet's centre. */
USTRUCT(BlueprintType)
struct FEphemeraTerrainFeatureDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature") FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature") EEphemeraTerrainFeature Type = EEphemeraTerrainFeature::Plateau;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature") FVector Direction = FVector::UpVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature") double RadiusM = 60.0;
	/** Plateau: target height above the datum. Crater: depth of the bowl. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature") double HeightM = 0.0;
	/** Width of the blend back into natural terrain, as a fraction of RadiusM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature") double Falloff = 0.5;
};

class UEphemeraHeightField;

/**
 * The impact craters of an airless world. A real cratered surface has no rolling hills: its relief
 * is craters on craters at every size, about as much ground taken up by each size as by the next
 * (the cumulative number larger than a diameter D goes as D^-2). They are not listed one by one:
 * each size class is laid out on its own grid of cells, and whether a cell holds a crater, where,
 * how big and how old, all come from a hash of the cell, so the same crater is there every time
 * any point near it is evaluated.
 */
USTRUCT(BlueprintType)
struct FEphemeraCraterSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craters") bool bEnabled = false;
	/** Diameters, in metres, of the largest and smallest craters modelled in the ground itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craters") double LargestM = 560.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craters") double SmallestM = 22.0;
	/**
	 * How crowded the highlands are, as a fraction of geometric saturation (craters rim to rim,
	 * 1.54 D^-2 per square kilometre larger than D km). The Moon's old highlands are a few tenths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craters") double Saturation = 0.18;
	/** Share of the surface that is mare: younger, darker, level lava plains with few craters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craters") double MariaFraction = 0.22;
	/** Craters on the maria, as a fraction of the highlands' number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craters") double MariaSaturation = 0.12;
	/** How far the maria lie below the datum, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craters") double MariaDepthM = 9.0;
};

USTRUCT(BlueprintType)
struct FEphemeraTerrainSettings
{
	GENERATED_BODY()

	/**
	 * A baked terrain. When set, heights come from it and nothing below (continents, relief,
	 * layers, features, craters) is used: the land was designed and eroded offline.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") TObjectPtr<UEphemeraHeightField> HeightField;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") int32 Seed = 1;
	/** Grid quads along one cube-face edge. 768 gives about 1 m spacing on a 500 m planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") int32 FaceResolution = 768;
	/** The visible terrain is cut into this many Nanite chunks along each face edge (about 100 m each on Dawnhollow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") int32 RenderChunksPerFace = 8;
	/** Collision is cut into this many chunks along each face edge. */
	/** 0 = build none (large worlds make collision at run time around the player instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") int32 CollisionChunksPerFace = 4;
	/**
	 * Continents and seas. When on, a slow continental field decides land against sea floor (sea
	 * level is the datum radius), mountains rise inland, and Layers add detail on top.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Continents") bool bContinents = false;
	/** Cycles around the planet: 1.5 gives a few large landmasses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Continents") double ContinentFrequency = 1.5;
	/** Height of the highest plateaus above the sea, and depth of the sea floor below it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Continents") double ContinentHeightM = 220.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Continents") double SeaDepthM = 380.0;
	/** -1..1: higher drowns more of the world (0 is about half land). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Continents") double SeaBias = 0.08;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Continents") double MountainHeightM = 650.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Continents") double MountainFrequency = 5.0;
	/**
	 * Eroded relief, in metres (0 = off): fractal noise in which every finer octave is weakened
	 * where the ground is already steep (Inigo Quilez's derivative-damped fBm). Plain fractal
	 * noise is evenly lumpy at every scale, which reads as modelling clay; this leaves broad
	 * smooth valley floors below rough, crested high ground, the way worn land looks. Strongest
	 * inland, slight at the shore (where it breaks the coast into inlets and skerries).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Relief") double ReliefHeightM = 0.0;
	/** Size of the largest hills: noise cells per planet radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Relief") double ReliefFrequency = 5.0;
	/** Each octave is half the size of the one before; enough of them to reach the mesh's own spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Relief") int32 ReliefOctaves = 9;
	/** How strongly steep ground suppresses finer detail (0 = plain fractal noise). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Relief") double ReliefErosion = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Relief") FEphemeraCraterSettings Craters;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") TArray<FEphemeraNoiseLayer> Layers;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") TArray<FEphemeraTerrainFeatureDef> Features;
};

/** Scattered instances of one mesh, in planet-local centimetres. Written by the planet builder. */
USTRUCT(BlueprintType)
struct FEphemeraScatterBatch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") TObjectPtr<UStaticMesh> Mesh;
	/** If set, what every instance is drawn with in place of the mesh's own materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") TObjectPtr<class UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") bool bCollision = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float CullDistanceM = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") TArray<FTransform> Instances;
};

/**
 * One band of the near sea's waves, baked by Tools/terrain/ocean_fft.py: a patch of sea that repeats
 * every PatchM metres and every LoopS seconds. The material reads it from two volume textures; the game
 * sums the same waves here (Waves) where it needs to know how high the water stands
 * (UEphemeraSeaSurfaceComponent). Written by the slice build from the bake's sea_waves.json.
 */
USTRUCT(BlueprintType)
struct FEphemeraSeaBand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float PatchM = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float LoopS = 24.f;
	/** How far water is drawn sideways toward the crests (Tessendorf's lambda). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float Choppiness = 0.f;
	/** How strong the band is drawn, 1 = as baked. The material is given the same number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float Gain = 1.f;
	/** Length of its main waves, metres: water shallower than half of it slows and steepens them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float PeakM = 50.f;
	/** Depth at which its waves have broken and are gone, metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float BreakDepthM = 1.f;
	/** The coarsest spacing of the near sea's mesh that still draws it, metres (coarser would alias it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float MaxSpacingM = 2.f;
	/**
	 * Its strongest waves, five numbers each: wavenumber along the chart's two axes (radians per metre),
	 * angular frequency (radians per second, a whole number of turns per loop) and complex amplitude (m).
	 * The height of the band is the sum of Re(A exp(i (kx u + kz v - omega t))).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") TArray<float> Waves;
};

/**
 * One of the three planes the near sea's waves are laid on: a point of the planet (metres) is at
 * (dot(P, AxisU), dot(P, AxisV)) on it, shifted by Offset (fractions of a band's patch).
 */
USTRUCT(BlueprintType)
struct FEphemeraSeaChart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") FVector AxisU = FVector::ForwardVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") FVector AxisV = FVector::RightVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") FVector2D Offset = FVector2D::ZeroVector;
};

/** Dense small plants (grass, flowers, ferns) grown at run time in a ring around the camera. */
USTRUCT(BlueprintType)
struct FEphemeraGroundCoverRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float DensityPerM2 = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float ScaleMin = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float ScaleMax = 1.3f;
	/** Steepest ground it grows on, in degrees from level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float MaxSlopeDeg = 28.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float MoistureMin = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float MoistureMax = 1.f;
	/** Grows in patches: noise cycles around the planet, and the fraction of ground the patches cover (1 = everywhere). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float PatchFrequency = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float PatchCoverage = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float SinkCm = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") bool bCastShadow = true;
	/** Gentlest ground it is found on, degrees (scree lies on slopes, not on the flat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float MinSlopeDeg = 0.f;
	/** What the ground must be covered with: -1 bare sand and gravel .. 0 lichened lava .. 1 deep moss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float CoverMin = -1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float CoverMax = 1.f;
	/** Height above the sea it is found between, metres (nothing grows below the tide line or on the ice). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float AltitudeMinM = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float AltitudeMaxM = 100000.f;
	/** Part of the object's own height sunk into the ground (a third for a bedded-in boulder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float SinkFraction = 0.f;
	/** 0 = stands upright (a plant), 1 = lies on the slope (a rock). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float AlignToGround = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float RandomTiltDeg = 0.f;
	/** Above 1, most are near ScaleMin and a few reach ScaleMax, as with stones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") float ScalePower = 1.f;
	/** Drawn with this material in place of the mesh's own (moss over a rock scan, say). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") TObjectPtr<UMaterialInterface> Material;
	/** Large things seen from far off (boulders, outcrops): grown in a wider ring of bigger cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover") bool bFar = false;
};

/**
 * Everything that defines a star, planet or moon: its physics, its rails, how its terrain is
 * generated, and (once built) the content the builder produced for it.
 * Lengths are metres and accelerations m/s² so the numbers match the design docs; code converts
 * to Unreal centimetres at the edges.
 */
UCLASS(BlueprintType)
class EPHEMERA_API UEphemeraBodyDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body") FName BodyId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body") bool bIsStar = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body") double RadiusM = 500.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body") double SurfaceGravity = 10.5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body") double MaxGravity = 40.0;
	/** Sphere of influence: inside it this body is the reference frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body") double InfluenceRadiusM = 1900.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body") bool bBreathable = false;

	/** Parent to orbit. None = the system's centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") FName ParentId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") double OrbitRadiusM = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") double OrbitPeriodS = 600.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") double OrbitPhaseRad = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") double OrbitEccentricity = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") double OrbitTiltRad = 0.0;
	/** Seconds per turn about the body's local +Z. 0 = no spin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") double SpinPeriodS = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") double SpinPhaseRad = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit") double AxialTiltRad = 0.0;

	/** 0 = airless. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") double AtmosphereHeightM = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") FLinearColor RayleighScattering = FLinearColor(0.175f, 0.409f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float RayleighScale = 0.0331f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float MieScale = 0.003996f;
	/** High-altitude absorption (Earth's ozone = 1). Deepens the zenith and colours twilight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float AbsorptionScale = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") FLinearColor Absorption = FLinearColor(0.345561f, 1.0f, 0.045189f);
	/** Cloud layer: altitude of its base above the datum and its thickness, metres. 0 thickness = no clouds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") double CloudBaseM = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") double CloudThicknessM = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") TObjectPtr<UMaterialInterface> CloudMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") FEphemeraTerrainSettings Terrain;

	// ---- Written by the planet builder ----
	/** A single mesh for bodies without built terrain (the star's sphere). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TObjectPtr<UStaticMesh> TerrainMesh;
	/** Built terrain: Nanite chunks that tile the globe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TArray<TObjectPtr<UStaticMesh>> TerrainChunks;
	/** Uniform scale applied to TerrainMesh (1 for built terrain; stars use a unit sphere scaled to their radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") double TerrainMeshScale = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TArray<TObjectPtr<UStaticMesh>> CollisionChunks;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TObjectPtr<UMaterialInterface> TerrainMaterial;
	/** Lowest and highest ground relative to the datum radius, metres (written by the terrain build). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") double TerrainMinM = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") double TerrainMaxM = 0.0;
	/** The sea: a smooth sphere at the datum radius. On a dry world, a plain sphere just under the lowest ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TObjectPtr<UStaticMesh> OceanMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TObjectPtr<UMaterialInterface> OceanMaterial;
	/**
	 * Running water: a sheet lying just over the terrain mesh wherever the baked terrain says a
	 * stream has gathered (UEphemeraPlanetBuilder::BuildStreams). The ground's relief is held down
	 * along the same channels, so the water shows only in the bed and the banks rise through it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TObjectPtr<UStaticMesh> StreamMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TObjectPtr<UMaterialInterface> StreamMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Built") TArray<FEphemeraScatterBatch> Scatter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Cover") TArray<FEphemeraGroundCoverRule> GroundCover;
	/** Ground cover exists within this distance of the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Cover") float GroundCoverRadiusM = 70.f;
	/**
	 * Ground mist: how much light a kilometre of air at sea level scatters (0 = none, 1 = things a
	 * few kilometres off are pale ghosts), and the height over which it thins out. It lies in the
	 * valleys and fades each ridge behind the last, which is most of what gives a landscape depth.
	 * Drawn only while the camera is down in the air; from orbit the planet stays clear.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float MistPerKm = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float MistHeightM = 140.f;
	/**
	 * Aurora: thin glowing shells high in the air, drawn with this material (it places the light in
	 * an oval round each magnetic pole, on the night side only). None if unset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") TObjectPtr<UMaterialInterface> AuroraMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float AuroraAltitudeM = 800.f;
	/** How far the curtains reach above their foot (AuroraAltitudeM): green fading upward, red at the top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float AuroraHeightM = 2400.f;
	/**
	 * Tides. The sea stands this many metres higher under the tide-raising body and on the side
	 * away from it, and half as much lower in the belt between (0 = no tides). The bulge follows
	 * that body round the sky, so the shallows flood and drain on a clock the player can read.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float TideAmplitudeM = 0.f;
	/**
	 * Where the tide is told to the ground: a material parameter collection whose "MoonTide" is the way to the
	 * moon in this world's own space and, in w, the amplitude above. The ground's material reads it to know
	 * where the sea's edge is now (wet sand is a mirror there). Made by the slice build.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") TObjectPtr<class UMaterialParameterCollection> TideCollection;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") FName TideBody;
	/**
	 * The near sea: a grid of rings kept under the camera whose material raises it into the waves of
	 * SeaBands (UEphemeraSeaSurfaceComponent). Where it lies, the far sea (the OceanMesh sphere) is drawn
	 * with OceanFarMaterial, the same water with a hole cut where the grid is. Without these, or with
	 * -EphemeraNoSeaGrid, the sea is the sphere alone with OceanMaterial, as it always was.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") TObjectPtr<UStaticMesh> SeaGridMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") TObjectPtr<UMaterialInterface> SeaGridMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") TObjectPtr<UMaterialInterface> OceanFarMaterial;
	/** How the grid was built (UEphemeraPlanetBuilder::BuildSeaGrid): its rings, their reach in cells, the innermost spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") int32 SeaGridRings = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") int32 SeaGridHalfCells = 64;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") float SeaGridSpacingM = 0.5f;
	/**
	 * The near ground: a grid of rings kept under the camera, laid on the land by its material
	 * (GroundGridMaterial) from the terrain's heights (a cubic through the height field's grid, as the game
	 * reads it) and raised into the relief the terrain's own material would otherwise only shade
	 * (UEphemeraGroundSurfaceComponent). Where it lies, the terrain chunks are drawn with TerrainFarMaterial,
	 * the same ground with a hole cut where the grid is. Without these, or with -EphemeraNoGroundGrid, the
	 * land is the chunks alone, as it was.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") TObjectPtr<UStaticMesh> GroundGridMesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") TObjectPtr<UMaterialInterface> GroundGridMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") TObjectPtr<UMaterialInterface> TerrainFarMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") int32 GroundGridRings = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") int32 GroundGridHalfCells = 96;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") float GroundGridSpacingM = 0.25f;
	/** The waves, as the material draws them (the swell first, then the wind sea). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") TArray<FEphemeraSeaBand> SeaBands;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea") TArray<FEphemeraSeaChart> SeaCharts;
	/** Reach of the rules marked bFar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Cover") float GroundCoverFarRadiusM = 700.f;
	/**
	 * Small relief the terrain material adds by displacement on top of the mesh: undulations and
	 * hummocks from a couple of metres to a couple of tens across (FEphemeraTerrain::MicroReliefM,
	 * which the material computes identically). 1 = the authored size, 0 = none. Whatever stands
	 * on the ground is lifted by it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain") float MicroRelief = 0.f;

	/** The body's own space, where none of its moons may take over: ground, air and the tallest hills. */
	double ShellRadiusM() const { return RadiusM + FMath::Max(AtmosphereHeightM, RadiusM * 0.35); }
};

/** Terrain height shared by the builder (meshes, scatter) and the game (spawn points, altitude). */
struct EPHEMERA_API FEphemeraTerrain
{
	/** Height above the datum radius, in metres, along a direction from the planet's centre (planet-local). */
	static double HeightM(const FEphemeraTerrainSettings& Settings, double RadiusM, const FVector& UnitDirection);
	/** -1 (open sea) .. 1 (deep inland): the slow field that draws coastlines. 0 where bContinents is off. */
	static double Continentalness(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection);
	/**
	 * 0 (dry) .. 1 (lush): a slow field that decides meadow against forest floor and what grows where.
	 * On a cratered world it is the mare mask instead: 0 highland, 1 mare.
	 */
	static double Moisture(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection);
	/**
	 * 0..1: how bright the ground is with fresh ejecta (the pale blanket and rays around the
	 * youngest craters, not yet darkened by ages of sunlight). 0 where there are no craters.
	 */
	static double FreshEjecta(const FEphemeraTerrainSettings& Settings, double RadiusM, const FVector& UnitDirection);
	/**
	 * Running water as the terrain material reads it. The baked flow (Moisture) travels to the
	 * material in a vertex colour, and the engine stores mesh colours sRGB-encoded: 0.2 in the
	 * bake arrives as 0.48. Anything that must agree with a threshold in the material (where a
	 * stream's bed is cleared, where its water lies) compares this value, not the baked one.
	 */
	static double FlowAsShaded(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection);
	/** 0..1: glacier ice (baked terrains only). */
	static double Ice(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection);
	/** -1 bare sand and gravel .. 0 lichened lava .. 1 deep moss (baked terrains only; 0 otherwise). */
	static double Cover(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection);
	/** 0..1 patch noise at a frequency in cycles around the planet (for clustering plants and trees). */
	static double Patch(const FEphemeraTerrainSettings& Settings, double Frequency, int32 Salt, const FVector& UnitDirection);
	/** True if the direction lies inside an authored plateau (kept clear for set pieces). */
	static bool InsidePlateau(const FEphemeraTerrainSettings& Settings, double RadiusM, const FVector& UnitDirection, double Margin = 1.0);
	/** Outward surface normal in planet-local space, from the height field's slope. */
	static FVector Normal(const FEphemeraTerrainSettings& Settings, double RadiusM, const FVector& UnitDirection);
	/**
	 * Instance transform that stands a mesh on a ground point. Scanned files often hold several
	 * objects side by side with pivots far from the object, so the mesh is seated by the bottom
	 * centre of its bounds rather than by its pivot.
	 */
	static FTransform Seat(const UStaticMesh* Mesh, const FQuat& Rotation, const FVector& GroundPoint, double Scale);
	/**
	 * Planet-local position of the ground along a direction, in centimetres. bWithRelief adds the
	 * material's micro relief; without it this is the terrain mesh itself.
	 */
	static FVector SurfacePointCm(const UEphemeraBodyDefinition& Definition, const FVector& UnitDirection, bool bWithRelief = true);
	/**
	 * Height in metres of the micro relief over a point of the terrain mesh (planet-local, metres),
	 * given what the baked terrain says of that ground (Flow: running water as the material reads
	 * it, FlowAsShaded; a stream keeps its bed clear). The planet material holds the same
	 * function (PLANET_RELIEF in Tools/build_slice.py): change one and change the other.
	 */
	static double MicroReliefM(const FVector& PositionM, double Cover, double Ice, double AltitudeM, double Flow);
	/**
	 * True while the terrain material's relief is being drawn as geometry (Nanite tessellation on).
	 * Where it is not (it is too slow to play with on the Mac), the ground stays on the mesh and
	 * nothing may be lifted to meet a relief that is only shading.
	 */
	static bool ReliefIsGeometry();
	/** True where a world's relief stands up as geometry where things are placed: drawn by tessellation
	 * (ReliefIsGeometry) or by its near ground's grid (UEphemeraGroundSurfaceComponent::Wanted). */
	static bool ReliefStandsUp(const UEphemeraBodyDefinition& Definition);
	/** The same, looked up along a direction; 0 for a world without micro relief or while it is only shading. */
	static double MicroReliefM(const UEphemeraBodyDefinition& Definition, const FVector& UnitDirection);
};
