#include "Frame/EphemeraGroundCover.h"
#include "Misc/ScopeExit.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Frame/EphemeraBody.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// Target cell edge on the ground. Small enough to stream smoothly, large enough to keep
	// the number of instanced components low.
	constexpr double TargetCellM = 16.0;
	constexpr double FarCellM = 96.0;
	// Cells grown per frame: growing is spread out so walking never hitches.
	// Cells are grown nearest first for a few milliseconds each frame, at least GrowPerFrame of them.
	// When there are none at all (arriving somewhere), far longer: a moment's pause on arrival is
	// better than watching the grass come in ring by ring for ten seconds afterwards.
	constexpr int32 GrowPerFrame = 2;
	constexpr double GrowSecondsPerFrame = 0.004;
	constexpr double GrowSecondsOnArrival = 1.5;
	constexpr int32 MaxPooled = 600;

	const FVector FaceN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	const FVector FaceU[6] = {{0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	const FVector FaceV[6] = {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
}

UEphemeraGroundCoverComponent::UEphemeraGroundCoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f;
}

FIntVector UEphemeraGroundCoverComponent::AddressOf(const FVector& D) const
{
	const FVector A = D.GetAbs();
	int32 Face;
	if (A.X >= A.Y && A.X >= A.Z) { Face = D.X > 0 ? 0 : 1; }
	else if (A.Y >= A.Z) { Face = D.Y > 0 ? 2 : 3; }
	else { Face = D.Z > 0 ? 4 : 5; }
	const double Along = FVector::DotProduct(D, FaceN[Face]);
	// Undo the tan() warp so cells are nearly equal in size across the face.
	const double U = FMath::Atan(FVector::DotProduct(D, FaceU[Face]) / Along) * 4.0 / UE_DOUBLE_PI;
	const double V = FMath::Atan(FVector::DotProduct(D, FaceV[Face]) / Along) * 4.0 / UE_DOUBLE_PI;
	const int32 I = FMath::Clamp(FMath::FloorToInt32((U + 1.0) * 0.5 * CellsPerFace), 0, CellsPerFace - 1);
	const int32 J = FMath::Clamp(FMath::FloorToInt32((V + 1.0) * 0.5 * CellsPerFace), 0, CellsPerFace - 1);
	return FIntVector(Face, I, J);
}

void UEphemeraGroundCoverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AEphemeraBody* Body = Cast<AEphemeraBody>(GetOwner());
	const UEphemeraBodyDefinition* D = Body ? Body->Definition.Get() : nullptr;
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!D || D->GroundCover.IsEmpty() || !Camera)
	{
		bSettled = true;
		return;
	}
	const double RadiusCm = D->RadiusM * 100.0;
	CellsPerFace = FMath::Max(1, FMath::RoundToInt32(UE_DOUBLE_HALF_PI * D->RadiusM / (bFarTier ? FarCellM : TargetCellM)));
	CellSizeCm = UE_DOUBLE_HALF_PI * RadiusCm / CellsPerFace;

	const FTransform BodyPose = Body->GetActorTransform();
	const FVector Local = BodyPose.InverseTransformPosition(Camera->GetCameraLocation());
	const double Altitude = Local.Size() - RadiusCm;
	const double Reach = (bFarTier ? D->GroundCoverFarRadiusM : D->GroundCoverRadiusM) * 100.0;
	++Pass;
	int32 Waiting = 0;
	ON_SCOPE_EXIT
	{
		LastLocal = Local;
		bHaveLast = true;
		bSettled = Waiting == 0;
	};
	// Above the reach nothing is close enough to see: let everything go.
	if (Altitude < Reach * 1.5)
	{
		const FVector Up = Local.GetSafeNormal();
		FVector TangentA, TangentB;
		Up.FindBestAxisVectors(TangentA, TangentB);
		const int32 Steps = FMath::CeilToInt32(Reach / CellSizeCm);
		int32 Grown = 0;
		// "Arriving": nothing grown yet and the camera not travelling (a start, a teleport, a landing).
		const bool bArrived = Cells.IsEmpty() && (!bHaveLast || FVector::DistSquared(Local, LastLocal) < FMath::Square(300.0));
		const double Until = FPlatformTime::Seconds() + (bArrived ? GrowSecondsOnArrival : GrowSecondsPerFrame);
		// Nearest first, so the ground under your feet fills in before the ring's edge.
		for (int32 Ring = 0; Ring <= Steps; ++Ring)
		{
			for (int32 X = -Ring; X <= Ring; ++X)
			{
				for (int32 Y = -Ring; Y <= Ring; ++Y)
				{
					if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring)
					{
						continue;
					}
					const FVector Offset = (TangentA * X + TangentB * Y) * CellSizeCm;
					if (Offset.Size() > Reach + CellSizeCm)
					{
						continue;
					}
					const FIntVector Address = AddressOf((Up * RadiusCm + Offset).GetSafeNormal());
					FCell& Cell = Cells.FindOrAdd(Address);
					if (Cell.Seen == 0 && (Grown < GrowPerFrame || FPlatformTime::Seconds() < Until))
					{
						GrowCell(Address, Cell);
						++Grown;
						Cell.Seen = Pass;
					}
					else if (Cell.Seen != 0)
					{
						Cell.Seen = Pass;
					}
					else
					{
						++Waiting;
					}
				}
			}
		}
	}
	for (auto It = Cells.CreateIterator(); It; ++It)
	{
		// Cells that were asked for but not grown yet (Seen == 0) are simply forgotten too.
		if (It.Value().Seen + 30 < Pass || It.Value().Seen == 0)
		{
			DropCell(It.Value());
			It.RemoveCurrent();
		}
	}
}

