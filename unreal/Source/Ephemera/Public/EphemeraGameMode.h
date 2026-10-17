#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EphemeraGameMode.generated.h"

UCLASS()
class EPHEMERA_API AEphemeraGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AEphemeraGameMode();
    virtual void InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage) override;
};
