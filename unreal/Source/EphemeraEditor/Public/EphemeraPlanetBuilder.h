#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EphemeraPlanetBuilder.generated.h"

class UTextureCube;
class UTexture2DArray;

class UEphemeraBodyDefinition;
class UStaticMesh;

/** How one kind of thing (boulders, firs, dead wood...) is spread over a planet. */
USTRUCT(BlueprintType)
struct FEphemeraScatterRule
{
	GENERATED_BODY()

	/** One of these is picked at random for each instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") TArray<TObjectPtr<UStaticMesh>> Meshes;
	/** Instances per hectare where the rule applies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float DensityPerHectare = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float MinSlopeDeg = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float MaxSlopeDeg = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float MoistureMin = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float MoistureMax = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float ScaleMin = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float ScaleMax = 1.4f;
	/** 0 = stands straight up (trees), 1 = lies along the ground's slope (rocks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float AlignToGround = 0.f;
	/** Random tilt in degrees (rocks tumble, trees lean a little). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float RandomTiltDeg = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float SinkCm = 5.f;
	/** Extra burial as a fraction of the mesh's height (0.3 = a third of a boulder is underground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float SinkFraction = 0.f;
	/** Rules with the same group (0 or more) share one cluster pattern, so scree gathers where the boulders are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") int32 PatchGroup = -1;
	/** Grows in groups: noise cycles around the planet and the fraction of ground the groups cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float PatchFrequency = 9.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float PatchCoverage = 1.f;
	/** Keep authored plateaus clear out to this multiple of their radius (0 = allowed on plateaus). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float PlateauClearance = 1.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") bool bCollision = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float CullDistanceM = 0.f;
	/** Drawn with this material in place of the meshes' own (a scan redrawn as the land's own stone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") TObjectPtr<UMaterialInterface> Material;
	/** Only where the ground stands between these heights over the sea, metres (a sea stack's foot is under water). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float MinAltitudeM = -100000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float MaxAltitudeM = 100000.f;
	/** Drawn taller than it is wide by a factor between these (a boulder stood up into a stack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float UprightMin = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float UprightMax = 1.f;
};

/**
 * Turns a body definition into content. Called from Tools/build_slice.py; safe to re-run, the
 * result is a pure function of the definition.
 */
UCLASS()
class EPHEMERAEDITOR_API UEphemeraPlanetBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Generates the terrain for a body: one seamless Nanite mesh for the whole globe (what you see)
	 * and a grid of collision meshes cut from the same height function (what you stand on). Assets
	 * are saved under PackageFolder (for example "/Game/Ephemera/Planets/Dawnhollow") and written
	 * into the definition's Built properties.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static bool BuildTerrain(UEphemeraBodyDefinition* Definition, const FString& PackageFolder);

