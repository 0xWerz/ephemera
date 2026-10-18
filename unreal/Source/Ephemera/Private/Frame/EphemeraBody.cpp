#include "Frame/EphemeraBody.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Frame/EphemeraFrameSubsystem.h"
#include "Frame/EphemeraGroundCover.h"
#include "Frame/EphemeraGroundPatch.h"
#include "Frame/EphemeraGroundSurface.h"
#include "Frame/EphemeraSeaSurface.h"
#include "Engine/CollisionProfile.h"
#include "SimulationMath.h"

namespace
{
	const FName GeneratedTag(TEXT("EphemeraGenerated"));

	template <typename T>
	T* AddGenerated(AActor* Owner, USceneComponent* Parent, const FName Name)
	{
		T* Component = NewObject<T>(Owner, MakeUniqueObjectName(Owner, T::StaticClass(), Name));
		Component->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Component->ComponentTags.Add(GeneratedTag);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetupAttachment(Parent);
		Component->RegisterComponent();
		return Component;
	}
}

AEphemeraBody::AEphemeraBody()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
	GroundCover = CreateDefaultSubobject<UEphemeraGroundCoverComponent>(TEXT("GroundCover"));
	GroundCoverFar = CreateDefaultSubobject<UEphemeraGroundCoverComponent>(TEXT("GroundCoverFar"));
	GroundCoverFar->bFarTier = true;
	GroundPatch = CreateDefaultSubobject<UEphemeraGroundPatchComponent>(TEXT("GroundPatch"));
	GroundPatch->SetMobility(EComponentMobility::Movable);
	GroundPatch->SetupAttachment(Root);
	// At the body's own origin and orientation: its material works in the planet's own space.
	SeaSurface = CreateDefaultSubobject<UEphemeraSeaSurfaceComponent>(TEXT("SeaSurface"));
	SeaSurface->SetupAttachment(Root);
	GroundSurface = CreateDefaultSubobject<UEphemeraGroundSurfaceComponent>(TEXT("GroundSurface"));
	GroundSurface->SetupAttachment(Root);
}

void AEphemeraBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void AEphemeraBody::Rebuild()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), GeneratedTag);
	for (UActorComponent* Component : Old)
	{
		Component->DestroyComponent();
	}
	if (!Definition)
	{
		SeaSurface->Configure(nullptr, nullptr);
		GroundSurface->Configure(nullptr, nullptr);
		return;
	}
	if (Definition->TerrainMesh)
	{
		UStaticMeshComponent* Terrain = AddGenerated<UStaticMeshComponent>(this, Root, TEXT("Terrain"));
		Terrain->SetStaticMesh(Definition->TerrainMesh);
		Terrain->SetRelativeScale3D(FVector(Definition->TerrainMeshScale));
		// A star lights the world; it must not shadow it or be lit by its own light.
		Terrain->SetCastShadow(!Definition->bIsStar);
		if (Definition->TerrainMaterial)
		{
			Terrain->SetMaterial(0, Definition->TerrainMaterial);
		}
		// The Nanite terrain is only looked at; walking uses the collision chunks below.
		Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Terrain->bAffectDistanceFieldLighting = !Definition->bIsStar;
	}
	// With a near sea (UEphemeraSeaSurfaceComponent), the sphere is the far sea: the same water with a hole
	// cut in it where the near sea's grid lies, which its material instance is told every frame.
	const bool bSeaGrid = UEphemeraSeaSurfaceComponent::Wanted(Definition);
	if (Definition->OceanMesh && bSeaGrid)
	{
		UStaticMeshComponent* Sea = AddGenerated<UStaticMeshComponent>(this, Root, TEXT("Ocean"));
		Sea->SetStaticMesh(Definition->OceanMesh);
		OceanInstance = UMaterialInstanceDynamic::Create(Definition->OceanFarMaterial, this);
		OceanInstance->SetScalarParameterValue(TEXT("TideAmplitude"), Definition->TideAmplitudeM);
		Sea->SetMaterial(0, OceanInstance);
		Sea->SetBoundsScale(1.02f);
		Sea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sea->SetCastShadow(false);
		Sea->bAffectDistanceFieldLighting = false;
		Sea->SetCanEverAffectNavigation(false);
	}
	else if (Definition->OceanMesh)
	{
		UStaticMeshComponent* Sea = AddGenerated<UStaticMeshComponent>(this, Root, TEXT("Ocean"));
		Sea->SetStaticMesh(Definition->OceanMesh);
		if (Definition->OceanMaterial)
		{
			Sea->SetMaterial(0, Definition->OceanMaterial);
			if (Definition->TideAmplitudeM > 0.f)
			{
				OceanInstance = UMaterialInstanceDynamic::Create(Definition->OceanMaterial, this);
				OceanInstance->SetScalarParameterValue(TEXT("TideAmplitude"), Definition->TideAmplitudeM);
				Sea->SetMaterial(0, OceanInstance);
				// The material lifts the surface by the tide: keep the mesh from being culled at its rest size.
				Sea->SetBoundsScale(1.02f);
			}
		}
		Sea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sea->SetCastShadow(false);
		Sea->bAffectDistanceFieldLighting = false;
		Sea->SetCanEverAffectNavigation(false);
	}
	SeaSurface->Configure(bSeaGrid ? Definition.Get() : nullptr, bSeaGrid ? OceanInstance.Get() : nullptr);
	if (Definition->StreamMesh)
	{
		UStaticMeshComponent* Streams = AddGenerated<UStaticMeshComponent>(this, Root, TEXT("Streams"));
		Streams->SetStaticMesh(Definition->StreamMesh);
		if (Definition->StreamMaterial)
		{
			Streams->SetMaterial(0, Definition->StreamMaterial);
		}
		Streams->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Streams->SetCastShadow(false);
		Streams->bAffectDistanceFieldLighting = false;
		Streams->SetCanEverAffectNavigation(false);
	}
	if (Definition->AuroraMaterial)
	{
		// One sphere bounding the curtains: the material marches through the air inside it (M_Aurora). The
		// engine's sphere is faceted, so it is made a little larger than the curtains' top to hold them whole.
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		if (Sphere)
		{
			const double TopM = Definition->AuroraAltitudeM + Definition->AuroraHeightM;
			const double ContainerM = (Definition->RadiusM + TopM) * 1.04;
			UStaticMeshComponent* Glow = AddGenerated<UStaticMeshComponent>(this, Root, TEXT("Aurora"));
			Glow->SetStaticMesh(Sphere);
			Glow->SetMaterial(0, Definition->AuroraMaterial);
			Glow->SetRelativeScale3D(FVector(ContainerM * 100.0 / 50.0));
			Glow->SetCustomPrimitiveDataFloat(0, float(ContainerM));
			Glow->SetCustomPrimitiveDataFloat(1, float(Definition->RadiusM));
			Glow->SetCustomPrimitiveDataFloat(2, Definition->AuroraAltitudeM);
			Glow->SetCustomPrimitiveDataFloat(3, float(TopM));
			Glow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Glow->SetCastShadow(false);
			Glow->bAffectDistanceFieldLighting = false;
			Glow->bAffectDynamicIndirectLighting = false;
			Glow->SetCanEverAffectNavigation(false);
		}
	}
	// With a near ground (UEphemeraGroundSurfaceComponent), the chunks are the far land: the same ground with a
	// hole cut where the grid lies, which their material instance is told every frame.
	const bool bGroundGrid = UEphemeraGroundSurfaceComponent::Wanted(Definition) && !Definition->TerrainChunks.IsEmpty();
	TerrainInstance = bGroundGrid ? UMaterialInstanceDynamic::Create(Definition->TerrainFarMaterial, this) : nullptr;
	GroundSurface->Configure(bGroundGrid ? Definition.Get() : nullptr, TerrainInstance);
	for (UStaticMesh* Chunk : Definition->TerrainChunks)
	{
		if (!Chunk)
		{
			continue;
		}
		UStaticMeshComponent* Piece = AddGenerated<UStaticMeshComponent>(this, Root, TEXT("TerrainChunk"));
		Piece->SetStaticMesh(Chunk);
		if (TerrainInstance)
		{
			Piece->SetMaterial(0, TerrainInstance);
		}
		else if (Definition->TerrainMaterial)
		{
			Piece->SetMaterial(0, Definition->TerrainMaterial);
		}
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(true);
		Piece->bAffectDistanceFieldLighting = true;
		Piece->SetCanEverAffectNavigation(false);
		// The terrain material's ReliefShade: full where its relief is geometry, gentler where
		// (on the Mac, in play) it is only shading.
		Piece->SetDefaultCustomPrimitiveDataFloat(0, FEphemeraTerrain::ReliefIsGeometry() ? 1.f : 0.55f);
	}
	for (UStaticMesh* Chunk : Definition->CollisionChunks)
	{
		if (!Chunk)
		{
			continue;
		}
		UStaticMeshComponent* Collider = AddGenerated<UStaticMeshComponent>(this, Root, TEXT("Ground"));
		Collider->SetStaticMesh(Chunk);
		Collider->SetVisibility(false);
		Collider->SetHiddenInGame(true);
		Collider->SetCastShadow(false);
		Collider->bAffectDistanceFieldLighting = false;
		Collider->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Collider->SetCanEverAffectNavigation(false);
	}
	for (const FEphemeraScatterBatch& Batch : Definition->Scatter)
	{
		if (!Batch.Mesh || Batch.Instances.IsEmpty())
		{
			continue;
		}
		UInstancedStaticMeshComponent* Instances = AddGenerated<UInstancedStaticMeshComponent>(this, Root, TEXT("Scatter"));
		Instances->SetStaticMesh(Batch.Mesh);
		if (Batch.Material)
		{
			for (int32 Slot = 0; Slot < Batch.Mesh->GetStaticMaterials().Num(); ++Slot)
			{
				Instances->SetMaterial(Slot, Batch.Material);
			}
		}
		Instances->SetCollisionEnabled(Batch.bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (Batch.bCollision)
		{
			Instances->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		}
		if (Batch.CullDistanceM > 0.f)
		{
			Instances->SetCullDistances(0, FMath::RoundToInt32(Batch.CullDistanceM * 100.f));
		}
		Instances->AddInstances(Batch.Instances, false, false, false);
	}
}

