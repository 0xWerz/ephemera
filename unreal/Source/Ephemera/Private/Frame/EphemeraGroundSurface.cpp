#include "Frame/EphemeraGroundSurface.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"

// The grid is laid out exactly as the near sea's (EphemeraSeaSurface.cpp); NEAR_GROUND_CODE in
// Tools/build_slice.py does on the GPU what this tells it to.
namespace
{
	const FVector FaceN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	const FVector FaceU[6] = {{0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	const FVector FaceV[6] = {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
	constexpr double FaceHysteresis = 1.15;

	// How much of its relief the near ground raises (1 all of it, 0 the plain land): for comparing and measuring.
	// Whether worlds get a near ground at all (read when a world is built: at load, or on a new map).
	TAutoConsoleVariable<int32> CVarNearGround(TEXT("ephemera.NearGround"), 1,
		TEXT("1: the land near the camera is drawn as fine geometry (UEphemeraGroundSurfaceComponent); 0: the terrain chunks alone."));
	TAutoConsoleVariable<float> CVarNearRelief(TEXT("ephemera.NearGround.Relief"), 1.f,
		TEXT("How much of the land's relief the near ground raises as geometry (0..1)."));
}

UEphemeraGroundSurfaceComponent::UEphemeraGroundSurfaceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	Mobility = EComponentMobility::Movable;
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	CanCharacterStepUpOn = ECB_No;
	SetCanEverAffectNavigation(false);
	// The ground casts its own shadows (the chunks under it are cut away there, shadows and all). The mesh as
	// built is a flat square at the planet's centre, so it stays out of everything that reads the mesh without
	// the material: distance fields and Lumen's scene see the chunks instead, which are the same land.
	CastShadow = true;
	bCastDynamicShadow = true;
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bVisibleInRayTracing = false;
	bNeverDistanceCull = true;
	SetVisibleFlag(false);
}

bool UEphemeraGroundSurfaceComponent::SwitchedOff()
{
	return FParse::Param(FCommandLine::Get(), TEXT("EphemeraNoGroundGrid")) || CVarNearGround.GetValueOnAnyThread() == 0;
}

bool UEphemeraGroundSurfaceComponent::Wanted(const UEphemeraBodyDefinition* Definition)
{
	return Definition && Definition->GroundGridMesh && Definition->GroundGridMaterial && Definition->TerrainFarMaterial
		&& Definition->GroundGridRings > 0 && !SwitchedOff();
}

void UEphemeraGroundSurfaceComponent::Configure(const UEphemeraBodyDefinition* Definition, UMaterialInstanceDynamic* InFarTerrain)
{
	FarTerrain = InFarTerrain;
	NearGround = nullptr;
	Face = INDEX_NONE;
	Show(false);
	if (!Definition || !Definition->GroundGridMesh || !Definition->GroundGridMaterial)
	{
		RadiusM = 0.0;
		HoleM = 0.0;
		SetStaticMesh(nullptr);
		return;
	}
	RadiusM = Definition->RadiusM;
	// As the near sea's: a square on the lattice face a little inside the coarsest ring, however it is snapped.
	const double Coarsest = Definition->GroundGridSpacingM * FMath::Pow(2.0, static_cast<double>(Definition->GroundGridRings - 1));
	HoleM = 0.96 * (Definition->GroundGridHalfCells - 2) * Coarsest;
	SetStaticMesh(Definition->GroundGridMesh);
	NearGround = UMaterialInstanceDynamic::Create(Definition->GroundGridMaterial, this);
	SetMaterial(0, NearGround);
	// Past the distance its relief stands up, the grid shades it as the chunks do (their ReliefShade).
	SetDefaultCustomPrimitiveDataFloat(0, FEphemeraTerrain::ReliefIsGeometry() ? 1.f : 0.55f);
	if (FarTerrain)
	{
		FarTerrain->SetVectorParameterValue(TEXT("GroundHole"), FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
	UpdateBounds();
	MarkRenderTransformDirty();
}

void UEphemeraGroundSurfaceComponent::Show(const bool bShow)
{
	if (bShow == bDrawn)
	{
		return;
	}
	bDrawn = bShow;
	SetVisibility(bShow);
	if (!bShow && FarTerrain)
	{
		FarTerrain->SetVectorParameterValue(TEXT("GroundHole"), FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
}

void UEphemeraGroundSurfaceComponent::Place(const FVector& Direction)
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
	const double Along = FVector::DotProduct(Direction, FaceN[Face]);
	Arc = FVector2D(RadiusM * FMath::Atan2(FVector::DotProduct(Direction, FaceU[Face]), Along),
		RadiusM * FMath::Atan2(FVector::DotProduct(Direction, FaceV[Face]), Along));
}

void UEphemeraGroundSurfaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Body = GetOwner();
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!NearGround || !Body || !Camera || RadiusM <= 0.0)
	{
		Show(false);
		return;
	}
	const FVector Local = Body->GetActorTransform().InverseTransformPosition(Camera->GetCameraLocation());
	const double AltitudeM = Local.Size() / 100.0 - RadiusM;
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
	for (UMaterialInstanceDynamic* Ground : {NearGround.Get(), FarTerrain.Get()})
	{
		if (Ground)
		{
			Ground->SetVectorParameterValue(TEXT("GroundFace"), Packed(FaceN[Face], RadiusM));
			Ground->SetVectorParameterValue(TEXT("GroundU"), Packed(FaceU[Face], Arc.X));
			Ground->SetVectorParameterValue(TEXT("GroundV"), Packed(FaceV[Face], Arc.Y));
			Ground->SetVectorParameterValue(TEXT("GroundHole"), FLinearColor(0.f, 0.f, 0.f, static_cast<float>(HoleM)));
		}
	}
	NearGround->SetScalarParameterValue(TEXT("NearRelief"), CVarNearRelief.GetValueOnGameThread());
}

FBoxSphereBounds UEphemeraGroundSurfaceComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (RadiusM <= 0.0)
	{
		return Super::CalcBounds(LocalToWorld);
	}
	// The whole land, with room for its mountains: the grid is laid wherever the camera is.
	const double Reach = (RadiusM + 2000.0) * 100.0;
	return FBoxSphereBounds(FBox(FVector(-Reach), FVector(Reach))).TransformBy(LocalToWorld);
}
