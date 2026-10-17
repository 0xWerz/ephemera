#include "EphemeraCharacter.h"
#include "EphemeraWorld.h"
#include "EphemeraShip.h"
#include "EphemeraGameInstance.h"
#include "CelestialBody.h"
#include "EphemeraHUD.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AEphemeraCharacter::AEphemeraCharacter(const FObjectInitializer& Initializer)
    :Super(Initializer.SetDefaultSubobjectClass<UPlanetMovementComponent>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick=true;
    GetCapsuleComponent()->InitCapsuleSize(28,70);
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Eyes"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0,0,48));
    Camera->FieldOfView=80;
    Flashlight=CreateDefaultSubobject<USpotLightComponent>(TEXT("Lamp"));
    Flashlight->SetupAttachment(Camera);
    Flashlight->SetIntensity(1800); Flashlight->SetAttenuationRadius(5500);
    Flashlight->SetInnerConeAngle(18); Flashlight->SetOuterConeAngle(32);
    Flashlight->SetVisibility(false);
    bUseControllerRotationPitch=bUseControllerRotationYaw=bUseControllerRotationRoll=false;
    auto* M=GetCharacterMovement();
    M->MaxWalkSpeed=600; M->JumpZVelocity=530; M->AirControl=0.65;
    M->bOrientRotationToMovement=false; M->bIgnoreBaseRotation=true;
    M->bImpartBaseVelocityX=M->bImpartBaseVelocityY=M->bImpartBaseVelocityZ=false;
    M->bImpartBaseAngularVelocity=false;
}
void AEphemeraCharacter::BeginPlay()
{
    Super::BeginPlay();
    TActorIterator<AEphemeraWorld> It(GetWorld());
    if (It) Universe=*It;
    if (Universe)
    {
        Universe->Player=this;
        AddTickPrerequisiteActor(Universe);
        GetCharacterMovement()->AddTickPrerequisiteActor(Universe);
        Universe->PlacePlayer();
    }
}
void AEphemeraCharacter::ResetAt(const FTransform& Surface,ACelestialBody* Body)
{
    SetShip(nullptr);
    FTransform Pose=Surface;
    Pose.AddToTranslation(Surface.GetUnitAxis(EAxis::Z)*75);
    SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics);
    Frame=Body; LastFramePose=Body?Body->GetActorTransform():FTransform::Identity;
    Oxygen=Fuel=1; Health=100; Pitch=0; bControls=true;
    MoveAxes=LookAxes=FVector::ZeroVector;
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Falling);
}
void AEphemeraCharacter::CarryFrame()
{
    if (!Frame||Ship) return;
    const auto Before=LastFramePose, After=Frame->GetActorTransform();
    const FQuat Rotation=After.GetRotation()*Before.GetRotation().Inverse();
    SetActorLocationAndRotation(After.TransformPosition(Before.InverseTransformPosition(GetActorLocation())),
        Rotation*GetActorQuat(),false,nullptr,ETeleportType::TeleportPhysics);
    GetCharacterMovement()->Velocity=Rotation.RotateVector(GetCharacterMovement()->Velocity);
    LastFramePose=After;
}
void AEphemeraCharacter::SetShip(AEphemeraShip* InShip)
{
    if (Ship) Ship->bPiloted=false;
    Ship=InShip;
    GetCapsuleComponent()->SetCollisionEnabled(Ship?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(Ship?MOVE_None:MOVE_Falling);
    if (Ship) Ship->bPiloted=true;
}
void AEphemeraCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Universe||Universe->bFrozen) { LookAxes=FVector::ZeroVector; return; }
    if (!bControls)
    {
        if(Ship)SetActorLocationAndRotation(Ship->Cockpit->GetComponentLocation(),Ship->GetActorQuat(),false,nullptr,ETeleportType::TeleportPhysics);
        else CarryFrame();
        LookAxes=FVector::ZeroVector;return;
    }
    if (Ship)
    {
        SetActorLocationAndRotation(Ship->Cockpit->GetComponentLocation(),Ship->GetActorQuat(),false,nullptr,ETeleportType::TeleportPhysics);
        Camera->SetRelativeLocation(FVector::ZeroVector);
        Camera->SetRelativeRotation(FRotator::ZeroRotator);
        LookAxes=FVector::ZeroVector;
        return;
    }
    Camera->SetRelativeLocation(FVector(0,0,48));
    CarryFrame();
    ACelestialBody* Next=Universe->DominantBody(GetActorLocation());
    auto* M=GetCharacterMovement();
    if (Frame!=Next)
    {
        if (Frame) M->Velocity+=Frame->PointVelocity(GetActorLocation());
        if (Next) M->Velocity-=Next->PointVelocity(GetActorLocation());
        Frame=Next;
        LastFramePose=Next?Next->GetActorTransform():FTransform::Identity;
    }
    const FVector Gravity=Universe->GravityAt(GetActorLocation());
    if (Gravity.Length()>1)
    {
        M->SetGravityDirection(Gravity.GetSafeNormal());
        M->GravityScale=Gravity.Length()/980.0;
        SetActorRotation(FQuat::FindBetweenNormals(GetActorUpVector(),-Gravity.GetSafeNormal())*GetActorQuat());
    }
    else M->GravityScale=0;
    AddActorLocalRotation(FQuat(FVector::UpVector,LookAxes.X*DeltaSeconds*1.8));
    Pitch=FMath::Clamp(Pitch-LookAxes.Y*DeltaSeconds*90.0,-87.0,87.0);
    Camera->SetRelativeRotation(FRotator(Pitch,0,0));
    LookAxes=FVector::ZeroVector;
    const double AgeSlow=FMath::Clamp((Universe->Clocks.proper-1260.0)/60.0,0.0,1.0);
    M->MaxWalkSpeed=(bBoost?900:600)*(1-AgeSlow*0.55);
    AddMovementInput(GetActorForwardVector(),MoveAxes.X);
    AddMovementInput(GetActorRightVector(),MoveAxes.Y);
    if (MoveAxes.Z>0&&M->IsMovingOnGround()) Jump();
    else if (!FMath::IsNearlyZero(MoveAxes.Z)&&Fuel>0)
    {
        M->AddForce(GetActorUpVector()*MoveAxes.Z*1800*M->Mass);
        Fuel=FMath::Max(0.0,Fuel-DeltaSeconds/35.0);
    }
    if (Gravity.Length()<10&&!FMath::IsNearlyZero(RollInput))
        AddActorLocalRotation(FQuat(FVector::ForwardVector,RollInput*DeltaSeconds));
    const FVector Eyes=Camera->GetComponentLocation();
    const bool Water=Frame&&Frame->InWater(Eyes);
    const bool Air=Frame&&Frame->Breathable(Eyes)&&!Water;
    if (Air) { Oxygen=FMath::Min(1.0,Oxygen+DeltaSeconds*0.1); Fuel=FMath::Min(1.0,Fuel+DeltaSeconds*0.06); }
    else Oxygen=FMath::Max(0.0,Oxygen-DeltaSeconds/180.0);
    if (Water) M->Velocity*=FMath::Max(0.0,1-DeltaSeconds*1.8);
    if (Oxygen<=0) Universe->Die(TEXT("You ran out of air."));
}
void AEphemeraCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    auto* Enhanced=CastChecked<UEnhancedInputComponent>(Input);
    Context=NewObject<UInputMappingContext>(this);
    auto Add=[&](const TCHAR* Name,EInputActionValueType Type,auto Method,
        std::initializer_list<TPair<FKey,bool>> Keys,bool Discrete=false)
    {
        auto* Action=NewObject<UInputAction>(this,FName(*(FString(TEXT("Input_"))+Name)));
        Action->ValueType=Type;
        Actions.Add(Action);
        for (const auto& Pair:Keys)
        {
            auto& Mapping=Context->MapKey(Action,Pair.Key);
            if (Pair.Value) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
        }
        Enhanced->BindAction(Action,Discrete?ETriggerEvent::Started:ETriggerEvent::Triggered,this,Method);
        if (!Discrete) Enhanced->BindAction(Action,ETriggerEvent::Completed,this,Method);
    };
    Add(TEXT("Forward"),EInputActionValueType::Axis1D,&AEphemeraCharacter::Forward,{{EKeys::W,false},{EKeys::S,true},{EKeys::Gamepad_LeftY,false}});
    Add(TEXT("Right"),EInputActionValueType::Axis1D,&AEphemeraCharacter::Right,{{EKeys::D,false},{EKeys::A,true},{EKeys::Gamepad_LeftX,false}});
    Add(TEXT("Vertical"),EInputActionValueType::Axis1D,&AEphemeraCharacter::Vertical,{{EKeys::SpaceBar,false},{EKeys::LeftControl,true},{EKeys::Gamepad_FaceButton_Bottom,false},{EKeys::Gamepad_LeftShoulder,true}});
    Add(TEXT("LookX"),EInputActionValueType::Axis1D,&AEphemeraCharacter::LookX,{{EKeys::MouseX,false}});
    Add(TEXT("LookY"),EInputActionValueType::Axis1D,&AEphemeraCharacter::LookY,{{EKeys::MouseY,false}});
    Add(TEXT("StickLookX"),EInputActionValueType::Axis1D,&AEphemeraCharacter::StickLookX,{{EKeys::Gamepad_RightX,false}});
    Add(TEXT("StickLookY"),EInputActionValueType::Axis1D,&AEphemeraCharacter::StickLookY,{{EKeys::Gamepad_RightY,false}});
    Add(TEXT("Roll"),EInputActionValueType::Axis1D,&AEphemeraCharacter::Roll,{{EKeys::Q,true},{EKeys::Z,false}});
    Add(TEXT("Boost"),EInputActionValueType::Boolean,&AEphemeraCharacter::Boost,{{EKeys::LeftShift,false},{EKeys::Gamepad_RightShoulder,false}});
    Add(TEXT("Match"),EInputActionValueType::Boolean,&AEphemeraCharacter::Match,{{EKeys::R,false},{EKeys::Gamepad_LeftTrigger,false}});
    Add(TEXT("Interact"),EInputActionValueType::Boolean,&AEphemeraCharacter::Interact,{{EKeys::E,false},{EKeys::Gamepad_FaceButton_Left,false}},true);
    Add(TEXT("Lamp"),EInputActionValueType::Boolean,&AEphemeraCharacter::Lamp,{{EKeys::F,false},{EKeys::Gamepad_DPad_Up,false}},true);
    Add(TEXT("Log"),EInputActionValueType::Boolean,&AEphemeraCharacter::Log,{{EKeys::I,false},{EKeys::Gamepad_Special_Left,false}},true);
    Add(TEXT("Map"),EInputActionValueType::Boolean,&AEphemeraCharacter::Map,{{EKeys::M,false},{EKeys::Tab,false},{EKeys::Gamepad_DPad_Right,false}},true);
    Add(TEXT("Pause"),EInputActionValueType::Boolean,&AEphemeraCharacter::Pause,{{EKeys::Escape,false},{EKeys::Gamepad_Special_Right,false}},true);
    Add(TEXT("Scout"),EInputActionValueType::Boolean,&AEphemeraCharacter::Scout,{{EKeys::C,false},{EKeys::Gamepad_RightTrigger,false}},true);
    Add(TEXT("Antennae"),EInputActionValueType::Boolean,&AEphemeraCharacter::Antennae,{{EKeys::V,false},{EKeys::Y,false},{EKeys::Gamepad_FaceButton_Top,false}},true);
    if (auto* PC=Cast<APlayerController>(GetController()))
        if (auto* Local=PC->GetLocalPlayer())
            if (auto* Subsystem=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Subsystem->AddMappingContext(Context,0);
}
void AEphemeraCharacter::Forward(const FInputActionValue& V){MoveAxes.X=V.Get<float>();}
void AEphemeraCharacter::Right(const FInputActionValue& V){MoveAxes.Y=V.Get<float>();}
void AEphemeraCharacter::Vertical(const FInputActionValue& V){MoveAxes.Z=V.Get<float>();}
void AEphemeraCharacter::LookX(const FInputActionValue& V){LookAxes.X=V.Get<float>()*0.00122/FMath::Max(0.001f,GetWorld()->GetDeltaSeconds());}
void AEphemeraCharacter::LookY(const FInputActionValue& V){LookAxes.Y=V.Get<float>()*0.0014/FMath::Max(0.001f,GetWorld()->GetDeltaSeconds());}
void AEphemeraCharacter::StickLookX(const FInputActionValue& V){LookAxes.X=V.Get<float>();}
void AEphemeraCharacter::StickLookY(const FInputActionValue& V){LookAxes.Y=V.Get<float>();}
void AEphemeraCharacter::Roll(const FInputActionValue& V){RollInput=V.Get<float>();}
void AEphemeraCharacter::Boost(const FInputActionValue& V){bBoost=V.Get<bool>();}
void AEphemeraCharacter::Match(const FInputActionValue& V){bMatching=V.Get<bool>();}
void AEphemeraCharacter::Interact(const FInputActionValue& V){if(Universe) Universe->Interact();}
void AEphemeraCharacter::Lamp(const FInputActionValue& V){Flashlight->ToggleVisibility();}
void AEphemeraCharacter::Log(const FInputActionValue& V){if(Universe) Universe->OpenLog();}
void AEphemeraCharacter::Map(const FInputActionValue& V){if(Universe) Universe->OpenMap();}
void AEphemeraCharacter::Pause(const FInputActionValue& V){if(Universe) Universe->TogglePause();}
void AEphemeraCharacter::Scout(const FInputActionValue& V){if(Universe) Universe->Photograph();}
void AEphemeraCharacter::Antennae(const FInputActionValue& V){if(Universe)Universe->ToggleAntennae();}
void AEphemeraCharacter::Landed(const FHitResult& Hit)
{
    const double Impact=FMath::Max(0.0,-FVector::DotProduct(GetCharacterMovement()->Velocity,Hit.Normal));
    Super::Landed(Hit);
    Health-=FMath::Max(0.0,Impact/100.0-10)*7;
    if(Health<=0&&Universe)Universe->Die(TEXT("You fell too far."));
}