void AEphemeraBody::BeginPlay()
{
	Super::BeginPlay();
	// The body's visible parts are put together in OnConstruction, which the editor runs for every
	// actor it loads. A packaged game does not: there the level arrives as it was saved, and the
	// packaged build of 2026-10-13 started with most of the planet's ground missing. So the parts
	// are assembled here as well, whatever came with the level.
	if (Definition)
	{
		Rebuild();
		int32 Meshes = 0;
		for (const UStaticMesh* Chunk : Definition->TerrainChunks)
		{
			Meshes += Chunk ? 1 : 0;
		}
		TInlineComponentArray<UStaticMeshComponent*> Parts(this);
		UE_LOG(LogTemp, Display, TEXT("Ephemera body %s: %d of %d terrain meshes loaded, %d mesh components"), *BodyId().ToString(), Meshes,
			Definition->TerrainChunks.Num(), Parts.Num());
	}
	// Whether the relief is geometry is only known for sure once the game is running (a photograph
	// switches it on from the command line): tell every piece of terrain again.
	ReliefChanged();
	if (UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>())
	{
		Frames->RegisterBody(this);
	}
}

void AEphemeraBody::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UEphemeraFrameSubsystem* Frames = World->GetSubsystem<UEphemeraFrameSubsystem>())
		{
			Frames->UnregisterBody(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool AEphemeraBody::GroundReady() const
{
	const bool bNeedsPatch = Definition && !Definition->TerrainChunks.IsEmpty() && Definition->CollisionChunks.IsEmpty();
	return GroundCover->IsSettled() && GroundCoverFar->IsSettled() && (!bNeedsPatch || GroundPatch->IsBuilt());
}

void AEphemeraBody::SetCastsShadows(const bool bCast)
{
	if (!Definition || Definition->bIsStar)
	{
		return;
	}
	TInlineComponentArray<UStaticMeshComponent*> Pieces(this);
	for (UStaticMeshComponent* Piece : Pieces)
	{
		if (Piece->GetStaticMesh() && (Piece->GetStaticMesh() == Definition->TerrainMesh || Definition->TerrainChunks.Contains(Piece->GetStaticMesh())))
		{
			Piece->SetCastShadow(bCast);
		}
	}
}

void AEphemeraBody::ReliefChanged()
{
	const float ReliefShade = FEphemeraTerrain::ReliefIsGeometry() ? 1.f : 0.55f;
	TInlineComponentArray<UStaticMeshComponent*> Pieces(this);
	for (UStaticMeshComponent* Piece : Pieces)
	{
		if (Definition && Definition->TerrainChunks.Contains(Piece->GetStaticMesh()))
		{
			Piece->SetCustomPrimitiveDataFloat(0, ReliefShade);
		}
	}
	// What grows on the ground stands at the relief's height: grow it again.
	GroundCover->Regrow();
	GroundCoverFar->Regrow();
	GroundPatch->Forget();
}

double AEphemeraBody::TideM(const FVector& WorldPosition) const
{
	if (!Definition || Definition->TideAmplitudeM <= 0.f || TideDirection.IsZero())
	{
		return 0.0;
	}
	// The same bulge the sea's material draws: high under the moon and opposite it, low between.
	const double Toward = FVector::DotProduct(GetActorTransform().InverseTransformPosition(WorldPosition).GetSafeNormal(), TideDirection);
	return Definition->TideAmplitudeM * (1.5 * Toward * Toward - 0.5);
}

void AEphemeraBody::SetTide(const FVector& LocalDirection)
{
	TideDirection = LocalDirection.GetSafeNormal();
	if (OceanInstance)
	{
		OceanInstance->SetVectorParameterValue(TEXT("MoonDir"), FLinearColor(LocalDirection.X, LocalDirection.Y, LocalDirection.Z, 0.f));
	}
	SeaSurface->SetTide(LocalDirection);
	if (Definition && Definition->TideCollection)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(this, Definition->TideCollection, TEXT("MoonTide"),
			FLinearColor(static_cast<float>(TideDirection.X), static_cast<float>(TideDirection.Y), static_cast<float>(TideDirection.Z), Definition->TideAmplitudeM));
	}
}

double AEphemeraBody::SeaSurfaceM(const FVector& WorldPosition) const
{
	const double Tide = TideM(WorldPosition);
	if (!SeaSurface || !SeaSurface->IsDrawn())
	{
		return Tide;
	}
	const FVector Up = GetActorTransform().InverseTransformPosition(WorldPosition).GetSafeNormal();
	return Tide + SeaSurface->WaveHeightM(Up, Tide);
}

FName AEphemeraBody::BodyId() const
{
	return Definition ? Definition->BodyId : NAME_None;
}

double AEphemeraBody::RadiusCm() const
{
	return Definition ? Definition->RadiusM * 100.0 : 0.0;
}

double AEphemeraBody::InfluenceRadiusCm() const
{
	return Definition ? Definition->InfluenceRadiusM * 100.0 : 0.0;
}

double AEphemeraBody::ShellRadiusCm() const
{
	return Definition ? Definition->ShellRadiusM() * 100.0 : 0.0;
}

FVector AEphemeraBody::GravityAt(const FVector& WorldPosition) const
{
	if (!Definition || Definition->SurfaceGravity <= 0.0)
	{
		return FVector::ZeroVector;
	}
	const FVector ToCentreM = (GetActorLocation() - WorldPosition) / 100.0;
	const EphemeraSim::Vec G = EphemeraSim::Gravity({ToCentreM.X, ToCentreM.Y, ToCentreM.Z}, Definition->RadiusM, Definition->SurfaceGravity, Definition->MaxGravity);
	return FVector(G.x, G.y, G.z) * 100.0;
}

double AEphemeraBody::AltitudeCm(const FVector& WorldPosition) const
{
	return FVector::Dist(WorldPosition, GetActorLocation()) - RadiusCm();
}
