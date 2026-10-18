#include "EphemeraPlanetBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureCube.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Frame/EphemeraHeightField.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	// Cube faces as (normal, u axis, v axis). The grid on each face is warped by tan() so cells are
	// nearly the same size at the face centre and at its corners.
	struct FCubeFace { FIntVector N, U, V; };
	const FCubeFace Faces[6] = {
		{{ 1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
		{{ 0, 1, 0}, {0, 0, 1}, {1, 0, 0}}, {{ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}},
		{{ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{ 0, 0,-1}, {0, 1, 0}, {1, 0, 0}},
	};

	/** Integer cube coordinate (each axis 0..Resolution) of grid point (I, J) on a face. Shared by neighbouring faces along edges. */
	FIntVector CubeKey(const FCubeFace& Face, int32 I, int32 J, int32 Resolution)
	{
		const int32 Half = Resolution;      // work in units of half-cells so the centre is an integer
		const FIntVector P = Face.N * Half + Face.U * (2 * I - Resolution) + Face.V * (2 * J - Resolution);
		return (P + FIntVector(Half)) ;      // 0..2*Resolution on every axis
	}

	FVector DirectionOf(const FIntVector& Key, int32 Resolution)
	{
		auto Warp = [Resolution](int32 K)
		{
			const double T = static_cast<double>(K - Resolution) / Resolution;   // -1..1
			return FMath::Tan(T * UE_DOUBLE_PI / 4.0);
		};
		return FVector(Warp(Key.X), Warp(Key.Y), Warp(Key.Z)).GetSafeNormal();
	}

	struct FSurfaceVertex
	{
		FVector3f Position;     // planet-local cm
		FVector3f Normal;
		FVector4f Color;        // R slope, G moisture (mare on a cratered world; running water on a baked one), B fresh ejecta or lava, A height (ice on a baked world)
		FVector2f UV;
	};

	FSurfaceVertex Evaluate(const UEphemeraBodyDefinition& D, const FVector& Direction, double MinHeight, double MaxHeight)
	{
		FSurfaceVertex V;
		const double Height = FEphemeraTerrain::HeightM(D.Terrain, D.RadiusM, Direction);
		const FVector Normal = FEphemeraTerrain::Normal(D.Terrain, D.RadiusM, Direction);
		V.Position = FVector3f(Direction * ((D.RadiusM + Height) * 100.0));
		V.Normal = FVector3f(Normal);
		const float Slope = static_cast<float>(1.0 - FMath::Clamp(FVector::DotProduct(Normal, Direction), 0.0, 1.0));
		const float Moisture = static_cast<float>(FEphemeraTerrain::Moisture(D.Terrain, Direction));
		const float Height01 = static_cast<float>(FMath::Clamp((Height - MinHeight) / FMath::Max(MaxHeight - MinHeight, 1.0), 0.0, 1.0));
		const float Fresh = static_cast<float>(FEphemeraTerrain::FreshEjecta(D.Terrain, D.RadiusM, Direction));
		const bool bBaked = D.Terrain.HeightField != nullptr;
		V.Color = FVector4f(Slope, Moisture, Fresh, bBaked ? static_cast<float>(FEphemeraTerrain::Ice(D.Terrain, Direction)) : Height01);
		// The material is world-aligned and needs no mapping, so the UV channel carries data instead:
		// U = height above the datum (sea level) in metres, V = continentalness (-1 sea .. 1 inland).
		// V carries continentalness, or on a baked world what covers the ground (-1 sand .. 1 moss).
		V.UV = FVector2f(static_cast<float>(Height), static_cast<float>(bBaked ? FEphemeraTerrain::Cover(D.Terrain, Direction) : FEphemeraTerrain::Continentalness(D.Terrain, Direction)));
		return V;
	}

	/** A mesh under construction: welded vertices plus triangles as index triples. */
	struct FBuiltMesh
	{
		TArray<FSurfaceVertex> Vertices;
		TArray<FIntVector> Triangles;
	};

	FMeshDescription ToMeshDescription(const FBuiltMesh& Built)
	{
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		Description.ReserveNewVertices(Built.Vertices.Num());
		Description.ReserveNewVertexInstances(Built.Vertices.Num());
		Description.ReserveNewTriangles(Built.Triangles.Num());
		Description.ReserveNewPolygons(Built.Triangles.Num());
		Description.ReserveNewEdges(Built.Triangles.Num() * 3 / 2 + 16);

		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		const FPolygonGroupID Group = Description.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Terrain");

		TArray<FVertexInstanceID> Instances;
		Instances.SetNumUninitialized(Built.Vertices.Num());
		for (int32 Index = 0; Index < Built.Vertices.Num(); ++Index)
		{
			const FSurfaceVertex& V = Built.Vertices[Index];
			const FVertexID Vertex = Description.CreateVertex();
			Positions[Vertex] = V.Position;
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
			Normals[Instance] = V.Normal;
			Colors[Instance] = V.Color;
			UVs.Set(Instance, 0, V.UV);
			Instances[Index] = Instance;
		}
		for (const FIntVector& T : Built.Triangles)
		{
			const FVertexInstanceID Corners[3] = {Instances[T.X], Instances[T.Y], Instances[T.Z]};
			Description.CreateTriangle(Group, Corners);
		}
		return Description;
	}

	/**
	 * Creates (or reuses) a static mesh asset and gives it its geometry, without building it.
	 * Building is done for all meshes at once (BuildAndSave), which uses every core.
	 */
	UStaticMesh* PrepareStaticMesh(const FString& PackageFolder, const FString& AssetName, FMeshDescription&& Description, bool bNanite, bool bCollision)
	{
		const FString PackageName = PackageFolder / AssetName;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *AssetName);
		if (!Mesh)
		{
			Mesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Mesh);
		}
		Mesh->Modify();
		Mesh->SetNumSourceModels(1);
		FMeshBuildSettings& Settings = Mesh->GetSourceModel(0).BuildSettings;
		Settings.bRecomputeNormals = false;        // normals come from the height field, seamless everywhere
		Settings.bRecomputeTangents = true;
		Settings.bUseMikkTSpace = false;
		Settings.bGenerateLightmapUVs = false;     // lighting is fully dynamic
		Settings.bRemoveDegenerates = true;
		Settings.bBuildReversedIndexBuffer = false;
		Settings.bUseFullPrecisionUVs = true;
		// Only what is seen needs a distance field (software Lumen and distance-field shadows trace it).
		// -EphemeraNoDistanceFields (unreal.py slice --no-df): skip them while a world's shape is
		// still changing. The game builds one per chunk on first run after every terrain build,
		// which takes longer than the terrain; without them Lumen gets no bounce or sky shadow
		// from the land, so final content must be built with them.
		const bool bFields = bNanite && !FParse::Param(FCommandLine::Get(), TEXT("EphemeraNoDistanceFields"));
		Settings.DistanceFieldResolutionScale = bFields ? 2.0f : 0.0f;
		Mesh->CreateMeshDescription(0, MoveTemp(Description));
		Mesh->CommitMeshDescription(0);
		if (Mesh->GetStaticMaterials().IsEmpty())
		{
			Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Terrain"), TEXT("Terrain")));
		}
		FMeshNaniteSettings Nanite = Mesh->GetNaniteSettings();
		Nanite.bEnabled = bNanite;
		Mesh->SetNaniteSettings(Nanite);
		Mesh->bGenerateMeshDistanceField = bFields;
		if (bCollision)
		{
			Mesh->CreateBodySetup();
			Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
			Mesh->bCustomizedCollision = true;
		}
		Mesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
		return Mesh;
	}

	bool BuildAndSave(const TArray<UStaticMesh*>& Meshes)
	{
		UStaticMesh::BatchBuild(Meshes, true);
		for (UStaticMesh* Mesh : Meshes)
		{
			UPackage* Package = Mesh->GetOutermost();
			Package->MarkPackageDirty();
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			if (!UPackage::SavePackage(Package, Mesh, *Filename, SaveArgs))
			{
				UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: could not save %s"), *Filename);
				return false;
			}
			// The editable source geometry is on disk now; holding thousands of chunks' worth in
			// memory is what runs a large planet's build out of RAM.
			Mesh->ClearMeshDescriptions();
		}
		return true;
	}

	/** Adds the two triangles of a grid quad, wound so the face normal points outward (Unreal is clockwise-front). */
	void AddQuad(FBuiltMesh& Mesh, int32 A, int32 B, int32 C, int32 D)
	{
		const FVector3f PA = Mesh.Vertices[A].Position, PB = Mesh.Vertices[B].Position, PC = Mesh.Vertices[C].Position;
		const bool bOutward = FVector3f::DotProduct(FVector3f::CrossProduct(PC - PA, PB - PA), PA) > 0.f;
		if (bOutward)
		{
			Mesh.Triangles.Add(FIntVector(A, B, C));
			Mesh.Triangles.Add(FIntVector(A, C, D));
		}
		else
		{
			Mesh.Triangles.Add(FIntVector(A, C, B));
			Mesh.Triangles.Add(FIntVector(A, D, C));
		}
	}
}

