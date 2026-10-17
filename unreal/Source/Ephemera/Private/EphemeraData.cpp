#include "EphemeraData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

EphemeraData::Object EphemeraData::Load(const FString& Filename)
{
    FString Text;
    Object Result;
    if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir()/TEXT("Data")/Filename)))
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
    return Result;
}
