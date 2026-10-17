#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EphemeraInputBuilder.generated.h"

/** Creates the Enhanced Input assets (actions, the on-foot mapping context, the config) as saved content. */
UCLASS()
class EPHEMERAEDITOR_API UEphemeraInputBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Writes IA_* actions, IMC_OnFoot and DA_InputConfig under Folder (for example "/Game/Ephemera/Input").
	 * Keyboard + mouse and gamepad follow Outer Wilds' layout. Returns the config's object path.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static FString BuildInputAssets(const FString& Folder);
};
