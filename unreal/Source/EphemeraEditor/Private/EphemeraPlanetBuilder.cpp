#include "EphemeraPlanetBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
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
