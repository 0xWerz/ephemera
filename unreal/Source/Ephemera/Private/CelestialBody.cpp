#include "CelestialBody.h"
#include "Components/SceneComponent.h"
using namespace EphemeraData;
ACelestialBody::ACelestialBody()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("PlanetFrame"));
    RootComponent->SetMobility(EComponentMobility::Movable);
}
void ACelestialBody::Configure(const Object& InData)
{
    Data=InData;
    BodyId=String(Data,TEXT("body_id")); DisplayName=String(Data,TEXT("display_name"));
    Radius=Number(Data,TEXT("radius"))*100; SphereOfInfluence=Number(Data,TEXT("soi_radius"))*100;
    SurfaceGravity=Number(Data,TEXT("surface_gravity"))*100; MaxGravity=Number(Data,TEXT("max_gravity"),40)*100;
    AtmosphereHeight=Number(Data,TEXT("atmosphere_height"))*100; WaterRadius=Number(Data,TEXT("water_radius"))*100;
    bStar=Boolean(Data,TEXT("is_star")); bOxygen=Boolean(Data,TEXT("has_oxygen"));
    SpinPeriod=Number(Data,TEXT("spin_period")); SpinPhase=Number(Data,TEXT("spin_phase"));
    AxialTilt=Number(Data,TEXT("axial_tilt"));
    Orbit={Number(Data,TEXT("orbit_radius")),Number(Data,TEXT("orbit_period"),600),
        Number(Data,TEXT("orbit_phase")),Number(Data,TEXT("orbit_ecc")),Number(Data,TEXT("orbit_tilt")),
        Number(Data,TEXT("orbit_peri_angle")),Number(Data,TEXT("orbit_peri_time"))};
    for (const auto& V:Array(Data,TEXT("dilation_depths")))
    {
        const auto& A=V->AsArray();
        if (A.Num()>=2) Depths.push_back({A[0]->AsNumber()*100,A[1]->AsNumber()});
    }
}
FTransform ACelestialBody::At(double Time) const
{
    const auto& Samples=Array(Data,TEXT("samples"));
    if (Samples.Num()>1)
    {
        const double T=FMath::Clamp(Time,0.0,double(Samples.Num()-1));
        const int32 I=FMath::FloorToInt(T);
        FTransform Out;
        Out.Blend(Transform(Samples[I]->AsObject()),Transform(Samples[FMath::Min(I+1,Samples.Num()-1)]->AsObject()),T-I);
        return Out;
    }
    const auto P=Orbit.Position(Time);
    FVector Location=Axis(FVector(P.x,P.y,P.z))*100;
    if (OrbitParent) Location+=OrbitParent->At(Time).GetLocation();
    const double Spin=SpinPhase+(SpinPeriod!=0?EphemeraSim::Tau*Time/SpinPeriod:0);
    // The axis map is a reflection, so right-handed rotations reverse sign.
    const FQuat Rotation=FQuat(FVector::RightVector,-AxialTilt)*FQuat(FVector::UpVector,-Spin);
    return FTransform(Rotation,Location);
}
void ACelestialBody::Advance(double Time,double Delta)
{
    Previous=GetActorTransform();
    const auto Next=At(Time);
    LinearVelocity=Delta>0?(Next.GetLocation()-Previous.GetLocation())/Delta:FVector::ZeroVector;
    FQuat Difference=Next.GetRotation()*Previous.GetRotation().Inverse();
    if (Difference.W<0) Difference=Difference*-1.0;
    FVector AxisVector;
    double Angle=0;
    Difference.ToAxisAndAngle(AxisVector,Angle);
    AngularVelocity=Delta>0?AxisVector*(Angle/Delta):FVector::ZeroVector;
    SetActorTransform(Next,false,nullptr,ETeleportType::TeleportPhysics);
}
FVector ACelestialBody::PointVelocity(FVector Point) const
{
    return LinearVelocity+FVector::CrossProduct(AngularVelocity,Point-GetActorLocation());
}
double ACelestialBody::Altitude(FVector Point) const { return FVector::Distance(Point,GetActorLocation())-Radius; }
FVector ACelestialBody::GravityAt(FVector Point) const
{
    const FVector To=GetActorLocation()-Point;
    const double R=To.Length();
    if (BodyId==TEXT("kernel")&&R<4640)
    {
        const double T=FMath::Clamp((R-540.0)/(3600.0-540.0),0.0,1.0);
        return -To.GetSafeNormal()*550.0*T*T*(3-2*T);
    }
    if (VisualParent) return VisualParent->GravityAt(Point);
    const auto G=EphemeraSim::Gravity({To.X,To.Y,To.Z},Radius,SurfaceGravity,MaxGravity);
    return FVector(G.x,G.y,G.z);
}
double ACelestialBody::DilationAt(FVector Point) const
{
    return EphemeraSim::Dilation(FVector::Distance(Point,GetActorLocation()),Depths);
}
bool ACelestialBody::Breathable(FVector Point) const
{
    if (VisualParent) return VisualParent->Breathable(Point);
    if (BodyId==TEXT("kernel")) return FVector::Distance(Point,GetActorLocation())<Radius;
    return bOxygen&&Altitude(Point)<AtmosphereHeight;
}
bool ACelestialBody::InWater(FVector Point) const
{
    if (WaterRadius<=0||FVector::Distance(Point,GetActorLocation())>=WaterRadius) return false;
    const FVector Local=GetActorTransform().InverseTransformPosition(Point);
    for (const auto& Z:Array(Data,TEXT("dry_zones")))
    {
        const auto& A=Z->AsArray();
        if (A.Num()>=2&&FVector::Distance(Local,Axis(Raw(A[0]->AsArray()))*100)<A[1]->AsNumber()*100) return false;
    }
    return true;
}
