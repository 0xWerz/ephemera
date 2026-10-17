#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "EphemeraShip.generated.h"
class USphereComponent;
class UCameraComponent;
class USceneComponent;
class AEphemeraWorld;
class ACelestialBody;

UCLASS(Blueprintable)
class EPHEMERA_API AEphemeraShip : public APawn
{
    GENERATED_BODY()
public:
    AEphemeraShip();
    void Simulate(double Delta, FVector Axes, FVector Look, double Roll, bool Boost, bool Match);
    void ResetAt(const FTransform& Surface, ACelestialBody* Body);
    void CarryFrame();
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> HullCollider;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Cockpit;
    UPROPERTY() TObjectPtr<AEphemeraWorld> Universe;
    UPROPERTY() TObjectPtr<ACelestialBody> Frame;
    UPROPERTY(BlueprintReadOnly) double Hull=100;
    UPROPERTY(BlueprintReadOnly) double Fuel=1;
    UPROPERTY(BlueprintReadOnly) bool bPiloted=false;
    UPROPERTY(BlueprintReadOnly) bool bLanded=true;
    FVector Velocity=FVector::ZeroVector;
    FVector Spool=FVector::ZeroVector;
    FTransform Previous=FTransform::Identity;
};