bool UEphemeraPlanetBuilder::BuildTerrain(UEphemeraBodyDefinition* Definition, const FString& PackageFolder)
{
	if (!Definition || Definition->RadiusM <= 0.0)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: no definition."));
		return false;
	}
	const UEphemeraBodyDefinition& D = *Definition;
	const int32 Resolution = FMath::Clamp(D.Terrain.FaceResolution, 8, 4096);
	const int32 Chunks = FMath::Clamp(D.Terrain.CollisionChunksPerFace, 0, 16);
	const double StartSeconds = FPlatformTime::Seconds();

	// ---- 1. The grid: (Resolution + 1)^2 points on each of the six cube faces ----
	// Faces keep their own copies of the points along shared edges. The positions are identical
	// (the same direction evaluates to the same height), so nothing gaps, and tens of millions of
	// points need no lookup table.
	const int32 Row = Resolution + 1;
	const int32 PointCount = 6 * Row * Row;
	auto DirectionAt = [&](int32 Index)
	{
		const int32 F = Index / (Row * Row), Rest = Index % (Row * Row);
		return DirectionOf(CubeKey(Faces[F], Rest % Row, Rest / Row, Resolution), Resolution);
	};
	TArray<int32> FaceGrid;                                   // kept as a table so chunk code reads the same
	FaceGrid.SetNumUninitialized(PointCount);
	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		FaceGrid[Index] = Index;
	}

	// ---- 2. Height range, then every vertex in parallel ----
	TArray<float> Heights;
	Heights.SetNumUninitialized(PointCount);
	ParallelFor(PointCount, [&](int32 Index)
	{
		Heights[Index] = static_cast<float>(FEphemeraTerrain::HeightM(D.Terrain, D.RadiusM, DirectionAt(Index)));
	});
	double MinHeight = TNumericLimits<double>::Max(), MaxHeight = TNumericLimits<double>::Lowest();
	for (const float H : Heights)
	{
		MinHeight = FMath::Min<double>(MinHeight, H);
		MaxHeight = FMath::Max<double>(MaxHeight, H);
	}
	Heights.Empty();
	FBuiltMesh Globe;
	Globe.Vertices.SetNumUninitialized(PointCount);
	ParallelFor(PointCount, [&](int32 Index)
	{
		Globe.Vertices[Index] = Evaluate(D, DirectionAt(Index), MinHeight, MaxHeight);
	});

	// ---- 3. The visible terrain: Nanite chunks ----
	// One mesh for the whole globe would be seamless, but Lumen lights a mesh through a handful of
	// flat cards and one coarse distance field: a planet-sized mesh gets no usable bounce light or
	// shadow detail. Chunks of about 100 m each get their own. A short skirt under every chunk edge
	// hides the hairline gaps that appear when neighbouring chunks simplify differently far away.
	const FString Name = D.BodyId.ToString();
	const int32 RenderChunks = FMath::Clamp(D.Terrain.RenderChunksPerFace, 1, 32);
	// Deep enough to cover the gap Nanite can open between pieces when they are simplified for a
	// view from orbit: over one percent of the radius.
	const float SkirtCm = FMath::Max(200.f, static_cast<float>(D.RadiusM * 100.0 * 0.014));
	// Nanite simplifies every chunk on its own, so from far away two neighbours' edges no longer
	// meet and a hairline of whatever is behind shows between them. A skirt alone does not cure it
	// (it is thin, and is simplified away too). So each visible chunk also reaches a couple of
	// cells past its border, lying on the same surface as its neighbour: wherever one has pulled
	// back, the other still covers. Past the edge of a cube face the grid simply continues; the
	// direction is still a valid one and the height function gives the true surface there.
	constexpr int32 Apron = 2;
	auto VertexAt = [&](int32 F, int32 I, int32 J)
	{
		if (I >= 0 && I <= Resolution && J >= 0 && J <= Resolution)
		{
			return Globe.Vertices[FaceGrid[(F * Row + J) * Row + I]];
		}
		return Evaluate(D, DirectionOf(CubeKey(Faces[F], I, J, Resolution), Resolution), MinHeight, MaxHeight);
	};
	auto CutChunk = [&](int32 F, int32 I0, int32 J0, int32 I1, int32 J1, bool bSkirt)
	{
		FBuiltMesh Chunk;
		if (bSkirt)
		{
			I0 -= Apron;
			J0 -= Apron;
			I1 += Apron;
			J1 += Apron;
		}
		const int32 Width = I1 - I0 + 1, Height = J1 - J0 + 1;
		Chunk.Vertices.Reserve(Width * Height + (bSkirt ? 2 * (Width + Height) : 0));
		for (int32 J = J0; J <= J1; ++J)
		{
			for (int32 I = I0; I <= I1; ++I)
			{
				Chunk.Vertices.Add(VertexAt(F, I, J));
			}
		}
		for (int32 J = 0; J < Height - 1; ++J)
		{
			for (int32 I = 0; I < Width - 1; ++I)
			{
				const int32 Base = J * Width + I;
				AddQuad(Chunk, Base, Base + 1, Base + Width + 1, Base + Width);
			}
		}
		if (bSkirt)
		{
			// Walk the border; each border vertex gets a twin pulled toward the planet's centre.
			TArray<int32> Border;
			for (int32 I = 0; I < Width; ++I) { Border.Add(I); }
			for (int32 J = 1; J < Height; ++J) { Border.Add(J * Width + Width - 1); }
			for (int32 I = Width - 2; I >= 0; --I) { Border.Add((Height - 1) * Width + I); }
			for (int32 J = Height - 2; J >= 1; --J) { Border.Add(J * Width); }
			const int32 FirstTwin = Chunk.Vertices.Num();
			for (const int32 Index : Border)
			{
				FSurfaceVertex Twin = Chunk.Vertices[Index];
				Twin.Position -= Twin.Position.GetSafeNormal() * SkirtCm;
				Chunk.Vertices.Add(Twin);
			}
			for (int32 K = 0; K < Border.Num(); ++K)
			{
				const int32 A = Border[K], B = Border[(K + 1) % Border.Num()];
				const int32 A2 = FirstTwin + K, B2 = FirstTwin + (K + 1) % Border.Num();
				// Both windings: a skirt is seen from either side depending on the neighbour.
				Chunk.Triangles.Add(FIntVector(A, B, B2));
				Chunk.Triangles.Add(FIntVector(A, B2, A2));
				Chunk.Triangles.Add(FIntVector(A, B2, B));
				Chunk.Triangles.Add(FIntVector(A, A2, B2));
			}
		}
		return Chunk;
	};
	auto ForEachChunk = [&](int32 PerFace, TFunctionRef<bool(int32, int32, int32, int32, int32, int32, int32)> Body)
	{
		const int32 Cell = FMath::Max(Resolution / PerFace, 1);
		for (int32 F = 0; F < 6; ++F)
		{
			for (int32 CJ = 0; CJ < PerFace; ++CJ)
			{
				for (int32 CI = 0; CI < PerFace; ++CI)
				{
					const int32 I0 = CI * Cell, J0 = CJ * Cell;
					const int32 I1 = CI == PerFace - 1 ? Resolution : I0 + Cell;
					const int32 J1 = CJ == PerFace - 1 ? Resolution : J0 + Cell;
					if (!Body(F, CI, CJ, I0, J0, I1, J1))
					{
						return false;
					}
				}
			}
		}
		return true;
	};
	// Chunks are built and saved in batches (all cores per batch) so memory stays bounded.
	constexpr int32 BatchSize = 64;
	TArray<UStaticMesh*> Pending;
	auto Queue = [&](UStaticMesh* Mesh)
	{
		if (!Mesh)
		{
			return false;
		}
		Pending.Add(Mesh);
		if (Pending.Num() >= BatchSize)
		{
			const bool bOk = BuildAndSave(Pending);
			Pending.Reset();
			return bOk;
		}
		return true;
	};
	TArray<TObjectPtr<UStaticMesh>> Visible;
	int32 VisibleTriangles = 0;
	if (!ForEachChunk(RenderChunks, [&](int32 F, int32 CI, int32 CJ, int32 I0, int32 J0, int32 I1, int32 J1)
	{
		const FBuiltMesh Chunk = CutChunk(F, I0, J0, I1, J1, true);
		VisibleTriangles += Chunk.Triangles.Num();
		UStaticMesh* Mesh = PrepareStaticMesh(PackageFolder / TEXT("Terrain"),
			FString::Printf(TEXT("SM_%s_Terrain_%d_%d_%d"), *Name, F, CI, CJ), ToMeshDescription(Chunk), true, false);
		Visible.Add(Mesh);
		return Queue(Mesh);
	}))
	{
		return false;
	}

	// ---- 4. Collision: the same surface in larger pieces, so physics only touches what is nearby ----
	TArray<TObjectPtr<UStaticMesh>> Colliders;
	if (Chunks > 0 && !ForEachChunk(Chunks, [&](int32 F, int32 CI, int32 CJ, int32 I0, int32 J0, int32 I1, int32 J1)
	{
		UStaticMesh* Collider = PrepareStaticMesh(PackageFolder / TEXT("Collision"),
			FString::Printf(TEXT("SM_%s_Ground_%d_%d_%d"), *Name, F, CI, CJ), ToMeshDescription(CutChunk(F, I0, J0, I1, J1, false)), false, true);
		Colliders.Add(Collider);
		return Queue(Collider);
	}))
	{
		return false;
	}

	// ---- 5. Build and save whatever is left in the last batch ----
	if (!Pending.IsEmpty() && !BuildAndSave(Pending))
	{
		return false;
	}

	// ---- 6. Record the result on the definition ----
	Definition->Modify();
	Definition->TerrainMinM = MinHeight;
	Definition->TerrainMaxM = MaxHeight;
	Definition->TerrainMesh = nullptr;
	Definition->TerrainChunks = Visible;
	Definition->CollisionChunks = Colliders;
	Definition->MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("Ephemera planet builder: %s: %d vertices, %d triangles in %d visible chunks, %d collision chunks, heights %.1f..%.1f m, %.1f s"),
		*Name, PointCount, VisibleTriangles, Visible.Num(), Colliders.Num(), MinHeight, MaxHeight, FPlatformTime::Seconds() - StartSeconds);
	return true;
}

