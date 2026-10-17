#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EphemeraData.h"
#include "SimulationMath.h"
#include "CelestialBody.generated.h"

UCLASS(Blueprintable)
class EPHEMERA_API ACelestialBody : public AActor
{
    GENERATED_BODY()
public:
    ACelestialBody();
    void Configure(const EphemeraData::Object& InData);
    void Advance(double Time,double Delta);
    FTransform At(double Time) const;
    FVector GravityAt(FVector Point) const;
    FVector PointVelocity(FVector Point) const;
    double DilationAt(FVector Point) const;
    bool Breathable(FVector Point) const;
    bool InWater(FVector Point) const;
    double Altitude(FVector Point) const;
    UPROPERTY(BlueprintReadOnly) FString BodyId;
    UPROPERTY(BlueprintReadOnly) FString DisplayName;
    UPROPERTY(BlueprintReadOnly) double Radius=5000;
    UPROPERTY(BlueprintReadOnly) double SphereOfInfluence=20000;
    UPROPERTY(BlueprintReadOnly) bool bStar=false;
    UPROPERTY() TObjectPtr<ACelestialBody> OrbitParent;
    UPROPERTY() TObjectPtr<ACelestialBody> VisualParent;
    FTransform Previous=FTransform::Identity;
    FVector LinearVelocity=FVector::ZeroVector;
    FVector AngularVelocity=FVector::ZeroVector;
    double AtmosphereHeight=0,WaterRadius=0;
    EphemeraData::Object Data;
private:
    EphemeraSim::Orbit Orbit;
    std::vector<EphemeraSim::DilationPoint> Depths;
    double SurfaceGravity=1000,MaxGravity=4000,SpinPeriod=0,SpinPhase=0,AxialTilt=0;
    bool bOxygen=false;
};
