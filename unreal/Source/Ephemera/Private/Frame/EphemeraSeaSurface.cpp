#include "Frame/EphemeraSeaSurface.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Frame/EphemeraBody.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Everything below that places or moves the water is done twice: here, for the game, and in the near
// sea's material (SEA_GRID_CODE and SEA_BANDS_CODE in Tools/build_slice.py), for the eye. Change one and
// change the other: the cube faces, the morph, the fades, the chart weights, the shore and the trough.
namespace
{
	// The lattice's cube faces: axis, then the two axes along the face, a right-handed set (U x V = N),
	// so the grid's triangles, wound to face +Z flat, face outward on every face. The planet builder's faces.
	const FVector FaceN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	const FVector FaceU[6] = {{0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	const FVector FaceV[6] = {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
	// The camera stays on its face until another face's axis is this much nearer it (about 4 degrees past
	// the edge): walking along an edge does not swap lattices back and forth.
	constexpr double FaceHysteresis = 1.15;
	// Waves fade out between these fractions of the hole's reach, so by its edge the grid is flat water.
	constexpr double FadeFrom = 0.62;
	constexpr double FadeTo = 0.96;

	/** Where a direction lies on a lattice face: metres of arc along its two axes. */
	FVector2D ArcOn(const int32 Face, const double RadiusM, const FVector& Direction)
	{
		const double Along = FVector::DotProduct(Direction, FaceN[Face]);
		return FVector2D(RadiusM * FMath::Atan2(FVector::DotProduct(Direction, FaceU[Face]), Along),
			RadiusM * FMath::Atan2(FVector::DotProduct(Direction, FaceV[Face]), Along));
	}

	/** One band of waves at a point of its chart (metres) and a moment: height and sideways move, metres. */
	void SumBand(const FEphemeraSeaBand& Band, const double U, const double V, const double TimeS, double& Height, double& AlongU, double& AlongV)
	{
		const TArray<float>& W = Band.Waves;
		double H = 0.0, Du = 0.0, Dv = 0.0;
		for (int32 I = 0; I + 4 < W.Num(); I += 5)
		{
			const double Kx = W[I], Kz = W[I + 1];
			double S, C;
			FMath::SinCos(&S, &C, Kx * U + Kz * V - W[I + 2] * TimeS);
			// A exp(i theta), A = (W[I+3], W[I+4]).
			const double Re = W[I + 3] * C - W[I + 4] * S;
			const double Im = W[I + 3] * S + W[I + 4] * C;
			const double K = FMath::Max(FMath::Sqrt(Kx * Kx + Kz * Kz), 1e-6);
			H += Re;
			// Toward the crest: lambda Re(i k/|k| A exp(i theta)).
			Du -= Kx / K * Im;
			Dv -= Kz / K * Im;
		}
		Height = H;
		AlongU = Du * Band.Choppiness;
		AlongV = Dv * Band.Choppiness;
	}
}

UEphemeraSeaSurfaceComponent::UEphemeraSeaSurfaceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the camera has been placed for this frame (the player controller places it once every
	// actor has ticked, before this group runs).
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	Mobility = EComponentMobility::Movable;
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	CanCharacterStepUpOn = ECB_No;
	SetCanEverAffectNavigation(false);
	// The mesh as built is a flat square at the planet's centre; only the material puts it where it is
	// seen. Anything that reads the mesh without the material would find it there: shadows (the sea casts
	// none anyway), distance fields, Lumen's scene, ray tracing. Keep it out of all of them.
	CastShadow = false;
	bCastDynamicShadow = false;
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bVisibleInRayTracing = false;
	bNeverDistanceCull = true;
	// Shown by the first tick in play, once it knows where the camera is.
	SetVisibleFlag(false);
}

bool UEphemeraSeaSurfaceComponent::SwitchedOff()
{
	return FParse::Param(FCommandLine::Get(), TEXT("EphemeraNoSeaGrid"));
}

bool UEphemeraSeaSurfaceComponent::Wanted(const UEphemeraBodyDefinition* Definition)
{
	return Definition && Definition->OceanMesh && Definition->SeaGridMesh && Definition->SeaGridMaterial && Definition->OceanFarMaterial
		&& Definition->SeaGridRings > 0 && !SwitchedOff();
}

void UEphemeraSeaSurfaceComponent::Configure(const UEphemeraBodyDefinition* Definition, UMaterialInstanceDynamic* InFarSea)
{
	FarSea = InFarSea;
	NearSea = nullptr;
	Face = INDEX_NONE;
	bDrawn = false;
	SetVisibility(false);
	if (!Definition || !Definition->SeaGridMesh || !Definition->SeaGridMaterial)
	{
		RadiusM = 0.0;
		HoleM = 0.0;
		SetStaticMesh(nullptr);
		return;
	}
	// Before the mesh: its bounds are worked out from the radius (CalcBounds).
	RadiusM = Definition->RadiusM;
	// The coarsest ring reaches HalfCells of its cells from its centre along the face's axes, and that
	// centre is snapped within two of them of the camera: a square on the face reaching that far, less
	// those two, is always covered by the grid. The far sea is cut away a little inside that, and the
	// waves are gone a little inside the cut.
	const double Coarsest = Definition->SeaGridSpacingM * FMath::Pow(2.0, static_cast<double>(Definition->SeaGridRings - 1));
	HoleM = 0.96 * (Definition->SeaGridHalfCells - 2) * Coarsest;
	SetStaticMesh(Definition->SeaGridMesh);
	NearSea = UMaterialInstanceDynamic::Create(Definition->SeaGridMaterial, this);
	NearSea->SetScalarParameterValue(TEXT("TideAmplitude"), Definition->TideAmplitudeM);
	for (int32 Band = 0; Band < Definition->SeaBands.Num(); ++Band)
	{
		NearSea->SetScalarParameterValue(*FString::Printf(TEXT("Band%dGain"), Band), Definition->SeaBands[Band].Gain);
	}
	SetMaterial(0, NearSea);
	if (FarSea)
	{
		FarSea->SetVectorParameterValue(TEXT("SeaHole"), FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
	UpdateBounds();
	MarkRenderTransformDirty();
}

void UEphemeraSeaSurfaceComponent::SetTide(const FVector& LocalDirection)
{
	if (NearSea)
	{
		NearSea->SetVectorParameterValue(TEXT("MoonDir"), FLinearColor(static_cast<float>(LocalDirection.X), static_cast<float>(LocalDirection.Y),
			static_cast<float>(LocalDirection.Z), 0.f));
	}
}

void UEphemeraSeaSurfaceComponent::Show(const bool bShow)
{
	if (bShow == bDrawn)
	{
		return;
	}
	bDrawn = bShow;
	SetVisibility(bShow);
	if (!bShow && FarSea)
	{
		// The sphere whole again.
		FarSea->SetVectorParameterValue(TEXT("SeaHole"), FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
}

void UEphemeraSeaSurfaceComponent::Place(const FVector& Direction)
{
	int32 Nearest = 0;
	double NearestAlong = -2.0;
	for (int32 F = 0; F < 6; ++F)
	{
		const double Along = FVector::DotProduct(Direction, FaceN[F]);
		if (Along > NearestAlong)
		{
			Nearest = F;
			NearestAlong = Along;
		}
	}
	if (Face == INDEX_NONE || FVector::DotProduct(Direction, FaceN[Face]) * FaceHysteresis < NearestAlong)
	{
		Face = Nearest;
	}
	// Equi-angular coordinates: arc length along each of the face's two great circles through its axis.
	// The material turns them back with normalize(N + tan(a / R) U + tan(b / R) V).
	Arc = ArcOn(Face, RadiusM, Direction);
}

void UEphemeraSeaSurfaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Body = GetOwner();
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!NearSea || !Body || !Camera || RadiusM <= 0.0)
	{
		Show(false);
		return;
	}
	const FVector Local = Body->GetActorTransform().InverseTransformPosition(Camera->GetCameraLocation());
	const double AltitudeM = Local.Size() / 100.0 - RadiusM;
	// A little lower to come back than to go: no flicker while hovering at the line.
	Show(AltitudeM < (bDrawn ? HideAboveM : HideAboveM - 100.0));
	if (!bDrawn)
	{
		return;
	}
	Place(Local.GetSafeNormal());
	auto Packed = [](const FVector& Axis, const double W)
	{
		return FLinearColor(static_cast<float>(Axis.X), static_cast<float>(Axis.Y), static_cast<float>(Axis.Z), static_cast<float>(W));
	};
	// The far sea's hole is measured on the same lattice face as the grid is laid out on, so it is always
	// inside the grid: both materials are told the same.
	for (UMaterialInstanceDynamic* Sea : {NearSea.Get(), FarSea.Get()})
	{
		if (Sea)
		{
			Sea->SetVectorParameterValue(TEXT("SeaFace"), Packed(FaceN[Face], RadiusM));
			Sea->SetVectorParameterValue(TEXT("SeaU"), Packed(FaceU[Face], Arc.X));
			Sea->SetVectorParameterValue(TEXT("SeaV"), Packed(FaceV[Face], Arc.Y));
			Sea->SetVectorParameterValue(TEXT("SeaHole"), FLinearColor(0.f, 0.f, 0.f, static_cast<float>(HoleM)));
		}
	}
}

FBoxSphereBounds UEphemeraSeaSurfaceComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (RadiusM <= 0.0)
	{
		return Super::CalcBounds(LocalToWorld);
	}
	// The mesh is a flat square at the planet's centre and the material lays it on the sea under the
	// camera, wherever on the sphere that is: the bounds are the whole sea, with room for tide and waves.
	// They always hold the camera when it is near the water, so the grid is never culled, nor occluded.
	const double Reach = (RadiusM + 100.0) * 100.0;
	return FBoxSphereBounds(FBox(FVector(-Reach), FVector(Reach))).TransformBy(LocalToWorld);
}

double UEphemeraSeaSurfaceComponent::ReachM(const FVector& Up) const
{
	if (FVector::DotProduct(Up, FaceN[Face]) <= 0.0)
	{
		return -1.0;
	}
	const FVector2D Here = ArcOn(Face, RadiusM, Up);
	return FMath::Max(FMath::Abs(Here.X - Arc.X), FMath::Abs(Here.Y - Arc.Y));
}

double UEphemeraSeaSurfaceComponent::SpacingAtM(const UEphemeraBodyDefinition& Definition, const double Reach) const
{
	// From the camera, not from each ring's snapped centre (at most two of the ring's cells away): near enough.
	const double Cells = Definition.SeaGridHalfCells;
	const double MorphFrom = 0.75 * Cells, MorphTo = Cells - 4.0;
	for (int32 Ring = 0; Ring < Definition.SeaGridRings; ++Ring)
	{
		const double Spacing = Definition.SeaGridSpacingM * FMath::Pow(2.0, static_cast<double>(Ring));
		if (Reach <= Cells * Spacing)
		{
			const double Morph = FMath::Clamp((Reach / Spacing - MorphFrom) / (MorphTo - MorphFrom), 0.0, 1.0);
			return Spacing * (1.0 + Morph);
		}
	}
	return -1.0;
}

FVector UEphemeraSeaSurfaceComponent::MoveM(const UEphemeraBodyDefinition& Definition, const FVector& Up, const double TideM, const double TimeS) const
{
	const double Reach = ReachM(Up);
	if (Reach < 0.0)
	{
		return FVector::ZeroVector;
	}
	const double Hole = FMath::Max(HoleM, 1.0);
	const double Near = 1.0 - FMath::SmoothStep(FadeFrom * Hole, FadeTo * Hole, Reach);
	const double Spacing = SpacingAtM(Definition, Reach);
	if (Near <= 0.0 || Spacing <= 0.0)
	{
		return FVector::ZeroVector;
	}
	// The material reads the depth from the baked depth cube, which was made from this same height.
	const double DepthM = FMath::Max(TideM - FEphemeraTerrain::HeightM(Definition.Terrain, Definition.RadiusM, Up), 0.0);
	// The three charts' weights: a sum of unrelated seas at weights w is as high as one when the squares
	// of the weights add to one.
	const FVector A(FMath::Pow(FMath::Abs(Up.X), 8.0), FMath::Pow(FMath::Abs(Up.Y), 8.0), FMath::Pow(FMath::Abs(Up.Z), 8.0));
	const FVector W = A / FMath::Max(A.Size(), 1e-12);
	const FVector Point = Up * RadiusM;
	FVector Sum = FVector::ZeroVector;
	for (const FEphemeraSeaBand& Band : Definition.SeaBands)
	{
		// Over shallow water a wave slows and steepens (Green's law, height ~ depth^-1/4, felt from half
		// a wavelength down), then breaks and is gone; and the grid draws a band only where it is fine
		// enough for it.
		const double Shoal = FMath::Clamp(FMath::Pow(Band.PeakM / (2.0 * FMath::Max(DepthM, 0.01)), 0.25), 1.0, 1.25);
		const double Shore = Shoal * FMath::SmoothStep(0.05, static_cast<double>(Band.BreakDepthM), DepthM);
		const double Fine = 1.0 - FMath::SmoothStep(static_cast<double>(Band.MaxSpacingM), 2.0 * Band.MaxSpacingM, Spacing);
		const double Gain = Band.Gain * Shore * Fine * Near;
		if (Gain <= 0.0)
		{
			continue;
		}
		for (int32 C = 0; C < FMath::Min(3, Definition.SeaCharts.Num()); ++C)
		{
			if (W[C] <= 0.02)
			{
				continue;
			}
			const FEphemeraSeaChart& Chart = Definition.SeaCharts[C];
			double Height, AlongU, AlongV;
			SumBand(Band, FVector::DotProduct(Point, Chart.AxisU) + Chart.Offset.X * Band.PatchM,
				FVector::DotProduct(Point, Chart.AxisV) + Chart.Offset.Y * Band.PatchM, TimeS, Height, AlongU, AlongV);
			const FVector TU = Chart.AxisU - Up * FVector::DotProduct(Chart.AxisU, Up);
			const FVector TV = Chart.AxisV - Up * FVector::DotProduct(Chart.AxisV, Up);
			Sum += (Up * Height + TU * AlongU + TV * AlongV) * (W[C] * Gain);
		}
	}
	// A trough never digs into the bed.
	const double Lift = FVector::DotProduct(Sum, Up);
	Sum += Up * FMath::Max(-0.9 * DepthM - Lift, 0.0);
	return Sum;
}

double UEphemeraSeaSurfaceComponent::WaveHeightM(const FVector& Up, const double TideM) const
{
	const AEphemeraBody* Body = Cast<AEphemeraBody>(GetOwner());
	const UEphemeraBodyDefinition* Definition = Body ? Body->Definition.Get() : nullptr;
	const UWorld* World = GetWorld();
	if (!bDrawn || !Definition || !World || Face == INDEX_NONE)
	{
		return 0.0;
	}
	// The material's clock (View.GameTime) is the world's.
	const double TimeS = World->GetTimeSeconds();
	// The water over a point came from somewhere else: the waves move it sideways as well as up. Find
	// where it rests (x0 = x - D(x0), a few steps; D changes slowly against its own size, so they settle
	// fast) and take the height from there.
	FVector Rest = Up;
	FVector Move = MoveM(*Definition, Rest, TideM, TimeS);
	for (int32 Step = 0; Step < 2; ++Step)
	{
		const FVector Across = Move - Rest * FVector::DotProduct(Move, Rest);
		Rest = (Up * RadiusM - Across).GetSafeNormal();
		Move = MoveM(*Definition, Rest, TideM, TimeS);
	}
	return FVector::DotProduct(Move, Rest);
}
