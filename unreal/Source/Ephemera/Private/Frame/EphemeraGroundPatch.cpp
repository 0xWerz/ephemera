#include "Frame/EphemeraGroundPatch.h"

#include "Engine/CollisionProfile.h"
#include "Frame/EphemeraBody.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UEphemeraGroundPatchComponent::UEphemeraGroundPatchComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	// Before the player moves, so the ground is there when their step is resolved.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bUseComplexAsSimpleCollision = true;
	bUseAsyncCooking = false;        // a sheet this small cooks in a few milliseconds, and must exist at once
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	SetVisibility(false);
	SetHiddenInGame(true);
	SetCastShadow(false);
	bAffectDistanceFieldLighting = false;
	SetCanEverAffectNavigation(false);
}

void UEphemeraGroundPatchComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AEphemeraBody* Body = Cast<AEphemeraBody>(GetOwner());
	const UEphemeraBodyDefinition* D = Body ? Body->Definition.Get() : nullptr;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!D || !Player || D->TerrainChunks.IsEmpty() || !D->CollisionChunks.IsEmpty())
	{
		return;
	}
	const FVector Local = Body->GetActorTransform().InverseTransformPosition(Player->GetActorLocation());
	const FVector Direction = Local.GetSafeNormal();
	const double GroundCm = FEphemeraTerrain::SurfacePointCm(*D, Direction).Size();
	// Only someone near the ground needs it: within a few hundred metres above, or below it (fallen through).
	if (Local.Size() - GroundCm > 30000.0)
	{
		if (!Centre.IsZero())
		{
			ClearAllMeshSections();
			Centre = FVector::ZeroVector;
		}
		return;
	}
	// Rebuilt when the player has covered a fifth of the sheet from its middle: the edge is never near.
	const double MovedM = Centre.IsZero() ? TNumericLimits<double>::Max() : FMath::Acos(FMath::Clamp(FVector::DotProduct(Centre, Direction), -1.0, 1.0)) * D->RadiusM;
	if (MovedM > SizeM * 0.2)
	{
		BuildAround(*D, Direction);
	}
}

void UEphemeraGroundPatchComponent::BuildAround(const UEphemeraBodyDefinition& Definition, const FVector& LocalDirection)
{
	const int32 Row = FMath::Clamp(FMath::RoundToInt32(SizeM / SpacingM), 8, 160) + 1;
	FVector TangentA, TangentB;
	LocalDirection.FindBestAxisVectors(TangentA, TangentB);
	TArray<FVector> Vertices;
	Vertices.SetNumUninitialized(Row * Row);
	for (int32 J = 0; J < Row; ++J)
	{
		for (int32 I = 0; I < Row; ++I)
		{
			const double U = (static_cast<double>(I) / (Row - 1) - 0.5) * SizeM / Definition.RadiusM;
			const double V = (static_cast<double>(J) / (Row - 1) - 0.5) * SizeM / Definition.RadiusM;
			Vertices[J * Row + I] = FEphemeraTerrain::SurfacePointCm(Definition, (LocalDirection + TangentA * U + TangentB * V).GetSafeNormal());
		}
	}
	TArray<int32> Triangles;
	Triangles.Reserve((Row - 1) * (Row - 1) * 6);
	// Wound to face outward whichever way the two tangents happen to turn.
	const bool bFlip = FVector::DotProduct(FVector::CrossProduct(TangentA, TangentB), LocalDirection) > 0.0;
	for (int32 J = 0; J < Row - 1; ++J)
	{
		for (int32 I = 0; I < Row - 1; ++I)
		{
			const int32 A = J * Row + I, B = A + 1, C = A + Row + 1, E = A + Row;
			if (bFlip)
			{
				Triangles.Append({A, C, B, A, E, C});
			}
			else
			{
				Triangles.Append({A, B, C, A, C, E});
			}
		}
	}
	CreateMeshSection(0, Vertices, Triangles, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	Centre = LocalDirection;
}
