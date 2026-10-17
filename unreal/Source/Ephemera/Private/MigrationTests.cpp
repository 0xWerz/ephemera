// These tests inspect imported assets through editor-only data, so they exist only in editor builds.
#if WITH_EDITOR
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "EphemeraData.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundWave.h"
#include "SimulationMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEphemeraCoordinateTest,"Ephemera.Migration.Coordinates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEphemeraCoordinateTest::RunTest(const FString& Parameters)
{
    using namespace EphemeraData;
    Object O=MakeShared<FJsonObject>();
    auto Vector=[](double X,double Y,double Z)
    {
        return TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueNumber>(X),MakeShared<FJsonValueNumber>(Y),MakeShared<FJsonValueNumber>(Z)};
    };
    O->SetArrayField(TEXT("p"),Vector(1,2,-3));
    O->SetArrayField(TEXT("x"),Vector(1,0,0));
    O->SetArrayField(TEXT("y"),Vector(0,1,0));
    O->SetArrayField(TEXT("z"),Vector(0,0,1));
    const FTransform Pose=Transform(O);
    TestTrue(TEXT("Metres, handedness, and up axis"),Pose.GetLocation().Equals(FVector(300,100,200),0.001));
    TestTrue(TEXT("Identity rotation survives conversion"),Pose.GetRotation().Equals(FQuat::Identity,0.00001));
    TestTrue(TEXT("Unit scale survives conversion"),Pose.GetScale3D().Equals(FVector::OneVector,0.00001));
    EphemeraSim::Clocks Clock;
    Clock.Advance(0.01,1);
    TestEqual(TEXT("Surface proper time"),Clock.proper,0.01);
    TestEqual(TEXT("Surface world time"),Clock.world,0.01);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEphemeraContentTest,"Ephemera.Migration.ImportedContent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEphemeraContentTest::RunTest(const FString& Parameters)
{
    using namespace EphemeraData;
    const Object World=Load(TEXT("world.json")),Assets=Load(TEXT("assets.json"));
    if(!TestTrue(TEXT("Exported world exists"),World.IsValid())||
       !TestTrue(TEXT("Imported asset manifest exists"),Assets.IsValid()))return false;
    const Object Meshes=Child(Assets,TEXT("meshes"));
    for(const auto& V:Array(World,TEXT("meshes")))
    {
        const FString Id=String(V->AsObject(),TEXT("id")),Path=String(Meshes,*Id);
        TestFalse(*FString::Printf(TEXT("Mapping for %s"),*Id),Path.IsEmpty());
        if(!Path.IsEmpty())
        {
            auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
            TestNotNull(*Id,Mesh);
            if(Mesh&&Boolean(V->AsObject(),TEXT("collision")))
            {
                TestFalse(*FString::Printf(TEXT("%s collision uses complete geometry"),*Id),Mesh->GetNaniteSettings().bEnabled);
                TestTrue(*FString::Printf(TEXT("%s uses triangle collision"),*Id),
                    Mesh->GetBodySetup()&&Mesh->GetBodySetup()->CollisionTraceFlag==CTF_UseComplexAsSimple);
            }
            else if(Mesh)
                for(const auto& Slot:Mesh->GetStaticMaterials())
                {
                    auto* Material=Slot.MaterialInterface.Get();
                    TestNotNull(*FString::Printf(TEXT("%s material slot"),*Id),Material);
                    if(Material)
                    {
                        TestTrue(*FString::Printf(TEXT("%s material supports instancing"),*Id),
                            Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
                        if(Mesh->GetNaniteSettings().bEnabled)
                            TestTrue(*FString::Printf(TEXT("%s material supports Nanite"),*Id),Material->GetUsageByFlag(MATUSAGE_Nanite));
                    }
                }
        }
    }
    const Object Audio=Child(Assets,TEXT("audio"));
    if(TestTrue(TEXT("Imported audio manifest exists"),Audio.IsValid()))
    {
        for(const auto& Pair:Audio->Values)
            TestNotNull(*Pair.Key,LoadObject<USoundWave>(nullptr,*Pair.Value->AsString()));
        for(const TCHAR* Id:{TEXT("dawn_song"),TEXT("space_loop"),TEXT("village_day_loop"),
            TEXT("stem_hum_voice"),TEXT("stem_glass_harmonium"),TEXT("stem_reed_horn"),TEXT("stem_silk_string"),TEXT("stem_hand_drum")})
            TestFalse(*FString::Printf(TEXT("Music and antenna audio: %s"),Id),String(Audio,Id).IsEmpty());
    }
    return !HasAnyErrors();
}
#endif
#endif
