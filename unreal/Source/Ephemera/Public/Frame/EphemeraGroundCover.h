#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "EphemeraGroundCover.generated.h"

class UInstancedStaticMeshComponent;
class UEphemeraBodyDefinition;

/**
 * Grass, flowers and ferns around the camera. A planet has room for millions of them, far too
 * many to store, so the surface is divided into cells and each cell's plants are grown when the
 * camera comes near and dropped when it leaves. A cell always grows the same plants (its seed is
 * its address), so the meadow you walked through is the same when you come back.
 */
UCLASS()
class EPHEMERA_API UEphemeraGroundCoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEphemeraGroundCoverComponent();

	/** The wide ring of large cells for the rules marked bFar; otherwise the near ring for the rest. */
	bool bFarTier = false;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/** Drops every cell: they grow again from the next tick (after the ground itself has changed). */
	void Regrow();
	/** True once everything within reach of the camera has been grown (or nothing is within reach). */
	bool IsSettled() const { return bSettled; }

private:
	struct FCell
	{
		TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;
		uint32 Seen = 0;
	};

	void GrowCell(const FIntVector& Address, FCell& Cell);
	void DropCell(FCell& Cell);
	/** Cube face (0..5) and cell (i, j) of a planet-local direction. */
	FIntVector AddressOf(const FVector& LocalDirection) const;

	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Pool;
	TMap<FIntVector, FCell> Cells;
	/** Where the camera was last tick (planet-local), to tell arriving somewhere from flying past it. */
	FVector LastLocal = FVector::ZeroVector;
	bool bHaveLast = false;
	bool bSettled = false;
	uint32 Pass = 0;
	int32 CellsPerFace = 48;
	double CellSizeCm = 1600.0;
};