	/**
	 * Places sparse things (rocks, trees, dead wood) over the whole body by rule and stores the
	 * instances on the definition, replacing any earlier scatter. Deterministic for a given seed.
	 * Returns the number of instances placed.
	 */
	/**
	 * Builds a smooth sphere for a body and writes it to the definition's OceanMesh. With
	 * RadiusOffsetM = 0 it is the sea (the datum radius is sea level). A dry world gets one just
	 * under its lowest ground instead: if neighbouring terrain pieces ever part by a hair at a
	 * distance, you see ground-coloured rock through the gap, not the sky.
	 */
	/**
	 * Bakes how deep the sea is all over a world into a cube texture (T_<Body>_Depth), looked up
	 * by direction from the planet's centre. One byte per texel holds sqrt((depth + 8 m) / 72 m):
	 * 0 for land 8 m or more above the datum, 1 at 64 m deep and deeper, finest where it matters,
	 * around the water's edge and over the ground the tide covers and uncovers. The sea's material
	 * reads it per pixel, so shallows and the surf line follow the coast exactly at any distance,
	 * however coarsely the sea's own mesh is being drawn. 2048 texels per face is under 4 m per
	 * texel on a 5 km world.
	 */
	/**
	 * The terrain's own values (slope, running water, fresh lava, ice, and what covers the ground) on its
	 * cube grid, one texel per grid point, two cells past each face's edge, as a 12-slice texture array:
	 * slices 0-5 hold slope R, flow G, young B, ice A per face; 6-11 cover in R (0..1 for -1..1). The ground
	 * material reads them per pixel with smooth filtering instead of from the mesh's vertices (whose values
	 * change in straight lines across each triangle and drew every triangle as a facet). Saved as Name.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static UTexture2DArray* BuildTerrainAttributes(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, const FString& Name);

	/**
	 * The terrain's heights for the GPU, on the same grid and with the same apron as the attributes: six
	 * slices (one per face) of 32-bit floats, metres above the datum, exact (no compression, no filtering:
	 * the near ground's material reads them point by point through a cubic, as the game does on the CPU).
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static UTexture2DArray* BuildTerrainHeights(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static UTextureCube* BuildDepthMap(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, int32 FaceSize = 2048);

	/**
	 * Reads a terrain baked offline by unreal/Tools/terrain/bake.py (a file starting "EPHF":
	 * resolution, the height range, then heights and the flow, young, ice and cover maps) into a
	 * height-field asset HF_<Body> under PackageFolder, and makes it the definition's terrain.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static bool ImportHeightField(UEphemeraBodyDefinition* Definition, const FString& FilePath, const FString& PackageFolder);

	/**
	 * Writes a world's heights as a picture, for finding places worth looking at: a 16-bit PGM,
	 * Width x Width/2, longitude -180..180 left to right (atan2 of planet-local y, x), latitude
	 * +90..-90 top to bottom (asin of z). A comment line in the header gives the heights, in
	 * metres, that 0 and 65535 stand for.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static bool DumpHeightMap(UEphemeraBodyDefinition* Definition, const FString& FilePath, int32 Width = 2048);

	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static bool BuildOcean(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, float RadiusOffsetM = 0.f);

	/**
	 * Builds the running water of a baked world as one mesh (Definition->StreamMesh): a copy of the
	 * terrain mesh's own cells wherever the flow (as the material reads it,
	 * FEphemeraTerrain::FlowAsShaded) reaches FlowThreshold, lifted LiftM above them there and
	 * sunk below the ground to either side, so the ground itself cuts the water off along a bank. Vertex colour RGB = the way downhill (a planet-local unit vector packed into 0..1),
	 * A = the flow; UV0 = (height in metres, slope as 1 - cos).
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static bool BuildStreams(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, float FlowThreshold = 0.60f, float LiftM = 0.22f);

	/**
	 * Builds a clump of grass as real geometry: tapered, curved blades, no alpha cut-out. Nanite
	 * draws opaque geometry far faster than alpha-tested cards, and blades hold up at any distance.
	 * Vertex colour: R = height along the blade (0 root, 1 tip), G = per-blade variation.
	 * Saved as a Nanite static mesh named Name under PackageFolder; returns it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static UStaticMesh* BuildGrassClump(const FString& PackageFolder, const FString& Name, int32 Seed, int32 Blades,
		float HeightCm, float SpreadCm, float WidthCm, float Droop);

	/**
	 * Builds a cloud of small squares for a material to turn to the camera and move about: motes in
	 * the air, the dust of a landing. Count squares, each 1 cm and lying flat, their centres strewn
	 * through a cube that reaches ExtentCm each way from the mesh's own centre. Texture coordinate:
	 * which corner (0 or 1 each way). Vertex colour: three numbers drawn for each square. A material
	 * finds a square's centre by taking the corner back off its position.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static UStaticMesh* BuildQuadCloud(const FString& PackageFolder, const FString& Name, int32 Seed, int32 Count, float ExtentCm);

	/**
	 * Builds the near sea's grid (UEphemeraSeaSurfaceComponent): Rings square rings of quads, flat, one
	 * inside the next, each with twice the spacing of the one inside it, SpacingM for the innermost.
	 * Every ring reaches HalfCells of its own cells from the centre; all but the innermost leave out
	 * the middle, a little less than the ring inside covers, so neighbours overlap by a cell or two
	 * whichever way each has been snapped to its own lattice. Every quad is split along the same
	 * diagonal, so the outer edge of a ring, folded onto the next ring's lattice by the material, is
	 * that ring's triangles exactly. Where the vertices go is all the material's work (it lays the grid
	 * on the sphere under the camera); the mesh only says which ring each vertex is in.
	 * Position: (i, j) * spacing, cm. UV0: (i, j). UV1: (ring, its spacing in metres).
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static UStaticMesh* BuildSeaGrid(const FString& PackageFolder, const FString& Name, int32 Rings, int32 HalfCells, float SpacingM);

	/**
	 * Builds a cluster of basalt columns: the prisms a thick lava flow cracks into as it cools, five-,
	 * six- and seven-sided, standing side by side (Reynisfjara, the Giant's Causeway). Real geometry
	 * a few centimetres fine, enough to carry what makes stone read as stone: edges worn round, the
	 * cross joints that split each column into drums, chipped corners, the grain of the rock.
	 * Style 0: a massif, full height over most of its top and breaking down at its edges in organ
	 * pipes. Style 1: a causeway, short columns whose tops climb in irregular steps toward one side.
	 * RadiusCm: the cluster's reach; HeightCm: its tallest column; ColumnCm: how far apart the columns
	 * stand. The mesh's origin is on the ground at the cluster's middle; the columns run on below it
	 * (4 m for a massif, 1.5 m for a causeway) to stand on uneven ground. Collision is
	 * a plain prism per column (Name_Collision). Saved as a Nanite mesh named Name; returns it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static UStaticMesh* BuildBasaltColumns(const FString& PackageFolder, const FString& Name, int32 Seed, int32 Style,
		float RadiusCm, float HeightCm, float ColumnCm);

	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static int32 BuildScatter(UEphemeraBodyDefinition* Definition, const TArray<FEphemeraScatterRule>& Rules);
};
