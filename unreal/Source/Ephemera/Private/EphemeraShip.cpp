#include "EphemeraShip.h"
#include "EphemeraWorld.h"
#include "CelestialBody.h"
#include "Components/SphereComponent.h"
#include "Camera/CameraComponent.h"
#include "Math/RotationMatrix.h"

AEphemeraShip::AEphemeraShip()
{
    HullCollider=CreateDefaultSubobject<USphereComponent>(TEXT("Hull"));
    HullCollider->InitSphereRadius(110);
    HullCollider->SetCollisionProfileName(TEXT("Pawn"));
    RootComponent=HullCollider;
    VisualRoot=CreateDefaultSubobject<USceneComponent>(TEXT("Seedling"));
    VisualRoot->SetupAttachment(RootComponent);
    VisualRoot->SetRelativeLocation(FVector(0,0,-170));
    Cockpit=CreateDefaultSubobject<UCameraComponent>(TEXT("Cockpit"));
    Cockpit->SetupAttachment(RootComponent);
    Cockpit->SetRelativeLocation(FVector(100,0,170));
    Cockpit->FieldOfView=80;
}
void AEphemeraShip::ResetAt(const FTransform& Surface,ACelestialBody* Body)
{
    FTransform Center=Surface;
    Center.AddToTranslation(Surface.GetUnitAxis(EAxis::Z)*170);
    SetActorTransform(Center,false,nullptr,ETeleportType::TeleportPhysics);
    Previous=Center; Frame=Body; Velocity=Spool=FVector::ZeroVector;
    Hull=100; Fuel=1; bPiloted=false; bLanded=true;
}
void AEphemeraShip::CarryFrame()
{
    Previous=GetActorTransform();
    if (!Frame) return;
    const FTransform Before=Frame->Previous, After=Frame->GetActorTransform();
    const FQuat Rotation=After.GetRotation()*Before.GetRotation().Inverse();
    SetActorLocationAndRotation(After.TransformPosition(Before.InverseTransformPosition(GetActorLocation())),
        Rotation*GetActorQuat(),false,nullptr,ETeleportType::TeleportPhysics);
    Velocity=Rotation.RotateVector(Velocity);
}
void AEphemeraShip::Simulate(double Delta,FVector Axes,FVector Look,double Roll,bool Boost,bool Match)
{
    CarryFrame();
    if (!Universe) return;
    ACelestialBody* Next=Universe->DominantBody(GetActorLocation());
    if (Next!=Frame)
    {
        if (Frame) Velocity+=Frame->PointVelocity(GetActorLocation());
        if (Next) Velocity-=Next->PointVelocity(GetActorLocation());
        Frame=Next;
    }
    if (bPiloted)
    {
        if (!Axes.IsNearlyZero()) bLanded=false;
        const FQuat Turn=FQuat(FVector::UpVector,Look.X*Delta*1.5)*
            FQuat(FVector::RightVector,Look.Y*Delta*1.5)*FQuat(FVector::ForwardVector,Roll*Delta*1.9);
        AddActorLocalRotation(Turn,false,nullptr,ETeleportType::TeleportPhysics);
    }
    if (bLanded) return;
    Spool=FMath::VInterpTo(Spool,Axes,Delta,8);
    const double Authority=(Boost?2.2:1.0)*(Fuel>0?1.0:0.15);
    const FVector Thrust=FVector(Spool.X*(Spool.X>=0?2000:1200),Spool.Y*1100,Spool.Z*(Spool.Z>=0?2200:1100))*Authority;
    Velocity+=(GetActorQuat().RotateVector(Thrust)+Universe->GravityAt(GetActorLocation()))*Delta;
    Fuel=FMath::Max(0.0,Fuel-Spool.Length()*Delta/150.0);
    if (Match&&Frame) Velocity=FMath::VInterpConstantTo(Velocity,FVector::ZeroVector,Delta,1200);
    FHitResult Hit;
    SetActorLocation(GetActorLocation()+Velocity*Delta,true,&Hit);
    if (Hit.bBlockingHit)
    {
        const double Impact=FMath::Max(0.0,-FVector::DotProduct(Velocity,Hit.Normal));
        Hull-=FMath::Max(0.0,Impact/100.0-8.0)*5;
        Velocity=FVector::VectorPlaneProject(Velocity,Hit.Normal)*0.55;
        if (Velocity.Length()<160&&Axes.IsNearlyZero())
        {
            bLanded=true; Velocity=FVector::ZeroVector;
            SetActorRotation(FRotationMatrix::MakeFromXZ(GetActorForwardVector(),Hit.Normal).ToQuat());
        }
        if (Hull<=0&&bPiloted) Universe->Die(TEXT("The Seedling hit too hard."));
    }
}
