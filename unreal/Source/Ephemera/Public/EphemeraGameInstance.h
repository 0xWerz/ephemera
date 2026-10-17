#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "EphemeraData.h"
#include "EphemeraGameInstance.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKnowledgeAdded, const FString&, Id);

UCLASS()
class EPHEMERA_API UEphemeraGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;
    UPROPERTY(BlueprintReadOnly) TSet<FString> Knowledge;
    UPROPERTY(BlueprintReadOnly) TSet<FString> LoopFlags;
    UPROPERTY(BlueprintReadOnly) TArray<FString> LogEntries;
    UPROPERTY(BlueprintReadOnly) int32 LoopCount=0;
    UPROPERTY(BlueprintReadOnly) double WorldTime=0;
    UPROPERTY(BlueprintAssignable) FKnowledgeAdded OnKnowledgeAdded;
    EphemeraData::Object Story;
    bool Has(const FString& Flag) const;
    bool Check(const FString& Condition) const;
    void Set(const FString& Flag);
    void AddLog(const FString& Id);
    void StartLoop();
    void Save() const;
    void Load();
    void Apply(const EphemeraData::Object& Node);
};