void UEphemeraGroundCoverComponent::GrowCell(const FIntVector& Address, FCell& Cell)
{
	const AEphemeraBody* Body = CastChecked<AEphemeraBody>(GetOwner());
	const UEphemeraBodyDefinition& D = *Body->Definition;
	const int32 Face = Address.X;
	const double CellAreaM2 = FMath::Square(CellSizeCm / 100.0);
	// The ground inside one cell is sampled once on a small lattice; every plant then reads its
	// position and slope from the lattice. Evaluating terrain noise per plant costs milliseconds
	// per cell at meadow densities, which shows as hitching while you walk.
	constexpr int32 Lattice = 9;
	FVector Ground[Lattice][Lattice];
	FVector GroundDir[Lattice][Lattice];
	for (int32 J = 0; J < Lattice; ++J)
	{
		for (int32 I = 0; I < Lattice; ++I)
		{
			const double U = FMath::Tan(((Address.Y + static_cast<double>(I) / (Lattice - 1)) / CellsPerFace * 2.0 - 1.0) * UE_DOUBLE_PI / 4.0);
			const double V = FMath::Tan(((Address.Z + static_cast<double>(J) / (Lattice - 1)) / CellsPerFace * 2.0 - 1.0) * UE_DOUBLE_PI / 4.0);
			GroundDir[J][I] = (FaceN[Face] + FaceU[Face] * U + FaceV[Face] * V).GetSafeNormal();
			Ground[J][I] = FEphemeraTerrain::SurfacePointCm(D, GroundDir[J][I], false);
		}
	}
	const FVector CellDir = GroundDir[Lattice / 2][Lattice / 2];
	const bool bLift = D.MicroRelief > 0.f && FEphemeraTerrain::ReliefIsGeometry();
	// One answer per cell is enough for these slow fields.
	const double CellMoisture = FEphemeraTerrain::Moisture(D.Terrain, CellDir);
	const double CellCover = FEphemeraTerrain::Cover(D.Terrain, CellDir);
	const bool bCellOnIce = FEphemeraTerrain::Ice(D.Terrain, CellDir) > 0.4;
	const double DatumCm = D.RadiusM * 100.0;
	const bool bCellOnPlateau = FEphemeraTerrain::InsidePlateau(D.Terrain, D.RadiusM, CellDir, 0.35);
	for (int32 RuleIndex = 0; RuleIndex < D.GroundCover.Num(); ++RuleIndex)
	{
		const FEphemeraGroundCoverRule& Rule = D.GroundCover[RuleIndex];
		if (!Rule.Mesh || Rule.DensityPerM2 <= 0.f || Rule.bFar != bFarTier)
		{
			continue;
		}
		FRandomStream Random(HashCombine(HashCombine(GetTypeHash(Address), D.Terrain.Seed), RuleIndex * 7919));
		// Sparse things (one boulder to several cells) still appear: the fraction is a chance.
		const double Expected = Rule.DensityPerM2 * CellAreaM2;
		const int32 Count = FMath::FloorToInt32(Expected) + (Random.FRand() < Expected - FMath::FloorToDouble(Expected) ? 1 : 0);
		const double MinUp = FMath::Cos(FMath::DegreesToRadians(Rule.MaxSlopeDeg));
		const double MaxUp = FMath::Cos(FMath::DegreesToRadians(Rule.MinSlopeDeg));
		const double MeshHeightCm = Rule.Mesh->GetBounds().BoxExtent.Z * 2.0;
		TArray<FTransform> Instances;
		Instances.Reserve(Count);
		if (bCellOnPlateau || bCellOnIce || CellMoisture < Rule.MoistureMin || CellMoisture > Rule.MoistureMax
			|| CellCover < Rule.CoverMin || CellCover > Rule.CoverMax)
		{
			continue;        // the middle of a plateau is trodden ground; or the wrong soil for this plant
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double FU = Random.FRand() * (Lattice - 1), FV = Random.FRand() * (Lattice - 1);
			const float Scale = Rule.ScaleMin * FMath::Pow(Rule.ScaleMax / FMath::Max(Rule.ScaleMin, 0.001f), FMath::Pow(Random.FRand(), Rule.ScalePower));
			const float Yaw = Random.FRandRange(0.f, 360.f);
			const float Chance = Random.FRand();
			const FVector Tilt(Random.FRandRange(-1.f, 1.f), Random.FRandRange(-1.f, 1.f), 0.f);
			const int32 I = FMath::Min(static_cast<int32>(FU), Lattice - 2), J = FMath::Min(static_cast<int32>(FV), Lattice - 2);
			const double A = FU - I, B = FV - J;
			const FVector P00 = Ground[J][I], P10 = Ground[J][I + 1], P01 = Ground[J + 1][I], P11 = Ground[J + 1][I + 1];
			const FVector Position0 = FMath::Lerp(FMath::Lerp(P00, P10, A), FMath::Lerp(P01, P11, A), B);
			const FVector Dir = Position0.GetSafeNormal();
			FVector Normal = FVector::CrossProduct(P10 - P00, P01 - P00).GetSafeNormal();
			if (FVector::DotProduct(Normal, Dir) < 0.0)
			{
				Normal = -Normal;
			}
			const double Upness = FVector::DotProduct(Normal, Dir);
			const double AltitudeM = (Position0.Size() - DatumCm) / 100.0;
			if (Upness < MinUp || Upness > MaxUp || AltitudeM < Rule.AltitudeMinM || AltitudeM > Rule.AltitudeMaxM)
			{
				continue;
			}
			if (Rule.PatchCoverage < 1.f)
			{
				// Soft-edged patches: plants thin out toward a patch's edge instead of stopping at a line.
				const double Patch = FEphemeraTerrain::Patch(D.Terrain, Rule.PatchFrequency, RuleIndex + 1, Dir);
				if (Patch - (1.0 - Rule.PatchCoverage) < Chance * 0.18)
				{
					continue;
				}
			}
			// The lattice is the terrain mesh; the material raises the ground over it by its micro
			// relief (along the mesh's normal, so further in height the steeper the slope).
			const double Flow = D.StreamMesh ? FEphemeraTerrain::FlowAsShaded(D.Terrain, Dir) : 0.0;
			if (Flow > 0.66)
			{
				continue;        // a stream's bed is washed clean
			}
			const double ReliefCm = !bLift ? 0.0 : D.MicroRelief * 100.0 / FMath::Max(Upness, 0.6)
				* FEphemeraTerrain::MicroReliefM(Position0 / 100.0, FEphemeraTerrain::Cover(D.Terrain, Dir), FEphemeraTerrain::Ice(D.Terrain, Dir), AltitudeM, Flow);
			const FVector Position = Position0 + Dir * (ReliefCm - Rule.SinkCm - Rule.SinkFraction * MeshHeightCm * Scale);
			// Plants grow up, leaning a little with the slope; stones lie on it, each tipped its own way.
			const FVector Up = FMath::Lerp(Dir, Normal, static_cast<double>(Rule.AlignToGround)).GetSafeNormal();
			const FQuat Rotation = FRotationMatrix::MakeFromZ(Up).ToQuat() * FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw))
				* FQuat(Tilt.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector), FMath::DegreesToRadians(Rule.RandomTiltDeg) * Tilt.Size() * 0.7);
			Instances.Add(FEphemeraTerrain::Seat(Rule.Mesh, Rotation, Position, Scale));
		}
		if (Instances.IsEmpty())
		{
			continue;
		}
		UInstancedStaticMeshComponent* Component = nullptr;
		while (!Pool.IsEmpty() && !Component)
		{
			Component = Pool.Pop().Get();
			if (Component)
			{
				Component->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			}
		}
		if (!Component)
		{
			Component = NewObject<UInstancedStaticMeshComponent>(GetOwner());
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCanEverAffectNavigation(false);
			Component->bAffectDistanceFieldLighting = false;
			Component->SetupAttachment(GetOwner()->GetRootComponent());
			Component->RegisterComponent();
		}
		Component->SetStaticMesh(Rule.Mesh);
		Component->EmptyOverrideMaterials();
		if (Rule.Material)
		{
			for (int32 Slot = 0; Slot < Rule.Mesh->GetStaticMaterials().Num(); ++Slot)
			{
				Component->SetMaterial(Slot, Rule.Material);
			}
		}
		Component->SetCastShadow(Rule.bCastShadow);
		// What grows on the ground is left out of the ray tracing scene: the reflections and bounce light
		// cannot make out a blade, and a ring of hundreds of thousands of instances there was the
		// costliest thing in the frame (Epic's guide: under 100,000 instances, and grass first to go).
		Component->SetVisibleInRayTracing(false);
		// Grass moves in the wind (its material carries each blade); past forty metres it stands
		// still, where the movement is smaller than a pixel and its cost is not.
		Component->SetWorldPositionOffsetDisableDistance(4000);
		Component->AddInstances(Instances, false, false, false);
		Component->SetVisibility(true);
		Cell.Components.Add(Component);
	}
}

void UEphemeraGroundCoverComponent::Regrow()
{
	for (TPair<FIntVector, FCell>& Entry : Cells)
	{
		DropCell(Entry.Value);
	}
	Cells.Reset();
}

void UEphemeraGroundCoverComponent::DropCell(FCell& Cell)
{
	for (UInstancedStaticMeshComponent* Component : Cell.Components)
	{
		if (Component)
		{
			Component->ClearInstances();
			Component->SetVisibility(false);
			// Set aside, it lets go of the planet: an attached component is moved every time the
			// planet is, used or not, and there can be thousands waiting here.
			Component->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
			if (Pool.Num() < MaxPooled)
			{
				Pool.Add(Component);
			}
			else
			{
				Component->DestroyComponent();
			}
		}
	}
	Cell.Components.Reset();
}
