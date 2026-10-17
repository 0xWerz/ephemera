#include "EphemeraWorld.h"
#include "CelestialBody.h"
#include "EphemeraCharacter.h"
#include "EphemeraShip.h"
#include "EphemeraHUD.h"
#include "EphemeraGameInstance.h"
#include "Components/SceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/AudioComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SpotLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Engine/World.h"

using namespace EphemeraData;
namespace
{
template<class T> T* Component(USceneComponent* Parent)
{
    auto* Owner=Parent->GetOwner();
    auto* C=NewObject<T>(Owner);
    Owner->AddInstanceComponent(C);
    C->SetMobility(EComponentMobility::Movable);
    C->SetupAttachment(Parent);
    C->RegisterComponent();
    return C;
}
FString GroupKind(const TMap<FString,Object>& Data,const FString& Id)
{
    return Data.Contains(Id)?String(Data[Id],TEXT("kind")):TEXT("");
}
}
AEphemeraWorld::AEphemeraWorld()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("AubadeSystem"));
    Music=CreateDefaultSubobject<UAudioComponent>(TEXT("AdaptiveScore"));
    Music->SetupAttachment(RootComponent); Music->bAutoActivate=false;
    PhotoCapture=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ScoutCamera"));
    PhotoCapture->SetupAttachment(RootComponent); PhotoCapture->bCaptureEveryFrame=false;
    PhotoCapture->bCaptureOnMovement=false;
    Post=CreateDefaultSubobject<UPostProcessComponent>(TEXT("Environment"));
    Post->SetupAttachment(RootComponent); Post->bUnbound=true;
    Post->Settings.bOverride_VignetteIntensity=true; Post->Settings.VignetteIntensity=0.18;
    Post->Settings.bOverride_BloomIntensity=true; Post->Settings.BloomIntensity=0.35;
}
void AEphemeraWorld::BeginPlay()
{
    Super::BeginPlay();
    State=Cast<UEphemeraGameInstance>(GetGameInstance());
    WorldData=EphemeraData::Load(TEXT("world.json"));
    AssetData=EphemeraData::Load(TEXT("assets.json"));
    WorldParameters=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/Migrated/Materials/MPC_Aubade.MPC_Aubade"));
    if (!WorldData.IsValid()||!AssetData.IsValid())
    {
        UE_LOG(LogTemp,Error,TEXT("Migration data is missing. Run Tools/prepare.py and Tools/import_assets.py."));
        bFrozen=true;
        return;
    }
    Build();
    PhotoTarget=NewObject<UTextureRenderTarget2D>(this);
    PhotoTarget->InitAutoFormat(960,540);
    PhotoCapture->TextureTarget=PhotoTarget;
    PhotoCapture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
    if (State) State->StartLoop();
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    if (PC)
    {
        HUD=CreateWidget<UEphemeraHUD>(PC,UEphemeraHUD::StaticClass());
        HUD->Universe=this; HUD->AddToViewport();
        PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor=false;
        Player=Cast<AEphemeraCharacter>(PC->GetPawn());
        if (Player) { Player->Universe=this; PlacePlayer(); }
    }
}
UStaticMesh* AEphemeraWorld::Mesh(const FString& Id) const
{
    const FString Path=String(Child(AssetData,TEXT("meshes")),*Id);
    return Path.IsEmpty()?nullptr:LoadObject<UStaticMesh>(nullptr,*Path);
}
void AEphemeraWorld::Build()
{
    for (const auto& V:Array(WorldData,TEXT("groups")))
    {
        const Object G=V->AsObject();
        const FString Id=String(G,TEXT("id")),Kind=String(G,TEXT("kind")),ParentId=String(G,TEXT("parent"));
        GroupData.Add(Id,G);
        USceneComponent* Root=nullptr;
        if (Kind==TEXT("body"))
        {
            auto* Body=GetWorld()->SpawnActor<ACelestialBody>();
            Body->Configure(Child(G,TEXT("data")));
            Bodies.Add(Body->BodyId,Body);
            Root=Body->GetRootComponent();
        }
        else if (Kind==TEXT("ship"))
        {
            Ship=GetWorld()->SpawnActor<AEphemeraShip>();
            Ship->Universe=this;
            Ship->Cockpit->SetRelativeTransform(Transform(Child(Child(G,TEXT("data")),TEXT("pilot_eye"))));
            Ship->Cockpit->AddLocalOffset(FVector(0,0,-170));
            Root=Ship->VisualRoot;
        }
        else Root=Component<USceneComponent>(Groups.Contains(ParentId)?Groups[ParentId].Get():RootComponent.Get());
        Groups.Add(Id,Root);
        const auto Pose=Transform(Child(G,TEXT("transform")));
        if(Kind==TEXT("ship"))Ship->SetActorTransform(Pose);
        else Root->SetRelativeTransform(Pose);
        RestTransforms.Add(Id,Pose);
        if(Kind==TEXT("nightbloom")&&Boolean(Child(G,TEXT("data")),TEXT("night")))Root->SetVisibility(false,true);
    }
    for (const auto& Pair:Bodies)
    {
        auto* Body=Pair.Value.Get();
        Body->OrbitParent=Bodies.FindRef(String(Body->Data,TEXT("orbit_parent")));
        Body->VisualParent=Bodies.FindRef(String(Body->Data,TEXT("visual_parent")));
        Body->SetActorTransform(Body->At(0));
        Body->Previous=Body->GetActorTransform();
    }
    TMap<FString,UHierarchicalInstancedStaticMeshComponent*> Batches;
    const FTransform MeshCorrection(FRotator(0,Number(AssetData,TEXT("mesh_yaw")),0));
    for (const auto& V:Array(WorldData,TEXT("placements")))
    {
        const Object P=V->AsObject();
        const FString Group=String(P,TEXT("group")),MeshId=String(P,TEXT("mesh")),Key=Group+TEXT(":")+MeshId;
        if (!Groups.Contains(Group)) continue;
        auto* Batch=Batches.FindRef(Key);
        if (!Batch)
        {
            auto* Asset=Mesh(MeshId);
            if (!Asset) continue;
            Batch=Component<UHierarchicalInstancedStaticMeshComponent>(Groups[Group]);
            Batch->SetStaticMesh(Asset);
            Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Batch->bAutoRebuildTreeOnInstanceChanges=false;
            Batches.Add(Key,Batch);
        }
        if (P->HasField(TEXT("instances")))
            for (const auto& T:Array(P,TEXT("instances"))) Batch->AddInstance(MeshCorrection*Transform(T->AsObject()));
        else Batch->AddInstance(MeshCorrection*Transform(Child(P,TEXT("transform"))));
    }
    for (const auto& Pair:Batches) Pair.Value->BuildTreeIfOutdated(true,true);
    for (const auto& V:Array(WorldData,TEXT("colliders")))
    {
        const Object C=V->AsObject();
        auto* Parent=Groups.FindRef(String(C,TEXT("group"))).Get();
        if (!Parent) continue;
        const FString Kind=String(C,TEXT("kind"));
        UPrimitiveComponent* Collider=nullptr;
        if (Kind==TEXT("mesh"))
        {
            auto* MeshComponent=Component<UStaticMeshComponent>(Parent);
            MeshComponent->SetStaticMesh(Mesh(String(C,TEXT("mesh"))));
            MeshComponent->SetVisibility(false);
            MeshComponent->SetHiddenInGame(true);
            MeshComponent->SetCastShadow(false);
            Collider=MeshComponent;
        }
        else if (Kind==TEXT("box"))
        {
            auto* Box=Component<UBoxComponent>(Parent);
            const FVector S=Raw(C,TEXT("size"))*50;
            Box->SetBoxExtent(FVector(S.Z,S.X,S.Y)); Collider=Box;
        }
        else if (Kind==TEXT("sphere"))
        {
            auto* Sphere=Component<USphereComponent>(Parent);
            Sphere->SetSphereRadius(Number(C,TEXT("radius"))*100); Collider=Sphere;
        }
        else if (Kind==TEXT("capsule"))
        {
            auto* Capsule=Component<UCapsuleComponent>(Parent);
            const double R=Number(C,TEXT("radius"))*100;
            Capsule->SetCapsuleSize(R,FMath::Max(R,Number(C,TEXT("height"))*50)); Collider=Capsule;
        }
        else if (Kind==TEXT("convex")||Kind==TEXT("cylinder"))
        {
            auto* Convex=Component<UProceduralMeshComponent>(Parent);
            Convex->bUseComplexAsSimpleCollision=false;
            TArray<FVector> Points;
            if(Kind==TEXT("convex"))
                for (const auto& P:Array(C,TEXT("points"))) Points.Add(Axis(Raw(P->AsArray()))*100);
            else for(int32 I=0;I<32;++I)
            {
                const double Angle=I*EphemeraSim::Tau/32,Radius=Number(C,TEXT("radius"))*100,Half=Number(C,TEXT("height"))*50;
                Points.Add(FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,Half));
                Points.Add(FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,-Half));
            }
            Convex->AddCollisionConvexMesh(Points); Collider=Convex;
        }
        if (Collider)
        {
            Collider->ComponentTags.Add(TEXT("EphemeraCollision"));
            Collider->SetRelativeTransform(Transform(Child(C,TEXT("transform"))));
            if (Kind==TEXT("mesh"))Collider->SetRelativeTransform(MeshCorrection*Transform(Child(C,TEXT("transform"))));
            Collider->SetCollisionProfileName(TEXT("BlockAll"));
            Collider->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }
    }
    for (const auto& V:Array(WorldData,TEXT("interactions")))
    {
        const Object I=V->AsObject();
        auto* Parent=Groups.FindRef(String(I,TEXT("group"))).Get();
        if (!Parent) continue;
        auto* Sphere=Component<USphereComponent>(Parent);
        Sphere->SetSphereRadius(Number(I,TEXT("radius"),0.9)*100);
        Sphere->SetRelativeTransform(Transform(Child(I,TEXT("transform"))));
        Sphere->SetCollisionProfileName(TEXT("EphemeraInteractable"));
        Sphere->SetGenerateOverlapEvents(false);
        InteractionData.Add(Sphere,I);
    }
    for (const auto& V:Array(WorldData,TEXT("lights")))
    {
        const Object L=V->AsObject();
        auto* Parent=Groups.FindRef(String(L,TEXT("group"))).Get();
        if (!Parent) continue;
        auto* Light=Component<UPointLightComponent>(Parent);
        Light->SetRelativeTransform(Transform(Child(L,TEXT("transform"))));
        Light->SetLightColor(Color(L,TEXT("color")));
        Light->SetIntensity(Number(L,TEXT("energy"))*1400);
        Light->SetAttenuationRadius(Number(L,TEXT("range"))*100);
        Light->SetCastShadows(Boolean(L,TEXT("shadow")));
        Light->SetSourceRadius(12);
    }
    for (const auto& Pair:Bodies) if (Pair.Value->bStar)
    {
        StarLight=Component<UPointLightComponent>(Pair.Value->GetRootComponent());
        StarLight->bUseInverseSquaredFalloff=false;
        StarLight->SetIntensity(10);
        StarLight->SetAttenuationRadius(1500000);
        StarLight->SetLightColor(FLinearColor(1,0.81,0.59));
        StarLight->SetCastShadows(true);
        StarLight->SetSourceRadius(Pair.Value->Radius*0.25);
        break;
    }
    const TMap<FString,FString> Stems={{TEXT("lull"),TEXT("stem_hum_voice")},{TEXT("rime"),TEXT("stem_glass_harmonium")},
        {TEXT("wisp"),TEXT("stem_silk_string")},{TEXT("gust"),TEXT("stem_reed_horn")},{TEXT("sunbreak"),TEXT("stem_hand_drum")}};
    for(const auto& Pair:Stems)
    {
        const FString Path=String(Child(AssetData,TEXT("audio")),*Pair.Value);
        if(auto* Sound=LoadObject<USoundWave>(nullptr,*Path))
        {
            auto* Audio=Component<UAudioComponent>(RootComponent);
            Audio->bAutoActivate=false;Audio->SetSound(Sound);Audio->SetVolumeMultiplier(0);Audio->Play();
            SignalAudio.Add(Pair.Key,Audio);
        }
    }
    Starfield=Component<UStaticMeshComponent>(RootComponent);
    Starfield->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    Starfield->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Migrated/Materials/M_Starfield.M_Starfield")));
    Starfield->SetWorldScale3D(FVector(100000));
    Starfield->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Starfield->SetCastShadow(false);
    Starfield->bAffectDistanceFieldLighting=false;
}
void AEphemeraWorld::PlacePlayer()
{
    if (!Player||!WorldData.IsValid()) return;
    auto* Body=Bodies.FindRef(String(WorldData,TEXT("spawn_body"))).Get();
    if (!Body) return;
    Player->Universe=this;
    if(!HUD)
        if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
        {
            HUD=CreateWidget<UEphemeraHUD>(PC,UEphemeraHUD::StaticClass());
            HUD->Universe=this;HUD->AddToViewport();
        }
    Player->ResetAt(Transform(Child(WorldData,TEXT("spawn")))*Body->GetActorTransform(),Body);
    if (Ship) Ship->ResetAt(Transform(Child(WorldData,TEXT("ship_spawn")))*Body->GetActorTransform(),Body);
}
ACelestialBody* AEphemeraWorld::DominantBody(FVector Point) const
{
    ACelestialBody* Best=nullptr;
    ACelestialBody* Star=nullptr;
    double Smallest=DBL_MAX;
    for (const auto& Pair:Bodies)
    {
        auto* B=Pair.Value.Get();
        if (B->bStar) { Star=B; continue; }
        if (B->SphereOfInfluence>=Smallest||FVector::Distance(Point,B->GetActorLocation())>=B->SphereOfInfluence) continue;
        if (B->OrbitParent&&!B->OrbitParent->bStar&&!B->VisualParent)
        {
            auto* P=B->OrbitParent.Get();
            if (FVector::Distance(Point,P->GetActorLocation())<P->Radius+FMath::Max(P->AtmosphereHeight,P->Radius*0.35)) continue;
        }
        Best=B; Smallest=B->SphereOfInfluence;
    }
    return Best?Best:Star;
}
FVector AEphemeraWorld::GravityAt(FVector Point) const
{
    auto* Body=DominantBody(Point);
    return Body?Body->GravityAt(Point):FVector::ZeroVector;
}
double AEphemeraWorld::DilationAt(FVector Point) const
{
    double D=1;
    for (const auto& Pair:Bodies) D=FMath::Max(D,Pair.Value->DilationAt(Point));
    return D;
}
void AEphemeraWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!WorldData.IsValid()||!Player||!State) return;
    if(Starfield)Starfield->SetWorldLocation(Player->GetActorLocation());
    if (bDying)
    {
        DeathDelay-=DeltaSeconds;
        if (DeathDelay<=0)
        {
            bDying=false;
            if (State->Has(TEXT("entangled"))&&!bEnding) ResetLoop();
            else
            {
                SetPanel(TEXT("finished"),bEnding?TEXT("A new dawn"):TEXT("A life, lived"),
                    bEnding?TEXT("You look up at the last stars, and you know what they are."):TEXT("You hatched. You lived. Your memories will go on."),
                    {TEXT("Begin again")},true);
            }
        }
        return;
    }
    if (!bFrozen)
    {
        // Substeps avoid tunnelling during flight and extreme dilation.
        const double Delta=FMath::Min(double(DeltaSeconds),0.05);
        const int32 Steps=FMath::Max(1,FMath::CeilToInt(Delta*90));
        for (int32 I=0;I<Steps;++I) Step(Delta/Steps);
        UpdateStory(Delta);
        UpdateQuantum(Delta);
        UpdateAudio();
        if (WorldParameters)
        {
            auto* MPC=GetWorld()->GetParameterCollectionInstance(WorldParameters);
            MPC->SetScalarParameterValue(TEXT("WorldTime"),Clocks.world);
            MPC->SetScalarParameterValue(TEXT("Resonance"),EphemeraSim::Resonance(Clocks.world));
            MPC->SetScalarParameterValue(TEXT("Night"),bEnding?1:0);
        }
    }
}
void AEphemeraWorld::Step(double Delta)
{
    Clocks.Advance(Delta,DilationAt(Player->GetActorLocation()));
    State->WorldTime=Clocks.world;
    for (const auto& Pair:Bodies) Pair.Value->Advance(Clocks.world,Delta);
    if (Ship) Ship->Simulate(Delta,Player->Ship&&Player->bControls?Player->MoveAxes:FVector::ZeroVector,
        Player->Ship&&Player->bControls?Player->LookAxes:FVector::ZeroVector,Player->RollInput,Player->bBoost,Player->bMatching);
    // Keep the observer inside its frame during each orbit substep, including 400× Slowfall time.
    if(Player->Ship)Player->SetActorLocationAndRotation(Player->Ship->Cockpit->GetComponentLocation(),
        Player->Ship->GetActorQuat(),false,nullptr,ETeleportType::TeleportPhysics);
    else Player->CarryFrame();
}
void AEphemeraWorld::ResetLoop()
{
    ClosePanel();
    Clocks.Reset(); bEnding=bDying=bEvensong=false; bFrozen=false; bPhotoVisible=false; EndingTime=DeepTime=0;
    Timers.Empty(); QuantumAt.Empty(); HiddenGroups.Empty();
    for (const auto& Pair:Bodies) { Pair.Value->Advance(0,0); Pair.Value->Previous=Pair.Value->GetActorTransform(); }
    for (const auto& Pair:Groups)
        if (GroupKind(GroupData,Pair.Key)!=TEXT("body")&&GroupKind(GroupData,Pair.Key)!=TEXT("ship"))
        {
            Pair.Value->SetRelativeTransform(RestTransforms[Pair.Key]);
            SetGroupVisible(Pair.Key,true);
        }
    State->StartLoop(); PlacePlayer();
    if (HUD) HUD->Toast(TEXT("Dawn. Again."));
}
void AEphemeraWorld::Die(const FString& Reason)
{
    if (bDying) return;
    ClosePanel(); bDying=true; bFrozen=true; DeathDelay=4;
    if (Player) { Player->bControls=false; Player->GetCharacterMovement()->DisableMovement(); }
    if (HUD) HUD->Toast(Reason);
}
void AEphemeraWorld::SetGroupVisible(const FString& Id,bool Visible)
{
    auto* Root=Groups.FindRef(Id).Get();
    if (!Root) return;
    Root->SetVisibility(Visible,true);
    TArray<USceneComponent*> Parts;
    Root->GetChildrenComponents(true,Parts);
    for (auto* C:Parts) if (auto* P=Cast<UPrimitiveComponent>(C))
    {
        if (!Visible) P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        else if (InteractionData.Contains(P)) P->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        else if (!Cast<UHierarchicalInstancedStaticMeshComponent>(P)) P->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
}
void AEphemeraWorld::UpdateStory(double Delta)
{
    const FVector Position=Player->GetActorLocation();
    for (const auto& Pair:GroupData)
    {
        const Object G=Pair.Value,D=Child(G,TEXT("data"));
        auto* Root=Groups.FindRef(Pair.Key).Get();
        if (!Root) continue;
        const FString Kind=String(G,TEXT("kind"));
        const double Distance=FVector::Distance(Position,Root->GetComponentLocation());
        if (Kind==TEXT("discovery")&&Distance<Number(D,TEXT("radius"))*100) State->AddLog(String(D,TEXT("id")));
        if (Kind==TEXT("nightbloom")) Root->SetVisibility(Boolean(D,TEXT("night"))==bEnding,true);
        if (Kind==TEXT("npc")&&Boolean(D,TEXT("dayling"))&&Number(D,TEXT("fixed_age"),-1)<0
            &&Clocks.world>=Number(D,TEXT("cocoon_at"),1320)&&!HiddenGroups.Contains(Pair.Key))
        {
            HiddenGroups.Add(Pair.Key);
            SetGroupVisible(Pair.Key,false);
        }
        const FString NpcId=String(D,TEXT("id"));
        if(Kind==TEXT("npc")&&SignalAudio.Contains(NpcId))
        {
            const FVector Direction=(Root->GetComponentLocation()-Player->Camera->GetComponentLocation()).GetSafeNormal();
            const double Align=FVector::DotProduct(Player->Camera->GetForwardVector(),Direction);
            const double Near=FMath::Clamp(1-Distance/6000.0,0.0,1.0);
            const double Range=FMath::Clamp(1-Distance/260000.0,0.0,1.0);
            const double Focus=bAntennae?FMath::SmoothStep(0.55,1.0,Align)*FMath::Pow(Range,0.6):0;
            SignalAudio[NpcId]->SetVolumeMultiplier(FMath::Max(Near*0.9,Focus)*0.7);
            double& Held=Timers.FindOrAdd(TEXT("signal_")+NpcId);
            Held=bAntennae&&Align>0.94&&Range>0?Held+Delta:0;
            if(Held>1.6&&!State->Has(TEXT("signal_")+NpcId))
            {
                State->Set(TEXT("signal_")+NpcId);
                if(HUD)HUD->Toast(TEXT("Signal identified: ")+String(Child(Child(State->Story,TEXT("dialogue")),*NpcId),TEXT("name")));
            }
        }
        if (Kind==TEXT("probe"))
        {
            if (!State->Has(TEXT("entangled"))&&Distance<3000)
            {
                State->Set(TEXT("entangled")); State->AddLog(TEXT("probe_flash"));
                if (HUD) HUD->Toast(TEXT("A flash. For an instant, the world stands still."));
                Timers.Add(Pair.Key,0);
            }
            if (State->Has(TEXT("entangled"))&&!Observed(Root->GetComponentLocation())&&!State->Has(TEXT("has_probe_today")))
            {
                double& T=Timers.FindOrAdd(Pair.Key);
                T+=Delta;
                if (T>1)
                {
                    Root->SetRelativeTransform(Transform(Child(D,TEXT("heart"))));
                    State->AddLog(TEXT("probe_moved"));
                }
            }
        }
        if (Kind==TEXT("lift"))
        {
            double& Target=Timers.FindOrAdd(Pair.Key+TEXT("_target"));
            double& Height=Timers.FindOrAdd(Pair.Key+TEXT("_height"));
            if (!FMath::IsNearlyEqual(Height,Target))
            {
                Height=FMath::FInterpConstantTo(Height,Target,Delta,260);
                const FTransform Rest=RestTransforms[Pair.Key];
                Root->SetRelativeLocation(Rest.GetLocation()+Rest.GetUnitAxis(EAxis::Z)*Height);
                if (Timers.Contains(Pair.Key+TEXT("_rider")))
                {
                    Player->SetActorLocation(Root->GetComponentLocation()+Root->GetUpVector()*80);
                    if (FMath::IsNearlyEqual(Height,Target))
                    {
                        Timers.Remove(Pair.Key+TEXT("_rider"));
                        Player->bControls=true; Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
                    }
                }
            }
        }
        if (Kind==TEXT("gulper"))
        {
            const double Dt=Delta*Clocks.dilation/FMath::Max(1.0,DilationAt(Root->GetComponentLocation()));
            auto* Deep=Bodies.FindRef(TEXT("slow_deep")).Get();
            if (Deep&&Deep->InWater(Position)&&Player->Flashlight->IsVisible()
                &&Distance<5500&&FVector::Distance(Deep->GetActorTransform().InverseTransformPosition(Position),EphemeraData::Position(D,TEXT("city_local")))>Number(D,TEXT("city_avoid"),34)*100)
            {
                Root->SetWorldLocation(FMath::VInterpConstantTo(Root->GetComponentLocation(),Position,Dt,650));
                if (Distance<700) Die(TEXT("Something large came out of the dark toward your light."));
            }
        }
    }
    if (Clocks.dilation>4) { DeepTime+=Delta; if (DeepTime>6) { State->AddLog(TEXT("deep_clock"));State->Set(TEXT("know_dilation")); } }
    if (State->Has(TEXT("know_night_unseen"))&&State->Has(TEXT("know_dilation"))&&State->Has(TEXT("know_lifespan"))) State->AddLog(TEXT("the_plan"));
    auto* Body=DominantBody(Position);
    if (Body)
    {
        if (Body->bStar&&Body->Altitude(Position)<100) Die(TEXT("You flew into Aubade."));
        const double Hazard=Number(Body->Data,TEXT("hazard_radius"))*100;
        if (Hazard>0&&FVector::Distance(Position,Body->GetActorLocation())<Hazard) Die(TEXT("You flew too close to the core."));
        if (Body->BodyId==TEXT("chorus"))
        {
            if (Body->Altitude(Position)<-12000) Die(TEXT("The Chorus closed over you."));
            if (Body->Altitude(Position)<90000&&EphemeraSim::Resonance(Clocks.world)>0.3) State->AddLog(TEXT("chorus_resonance"));
            if (Body->Altitude(Position)<45000&&EphemeraSim::Resonance(Clocks.world)>0.85)
            { State->Set(TEXT("heard_hum")); State->AddLog(TEXT("chorus_hum")); }
        }
    }
    if (bEnding)
    {
        const double Before=EndingTime; EndingTime+=Delta;
        if (Before<25&&EndingTime>=25&&HUD) HUD->Toast(TEXT("The flowers are opening. Nobody knew they did that."));
        if (Before<70&&EndingTime>=70&&HUD) HUD->Toast(TEXT("Under the crickets, the sky is singing the dawn song."));
    }
    if (!bEvensong&&FMath::Min(1320-Clocks.proper,(1320-Clocks.world)/Clocks.dilation)<=95)
    { bEvensong=true; CurrentTrack.Empty(); }
    if (Clocks.proper>=1320) { Die(TEXT("Your legs fold. The silk closes.")); return; }
    if (Clocks.world>=1320&&!bEnding)
    {
        if (Body&&Body->BodyId==TEXT("dawnhollow")&&Body->Altitude(Position)<Body->AtmosphereHeight)
        {
            bEnding=true; State->Set(TEXT("saw_night")); State->AddLog(TEXT("night_seen")); CurrentTrack.Empty();
            if (HUD) HUD->Toast(TEXT("For the first time, someone is here to see the night."));
        }
        else if (State->Has(TEXT("stay_offer_today"))&&Body&&Body->BodyId==TEXT("slow_deep")&&Body->InWater(Position))
        {
            State->Set(TEXT("ending_long_afternoon")); bEnding=true;
            Die(TEXT("Far above, a day ends without you. Down here, the afternoon goes on."));
        }
        else { State->AddLog(TEXT("unobserved_reset")); Die(TEXT("The world forgets.")); }
    }
    Post->Settings.bOverride_SceneColorTint=true;
    Post->Settings.SceneColorTint=Body&&Body->InWater(Player->Camera->GetComponentLocation())?
        FLinearColor(0.22,0.52,0.66):FLinearColor::White;
}
bool AEphemeraWorld::Observed(FVector Point,double Range) const
{
    if (!Player) return false;
    auto Test=[&](const FTransform& Pose,double Fov)
    {
        const FVector Offset=Point-Pose.GetLocation();
        if (Offset.Length()>Range) return false;
        const FVector Local=Pose.GetRotation().UnrotateVector(Offset);
        const double Tangent=FMath::Tan(FMath::DegreesToRadians(Fov*0.5));
        if (Local.X<=0||FMath::Abs(Local.Y)>Local.X*Tangent||FMath::Abs(Local.Z)>Local.X*Tangent*9.0/16.0) return false;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(QuantumObservation),true);
        Query.AddIgnoredActor(Player); if (Ship) Query.AddIgnoredActor(Ship);
        const bool Blocked=GetWorld()->LineTraceSingleByChannel(Hit,Pose.GetLocation(),Point,ECC_Visibility,Query);
        return !Blocked||FVector::Distance(Hit.ImpactPoint,Point)<150;
    };
    return Test(Player->Camera->GetComponentTransform(),Player->Camera->FieldOfView)||(bPhotoVisible&&Test(PhotoPose,PhotoFov));
}
void AEphemeraWorld::UpdateQuantum(double Delta)
{
    for (const auto& Pair:GroupData)
    {
        const FString Kind=String(Pair.Value,TEXT("kind"));
        const Object Data=Child(Pair.Value,TEXT("data"));
        auto* Root=Groups.FindRef(Pair.Key).Get();
        if (!Root) continue;
        if (Kind==TEXT("quantum"))
        {
            if (Observed(Root->GetComponentLocation())) { Timers.Add(Pair.Key,0); continue; }
            double& Timer=Timers.FindOrAdd(Pair.Key);
            Timer+=Delta;
            const auto& Anchors=Array(Data,TEXT("anchors"));
            if (Timer<0.35||Anchors.Num()<2) continue;
            const int32 At=QuantumAt.Contains(Pair.Key)?QuantumAt[Pair.Key]:Number(Data,TEXT("at"));
            for (int32 I=1;I<Anchors.Num();++I)
            {
                const int32 Next=(At+I)%Anchors.Num();
                const FTransform Pose=Transform(Anchors[Next]->AsObject());
                const FVector Point=Root->GetAttachParent()->GetComponentTransform().TransformPosition(Pose.GetLocation());
                if (!Observed(Point))
                { Root->SetRelativeTransform(Pose); QuantumAt.Add(Pair.Key,Next); Timer=-FMath::FRandRange(0,1.5); break; }
            }
        }
        if (Kind==TEXT("quantum_ring"))
        {
            double& Timer=Timers.FindOrAdd(Pair.Key);
            Timer+=Delta; if (Timer<0.2) continue; Timer=0;
            const auto& Slots=Array(Data,TEXT("slots"));
            const auto& Trees=Array(Data,TEXT("trees"));
            const int32 Gap=QuantumAt.Contains(Pair.Key)?QuantumAt[Pair.Key]:Number(Data,TEXT("gap"));
            if (!Slots.IsValidIndex(Gap)) continue;
            auto* Parent=Root->GetAttachParent();
            const auto GapPose=Transform(Slots[Gap]->AsObject());
            if (Observed(Parent->GetComponentTransform().TransformPosition(GapPose.GetLocation()),9000)) continue;
            int32 Best=-1; double Dot=2;
            for (int32 I=0;I<Slots.Num();++I)
            {
                if (I==Gap) continue;
                const FVector Point=Parent->GetComponentTransform().TransformPosition(Transform(Slots[I]->AsObject()).GetLocation());
                if (Observed(Point,9000)) continue;
                const double D=FVector::DotProduct(Player->Camera->GetForwardVector(),(Point-Player->Camera->GetComponentLocation()).GetSafeNormal());
                if (D<Dot) { Dot=D; Best=I; }
            }
            if (Best>=0)
            {
                // Slot ownership is tracked separately from immutable exported metadata.
                const FString Key=Pair.Key+TEXT("_slot_")+FString::FromInt(Best);
                const int32 Original=QuantumAt.Contains(Key)?QuantumAt[Key]:Best;
                if (Trees.IsValidIndex(Original))
                {
                    auto* Tree=Groups.FindRef(Trees[Original]->AsString()).Get();
                    if (Tree)
                    {
                        Tree->SetRelativeTransform(GapPose);
                        QuantumAt.Add(Pair.Key+TEXT("_slot_")+FString::FromInt(Gap),Original);
                        QuantumAt.Add(Key,-1);
                        QuantumAt.Add(Pair.Key,Best);
                    }
                }
            }
        }
    }
}
Object AEphemeraWorld::Focused() const
{
    if (!Player) return nullptr;
    TArray<FHitResult> Hits;
    const FVector Start=Player->Camera->GetComponentLocation(),End=Start+Player->Camera->GetForwardVector()*500;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(Interaction),true); Query.AddIgnoredActor(Player);
    GetWorld()->LineTraceMultiByChannel(Hits,Start,End,ECC_Visibility,Query);
    for (const auto& Hit:Hits)
        if (InteractionData.Contains(Hit.GetComponent()))
        {
            const Object I=InteractionData[Hit.GetComponent()];
            const FString Group=String(I,TEXT("group"));
            if (!HiddenGroups.Contains(Group)&&Boolean(I,TEXT("enabled"),true)) return I;
        }
    return nullptr;
}
FString AEphemeraWorld::FocusPrompt() const
{
    if (Player&&Player->Ship) return TEXT("[E] Leave the seat    [R] Match velocity");
    const Object I=Focused();
    return I.IsValid()?TEXT("[E] ")+String(I,TEXT("prompt")):TEXT("");
}
void AEphemeraWorld::Interact()
{
    if (!Player) return;
    if (!PanelMode.IsEmpty()) { Choose(0); return; }
    if (Player->Ship)
    {
        auto* S=Player->Ship.Get();
        const FVector Exit=S->GetActorLocation()+S->GetActorRightVector()*240+S->GetActorUpVector()*100;
        Player->SetShip(nullptr);
        Player->SetActorLocation(Exit,false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->Velocity=S->Velocity;
        return;
    }
    const Object I=Focused();
    if (I.IsValid()) Use(I);
}
void AEphemeraWorld::Use(const Object& I)
{
    const FString Group=String(I,TEXT("group"));
    const Object G=GroupData.FindRef(Group),Data=Child(G,TEXT("data"));
    const FString Kind=String(G,TEXT("kind")),Prompt=String(I,TEXT("prompt"));
    const Object Action=Child(I,TEXT("action"));
    if(String(Action,TEXT("type"))==TEXT("dialogue"))OpenDialogue(String(Action,TEXT("id")));
    else if(Prompt.Contains(TEXT("very old marshmallow")))
    {
        if(!State->Has(TEXT("giant_antennae_today")))
        {
            State->Set(TEXT("giant_antennae_today"));State->AddLog(TEXT("old_marshmallow"));
            if(HUD)HUD->Toast(TEXT("It tastes like a yesterday nobody remembers. Your antennae feel... roomy."));
        }
    }
    else if (Kind==TEXT("npc")||Kind==TEXT("quantum")) OpenDialogue(String(Data,TEXT("id")));
    else if (Kind==TEXT("text")) ReadText(String(Data,TEXT("id")));
    else if (Kind==TEXT("probe")&&State->Has(TEXT("entangled")))
    {
        State->Set(TEXT("has_probe_today")); SetGroupVisible(Group,false); OpenDialogue(TEXT("camera_roll"));
    }
    else if (Kind==TEXT("ship")&&(Prompt.Contains(TEXT("seat"))||Prompt.Contains(TEXT("Fly"))||Prompt.Contains(TEXT("Pilot"))))
    {
        if (!State->Has(TEXT("has_knot"))) { if(HUD)HUD->Toast(TEXT("Sleet ties launch knots down at the Launch Reed."));return; }
        State->Set(TEXT("has_knot_today")); Player->SetShip(Ship);
    }
    else if (Kind==TEXT("ship")&&Prompt.Contains(TEXT("log"))) OpenLog();
    else if (Kind==TEXT("lift"))
    {
        double& Target=Timers.FindOrAdd(Group+TEXT("_target"));
        Target=Target>0?0:Number(Data,TEXT("travel"),20)*100;
        Timers.Add(Group+TEXT("_rider"),1);
        Player->bControls=false; Player->GetCharacterMovement()->DisableMovement();
    }
    else if (Kind==TEXT("campfire"))
    {
        const FString Key=Group+TEXT("_roast");
        if(!Timers.Contains(Key))
        {
            Timers.Add(Key,Clocks.proper);
            if(HUD)HUD->Toast(TEXT("You hold a marshmallow over the fire."));
        }
        else
        {
            const double Roast=(Clocks.proper-Timers[Key])/12.0;
            Timers.Remove(Key);
            FString Message=TEXT("Raw. Squishy. A very young marshmallow.");double Heal=10;
            if(Roast>0.3){Message=TEXT("Golden all the way round. Perfect.");Heal=100;State->LoopFlags.Add(TEXT("perfect_marshmallow"));}
            if(Roast>0.55){Message=TEXT("Crisp at the edges. Gust would call it experienced.");Heal=50;}
            if(Roast>0.8){Message=TEXT("It's on fire. You eat it anyway. Tradition.");Heal=20;}
            if(Roast>1.2){Message=TEXT("A small comet of ash, and then nothing.");Heal=0;}
            Player->Health=FMath::Min(100.0,Player->Health+Heal);if(HUD)HUD->Toast(Message);
        }
    }
    else if (HUD) HUD->Toast(Prompt);
}
void AEphemeraWorld::SetPanel(const FString& Mode,const FString& Title,const FString& Body,const TArray<FString>& Choices,bool Freeze)
{
    if (!HUD) return;
    PanelMode=Mode; bFrozen=Freeze;
    if (Player) { Player->bControls=false;Player->MoveAxes=Player->LookAxes=FVector::ZeroVector; Player->GetCharacterMovement()->DisableMovement(); }
    HUD->ShowPanel(Title,Body,Choices);
    if (auto* PC=UGameplayStatics::GetPlayerController(this,0))
    { PC->bShowMouseCursor=true; PC->SetInputMode(FInputModeGameAndUI().SetWidgetToFocus(HUD->TakeWidget()).SetHideCursorDuringCapture(false)); }
}
void AEphemeraWorld::ClosePanel()
{
    PanelMode.Empty(); ChoiceTargets.Empty(); bFrozen=false;
    if (HUD) HUD->HidePanel();
    if (Player)
    {
        Player->bControls=true;
        Player->GetCharacterMovement()->SetMovementMode(Player->Ship?MOVE_None:MOVE_Falling);
    }
    if (auto* PC=UGameplayStatics::GetPlayerController(this,0))
    { PC->bShowMouseCursor=false; PC->SetInputMode(FInputModeGameOnly()); }
}
void AEphemeraWorld::OpenDialogue(const FString& Id)
{
    const Object Tree=Child(Child(State->Story,TEXT("dialogue")),*Id);
    if (!Tree.IsValid()) return;
    if (Boolean(Tree,TEXT("light"))&&!State->Has(TEXT("has_translator")))
    { if(HUD)HUD->Toast(TEXT("The light is saying something. You cannot read it yet."));return; }
    DialogueId=Id;
    FString Start=TEXT("start");
    for (const auto& E:Array(Tree,TEXT("entry")))
        if (State->Check(String(E->AsObject(),TEXT("cond")))) { Start=String(E->AsObject(),TEXT("goto"));break; }
    DialogueNode(Start);
}
void AEphemeraWorld::DialogueNode(const FString& Id)
{
    const Object Tree=Child(Child(State->Story,TEXT("dialogue")),*DialogueId);
    const Object Node=Child(Child(Tree,TEXT("nodes")),*Id);
    if (!Node.IsValid()) { ClosePanel(); return; }
    State->Apply(Node);
    FString Body;
    for (const auto& V:Array(Node,TEXT("lines")))
    {
        const auto& Line=V->AsArray();
        if (Line.Num()<2) continue;
        FString Speaker=Line[0]->AsString(); if(Speaker==TEXT("*"))Speaker=String(Tree,TEXT("name"));
        Body+=(!Speaker.IsEmpty()?Speaker+TEXT("\n"):TEXT(""))+Line[1]->AsString()+TEXT("\n\n");
    }
    ChoiceTargets.Empty();
    TArray<FString> Choices;
    for (const auto& C:Array(Node,TEXT("choices")))
        if (State->Check(String(C->AsObject(),TEXT("cond"))))
        { Choices.Add(String(C->AsObject(),TEXT("text")));ChoiceTargets.Add(String(C->AsObject(),TEXT("goto"))); }
    if (Choices.IsEmpty()) { Choices.Add(TEXT("Leave"));ChoiceTargets.Add(TEXT("close")); }
    SetPanel(TEXT("dialogue"),String(Tree,TEXT("name")),Body,Choices,false);
}
void AEphemeraWorld::ReadText(const FString& Id)
{
    const Object Data=Child(Child(State->Story,TEXT("texts")),*Id);
    if (!Data.IsValid()) return;
    if (String(Data,TEXT("kind"))==TEXT("light")&&!State->Has(TEXT("has_translator")))
    { if(HUD)HUD->Toast(TEXT("Light crawls across the panel. You cannot read it."));return; }
    FString Body;
    for (const auto& V:Array(Data,TEXT("lines")))
    {
        const auto& A=V->AsArray();if(A.Num()<2)continue;
        const int32 Depth=A.Num()>2?A[2]->AsNumber():0;
        Body+=FString::ChrN(Depth*3,TEXT(' '))+A[0]->AsString()+TEXT("\n")+A[1]->AsString()+TEXT("\n\n");
    }
    State->Set(TEXT("read_")+Id);State->Apply(Data);
    ChoiceTargets={TEXT("close")};
    SetPanel(TEXT("reader"),String(Data,TEXT("title"),TEXT("MEMORY STONE")),Body,{TEXT("Close")},!bEvensong&&!bEnding);
}
void AEphemeraWorld::OpenLog()
{
    if (!PanelMode.IsEmpty()) { ClosePanel();return; }
    FString Body;
    const Object Logs=Child(State->Story,TEXT("log"));
    for (const FString& Id:State->LogEntries)
    {
        const Object L=Child(Logs,*Id);
        Body+=String(L,TEXT("title"))+TEXT("\n")+String(L,TEXT("text"))+TEXT("\n\n");
    }
    if(Body.IsEmpty())Body=TEXT("A new life. Nothing written yet.");
    ChoiceTargets={TEXT("close")};
    SetPanel(TEXT("log"),TEXT("THE INHERITANCE"),Body,{TEXT("Close")},!bEvensong&&!bEnding);
}
void AEphemeraWorld::OpenMap()
{
    if (!PanelMode.IsEmpty()) { ClosePanel();return; }
    FString Body=TEXT("THE AUBADE SYSTEM\n\n");
    for(const auto& Pair:Bodies)if(!Pair.Value->bStar)
        Body+=FString::Printf(TEXT("%s    %.0f m away\n"),*Pair.Value->DisplayName,
            FVector::Distance(Player->GetActorLocation(),Pair.Value->GetActorLocation())/100);
    ChoiceTargets={TEXT("close")};
    SetPanel(TEXT("map"),TEXT("WAYFARER'S CHART"),Body,{TEXT("Close")},false);
}
void AEphemeraWorld::TogglePause()
{
    if (!PanelMode.IsEmpty()){ClosePanel();return;}
    ChoiceTargets={TEXT("close"),TEXT("restart")};
    SetPanel(TEXT("pause"),TEXT("EPHEMERA"),TEXT("No one sees two dawns.\n\nWASD move · Mouse look · Space / Ctrl jetpack · Shift boost\nE interact · F lamp · C photograph · V antennae · I Inheritance · M / Tab chart\nQ / Z roll · Hold R match velocity"),{TEXT("Resume"),TEXT("Wake at dawn")},true);
}
void AEphemeraWorld::Choose(int32 Index)
{
    if (PanelMode==TEXT("finished")) { ResetLoop();return; }
    if (!ChoiceTargets.IsValidIndex(Index)) return;
    const FString Target=ChoiceTargets[Index];
    if (Target==TEXT("close")) ClosePanel();
    else if (Target==TEXT("restart")) ResetLoop();
    else if (Target==TEXT("rest")) { ClosePanel();Die(TEXT("You settle beside the fire.")); }
    else if(PanelMode==TEXT("dialogue"))DialogueNode(Target);
}
void AEphemeraWorld::Photograph()
{
    if(!Player||!State->Has(TEXT("has_probe_today"))||!PanelMode.IsEmpty())return;
    if(bPhotoVisible){bPhotoVisible=false;return;}
    PhotoPose=Player->Camera->GetComponentTransform();PhotoFov=Player->Camera->FieldOfView;
    PhotoCapture->SetWorldTransform(PhotoPose);PhotoCapture->FOVAngle=PhotoFov;
    PhotoCapture->CaptureScene();bPhotoVisible=true;
}
void AEphemeraWorld::ToggleAntennae()
{
    bAntennae=!bAntennae;
    if(HUD)HUD->Toast(bAntennae?TEXT("Your antennae rise. Turn toward a signal."):TEXT("Your antennae settle."));
}
void AEphemeraWorld::UpdateAudio()
{
    if (!Player) return;
    auto* Body=DominantBody(Player->GetActorLocation());
    if (Body&&Body->VisualParent) Body=Body->VisualParent;
    FString Track=TEXT("space_loop");
    if(bEnding)Track=TEXT("night");
    else if(bEvensong)Track=TEXT("evensong");
    else if(Body)
    {
        if(Body->BodyId==TEXT("dawnhollow")&&Body->Altitude(Player->GetActorLocation())<4000)Track=Clocks.world<110?TEXT("dawn_song"):TEXT("village_day_loop");
        if(Body->BodyId==TEXT("hush")&&Body->Altitude(Player->GetActorLocation())<4000)Track=TEXT("nightwatch_loop");
        if(Body->BodyId==TEXT("slow_deep")&&Body->InWater(Player->GetActorLocation()))Track=TEXT("slow_deep_loop");
        if(Body->BodyId==TEXT("kernel")&&Body->Altitude(Player->GetActorLocation())<1000)Track=TEXT("kernel_loop");
        if(Body->BodyId==TEXT("seam")&&Body->Altitude(Player->GetActorLocation())<5000)Track=TEXT("seam_loop");
        if(Body->BodyId==TEXT("chorus")&&Body->Altitude(Player->GetActorLocation())<16000)Track=TEXT("chorus_resonance_loop");
    }
    if(Track==CurrentTrack)return;
    CurrentTrack=Track;
    const FString Path=String(Child(AssetData,TEXT("audio")),*Track);
    if(auto* Sound=LoadObject<USoundWave>(nullptr,*Path)){Music->SetSound(Sound);Music->FadeIn(3,0.7);}
    else Music->FadeOut(3,0);
}
