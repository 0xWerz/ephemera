#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EphemeraSliceGameMode.generated.h"

/** Game mode for the authored solar-system map: spawns the Dayling; the frame subsystem does the rest. */
UCLASS()
class EPHEMERA_API AEphemeraSliceGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEphemeraSliceGameMode();
};