int32 UEphemeraPlanetBuilder::BuildScatter(UEphemeraBodyDefinition* Definition, const TArray<FEphemeraScatterRule>& Rules)
{
	if (!Definition)
	{
		return 0;
	}
	const UEphemeraBodyDefinition& D = *Definition;
	const double AreaHectares = 4.0 * UE_DOUBLE_PI * D.RadiusM * D.RadiusM / 10000.0;
	TMap<TPair<UStaticMesh*, UMaterialInterface*>, int32> BatchOf;
	TArray<FEphemeraScatterBatch> Batches;
	int32 Total = 0;
	for (int32 RuleIndex = 0; RuleIndex < Rules.Num(); ++RuleIndex)
	{
		const FEphemeraScatterRule& Rule = Rules[RuleIndex];
		if (Rule.Meshes.IsEmpty())
		{
			continue;
		}
		FRandomStream Random(D.Terrain.Seed * 9973 + RuleIndex * 131 + 17);
		const int32 Attempts = FMath::RoundToInt32(Rule.DensityPerHectare * AreaHectares);
		const double MaxUp = FMath::Cos(FMath::DegreesToRadians(Rule.MinSlopeDeg));
		const double MinUp = FMath::Cos(FMath::DegreesToRadians(Rule.MaxSlopeDeg));
		for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
		{
			const FVector Dir = Random.VRand();                    // uniform over the sphere
			const float Chance = Random.FRand();
			const float Scale = Random.FRandRange(Rule.ScaleMin, Rule.ScaleMax);
			const float Yaw = Random.FRandRange(0.f, 360.f);
			const FVector TiltAxis = Random.VRand();
			const float Tilt = Random.FRandRange(0.f, Rule.RandomTiltDeg);
			const int32 Pick = Random.RandRange(0, Rule.Meshes.Num() - 1);
			const float Upright = Random.FRandRange(Rule.UprightMin, Rule.UprightMax);
			UStaticMesh* Mesh = Rule.Meshes[Pick];
			if (!Mesh)
			{
				continue;
			}
			const double AltitudeM = FEphemeraTerrain::SurfacePointCm(D, Dir).Size() / 100.0 - D.RadiusM;
			if (AltitudeM < Rule.MinAltitudeM || AltitudeM > Rule.MaxAltitudeM)
			{
				continue;
			}
			if (Rule.PlateauClearance > 0.f && FEphemeraTerrain::InsidePlateau(D.Terrain, D.RadiusM, Dir, Rule.PlateauClearance))
			{
				continue;
			}
			const double Moisture = FEphemeraTerrain::Moisture(D.Terrain, Dir);
			if (Moisture < Rule.MoistureMin || Moisture > Rule.MoistureMax)
			{
				continue;
			}
			if (Rule.PatchCoverage < 1.f
				&& FEphemeraTerrain::Patch(D.Terrain, Rule.PatchFrequency, Rule.PatchGroup >= 0 ? 500 + Rule.PatchGroup : 100 + RuleIndex, Dir) - (1.0 - Rule.PatchCoverage) < Chance * 0.15)
			{
				continue;
			}
			const FVector Normal = FEphemeraTerrain::Normal(D.Terrain, D.RadiusM, Dir);
			const double Upness = FVector::DotProduct(Normal, Dir);
			if (Upness < MinUp || Upness > MaxUp)
			{
				continue;
			}
			const FVector Up = FMath::Lerp(Dir, Normal, static_cast<double>(Rule.AlignToGround)).GetSafeNormal();
			FQuat Rotation = FRotationMatrix::MakeFromZ(Up).ToQuat() * FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw));
			if (Tilt > 0.f)
			{
				Rotation = FQuat(TiltAxis, FMath::DegreesToRadians(Tilt)) * Rotation;
			}
			// Bedded in: sunk by a fixed amount plus a share of its own height, so big rocks sit in
			// the ground instead of resting on it.
			const double MeshHeight = Mesh->GetBounds().BoxExtent.Z * 2.0;
			const FVector Position = FEphemeraTerrain::SurfacePointCm(D, Dir) - Dir * ((Rule.SinkCm + MeshHeight * Rule.SinkFraction) * Scale);
			const TPair<UStaticMesh*, UMaterialInterface*> Key(Mesh, Rule.Material.Get());
			int32* Found = BatchOf.Find(Key);
			if (!Found)
			{
				FEphemeraScatterBatch Batch;
				Batch.Mesh = Mesh;
				Batch.Material = Rule.Material;
				Batch.bCollision = Rule.bCollision;
				Batch.CullDistanceM = Rule.CullDistanceM;
				Found = &BatchOf.Add(Key, Batches.Add(MoveTemp(Batch)));
			}
			// Seated on its foot, and (Upright above 1) stood up: taller along its own up axis.
			const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
			const FVector Foot(MeshBounds.Origin.X, MeshBounds.Origin.Y, MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z);
			const FVector Scale3(Scale, Scale, Scale * Upright);
			Batches[*Found].Instances.Add(FTransform(Rotation, Position - Rotation.RotateVector(Foot * Scale3), Scale3));
			++Total;
		}
	}
	Definition->Modify();
	Definition->Scatter = MoveTemp(Batches);
	Definition->MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("Ephemera planet builder: %s: %d scattered instances in %d batches"), *D.BodyId.ToString(), Total, Definition->Scatter.Num());
	return Total;
}

UStaticMesh* UEphemeraPlanetBuilder::BuildGrassClump(const FString& PackageFolder, const FString& Name, int32 Seed, int32 Blades,
	float HeightCm, float SpreadCm, float WidthCm, float Droop)
{
	constexpr int32 Rings = 5;                 // cross-sections per blade below the tip
	FRandomStream Random(Seed);
	FBuiltMesh Clump;
	Clump.Vertices.Reserve(Blades * (Rings * 2 + 1));
	Clump.Triangles.Reserve(Blades * ((Rings - 1) * 2 + 1) * 2);
	for (int32 Blade = 0; Blade < Blades; ++Blade)
	{
		// Roots cluster toward the middle of the clump; outer blades lean outward.
		const float RootRadius = SpreadCm * FMath::Sqrt(Random.FRand()) * 0.5f;
		const float RootAngle = Random.FRandRange(0.f, UE_TWO_PI);
		const FVector3f Root(RootRadius * FMath::Cos(RootAngle), RootRadius * FMath::Sin(RootAngle), 0.f);
		const float Height = HeightCm * Random.FRandRange(0.55f, 1.15f);
		const float Width = WidthCm * Random.FRandRange(0.7f, 1.25f);
		const float LeanAngle = RootAngle + Random.FRandRange(-0.9f, 0.9f);
		const FVector3f LeanDir(FMath::Cos(LeanAngle), FMath::Sin(LeanAngle), 0.f);
		const float Lean = Random.FRandRange(0.08f, 0.35f) + RootRadius / FMath::Max(SpreadCm, 1.f) * 0.5f;
		const float Bend = Droop * Random.FRandRange(0.4f, 1.3f);
		// The blade's flat side faces roughly across its lean, with a little twist.
		const float FaceAngle = LeanAngle + UE_HALF_PI + Random.FRandRange(-0.6f, 0.6f);
		const FVector3f Side(FMath::Cos(FaceAngle), FMath::Sin(FaceAngle), 0.f);
		const float Variation = Random.FRand();
		const int32 First = Clump.Vertices.Num();
		auto Spine = [&](float T)
		{
			// Rises, leans, and droops more toward the tip.
			const float Out = Height * (Lean * T + Bend * T * T);
			return Root + LeanDir * Out + FVector3f(0.f, 0.f, Height * T * (1.f - 0.35f * Bend * T));
		};
		for (int32 Ring = 0; Ring <= Rings; ++Ring)
		{
			const float T = static_cast<float>(Ring) / Rings;
			const FVector3f Centre = Spine(T);
			const FVector3f Tangent = (Spine(FMath::Min(T + 0.05f, 1.f)) - Spine(FMath::Max(T - 0.05f, 0.f))).GetSafeNormal();
			const FVector3f Face = FVector3f::CrossProduct(Side, Tangent).GetSafeNormal();
			const float HalfWidth = Width * 0.5f * (1.f - FMath::Pow(T, 1.6f));
			const FVector4f Colour(T, Variation, 0.f, 1.f);
			if (Ring == Rings)
			{
				FSurfaceVertex Tip;
				Tip.Position = Centre;
				Tip.Normal = (FVector3f::UpVector * 0.85f + Face * 0.15f).GetSafeNormal();
				Tip.Color = Colour;
				Tip.UV = FVector2f(0.5f, 1.f);
				Clump.Vertices.Add(Tip);
				break;
			}
			for (int32 Edge = 0; Edge < 2; ++Edge)
			{
				const float Sign = Edge == 0 ? -1.f : 1.f;
				FSurfaceVertex V;
				V.Position = Centre + Side * (HalfWidth * Sign);
				// Blades take the ground's lighting direction (the clump's up), with only a trace of
				// their own facing. Lit by its own face normal every blade catches the sun differently
				// and a meadow sparkles like static; lit this way it shades as one soft surface, which
				// is how grass is done in shipped games.
				V.Normal = (FVector3f::UpVector * 0.85f + Face * 0.12f + Side * (0.10f * Sign)).GetSafeNormal();
				V.Color = Colour;
				V.UV = FVector2f(Edge, T);
				Clump.Vertices.Add(V);
			}
		}
		for (int32 Ring = 0; Ring < Rings - 1; ++Ring)
		{
			const int32 A = First + Ring * 2;
			Clump.Triangles.Add(FIntVector(A, A + 1, A + 3));
			Clump.Triangles.Add(FIntVector(A, A + 3, A + 2));
		}
		const int32 Last = First + (Rings - 1) * 2;
		Clump.Triangles.Add(FIntVector(Last, Last + 1, Last + 2));
	}
	UStaticMesh* Mesh = PrepareStaticMesh(PackageFolder, Name, ToMeshDescription(Clump), true, false);
	if (!Mesh)
	{
		return nullptr;
	}
	FMeshNaniteSettings Nanite = Mesh->GetNaniteSettings();
	Nanite.ShapePreservation = ENaniteShapePreservation::PreserveArea;     // thin blades must not vanish with distance
	Mesh->SetNaniteSettings(Nanite);
	Mesh->bGenerateMeshDistanceField = false;
	return BuildAndSave({Mesh}) ? Mesh : nullptr;
}

UStaticMesh* UEphemeraPlanetBuilder::BuildQuadCloud(const FString& PackageFolder, const FString& Name, const int32 Seed, const int32 Count, const float ExtentCm)
{
	FRandomStream Random(Seed);
	FBuiltMesh Cloud;
	Cloud.Vertices.Reserve(Count * 4);
	Cloud.Triangles.Reserve(Count * 2);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector3f Centre(Random.FRandRange(-ExtentCm, ExtentCm), Random.FRandRange(-ExtentCm, ExtentCm), Random.FRandRange(-ExtentCm, ExtentCm));
		const FVector4f Drawn(Random.FRand(), Random.FRand(), Random.FRand(), 1.f);
		const int32 First = Cloud.Vertices.Num();
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			FSurfaceVertex V;
			V.UV = FVector2f(Corner == 1 || Corner == 2 ? 1.f : 0.f, Corner >= 2 ? 1.f : 0.f);
			V.Position = Centre + FVector3f(V.UV.X - 0.5f, V.UV.Y - 0.5f, 0.f);
			V.Normal = FVector3f::UpVector;
			V.Color = Drawn;
			Cloud.Vertices.Add(V);
		}
		Cloud.Triangles.Add(FIntVector(First, First + 1, First + 2));
		Cloud.Triangles.Add(FIntVector(First, First + 2, First + 3));
	}
	UStaticMesh* Mesh = PrepareStaticMesh(PackageFolder, Name, ToMeshDescription(Cloud), false, false);
	if (!Mesh)
	{
		return nullptr;
	}
	Mesh->bGenerateMeshDistanceField = false;
	return BuildAndSave({Mesh}) ? Mesh : nullptr;
}

