#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "EphemeraCharacter.generated.h"

class UCameraComponent;
class USpotLightComponent;
class UInputMappingContext;
class UInputAction;
class AEphemeraWorld;
class ACelestialBody;
class AEphemeraShip;

UCLASS()
class EPHEMERA_API UPlanetMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    // Planet and ship frame transport is evaluated once, before CharacterMovement.
    virtual void UpdateBasedMovement(float DeltaSeconds) override {}
};

UCLASS(Blueprintable)
class EPHEMERA_API AEphemeraCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AEphemeraCharacter(const FObjectInitializer& Initializer);
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void Landed(const FHitResult& Hit) override;
    void ResetAt(const FTransform& Surface, ACelestialBody* Body);
    void CarryFrame();
    void SetShip(AEphemeraShip* InShip);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USpotLightComponent> Flashlight;
    UPROPERTY(BlueprintReadOnly) double Oxygen=1;
    UPROPERTY(BlueprintReadOnly) double Fuel=1;
    UPROPERTY(BlueprintReadOnly) double Health=100;
    UPROPERTY() TObjectPtr<AEphemeraWorld> Universe;
    UPROPERTY() TObjectPtr<ACelestialBody> Frame;
    UPROPERTY() TObjectPtr<AEphemeraShip> Ship;
    FVector MoveAxes=FVector::ZeroVector;
    FVector LookAxes=FVector::ZeroVector;
    bool bBoost=false,bMatching=false,bControls=true;
    double Pitch=0,RollInput=0;
    FTransform LastFramePose=FTransform::Identity;
private:
    UPROPERTY() TObjectPtr<UInputMappingContext> Context;
    UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions;
    void Forward(const FInputActionValue& V);
    void Right(const FInputActionValue& V);
    void Vertical(const FInputActionValue& V);
    void LookX(const FInputActionValue& V);
    void LookY(const FInputActionValue& V);
    void Roll(const FInputActionValue& V);
    void Boost(const FInputActionValue& V);
    void Match(const FInputActionValue& V);
    void Interact(const FInputActionValue& V);
    void Lamp(const FInputActionValue& V);
    void Log(const FInputActionValue& V);
    void Map(const FInputActionValue& V);
    void Pause(const FInputActionValue& V);
    void Scout(const FInputActionValue& V);
    void Antennae(const FInputActionValue& V);
    void StickLookX(const FInputActionValue& V);
    void StickLookY(const FInputActionValue& V);
};
