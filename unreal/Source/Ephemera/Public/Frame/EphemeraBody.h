#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EphemeraBody.generated.h"

class UEphemeraBodyDefinition;
class UEphemeraGroundCoverComponent;

/**
 * A star, planet or moon in the level. It owns no logic of its own: the frame subsystem moves it,
 * and its visible parts (terrain, collision, scatter) are assembled from what the planet builder
 * wrote into its definition.
 */
UCLASS()
class EPHEMERA_API AEphemeraBody : public AActor
{
	GENERATED_BODY()

public:
	AEphemeraBody();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ephemera")
	TObjectPtr<UEphemeraBodyDefinition> Definition;

	/** Recreates terrain, collision and scatter components from the definition. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Ephemera")
	void Rebuild();

	/** Points the tidal bulge: the direction to the tide-raising body, in this body's own space. */
	void SetTide(const FVector& LocalDirection);
	/** True once what a player standing here needs is in place: ground to stand on, and everything that grows within sight. */
	bool GroundReady() const;
	/** Whether this world's ground throws shadows (everything else about it stays as built). */
	void SetCastsShadows(bool bCast);
	/** Called when the terrain's relief is switched between geometry and shading (the settings menu). */
	void ReliefChanged();
	/** How far the sea stands above (or below) its datum at a world position right now, in metres. */
	double TideM(const FVector& WorldPosition) const;
	/**
	 * How high the sea's surface stands over its datum at a world position right now, in metres: the
	 * tide, and the waves as the near sea draws them (UEphemeraSeaSurfaceComponent::WaveHeightM), read
	 * along the line from the planet's centre. The tide alone where no waves are drawn (far from the
	 * camera, high above the sea, or with -EphemeraNoSeaGrid). Cheap enough for a few calls a frame.
	 */
	double SeaSurfaceM(const FVector& WorldPosition) const;

	FName BodyId() const;
	double RadiusCm() const;
	double InfluenceRadiusCm() const;
	double ShellRadiusCm() const;
	/** Gravity this body alone exerts at a world position, in cm/s². */
	FVector GravityAt(const FVector& WorldPosition) const;
	/** Height above the datum sphere, in cm (not above the terrain). */
	double AltitudeCm(const FVector& WorldPosition) const;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Ephemera")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Ephemera")
	TObjectPtr<UEphemeraGroundCoverComponent> GroundCover;

	UPROPERTY(VisibleAnywhere, Category = "Ephemera")
	TObjectPtr<UEphemeraGroundCoverComponent> GroundCoverFar;

	/** Solid ground under the player where the definition has no baked collision. */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera")
	TObjectPtr<class UEphemeraGroundPatchComponent> GroundPatch;

	/** The near sea: waves as geometry under the camera (empty on a world without a sea grid). */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera")
	TObjectPtr<class UEphemeraSeaSurfaceComponent> SeaSurface;

	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> OceanInstance;
	/** Direction to the tide-raising body in this body's own space (zero until first set). */
	FVector TideDirection = FVector::ZeroVector;
};