UStaticMesh* UEphemeraPlanetBuilder::BuildSeaGrid(const FString& PackageFolder, const FString& Name, const int32 Rings, const int32 HalfCells, const float SpacingM)
{
	if (Rings < 1 || Rings > 12 || HalfCells < 8 || HalfCells % 2 != 0 || SpacingM <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: sea grid %s: %d rings of %d half cells at %.2f m is not a grid"), *Name, Rings, HalfCells, SpacingM);
		return nullptr;
	}
	// The middle a ring leaves out: cells wholly inside this many of its own cells from its centre. The
	// ring inside reaches HalfCells / 2 of these coarser cells from its own centre, which is the same
	// point or one coarse cell away (each ring is snapped to twice its own spacing), so leaving out one
	// cell less than that keeps the two overlapping whichever way the snapping has gone.
	const int32 Hole = HalfCells / 2 - 1;
	const int32 Row = 2 * HalfCells + 1;
	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	UVs.SetNumChannels(2);
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Terrain");
	int32 Triangles = 0;
	for (int32 Ring = 0; Ring < Rings; ++Ring)
	{
		const float Spacing = SpacingM * static_cast<float>(1 << Ring);
		TArray<FVertexInstanceID> Placed;
		Placed.Init(FVertexInstanceID(INDEX_NONE), Row * Row);
		auto Corner = [&](const int32 I, const int32 J)
		{
			FVertexInstanceID& Instance = Placed[(J + HalfCells) * Row + (I + HalfCells)];
			if (Instance == INDEX_NONE)
			{
				const FVertexID Vertex = Description.CreateVertex();
				Positions[Vertex] = FVector3f(I * Spacing * 100.f, J * Spacing * 100.f, 0.f);
				Instance = Description.CreateVertexInstance(Vertex);
				Normals[Instance] = FVector3f::UpVector;
				Colors[Instance] = FVector4f(1.f, 1.f, 1.f, 1.f);
				UVs.Set(Instance, 0, FVector2f(static_cast<float>(I), static_cast<float>(J)));
				UVs.Set(Instance, 1, FVector2f(static_cast<float>(Ring), Spacing));
			}
			return Instance;
		};
		for (int32 J = -HalfCells; J < HalfCells; ++J)
		{
			for (int32 I = -HalfCells; I < HalfCells; ++I)
			{
				if (Ring > 0 && I >= -Hole && I + 1 <= Hole && J >= -Hole && J + 1 <= Hole)
				{
					continue;
				}
				// Both triangles face +Z, the side the material turns outward (the lattice's two axes and
				// its face's normal are a right-handed set), and share the diagonal (I, J)-(I+1, J+1).
				const FVertexInstanceID A = Corner(I, J), B = Corner(I + 1, J), C = Corner(I + 1, J + 1), D = Corner(I, J + 1);
				const FVertexInstanceID First[3] = {A, C, B};
				const FVertexInstanceID Second[3] = {A, D, C};
				Description.CreateTriangle(Group, First);
				Description.CreateTriangle(Group, Second);
				Triangles += 2;
			}
		}
	}
	UStaticMesh* Mesh = PrepareStaticMesh(PackageFolder, Name, MoveTemp(Description), false, false);
	if (!Mesh)
	{
		return nullptr;
	}
	Mesh->bGenerateMeshDistanceField = false;
	if (!BuildAndSave({Mesh}))
	{
		return nullptr;
	}
	UE_LOG(LogTemp, Display, TEXT("Ephemera planet builder: sea grid %s: %d rings, %d triangles, out to %.0f m"), *Name, Rings, Triangles,
		HalfCells * SpacingM * static_cast<float>(1 << (Rings - 1)));
	return Mesh;
}

UTextureCube* UEphemeraPlanetBuilder::BuildDepthMap(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, const int32 FaceSize)
{
	if (!Definition || FaceSize < 16)
	{
		return nullptr;
	}
	const int32 N = FaceSize;
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(6 * N * N);
	// The graphics card's own rule for which texel of which face a direction lands on, turned
	// round: the direction through the centre of texel (X, Y) of each face, in the order the
	// faces are stored (+X, -X, +Y, -Y, +Z, -Z).
	ParallelFor(6 * N, [&](const int32 Line)
	{
		const int32 Face = Line / N, Y = Line % N;
		const double Vc = (Y + 0.5) / N * 2.0 - 1.0;
		for (int32 X = 0; X < N; ++X)
		{
			const double Uc = (X + 0.5) / N * 2.0 - 1.0;
			FVector Dir;
			switch (Face)
			{
			case 0: Dir = FVector(1.0, -Vc, -Uc); break;
			case 1: Dir = FVector(-1.0, -Vc, Uc); break;
			case 2: Dir = FVector(Uc, 1.0, Vc); break;
			case 3: Dir = FVector(Uc, -1.0, -Vc); break;
			case 4: Dir = FVector(Uc, -Vc, 1.0); break;
			default: Dir = FVector(-Uc, -Vc, -1.0); break;
			}
			const double DepthM = -FEphemeraTerrain::HeightM(Definition->Terrain, Definition->RadiusM, Dir.GetSafeNormal());
			// From 8 m above the datum (land the tide can reach) to 64 m below it.
			Bytes[(Face * N + Y) * N + X] = static_cast<uint8>(FMath::RoundToInt32(FMath::Sqrt(FMath::Clamp((DepthM + 8.0) / 72.0, 0.0, 1.0)) * 255.0));
		}
	});
	const FString Name = FString::Printf(TEXT("T_%s_Depth"), *Definition->BodyId.ToString());
	const FString PackageName = PackageFolder / Name;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UTextureCube* Texture = FindObject<UTextureCube>(Package, *Name);
	if (!Texture)
	{
		Texture = NewObject<UTextureCube>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Texture);
	}
	Texture->Modify();
	Texture->Source.Init(N, N, 6, 1, TSF_G8, Bytes.GetData());
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_Grayscale;
	Texture->NeverStream = true;
	Texture->PostEditChange();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Texture, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: could not save %s"), *Filename);
		return nullptr;
	}
	return Texture;
}

bool UEphemeraPlanetBuilder::ImportHeightField(UEphemeraBodyDefinition* Definition, const FString& FilePath, const FString& PackageFolder)
{
	if (!Definition)
	{
		return false;
	}
	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *FilePath) || File.Num() < 16 || FMemory::Memcmp(File.GetData(), "EPHF", 4) != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: %s is not a baked height field."), *FilePath);
		return false;
	}
	int32 Resolution = 0;
	float Low = 0.f, High = 0.f;
	FMemory::Memcpy(&Resolution, File.GetData() + 4, 4);
	FMemory::Memcpy(&Low, File.GetData() + 8, 4);
	FMemory::Memcpy(&High, File.GetData() + 12, 4);
	const int64 Count = 6LL * (Resolution + 1) * (Resolution + 1);
	if (Resolution < 8 || File.Num() != 16 + Count * 6)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: %s has the wrong size for resolution %d."), *FilePath, Resolution);
		return false;
	}
	const FString Name = FString::Printf(TEXT("HF_%s"), *Definition->BodyId.ToString());
	const FString PackageName = PackageFolder / Name;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UEphemeraHeightField* Field = FindObject<UEphemeraHeightField>(Package, *Name);
	if (!Field)
	{
		Field = NewObject<UEphemeraHeightField>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Field);
	}
	Field->Modify();
	Field->Resolution = Resolution;
	Field->LowM = Low;
	Field->HighM = High;
	const uint8* Cursor = File.GetData() + 16;
	Field->Heights.SetNumUninitialized(Count);
	FMemory::Memcpy(Field->Heights.GetData(), Cursor, Count * 2);
	Cursor += Count * 2;
	for (TArray<uint8>* Map : {&Field->Flow, &Field->Young, &Field->Ice, &Field->Cover})
	{
		Map->SetNumUninitialized(Count);
		FMemory::Memcpy(Map->GetData(), Cursor, Count);
		Cursor += Count;
	}
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Field, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: could not save %s"), *Filename);
		return false;
	}
	Definition->Modify();
	Definition->Terrain.HeightField = Field;
	UE_LOG(LogTemp, Display, TEXT("Ephemera planet builder: %s baked terrain, %d per face, heights %.0f..%.0f m"), *Definition->BodyId.ToString(), Resolution, Low, High);
	return true;
}

