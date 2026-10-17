#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "EphemeraInputConfig.generated.h"

class UEphemeraAudioSet;
class UInputAction;
class UInputMappingContext;

/** The Enhanced Input assets the on-foot player uses. Created by Tools/build_slice.py. */
UCLASS(BlueprintType)
class EPHEMERA_API UEphemeraInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputMappingContext> OnFootContext;
	/** 2D: X strafe, Y forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> Move;
	/** 2D mouse delta: X yaw, Y pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> LookMouse;
	/** 2D right stick, already dead-zoned: X yaw, Y pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> LookStick;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> Jump;
	/** 1D: jets up (positive) / down (negative). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> Jets;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> Boost;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> Lamp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> Interact;
	/** Toggles free flight (a look-around mode for inspecting the world). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> Fly;
	/** 1D: mouse wheel, changes free-flight speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TObjectPtr<UInputAction> FlySpeed;
};

/** Project-wide references, set in Config/DefaultGame.ini, so C++ never hard-codes an asset path. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Ephemera"))
class EPHEMERA_API UEphemeraSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UEphemeraInputConfig> InputConfig;
	/** The player's sounds (Tools/build_audio.py). Unset or missing: the game is simply silent. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio") TSoftObjectPtr<UEphemeraAudioSet> AudioSet;
	/** Body the player wakes on. */
	UPROPERTY(Config, EditAnywhere, Category = "Start") FName StartBody = TEXT("dawnhollow");
	/** Direction from that body's centre to the spawn point (planet-local). */
	UPROPERTY(Config, EditAnywhere, Category = "Start") FVector StartDirection = FVector(0.0, 0.0, 1.0);
	/**
	 * The time of the solar system's clock, in seconds, at which a game begins. 0 is first light at the Cradle: the
	 * ground is nearly black under a bright sky and the sun glares along the land. Mid-morning (the sun about 35
	 * degrees up, long shadows, everything lit) is a better first sight of the world.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Start") double StartTimeSeconds = 180.0;
};
