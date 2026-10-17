#include "EphemeraGameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
using namespace EphemeraData;

namespace
{
FString SavePath()
{
    return FParse::Param(FCommandLine::Get(),TEXT("EphemeraSmoke"))?
        FPaths::ProjectDir()/TEXT("Saved/Smoke/SaveGames/ephemera.json"):
        FPaths::ProjectSavedDir()/TEXT("SaveGames/ephemera.json");
}
}

void UEphemeraGameInstance::Init()
{
    Super::Init();
    Story=EphemeraData::Load(TEXT("story.json"));
    Load();
}
bool UEphemeraGameInstance::Has(const FString& Flag) const
{
    return Knowledge.Contains(Flag)||LoopFlags.Contains(Flag);
}
bool UEphemeraGameInstance::Check(const FString& Condition) const
{
    TArray<FString> Parts;
    Condition.ParseIntoArray(Parts,TEXT("&"),true);
    for (FString Part:Parts)
    {
        Part.TrimStartAndEndInline();
        if (Part.IsEmpty()) continue;
        const bool Negate=Part.StartsWith(TEXT("!"));
        if (Negate) Part.RightChopInline(1);
        bool Value=Part==TEXT("first_loop") ? LoopCount<=1 : Has(Part);
        if(Part==TEXT("morning"))Value=WorldTime<440;
        if(Part==TEXT("afternoon"))Value=WorldTime>=440&&WorldTime<880;
        if(Part==TEXT("evening"))Value=WorldTime>=880&&WorldTime<1320;
        if(Part==TEXT("night"))Value=WorldTime>=1320;
        if (Negate==Value) return false;
    }
    return true;
}
void UEphemeraGameInstance::Set(const FString& Flag)
{
    if (Flag.IsEmpty()||Has(Flag)) return;
    if (Flag.EndsWith(TEXT("_today"))) LoopFlags.Add(Flag);
    else { Knowledge.Add(Flag); Save(); }
}
void UEphemeraGameInstance::AddLog(const FString& Id)
{
    const Object Log=Child(Story,TEXT("log"));
    if (!Log.IsValid()||!Log->HasField(Id)||LogEntries.Contains(Id)) return;
    LogEntries.Add(Id);
    Save();
    OnKnowledgeAdded.Broadcast(Id);
}
void UEphemeraGameInstance::StartLoop()
{
    ++LoopCount;
    LoopFlags.Empty();
    if (LoopCount>1) { Set(TEXT("loop_known")); AddLog(TEXT("loop_time")); }
    Save();
}
void UEphemeraGameInstance::Apply(const Object& Node)
{
    for (const auto& F:Array(Node,TEXT("sets"))) Set(F->AsString());
    for (const auto& L:Array(Node,TEXT("log"))) AddLog(L->AsString());
}
void UEphemeraGameInstance::Save() const
{
    // Deliberately project-local, including packaged runs launched from the external disk.
    const FString Path=SavePath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    Object Data=MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Flags,Logs;
    for (const auto& F:Knowledge) Flags.Add(MakeShared<FJsonValueString>(F));
    for (const auto& L:LogEntries) Logs.Add(MakeShared<FJsonValueString>(L));
    Data->SetArrayField(TEXT("flags"),Flags);
    Data->SetArrayField(TEXT("log"),Logs);
    Data->SetNumberField(TEXT("loop_count"),LoopCount);
    FString Text;
    FJsonSerializer::Serialize(Data.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    const FString Temporary=Path+TEXT(".pending");
    if (FFileHelper::SaveStringToFile(Text,*Temporary))
        IFileManager::Get().Move(*Path,*Temporary,true,true);
}
void UEphemeraGameInstance::Load()
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text,*SavePath())) return;
    Object Data;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)||!Data.IsValid()) return;
    for (const auto& V:Array(Data,TEXT("flags"))) Knowledge.Add(V->AsString());
    for (const auto& V:Array(Data,TEXT("log"))) LogEntries.AddUnique(V->AsString());
    LoopCount=Number(Data,TEXT("loop_count"));
}