bool UEphemeraPlanetBuilder::DumpHeightMap(UEphemeraBodyDefinition* Definition, const FString& FilePath, const int32 Width)
{
	if (!Definition || Width < 16)
	{
		return false;
	}
	const int32 W = Width, H = Width / 2;
	TArray<float> Heights;
	Heights.SetNumUninitialized(W * H);
	ParallelFor(H, [&](const int32 Y)
	{
		const double Latitude = (0.5 - (Y + 0.5) / H) * UE_DOUBLE_PI;
		for (int32 X = 0; X < W; ++X)
		{
			const double Longitude = ((X + 0.5) / W - 0.5) * 2.0 * UE_DOUBLE_PI;
			const FVector Dir(FMath::Cos(Latitude) * FMath::Cos(Longitude), FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
			Heights[Y * W + X] = static_cast<float>(FEphemeraTerrain::HeightM(Definition->Terrain, Definition->RadiusM, Dir));
		}
	});
	float Low = TNumericLimits<float>::Max(), High = TNumericLimits<float>::Lowest();
	for (const float Value : Heights)
	{
		Low = FMath::Min(Low, Value);
		High = FMath::Max(High, Value);
	}
	const FString Header = FString::Printf(TEXT("P5\n# heights_m %f %f\n%d %d\n65535\n"), Low, High, W, H);
	TArray<uint8> File;
	File.Append(reinterpret_cast<const uint8*>(TCHAR_TO_ANSI(*Header)), Header.Len());
	File.Reserve(File.Num() + W * H * 2);
	for (const float Value : Heights)
	{
		const uint16 Level = static_cast<uint16>(FMath::RoundToInt32(FMath::Clamp((Value - Low) / FMath::Max(High - Low, 1.f), 0.f, 1.f) * 65535.f));
		File.Add(static_cast<uint8>(Level >> 8));       // PGM is big-endian
		File.Add(static_cast<uint8>(Level & 0xff));
	}
	return FFileHelper::SaveArrayToFile(File, *FilePath);
}

bool UEphemeraPlanetBuilder::BuildOcean(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, float RadiusOffsetM)
{
	if (!Definition)
	{
		return false;
	}
	constexpr int32 Resolution = 256;
	const int32 Row = Resolution + 1;
	const float RadiusCm = static_cast<float>((Definition->RadiusM + RadiusOffsetM) * 100.0);
	FBuiltMesh Sea;
	Sea.Vertices.SetNumUninitialized(6 * Row * Row);
	for (int32 F = 0; F < 6; ++F)
	{
		for (int32 J = 0; J <= Resolution; ++J)
		{
			for (int32 I = 0; I <= Resolution; ++I)
			{
				const FVector Dir = DirectionOf(CubeKey(Faces[F], I, J, Resolution), Resolution);
				FSurfaceVertex& V = Sea.Vertices[(F * Row + J) * Row + I];
				V.Position = FVector3f(Dir) * RadiusCm;
				V.Normal = FVector3f(Dir);
				V.Color = FVector4f(0.f, 0.f, 0.f, 1.f);
				V.UV = FVector2f(static_cast<float>(I) / Resolution, static_cast<float>(J) / Resolution);
			}
		}
	}
	Sea.Triangles.Reserve(6 * Resolution * Resolution * 2);
	for (int32 F = 0; F < 6; ++F)
	{
		for (int32 J = 0; J < Resolution; ++J)
		{
			for (int32 I = 0; I < Resolution; ++I)
			{
				const int32 Base = (F * Row + J) * Row + I;
				AddQuad(Sea, Base, Base + 1, Base + Row + 1, Base + Row);
			}
		}
	}
	// A real sea (at the datum) is an ordinary mesh, not Nanite: its material moves the surface
	// with the tide. The plain underlay of a dry world stays Nanite.
	const bool bSea = FMath::IsNearlyZero(RadiusOffsetM);
	UStaticMesh* Mesh = PrepareStaticMesh(PackageFolder, FString::Printf(TEXT("SM_%s_Ocean"), *Definition->BodyId.ToString()), ToMeshDescription(Sea), !bSea, false);
	if (!Mesh)
	{
		return false;
	}
	Mesh->bGenerateMeshDistanceField = false;
	if (!BuildAndSave({Mesh}))
	{
		return false;
	}
	Definition->Modify();
	Definition->OceanMesh = Mesh;
	Definition->MarkPackageDirty();
	return true;
}

bool UEphemeraPlanetBuilder::BuildStreams(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, const float FlowThreshold, const float LiftM)
{
	if (!Definition || !Definition->Terrain.HeightField)
	{
		return false;
	}
	const UEphemeraBodyDefinition& D = *Definition;
	const int32 Resolution = FMath::Clamp(D.Terrain.FaceResolution, 8, 4096);
	const int32 Row = Resolution + 1;
	// What the baked terrain says at every grid point: one pass, then the cells are picked from it.
	TArray<float> Flow, Height;
	Flow.SetNumUninitialized(6 * Row * Row);
	Height.SetNumUninitialized(6 * Row * Row);
	ParallelFor(6 * Row, [&](const int32 Line)
	{
		const int32 F = Line / Row, J = Line % Row;
		for (int32 I = 0; I <= Resolution; ++I)
		{
			const FVector Dir = DirectionOf(CubeKey(Faces[F], I, J, Resolution), Resolution);
			const int32 Index = (F * Row + J) * Row + I;
			Height[Index] = static_cast<float>(FEphemeraTerrain::HeightM(D.Terrain, D.RadiusM, Dir));
			// Not on the ice, and not on bare sand: there the ground has no relief to hide the sheet's
			// edges, and what shows is the mesh's own cells lying on the dunes.
			const bool bDry = FEphemeraTerrain::Ice(D.Terrain, Dir) > 0.5 || FEphemeraTerrain::Cover(D.Terrain, Dir) < -0.1;
			Flow[Index] = bDry ? 0.f : static_cast<float>(FEphemeraTerrain::FlowAsShaded(D.Terrain, Dir));
		}
	});
	FBuiltMesh Water;
	TMap<int32, int32> Placed;
	auto VertexFor = [&](const int32 F, const int32 I, const int32 J)
	{
		const int32 Index = (F * Row + J) * Row + I;
		if (const int32* Found = Placed.Find(Index))
		{
			return *Found;
		}
		const FVector Dir = DirectionOf(CubeKey(Faces[F], I, J, Resolution), Resolution);
		const FVector Normal = FEphemeraTerrain::Normal(D.Terrain, D.RadiusM, Dir);
		FSurfaceVertex V;
		// The sheet stands LiftM over the bed where the stream runs (a little more the more water
		// there is) and dips under the ground to either side: what shows is cut off by the ground
		// itself along a waterline, not by the edges of the mesh's cells.
		const float Wet = FMath::SmoothStep(FlowThreshold - 0.17f, FlowThreshold + 0.08f, Flow[Index]);
		const float Lift = FMath::Lerp(-0.6f, LiftM + 0.2f * FMath::Clamp((Flow[Index] - FlowThreshold) / 0.4f, 0.f, 1.f), Wet);
		V.Position = FVector3f(Dir * ((D.RadiusM + Height[Index] + Lift) * 100.0));
		V.Normal = FVector3f(Normal);
		// Water runs the way the ground leans: the part of the normal that lies along the ground.
		const FVector Down = (Normal - Dir * FVector::DotProduct(Normal, Dir)).GetSafeNormal();
		// Mesh colours are stored sRGB-encoded; the direction is written through the inverse of
		// that, so the material reads back exactly the packed vector.
		auto Packed = [](const double Component)
		{
			const double Byte = Component * 0.5 + 0.5;
			return static_cast<float>(Byte <= 0.04045 ? Byte / 12.92 : FMath::Pow((Byte + 0.055) / 1.055, 2.4));
		};
		V.Color = FVector4f(Packed(Down.X), Packed(Down.Y), Packed(Down.Z), Flow[Index]);
		V.UV = FVector2f(Height[Index], static_cast<float>(1.0 - FMath::Clamp(FVector::DotProduct(Normal, Dir), 0.0, 1.0)));
		const int32 Added = Water.Vertices.Add(V);
		Placed.Add(Index, Added);
		return Added;
	};
	for (int32 F = 0; F < 6; ++F)
	{
		for (int32 J = 0; J < Resolution; ++J)
		{
			for (int32 I = 0; I < Resolution; ++I)
			{
				const int32 A = (F * Row + J) * Row + I, B = A + 1, C = A + Row + 1, E = A + Row;
				const float Most = FMath::Max(FMath::Max(Flow[A], Flow[B]), FMath::Max(Flow[C], Flow[E]));
				const float Lowest = FMath::Min(FMath::Min(Height[A], Height[B]), FMath::Min(Height[C], Height[E]));
				// Above the tide line only: below it the sea has the water.
				if (Most < FlowThreshold || Lowest < 0.5f)
				{
					continue;
				}
				AddQuad(Water, VertexFor(F, I, J), VertexFor(F, I + 1, J), VertexFor(F, I + 1, J + 1), VertexFor(F, I, J + 1));
			}
		}
	}
	if (Water.Triangles.IsEmpty())
	{
		Definition->Modify();
		Definition->StreamMesh = nullptr;
		return true;
	}
	UStaticMesh* Mesh = PrepareStaticMesh(PackageFolder, FString::Printf(TEXT("SM_%s_Streams"), *D.BodyId.ToString()), ToMeshDescription(Water), false, false);
	if (!Mesh)
	{
		return false;
	}
	Mesh->bGenerateMeshDistanceField = false;
	if (!BuildAndSave({Mesh}))
	{
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("Ephemera planet builder: streams of %s: %d triangles"), *D.BodyId.ToString(), Water.Triangles.Num());
	Definition->Modify();
	Definition->StreamMesh = Mesh;
	Definition->MarkPackageDirty();
	return true;
}

UTexture2DArray* UEphemeraPlanetBuilder::BuildTerrainAttributes(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, const FString& Name)
{
	if (!Definition || Definition->RadiusM <= 0.0)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: no definition for the terrain attributes."));
		return nullptr;
	}
	const UEphemeraBodyDefinition& D = *Definition;
	const int32 Resolution = FMath::Clamp(D.Terrain.FaceResolution, 8, 4096);
	// Two grid cells past every face edge, evaluated where they lie on the sphere (the grid simply continues
	// past an edge): a cubic filter reads two texels either side, and finds the neighbouring face's values there.
	constexpr int32 Apron = 2;
	const int32 Size = Resolution + 1 + 2 * Apron;
	TArray<uint8> Bytes;
	Bytes.SetNumZeroed(Size * Size * 12 * 4);
	auto Byte = [](const float Value) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.f), 0, 255)); };
	ParallelFor(6 * Size, [&](const int32 Line)
	{
		const int32 F = Line / Size, J = Line % Size;
		for (int32 I = 0; I < Size; ++I)
		{
			const FSurfaceVertex V = Evaluate(D, DirectionOf(CubeKey(Faces[F], I - Apron, J - Apron, Resolution), Resolution), 0.0, 1.0);
			// Encoded exactly as the engine encodes a mesh's vertex colours (sRGB in RGB, alpha linear), so the
			// material reads the very numbers it read from the vertices, and every threshold tuned on them holds.
			const FColor Encoded = FLinearColor(V.Color.X, V.Color.Y, V.Color.Z, V.Color.W).ToFColor(true);
			uint8* A = &Bytes[((F * Size + J) * Size + I) * 4];          // BGRA
			A[0] = Encoded.B;
			A[1] = Encoded.G;
			A[2] = Encoded.R;
			A[3] = Encoded.A;
			uint8* C = &Bytes[(((6 + F) * Size + J) * Size + I) * 4];
			C[2] = Byte(0.5f * (V.UV.Y + 1.f));
		}
	});
	const FString PackageName = PackageFolder / Name;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UTexture2DArray* Texture = FindObject<UTexture2DArray>(Package, *Name);
	if (!Texture)
	{
		Texture = NewObject<UTexture2DArray>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Texture);
	}
	Texture->Modify();
	Texture->Source.Init(Size, Size, 12, 1, TSF_BGRA8, Bytes.GetData());
	Texture->SRGB = false;
	// Uncompressed, no mips: these are data, read at their own texels by a cubic filter.
	Texture->CompressionSettings = TC_VectorDisplacementmap;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	Texture->PostEditChange();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Texture, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: could not save %s"), *Filename);
		return nullptr;
	}
	UE_LOG(LogTemp, Display, TEXT("Ephemera planet builder: terrain attributes %s, %d x %d x 12"), *PackageName, Size, Size);
	return Texture;
}

