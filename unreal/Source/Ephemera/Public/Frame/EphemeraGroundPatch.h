#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "EphemeraGroundPatch.generated.h"

/**
 * Solid ground under the player on a world too large to give collision everywhere. A planet's
 * terrain is drawn from hundreds of render-only meshes; this is one small invisible sheet of the
 * same ground (FEphemeraTerrain::SurfacePointCm, so with the micro relief the material adds),
 * rebuilt around the player whenever they near its edge. Used by bodies whose definition has
 * terrain but no baked collision chunks.
 */
UCLASS()
class EPHEMERA_API UEphemeraGroundPatchComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UEphemeraGroundPatchComponent(const FObjectInitializer& ObjectInitializer);

	/** Side of the sheet, in metres, and the spacing of its points. */
	UPROPERTY(EditAnywhere, Category = "Ephemera") float SizeM = 96.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera") float SpacingM = 1.5f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/** Has the sheet rebuilt at the next tick (after the ground itself has changed). */
	void Forget() { Centre = FVector::ZeroVector; }
	/** True while there is solid ground under the player. */
	bool IsBuilt() const { return !Centre.IsZero(); }

private:
	void BuildAround(const class UEphemeraBodyDefinition& Definition, const FVector& LocalDirection);

	/** Planet-local direction the sheet is centred on; zero while there is none. */
	FVector Centre = FVector::ZeroVector;
};
