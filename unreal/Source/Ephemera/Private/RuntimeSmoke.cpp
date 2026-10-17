#if WITH_DEV_AUTOMATION_TESTS
#include "EphemeraWorld.h"
#include "EphemeraCharacter.h"
#include "EphemeraGameInstance.h"
#include "EphemeraShip.h"
#include "CelestialBody.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace
{
struct FRuntimeSmoke
{
    double Started=0,PhaseClock=0;
    int32 Phase=0;
    bool Passed=true;
    FVector WalkStart=FVector::ZeroVector;
    TWeakObjectPtr<AEphemeraWorld> Universe;
    TArray<TSharedPtr<FJsonValue>> Checks;
    FString Directory=FPaths::ProjectDir()/TEXT("Saved/Smoke");

    void Check(const TCHAR* Name,bool Value)
    {
        auto Entry=MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"),Name);
        Entry->SetBoolField(TEXT("passed"),Value);
        Checks.Add(MakeShared<FJsonValueObject>(Entry));
        Passed&=Value;
        UE_LOG(LogTemp,Display,TEXT("Ephemera runtime check: %s = %s"),Name,Value?TEXT("PASS"):TEXT("FAIL"));
    }
    bool Finish()
    {
        auto Report=MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"),Passed);
        Report->SetArrayField(TEXT("checks"),Checks);
        FString Text;
        FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
        IFileManager::Get().MakeDirectory(*Directory,true);
        if(!FFileHelper::SaveStringToFile(Text,*(Directory/TEXT("runtime.json"))))Passed=false;
        FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
        return false;
    }
    bool Tick(float Delta)
    {
        if(Started==0)Started=FPlatformTime::Seconds();
        if(FPlatformTime::Seconds()-Started>600)
        {
            Check(TEXT("World and rendered screenshot completed within ten minutes"),false);
            return Finish();
        }
        if(!Universe.IsValid()&&GEngine)
            for(const auto& Context:GEngine->GetWorldContexts())
                if(auto* World=Context.World();World&&World->IsGameWorld()&&World->HasBegunPlay())
                {
                    TActorIterator<AEphemeraWorld> It(World);
                    if(It&&It->Player&&It->State)Universe=*It;
                }
        if(!Universe.IsValid())return true;
        auto* U=Universe.Get();
        auto* P=U->Player.Get();
        if(Phase==0)
        {
            const auto WorldData=EphemeraData::Load(TEXT("world.json"));
            int32 ExpectedInstances=0,Instances=0,Colliders=0;
            for(const auto& V:EphemeraData::Array(WorldData,TEXT("placements")))
                ExpectedInstances+=V->AsObject()->HasField(TEXT("instances"))?
                    EphemeraData::Array(V->AsObject(),TEXT("instances")).Num():1;
            for(TActorIterator<AActor> It(U->GetWorld());It;++It)
            {
                TInlineComponentArray<UPrimitiveComponent*> Components(*It);
                for(auto* Component:Components)
                {
                    if(auto* Batch=Cast<UHierarchicalInstancedStaticMeshComponent>(Component))
                        Instances+=Batch->GetInstanceCount();
                    if(Component->ComponentHasTag(TEXT("EphemeraCollision")))++Colliders;
                }
            }
            Check(TEXT("All fifteen celestial bodies spawned"),U->Bodies.Num()==15);
            UE_LOG(LogTemp,Display,TEXT("Ephemera runtime inventory: %d instances, %d colliders"),Instances,Colliders);
            Check(TEXT("Every exported scenery instance spawned"),Instances==ExpectedInstances);
            Check(TEXT("Every exported collider spawned"),Colliders==EphemeraData::Array(WorldData,TEXT("colliders")).Num());
            Check(TEXT("Player spawned on Dawnhollow"),P->Frame&&P->Frame->BodyId==TEXT("dawnhollow"));
            Check(TEXT("Ship, HUD, and local player controller exist"),U->Ship&&U->HUD&&P->GetController());
            if(!P->Frame||!Cast<APlayerController>(P->GetController())||!U->HUD||!U->Ship)return Finish();
            ++Phase;
        }
        else if(Phase==1&&U->Clocks.proper>=2)
        {
            Check(TEXT("Player settles on collision geometry"),P->GetCharacterMovement()->IsMovingOnGround());
            Check(TEXT("Player remains finite and alive"),!P->GetActorLocation().ContainsNaN()&&P->Health>0);
            WalkStart=P->Frame->GetActorTransform().InverseTransformPosition(P->GetActorLocation());
            PhaseClock=U->Clocks.proper;
            CastChecked<APlayerController>(P->GetController())->InputKey(
                FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1));
            ++Phase;
        }
        else if(Phase==2&&U->Clocks.proper-PhaseClock>=0.3)
        {
            CastChecked<APlayerController>(P->GetController())->InputKey(
                FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0));
            const FVector WalkEnd=P->Frame->GetActorTransform().InverseTransformPosition(P->GetActorLocation());
            Check(TEXT("Keyboard W walks relative to the orbiting world"),FVector::Distance(WalkStart,WalkEnd)>20);
            U->OpenLog();
            Check(TEXT("Inheritance opens and pauses the simulation"),U->PanelMode==TEXT("log")&&U->bFrozen);
            U->Choose(0);
            Check(TEXT("Closing the panel restores controls"),U->PanelMode.IsEmpty()&&!U->bFrozen&&P->bControls);
            U->ResetLoop();
            Check(TEXT("Loop reset restores dawn, oxygen, and fuel"),U->Clocks.proper==0&&P->Oxygen==1&&P->Fuel==1&&P->Frame);
            ++Phase;
        }
        else if(Phase==3&&U->Clocks.proper>=1)
        {
            Check(TEXT("A game viewport is rendering"),GEngine&&GEngine->GameViewport);
            IFileManager::Get().MakeDirectory(*Directory,true);
            FScreenshotRequest::RequestScreenshot(Directory/TEXT("spawn.png"),true,false,false,FIntRect(),true);
            ++Phase;
        }
        else if(Phase==4&&IFileManager::Get().FileSize(*(Directory/TEXT("spawn.png")))>0)
        {
            Check(TEXT("Rendered spawn screenshot saved on external disk"),true);
            return Finish();
        }
        return true;
    }
};
}

void StartEphemeraRuntimeSmoke()
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("EphemeraSmoke")))return;
    auto Smoke=MakeShared<FRuntimeSmoke>();
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [Smoke](float Delta){return Smoke->Tick(Delta);}),0.1f);
}
#endif