UTexture2DArray* UEphemeraPlanetBuilder::BuildTerrainHeights(UEphemeraBodyDefinition* Definition, const FString& PackageFolder, const FString& Name)
{
	if (!Definition || Definition->RadiusM <= 0.0)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: no definition for the terrain heights."));
		return nullptr;
	}
	const UEphemeraBodyDefinition& D = *Definition;
	const int32 Resolution = FMath::Clamp(D.Terrain.FaceResolution, 8, 4096);
	constexpr int32 Apron = 2;
	const int32 Size = Resolution + 1 + 2 * Apron;
	TArray<float> Heights;
	Heights.SetNumZeroed(Size * Size * 6);
	ParallelFor(6 * Size, [&](const int32 Line)
	{
		const int32 F = Line / Size, J = Line % Size;
		for (int32 I = 0; I < Size; ++I)
		{
			const FVector Direction = DirectionOf(CubeKey(Faces[F], I - Apron, J - Apron, Resolution), Resolution);
			Heights[(F * Size + J) * Size + I] = static_cast<float>(FEphemeraTerrain::HeightM(D.Terrain, D.RadiusM, Direction));
		}
	});
	const FString PackageName = PackageFolder / Name;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UTexture2DArray* Texture = FindObject<UTexture2DArray>(Package, *Name);
	if (!Texture)
	{
		Texture = NewObject<UTexture2DArray>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Texture);
	}
	Texture->Modify();
	Texture->Source.Init(Size, Size, 6, 1, TSF_R32F, reinterpret_cast<const uint8*>(Heights.GetData()));
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_SingleFloat;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->Filter = TF_Nearest;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	Texture->PostEditChange();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Texture, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: could not save %s"), *Filename);
		return nullptr;
	}
	UE_LOG(LogTemp, Display, TEXT("Ephemera planet builder: terrain heights %s, %d x %d x 6"), *PackageName, Size, Size);
	return Texture;
}

namespace
{
	/** Fractal noise for the surface of stone, about -0.5..0.5. */
	float StoneNoise(const FVector3f& P, const int32 Octaves)
	{
		float Sum = 0.f;
		float Amplitude = 0.5f;
		FVector Q(P);
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			Sum += Amplitude * FMath::PerlinNoise3D(Q + FVector(Octave * 17.3, Octave * 5.1, Octave * 11.7));
			Q *= 2.03;
			Amplitude *= 0.5f;
		}
		return Sum;
	}

	/** What is left of a convex polygon on the side of a line where Dot(X, Normal) <= Limit. */
	TArray<FVector2f> ClipToHalfPlane(const TArray<FVector2f>& Polygon, const FVector2f& Normal, const float Limit)
	{
		TArray<FVector2f> Kept;
		const int32 Count = Polygon.Num();
		for (int32 K = 0; K < Count; ++K)
		{
			const FVector2f A = Polygon[K];
			const FVector2f B = Polygon[(K + 1) % Count];
			const float Da = FVector2f::DotProduct(A, Normal) - Limit;
			const float Db = FVector2f::DotProduct(B, Normal) - Limit;
			if (Da <= 0.f)
			{
				Kept.Add(A);
			}
			if ((Da <= 0.f) != (Db <= 0.f))
			{
				Kept.Add(A + (B - A) * (Da / (Da - Db)));
			}
		}
		return Kept;
	}

	/** Adds a triangle wound so its front faces along Outward (Unreal's front is clockwise). */
	void AddFacing(FBuiltMesh& Mesh, const int32 A, const int32 B, const int32 C, const FVector3f& Outward)
	{
		const FVector3f PA = Mesh.Vertices[A].Position, PB = Mesh.Vertices[B].Position, PC = Mesh.Vertices[C].Position;
		if (FVector3f::DotProduct(FVector3f::CrossProduct(PC - PA, PB - PA), Outward) >= 0.f)
		{
			Mesh.Triangles.Add(FIntVector(A, B, C));
		}
		else
		{
			Mesh.Triangles.Add(FIntVector(A, C, B));
		}
	}
}

