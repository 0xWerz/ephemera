#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EphemeraSkyRig.generated.h"

class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UPostProcessComponent;
class UVolumetricCloudComponent;
class UExponentialHeightFogComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class AEphemeraBody;

/**
 * The one sun, sky and exposure for the whole system. It follows the reference frame: the sun
 * light points from the star, and the scattering atmosphere wraps whichever planet you are at
 * (airless bodies and open space get none). Light is in physical units.
 */
UCLASS()
class EPHEMERA_API AEphemeraSkyRig : public AActor
{
	GENERATED_BODY()

public:
	AEphemeraSkyRig();

	/** Illuminance of the star at Dawnhollow's distance, in lux. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Sun") float SunLux = 95000.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Sun") FLinearColor SunColor = FLinearColor(1.0f, 0.93f, 0.82f);
	/** Angular diameter of the star's disc as seen from the ground, degrees. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Sun") float SunAngleDeg = 4.3f;

	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UDirectionalLightComponent> Sun;
	/**
	 * Night is not the absence of light. Shine is the sunlight a neighbouring world throws back
	 * (the moon over Dawnhollow, Dawnhollow over its moon); NightFill is everything fainter, the
	 * stars and the glow of the air, as one dim light from overhead.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UDirectionalLightComponent> Shine;
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UDirectionalLightComponent> NightFill;
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UPostProcessComponent> PostProcess;
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UVolumetricCloudComponent> Clouds;
	/** The fixed stars: a huge inside-out sphere centred on the camera. Its material is set by the slice build. */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UStaticMeshComponent> Stars;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Stars") TObjectPtr<UMaterialInterface> StarMaterial;
	/**
	 * Mist you can see into: the engine's fog cells (switched on by Fog, which has no fog of its
	 * own to speak of) filled by MistVolume, a box that goes with the camera. Its material, set by
	 * the slice build, gives the air's thickness at each point from its height over the sea.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UStaticMeshComponent> MistVolume;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Mist") TObjectPtr<UMaterialInterface> MistMaterial;
	/**
	 * What hangs in the air: dust and pollen, a mote to every few cubic metres round the eye, carried
	 * on the wind that moves the grass. They are lit as the air they hang in is lit: bright where the
	 * sun reaches them, faint in shade, and caught in the lamp's beam by night. The mesh (a cloud of
	 * small squares) and the material that turns and moves them are made by the slice build.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UStaticMeshComponent> Motes;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Air") TObjectPtr<UStaticMesh> MotesMesh;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Air") TObjectPtr<UMaterialInterface> MotesMaterial;
	/**
	 * The player's own effects (the jets' flame, the dust a landing throws up). They are kept here
	 * because this actor is in the map: what it holds is packaged with the map, and the player,
	 * who is made when the game starts, asks it for them.
	 */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Player") TObjectPtr<UMaterialInterface> FlameMaterial;
	/**
	 * What the eye sees with the sea over it (a post-process, made by the slice build): the water's
	 * own colour and murk with distance, light fading with depth, the surface seen from below (the
	 * sky through a round window straight up, a mirror of the deep elsewhere) and the light the waves
	 * focus onto the bottom.
	 */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Sea") TObjectPtr<UMaterialInterface> UnderwaterMaterial;
	/** Whether the eye is under the sea now. */
	bool IsEyeUnderwater() const { return bUnderNow; }
	UPROPERTY(EditAnywhere, Category = "Ephemera|Player") TObjectPtr<UStaticMesh> PuffMesh;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Player") TObjectPtr<UMaterialInterface> PuffMaterial;

	/**
	 * Named skies: each a set of the same key=value overrides -EphemeraSky takes. "clear" has no
	 * cloud; "fronts" is weather systems over half the world, overcast beneath them and clear
	 * between; "overcast" one unbroken deck; "broken" cloud with sun between.
	 */
	static const TMap<FName, FString>& WeatherPresets();
	/** Changes the sky to a named one, now. An unknown name leaves it as it is. */
	void SetWeather(FName Name);
	FName GetWeather() const { return Weather; }
	/** How far into night the eye is: 0 with the sun up, 1 in full night. */
	float NightAmount() const { return NightNow; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void UpdateSky();
	void FitAtmosphere(const AEphemeraBody* Body);
	/** Lights the night at the eye; returns how far the eye is into night there, 0 (sun up) to 1. */
	float UpdateNight(const class UEphemeraFrameSubsystem& Frames, const FVector& Eye, const FVector& ToStar);
	/** Reads "key=value,key=value" into Tuning, over whatever is there. */
	void ReadTuning(const FString& Spec);
	/** Puts the parts of Tuning that belong to the lights and the camera into effect. */
	void ApplyTuning();

	FName Weather = NAME_None;
	/** What -EphemeraSky asked for: laid over every weather, so a render's own overrides survive a change of sky. */
	FString CommandLineTuning;

	TWeakObjectPtr<const AEphemeraBody> AtmosphereBody;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> StarInstance;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> CloudInstance;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> MistInstance;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> MotesInstance;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> UnderwaterInstance;
	/** The night's own light where the eye is, lux (the neighbour world's and the sky's), as UpdateNight last set it. */
	float ShineLuxNow = 0.f;
	float FillLuxNow = 0.f;
	float SkyStateSent = -1.f;
	bool bUnderNow = false;
	/** Where the eye was a frame ago, and how fast it has been going (cm/s, eased): motes are not drawn past a run. */
	FVector EyeBefore = FVector::ZeroVector;
	float EyeSpeed = 0.f;
	FDelegateHandle FrameHandle;

	/**
	 * The sky's settings where they differ from the built-in ones, as key -> value: the current
	 * weather's, then anything given on the command line (-EphemeraSky=key=value,key=value, which
	 * lets a render try a different sky without rebuilding anything).
	 */
	TMap<FString, float> Tuning;
	/** Haze as seen from orbit, and the ground mist added to it as the camera comes down (both per km). */
	float ClearHaze = 0.f;
	float Mist = 0.f;
	float HazeNow = -1.f;
	/** How far into night the eye was at the last update (0 sun up, 1 full night): the mist is thinned by it. */
	float NightNow = 0.f;
	double NightSaidAt = 0.0;
	/** The neighbour whose own shadows are off while its light throws them (see UpdateNight). */
	TWeakObjectPtr<AEphemeraBody> ShineUnshadowed;
	float Tuned(const TCHAR* Key, float Default) const;
	/** The film curve, colour and lens of the picture (each number can be tried from -EphemeraSky). */
	void ApplyGrade();
};
