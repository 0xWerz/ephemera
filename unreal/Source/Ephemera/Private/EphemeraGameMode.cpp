#include "EphemeraGameMode.h"
#include "EphemeraWorld.h"
#include "EphemeraCharacter.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
AEphemeraGameMode::AEphemeraGameMode()
{
    DefaultPawnClass=AEphemeraCharacter::StaticClass();
    PlayerControllerClass=APlayerController::StaticClass();
}
void AEphemeraGameMode::InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage)
{
    Super::InitGame(MapName,Options,ErrorMessage);
    if(!TActorIterator<AEphemeraWorld>(GetWorld()))GetWorld()->SpawnActor<AEphemeraWorld>();
}