UStaticMesh* UEphemeraPlanetBuilder::BuildBasaltColumns(const FString& PackageFolder, const FString& Name, const int32 Seed, const int32 Style,
	const float RadiusCm, const float HeightCm, const float ColumnCm)
{
	FRandomStream Random(Seed);
	auto Draw = [&Random](const float Low, const float High) { return static_cast<float>(Random.FRandRange(Low, High)); };
	auto Chance = [&Random]() { return static_cast<float>(Random.FRand()); };
	const float Spacing = FMath::Max(ColumnCm, 15.f);
	const float Unit = Spacing / 55.f;                                // detail scales with the columns
	const float Around = FMath::Clamp(Spacing * 0.05f, 2.f, 4.5f);    // the mesh's own spacing round a column
	const float Up = Around * 1.2f;                                   // and up it
	// Massifs stand on uneven ground and run deeper into it.
	const float BottomCm = Style == 0 ? -400.f : -150.f;
	constexpr float GroundCm = -20.f;
	const float JointCm = FMath::Max(Spacing * 0.04f, 1.5f);          // the open crack between two columns
	const FVector3f Offset(Draw(-500.f, 500.f), Draw(-500.f, 500.f), Draw(-500.f, 500.f));

	// Where the columns stand: a hexagonal lattice, shaken. The joints of a cooling flow are nearly
	// regular but never quite; most columns come out six-sided, some five or seven.
	TArray<FVector2f> Seeds;
	const int32 Span = FMath::CeilToInt32(RadiusCm / Spacing) + 3;
	for (int32 Row = -Span; Row <= Span; ++Row)
	{
		for (int32 Col = -Span; Col <= Span; ++Col)
		{
			FVector2f P((Col + 0.5f * (Row & 1)) * Spacing, Row * Spacing * 0.8660254f);
			P += FVector2f(Draw(-0.21f, 0.21f), Draw(-0.21f, 0.21f)) * Spacing;
			if (P.Size() < RadiusCm + 2.5f * Spacing)
			{
				Seeds.Add(P);
			}
		}
	}
	// The cluster's outline is ragged, not a disc; one side of it (Toward) is its face.
	const float Lean = Draw(0.f, UE_TWO_PI);
	const FVector2f Toward(FMath::Cos(Lean), FMath::Sin(Lean));
	auto Reach = [&](const FVector2f& P)
	{
		const FVector2f D = P.GetSafeNormal();
		return RadiusCm * (0.8f + 0.55f * StoneNoise(FVector3f(D.X * 1.2f, D.Y * 1.2f, 0.f) + Offset * 0.01f, 3));
	};

	// First every column's cell and height, so each can see its neighbours.
	struct FColumn { TArray<FVector2f> Cell; FVector2f Centre; float Inradius; float Height; int32 Seed; };
	TArray<FColumn> Columns;
	TArray<int32> ColumnOfSeed;
	ColumnOfSeed.Init(INDEX_NONE, Seeds.Num());
	for (int32 Index = 0; Index < Seeds.Num(); ++Index)
	{
		const FVector2f Centre = Seeds[Index];
		const float Out = Centre.Size() / FMath::Max(Reach(Centre), 1.f);
		if (Out > 1.f)
		{
			continue;
		}
		// Its cell: everything nearer this seed than any other, less half a joint each side.
		const float Box = Spacing * 1.6f;
		TArray<FVector2f> Cell = {{-Box, -Box}, {Box, -Box}, {Box, Box}, {-Box, Box}};
		for (int32 Other = 0; Other < Seeds.Num() && Cell.Num() >= 3; ++Other)
		{
			const FVector2f Between = Seeds[Other] - Centre;
			const float Apart = Between.Size();
			if (Other == Index || Apart > Spacing * 2.4f)
			{
				continue;
			}
			Cell = ClipToHalfPlane(Cell, Between / Apart, Apart * 0.5f - JointCm * 0.5f);
		}
		if (Cell.Num() < 3)
		{
			continue;
		}
		float Inradius = TNumericLimits<float>::Max();
		for (int32 K = 0; K < Cell.Num(); ++K)
		{
			const FVector2f A = Cell[K], B = Cell[(K + 1) % Cell.Num()];
			const FVector2f Along = (B - A).GetSafeNormal();
			Inradius = FMath::Min(Inradius, FMath::Abs(A.X * Along.Y - A.Y * Along.X));
		}
		if (Inradius < Spacing * 0.15f)
		{
			continue;
		}
		for (FVector2f& P : Cell)
		{
			P += Centre;
		}

		// How tall. A massif stands full height over most of its top, which tilts and swells a little,
		// and breaks down at its edges in organ pipes, steepest on its face. A causeway climbs in
		// irregular steps toward one side. Neighbours differ, and here and there one has broken off.
		const float Side = FVector2f::DotProduct(Centre, Toward) / RadiusCm;
		const float Swell = StoneNoise(FVector3f(Centre.X, Centre.Y, 0.f) / (Spacing * 4.f) + Offset, 2);
		float Height;
		if (Style == 0)
		{
			const float Shoulder = 1.f - FMath::SmoothStep(0.55f - 0.25f * FMath::Max(-Side, 0.f), 1.0f, Out);
			Height = HeightCm * (0.18f + 0.82f * Shoulder) * (0.9f + 0.1f * Side + Swell * 0.35f) * Draw(0.93f, 1.04f);
			if (Chance() < 0.07f)
			{
				Height *= Draw(0.55f, 0.85f);
			}
		}
		else
		{
			const float Ramp = FMath::Clamp(0.45f + 0.6f * Side + Swell * 0.9f, 0.f, 1.f);
			Height = HeightCm * (0.14f + 0.86f * Ramp * Ramp) * (1.f - 0.4f * Out * Out) * Draw(0.85f, 1.1f);
			if (Chance() < 0.18f)
			{
				Height *= Draw(0.5f, 0.82f);
			}
		}
		ColumnOfSeed[Index] = Columns.Num();
		Columns.Add({MoveTemp(Cell), Centre, Inradius, FMath::Max(Height, 22.f), Index});
	}

	FBuiltMesh Stone;
	FBuiltMesh Blocks;
	// One prism of stone. Cell: its outline (anticlockwise), Height: its top, Hidden: how far down its
	// sides can be seen, Bottom: where it ends below. With bCapped it is closed at the bottom too (a
	// broken-off piece). Place: where it goes in the mesh.
	auto AddPrism = [&](const TArray<FVector2f>& Cell, const float Inradius, const float Height, const float Hidden, const float Bottom,
		const bool bCapped, const FTransform3f& Place)
	{
		const int32 Sides = Cell.Num();
		// Round its corners (worn, a few cm) and walk its outline: positions and outward normals.
		TArray<float> Cuts, Turns;
		for (int32 K = 0; K < Sides; ++K)
		{
			const FVector2f Prev = Cell[(K + Sides - 1) % Sides], Corner = Cell[K], Next = Cell[(K + 1) % Sides];
			const float Turn = FMath::Acos(FMath::Clamp(FVector2f::DotProduct((Corner - Prev).GetSafeNormal(), (Next - Corner).GetSafeNormal()), -1.f, 1.f));
			Turns.Add(Turn);
			Cuts.Add(FMath::Min(Draw(2.f, 8.f) * Unit * FMath::Tan(Turn * 0.5f), 0.4f * FMath::Min((Corner - Prev).Size(), (Next - Corner).Size())));
		}
		TArray<FVector2f> Ring;
		TArray<FVector2f> RingN;
		for (int32 K = 0; K < Sides; ++K)
		{
			const FVector2f Prev = Cell[(K + Sides - 1) % Sides], Corner = Cell[K], Next = Cell[(K + 1) % Sides];
			const FVector2f In = (Corner - Prev).GetSafeNormal(), OutDir = (Next - Corner).GetSafeNormal();
			const float Radius = Cuts[K] / FMath::Max(FMath::Tan(Turns[K] * 0.5f), 1e-3f);
			const FVector2f NIn(In.Y, -In.X);             // outward normal of the edge coming in (the outline runs anticlockwise)
			const FVector2f Hub = Corner - In * Cuts[K] - NIn * Radius;
			constexpr int32 Arc = 4;
			for (int32 A = 0; A < Arc; ++A)
			{
				const float Angle = Turns[K] * A / Arc;
				const FVector2f N = NIn * FMath::Cos(Angle) + In * FMath::Sin(Angle);
				Ring.Add(Hub + N * Radius);
				RingN.Add(N);
			}
			// The straight run to where the next corner's arc begins.
			const FVector2f Start = Corner + OutDir * Cuts[K];
			const float Run = FMath::Max((Next - Corner).Size() - Cuts[K] - Cuts[(K + 1) % Sides], 0.f);
			const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Run / Around));
			const FVector2f EdgeN(OutDir.Y, -OutDir.X);
			for (int32 S = 0; S < Steps; ++S)
			{
				Ring.Add(Start + OutDir * (Run * S / Steps));
				RingN.Add(EdgeN);
			}
		}
		FVector2f Mid(0.f, 0.f);
		for (const FVector2f& P : Cell)
		{
			Mid += P;
		}
		Mid /= Sides;

		// Its top: tilted a little, dished or domed, its edge worn round. (A broken end is rougher.)
		const FVector2f Tilt = FVector2f(Draw(-1.f, 1.f), Draw(-1.f, 1.f)) * (bCapped ? 0.18f : Style == 0 ? 0.08f : 0.06f);
		const float Dish = Draw(-0.06f, 0.04f) * Inradius;
		const float Bevel = FMath::Min(Draw(3.f, 10.f) * Unit, Inradius * 0.3f);
		const FVector2f FootTilt = FVector2f(Draw(-1.f, 1.f), Draw(-1.f, 1.f)) * 0.18f;
		auto TopAt = [&](const FVector2f& P) { return Height + FVector2f::DotProduct(Tilt, P - Mid); };
		auto FootAt = [&](const FVector2f& P) { return bCapped ? Bottom + FVector2f::DotProduct(FootTilt, P - Mid) : Bottom; };
		// The cross joints that part a column into drums: tilted a little, a metre or two apart, most
		// of them closed tight, a few weathered open.
		struct FJoint { float Z; FVector2f Tilt; float Depth; float Bulge; };
		TArray<FJoint> Joints;
		for (float Z = Bottom + Draw(30.f, 120.f); Z < Height - Bevel * 2.f; Z += Draw(1.3f, 3.6f) * Spacing)
		{
			const bool bOpen = Chance() < 0.25f;
			Joints.Add({Z, FVector2f(Draw(-0.12f, 0.12f), Draw(-0.12f, 0.12f)), (bOpen ? Draw(2.f, 3.5f) : Draw(0.5f, 1.2f)) * Unit, Draw(-0.45f, 0.45f) * Unit});
		}
		// Chips knocked off its corners and its ends.
		struct FChip { FVector3f At; float Radius; };
		TArray<FChip> Chips;
		const int32 ChipCount = Random.RandRange(3, 9);
		for (int32 C = 0; C < ChipCount; ++C)
		{
			const int32 K = Random.RandRange(0, Sides - 1);
			const float Pick = Chance();
			const float Z = Pick < 0.5f ? Height : bCapped && Pick < 0.75f ? Bottom : Draw(FMath::Max(Hidden, Bottom), Height);
			Chips.Add({FVector3f(Cell[K].X, Cell[K].Y, Z), Draw(5.f, 18.f) * Unit});
		}

		// A lathe over the outline: (in across the foot and round its edge, for a broken piece,) up the
		// side, round the worn top edge, in across the top. The side below Hidden (in the ground, or
		// behind the neighbours) is carried by a few levels only.
		enum class EPart : uint8 { FootCap, FootBevel, Side, TopBevel, TopCap };
		struct FLevel { EPart Part; float T; };
		TArray<FLevel> Profile;
		const int32 CapLevels = FMath::Max(2, FMath::CeilToInt32(Inradius / (Around * 1.3f)));
		constexpr int32 BevelLevels = 5;
		if (bCapped)
		{
			for (int32 L = 1; L < CapLevels; ++L)
			{
				Profile.Add({EPart::FootCap, 1.f - static_cast<float>(L) / CapLevels});
			}
			for (int32 L = 0; L < BevelLevels; ++L)
			{
				Profile.Add({EPart::FootBevel, static_cast<float>(L) / BevelLevels});
			}
		}
		const float From = FMath::Max(Hidden, Bottom);
		const int32 Coarse = bCapped || From <= Bottom + 1.f ? 0 : 3;
		const int32 Fine = FMath::Max(2, FMath::CeilToInt32((Height - Bevel - From) / Up));
		for (int32 L = 0; L < Coarse; ++L)
		{
			Profile.Add({EPart::Side, -1.f + static_cast<float>(L) / Coarse});      // -1..0: Bottom up to From
		}
		for (int32 L = 0; L <= Fine; ++L)
		{
			Profile.Add({EPart::Side, static_cast<float>(L) / Fine});               // 0..1: From up to the top's bevel
		}
		for (int32 L = 1; L <= BevelLevels; ++L)
		{
			Profile.Add({EPart::TopBevel, static_cast<float>(L) / BevelLevels});
		}
		for (int32 L = 1; L < CapLevels; ++L)
		{
			Profile.Add({EPart::TopCap, static_cast<float>(L) / CapLevels});
		}
		const int32 M = Ring.Num();
		const int32 Levels = Profile.Num();
		TArray<FVector3f> Base;
		TArray<FVector3f> BaseN;
		TArray<float> SideWeight;
		Base.Reserve(Levels * M);
		for (const FLevel& Level : Profile)
		{
			for (int32 I = 0; I < M; ++I)
			{
				const FVector2f P = Ring[I], N = RingN[I];
				const float Top = TopAt(P);
				const float Foot = FootAt(P) + (bCapped ? Bevel : 0.f);
				FVector3f At, Normal;
				float SideW = 1.f;
				switch (Level.Part)
				{
				case EPart::FootCap:
				case EPart::TopCap:
				{
					const FVector2f Q = Mid + (P - N * Bevel - Mid) * (1.f - Level.T);
					const bool bTop = Level.Part == EPart::TopCap;
					At = FVector3f(Q.X, Q.Y, bTop ? TopAt(Q) + Dish * (1.f - FMath::Square(1.f - Level.T)) : FootAt(Q));
					Normal = FVector3f(0.f, 0.f, bTop ? 1.f : -1.f);
					SideW = 0.f;
					break;
				}
				case EPart::FootBevel:
				case EPart::TopBevel:
				{
					const bool bTop = Level.Part == EPart::TopBevel;
					const float Angle = UE_HALF_PI * (bTop ? Level.T : 1.f - Level.T);
					const float Inset = Bevel * (1.f - FMath::Cos(Angle));
					const float Z = bTop ? Top - Bevel + Bevel * FMath::Sin(Angle) : Foot - Bevel * FMath::Sin(Angle);
					At = FVector3f(P.X - N.X * Inset, P.Y - N.Y * Inset, Z);
					Normal = FVector3f(N.X * FMath::Cos(Angle), N.Y * FMath::Cos(Angle), bTop ? FMath::Sin(Angle) : -FMath::Sin(Angle));
					SideW = FMath::Cos(Angle);
					break;
				}
				default:
				{
					const float Z = Level.T < 0.f ? From + (From - Bottom) * Level.T : FMath::Lerp(bCapped ? Foot : From, Top - Bevel, Level.T);
					At = FVector3f(P.X, P.Y, Z);
					Normal = FVector3f(N.X, N.Y, 0.f);
					break;
				}
				}
				Base.Add(At);
				BaseN.Add(Normal);
				SideWeight.Add(SideW);
			}
		}
		const FVector3f TopApex(Mid.X, Mid.Y, TopAt(Mid) + Dish);
		const FVector3f FootApex(Mid.X, Mid.Y, FootAt(Mid));

		// Displace the lathe into stone.
		auto Displace = [&](const FVector3f& At, const float SideW)
		{
			// The grain of the rock, and the slow waviness of a face.
			float D = StoneNoise(At / (9.f * Unit) + Offset, 3) * 0.9f * Unit + StoneNoise(At / (30.f * Unit) + Offset, 3) * 2.2f * Unit;
			if (SideW > 0.f)
			{
				// Faint banding across each face (the "chisel marks" every column carries).
				D += SideW * 0.3f * Unit * FMath::Sin(At.Z * (UE_TWO_PI / (6.f * Unit)) + 4.f * StoneNoise(At / 30.f + Offset, 2));
				// The joints: a crack with weathered shoulders; each drum stands a little proud or shy of the next.
				float Drum = 0.f;
				for (const FJoint& Joint : Joints)
				{
					const float Dz = At.Z - (Joint.Z + FVector2f::DotProduct(Joint.Tilt, FVector2f(At.X, At.Y) - Mid)) - StoneNoise(At / 18.f + Offset, 2) * 3.f * Unit;
					D -= SideW * Joint.Depth * (FMath::Exp(-FMath::Square(Dz / (1.6f * Unit))) + 0.4f * FMath::Exp(-FMath::Square(Dz / (5.f * Unit))));
					if (Dz > 0.f)
					{
						Drum = Joint.Bulge;
					}
				}
				D += SideW * Drum;
			}
			for (const FChip& Chip : Chips)
			{
				D -= FMath::Max(Chip.Radius - FVector3f::Dist(At, Chip.At), 0.f) * 0.8f;
			}
			// Below the ground no one sees it.
			return !bCapped && At.Z < GroundCm ? D * 0.3f : D;
		};
		TArray<FVector3f> Local;
		Local.SetNumUninitialized(Base.Num() + 2);
		for (int32 V = 0; V < Base.Num(); ++V)
		{
			Local[V] = Base[V] + BaseN[V] * Displace(Base[V], SideWeight[V]);
		}
		const int32 TopIndex = Base.Num();
		const int32 FootIndex = Base.Num() + 1;
		Local[TopIndex] = TopApex + FVector3f(0.f, 0.f, Displace(TopApex, 0.f));
		Local[FootIndex] = FootApex - FVector3f(0.f, 0.f, Displace(FootApex, 0.f));

		// Normals from the displaced surface itself, so every crack and chip is lit.
		auto Index = [M](const int32 I, const int32 J) { return J * M + ((I % M) + M) % M; };
		TArray<FVector3f> Normals;
		Normals.SetNumUninitialized(Local.Num());
		for (int32 J = 0; J < Levels; ++J)
		{
			for (int32 I = 0; I < M; ++I)
			{
				const FVector3f Across = Local[Index(I + 1, J)] - Local[Index(I - 1, J)];
				const FVector3f Below = J > 0 ? Local[Index(I, J - 1)] : bCapped ? Local[FootIndex] : Local[Index(I, J)];
				const FVector3f Above = J + 1 < Levels ? Local[Index(I, J + 1)] : Local[TopIndex];
				FVector3f N = FVector3f::CrossProduct(Across, Above - Below).GetSafeNormal();
				if (FVector3f::DotProduct(N, BaseN[Index(I, J)]) < 0.f)
				{
					N = -N;
				}
				Normals[Index(I, J)] = N.IsNearlyZero() ? BaseN[Index(I, J)] : N;
			}
		}
		Normals[TopIndex] = FVector3f(0.f, 0.f, 1.f);
		Normals[FootIndex] = FVector3f(0.f, 0.f, -1.f);

		const int32 First = Stone.Vertices.Num();
		for (int32 V = 0; V < Local.Num(); ++V)
		{
			FSurfaceVertex Vertex;
			Vertex.Position = Place.TransformPosition(Local[V]);
			Vertex.Normal = Place.TransformVectorNoScale(Normals[V]);
			Vertex.Color = FVector4f(1.f, 1.f, 1.f, 1.f);
			Vertex.UV = FVector2f(Vertex.Position.X * 0.01f, Vertex.Position.Z * 0.01f);
			Stone.Vertices.Add(Vertex);
		}
		auto Facing = [&](const int32 A, const int32 B, const int32 C, const FVector3f& Out)
		{
			AddFacing(Stone, First + A, First + B, First + C, Place.TransformVectorNoScale(Out));
		};
		for (int32 J = 0; J + 1 < Levels; ++J)
		{
			for (int32 I = 0; I < M; ++I)
			{
				const FVector3f Outward = BaseN[Index(I, J)] + BaseN[Index(I, J + 1)];
				Facing(Index(I, J), Index(I + 1, J), Index(I + 1, J + 1), Outward);
				Facing(Index(I, J), Index(I + 1, J + 1), Index(I, J + 1), Outward);
			}
		}
		for (int32 I = 0; I < M; ++I)
		{
			Facing(Index(I, Levels - 1), Index(I + 1, Levels - 1), TopIndex, FVector3f(0.f, 0.f, 1.f));
			if (bCapped)
			{
				Facing(Index(I, 0), Index(I + 1, 0), FootIndex, FVector3f(0.f, 0.f, -1.f));
			}
		}

		// Its collision: the plain prism.
		const int32 BlockFirst = Blocks.Vertices.Num();
		for (int32 K = 0; K < Sides; ++K)
		{
			for (const float Z : {FootAt(Cell[K]), TopAt(Cell[K])})
			{
				FSurfaceVertex Vertex;
				Vertex.Position = Place.TransformPosition(FVector3f(Cell[K].X, Cell[K].Y, Z));
				Vertex.Normal = FVector3f(0.f, 0.f, 1.f);
				Vertex.Color = FVector4f(1.f, 1.f, 1.f, 1.f);
				Vertex.UV = FVector2f(0.f, 0.f);
				Blocks.Vertices.Add(Vertex);
			}
		}
		for (int32 K = 0; K < Sides; ++K)
		{
			const int32 A = BlockFirst + K * 2, B = BlockFirst + ((K + 1) % Sides) * 2;
			const FVector2f Along = (Cell[(K + 1) % Sides] - Cell[K]).GetSafeNormal();
			const FVector3f Outward = Place.TransformVectorNoScale(FVector3f(Along.Y, -Along.X, 0.f));
			AddFacing(Blocks, A, B, B + 1, Outward);
			AddFacing(Blocks, A, B + 1, A + 1, Outward);
			if (K >= 2)
			{
				AddFacing(Blocks, BlockFirst + 1, BlockFirst + (K - 1) * 2 + 1, BlockFirst + K * 2 + 1, Place.TransformVectorNoScale(FVector3f(0.f, 0.f, 1.f)));
				if (bCapped)
				{
					AddFacing(Blocks, BlockFirst, BlockFirst + (K - 1) * 2, BlockFirst + K * 2, Place.TransformVectorNoScale(FVector3f(0.f, 0.f, -1.f)));
				}
			}
		}
	};

	for (const FColumn& Column : Columns)
	{
		// How far down its sides can be seen: to the lowest neighbour's top, or to the ground at the edge.
		float Hidden = TNumericLimits<float>::Max();
		for (int32 Other = 0; Other < Seeds.Num(); ++Other)
		{
			if (Other == Column.Seed || FVector2f::Distance(Seeds[Other], Column.Centre) > Spacing * 1.4f)
			{
				continue;
			}
			const int32 Neighbour = ColumnOfSeed[Other];
			Hidden = FMath::Min(Hidden, Neighbour == INDEX_NONE ? GroundCm : Columns[Neighbour].Height - 25.f * Unit);
		}
		Hidden = FMath::Clamp(Hidden, GroundCm, Column.Height - 30.f * Unit);
		AddPrism(Column.Cell, Column.Inradius, Column.Height, Hidden, BottomCm, false, FTransform3f::Identity);

		// What has broken off the outermost columns lies at the foot: drums of the same prism, on their
		// sides, half bedded in the ground, most lying along the cluster's edge.
		if (Hidden <= GroundCm && Chance() < (Style == 0 ? 0.45f : 0.2f))
		{
			const int32 Pieces = Chance() < 0.3f ? 2 : 1;
			for (int32 Piece = 0; Piece < Pieces; ++Piece)
			{
				TArray<FVector2f> Cell = Column.Cell;
				for (FVector2f& P : Cell)
				{
					P -= Column.Centre;
				}
				const float Length = FMath::Min(Draw(0.7f, 2.4f) * Spacing, Column.Height * 0.8f + Spacing * 0.5f);
				const FVector2f Outward = Column.Centre.GetSafeNormal();
				const FVector2f Along(-Outward.Y, Outward.X);
				const float Yaw = FMath::Atan2(Along.Y, Along.X) + Draw(-0.6f, 0.6f);
				const FVector2f Lie = Column.Centre + Outward * (Column.Inradius + Draw(0.9f, 2.6f) * Spacing) + Along * Draw(-1.f, 1.f) * Spacing;
				// Local z (the prism's axis) laid along the ground, turned to Yaw, rolled any way about its axis.
				const FQuat4f Turn = FQuat4f(FVector3f::UpVector, Yaw) * FQuat4f(FVector3f(0.f, 1.f, 0.f), UE_HALF_PI)
					* FQuat4f(FVector3f::UpVector, Draw(0.f, UE_TWO_PI));
				const FVector3f Centre = FVector3f(Lie.X, Lie.Y, Column.Inradius * Draw(0.35f, 0.7f) + GroundCm * 0.5f);
				const FTransform3f Place(Turn, Centre - Turn.RotateVector(FVector3f(0.f, 0.f, Length * 0.5f)));
				AddPrism(Cell, Column.Inradius, Length, 0.f, 0.f, true, Place);
			}
		}
	}
	if (Columns.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera planet builder: %s has no columns"), *Name);
		return nullptr;
	}

	UStaticMesh* Collision = PrepareStaticMesh(PackageFolder, Name + TEXT("_Collision"), ToMeshDescription(Blocks), false, false);
	if (!Collision)
	{
		return nullptr;
	}
	Collision->bGenerateMeshDistanceField = false;
	if (!BuildAndSave({Collision}))
	{
		return nullptr;
	}
	UStaticMesh* Mesh = PrepareStaticMesh(PackageFolder, Name, ToMeshDescription(Stone), true, true);
	if (!Mesh)
	{
		return nullptr;
	}
	Mesh->ComplexCollisionMesh = Collision;
	UE_LOG(LogTemp, Display, TEXT("Ephemera planet builder: basalt %s, %d columns, %d triangles"), *Name, Columns.Num(), Stone.Triangles.Num());
	return BuildAndSave({Mesh}) ? Mesh : nullptr;
}
