#include "Player/EphemeraSliceGameMode.h"

#include "Player/EphemeraPlayer.h"

AEphemeraSliceGameMode::AEphemeraSliceGameMode()
{
	DefaultPawnClass = AEphemeraPlayer::StaticClass();
}
