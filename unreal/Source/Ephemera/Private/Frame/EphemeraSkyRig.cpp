#include "Frame/EphemeraSkyRig.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "Frame/EphemeraBody.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Frame/EphemeraFrameSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/EphemeraUserSettings.h"

namespace
{
	// Unreal's defaults describe Earth: a 60 km atmosphere whose density falls off over 8 km
	// (Rayleigh) and 1.2 km (Mie). Our worlds are hundreds of metres across, so the same sky needs
	// the same optical depth packed into a thinner shell: densities scale up by the ratio of the
	// heights and the falloff distances scale down by it.
	constexpr float EarthAtmosphereKm = 60.f;
	constexpr float EarthRayleighFalloffKm = 8.f;
	constexpr float EarthMieAbsorption = 0.000444f;
	constexpr float EarthMieFalloffKm = 1.2f;
	// Exposure limits (EV100). Under a sky the eye adapts all the way down to starlight. In space
	// the camera is set for sunlit rock and stays there, as on every real space photograph: the lit
	// side of a world holds its detail and its night side falls to black, however little of the
	// frame the world fills.
	constexpr float GroundExposureFloorEV = -2.f;
	constexpr float GroundExposureCeilingEV = 15.5f;
	// In space the exposure is not metered at all, it is set: a meter would darken the land every
	// time the sun's glint on the sea or the bright limb came into frame.
	constexpr float SpaceExposureEV = 13.0f;
	// The look of the picture where no weather or render says otherwise. (Measured against stills of
	// Death Stranding and Alan Wake 2, docs/research/look_gap_vs_aw2.md: an open sky belongs two
	// stops and more above the ground under it, the ground a little under middle grey.)
	constexpr float GroundExposureBias = 0.3f;
	constexpr float SpaceExposureBias = 0.5f;
	// By night the picture is held a stop and more under what the meter asks for: night is dark,
	// and a place you can still find your way across (the owner: "way bright and not a dark vibe at all").
	constexpr float NightExposureBias = -0.5f;
	// And the eye opens no further than this for the dark (EV100). It is what makes night dark: at
	// this setting ground under the full moon (1.2 lux here) stands about two stops under middle
	// grey and ground under the stars alone three and a half, there to be seen and plainly night.
	// Left to open to the engine's -2, the meter lifted moonlit ground to the grey of a dull day,
	// and whatever was brighter than the ground (cloud, the moon) burned white.
	constexpr float NightExposureFloorEV = -1.0f;
	constexpr float GroundMeterLow = 35.f;
	constexpr float GroundMeterHigh = 90.f;
	constexpr float SpaceMeterLow = 55.f;
	constexpr float SpaceMeterHigh = 95.f;
	constexpr float GroundSaturation = 0.82f;
	constexpr float GroundBloom = 0.30f;
	// By day the engine's local exposure draws the highlights a little toward the middle, so cloud
	// tops keep their shape; shadows are left alone (lifted, the whole picture goes milky: tried).
	constexpr float DayHighlightContrast = 0.78f;
	constexpr float DayShadowContrast = 1.0f;
	// The sky's share of the light, as the engine's capture of the sky itself measures it. With the air at
	// Earth's own optical depth (rayleigh_scale 0.0331, Tools/build_slice.py; it was a quarter of that, a
	// thin navy sky) and this, shade on open ground is lit by the sky as in a photograph: darker than sun
	// by a few stops, never black. (2026-10-17, A/B at 1.2 / 2.5 / 4: at 1.2 shade measured 0% of the sunlit
	// value after the tone curve, a black wedge on every slope facing away from the sun; at 4 the land went
	// flat and the sea glared.)
	constexpr float SkyLightScale = 2.5f;
	constexpr float AirDepthScale = 0.38f;
	// Mist you can see into (see the Fog component): how far from the eye the air is drawn as
	// cells of fog, its density at sea level and the height over which it thins, where no weather
	// says otherwise. Thin: at three times this (the first try) a low sun lit it into a grey wash
	// over every mountain, and the owner's first words on seeing it were "grey ish washed out".
	constexpr float MistReachM = 700.f;
	constexpr float MistPerKm = 0.12f;
	constexpr float MistHeightM = 90.f;
	// Night light, in lux. The eye's limit is EV -2, where ground as dark as Dawnhollow's turf and
	// rock (it throws back under a tenth of what falls on it) is mid grey at a little over one lux.
	// These put a night with no moon two stops under that and a night under the full moon most of
	// a stop under: dark, plainly night, and a place you can find your way across.
	// A neighbour's light worked out physically would be far more than that (Hush fills 150 times
	// the sky our Moon does, Dawnhollow over Hush 2,000 times), and the eye would simply adapt and
	// call it day. So it is compressed: half of ShineCapLux at ShineHalfLux of physical light, never
	// more than the cap.
	constexpr float ShineCapLux = 1.2f;
	// How strongly the motes in the air show (0 none): a few are seen at a time, not a snowfall.
	constexpr float MoteAmount = 0.75f;
	constexpr float ShineHalfLux = 52.f;
	constexpr float NightFillLux = 0.45f;
}

AEphemeraSkyRig::AEphemeraSkyRig()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->bAtmosphereSunLight = true;
	Sun->Intensity = SunLux;
	Sun->LightSourceAngle = SunAngleDeg;
	Sun->bUseTemperature = false;
	Sun->SetCastShadows(true);
	// The star is a real object in the sky; the atmosphere must not paint a second disc over it.
	Sun->AtmosphereSunDiskColorScale = FLinearColor::Black;
	// Small things (grass blades, twigs, pebbles) get a sharp screen-space shadow where they touch.
	Sun->ContactShadowLength = 0.06f;
	// The star is a disc 4.3 degrees wide, eight times the Sun's: shadows drawn truly to that have
	// edges so wide that a stone's shadow, or a ledge's, dissolves a stride from what casts it, and
	// the land loses its modelling. The shadows are drawn as if from a disc a third as wide (the
	// star in the sky and its glint on the sea keep their size).
	Sun->ShadowSourceAngleFactor = 0.35f;
	// Worlds are small and the camera can be far above them: let the shadow clipmaps reach.
	Sun->DynamicShadowDistanceMovableLight = 3000000.f;
	// Per-pixel atmosphere transmittance stays off: with it on, the engine leaves these clouds unlit.
	Sun->bCastCloudShadows = false;
	// Cloud shadows: the engine's map is 512 texels across the whole planet, 23 m a texel, and at a low
	// sun each texel lies 50 to 140 m long on the ground: blotches. Three times the texels, and twice
	// the steps through the cloud (the default 16 left bands that moved with the camera).
	// (docs/research/night_cloudshadows_stability.md, section 2.)
	// (2026-10-17: 3x and 2x cost 7 ms of a 26 ms frame on the RTX 3060; 2x and 1.5x, with the cloud base now
	// above the hills and a thinner deck, measured against them for the blotches and bands that started this.)
	Sun->CloudShadowMapResolutionScale = 2.f;
	Sun->CloudShadowRaySampleCountScale = 1.5f;
	// Real cumulus shadows keep a little of the direct light (it is scattered on through the cloud).
	Sun->CloudShadowStrength = 0.88f;
	// Cloud shadows on the ground only: cast through the air as well they drew shafts and bands up the sky from
	// the horizon ("shades and lighting waves ... coming from the ground up").
	Sun->CloudShadowOnAtmosphereStrength = 0.f;
	Sun->CloudShadowExtent = 6.12f;

	// Where several suns compete (water, glass, fog), the star is the one that counts.
	Sun->ForwardShadingPriority = 1;

	Shine = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Shine"));
	Shine->SetupAttachment(Root);
	Shine->SetMobility(EComponentMobility::Movable);
	Shine->bAtmosphereSunLight = false;
	Shine->Intensity = 0.f;
	Shine->bUseTemperature = false;
	Shine->SetCastShadows(false);
	Shine->ContactShadowLength = 0.06f;
	Shine->DynamicShadowDistanceMovableLight = 3000000.f;
	// Night light does not light the mist: with the eye opened up for the dark, a moonlit haze is as
	// bright on the screen as a sunlit one, and night stops being dark.
	Shine->VolumetricScatteringIntensity = 0.f;
	Shine->SetVisibility(false);

	NightFill = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("NightFill"));
	NightFill->SetupAttachment(Root);
	NightFill->SetMobility(EComponentMobility::Movable);
	NightFill->bAtmosphereSunLight = false;
	NightFill->Intensity = 0.f;
	NightFill->bUseTemperature = false;
	NightFill->SetCastShadows(false);
	NightFill->SetLightColor(FLinearColor(0.56f, 0.70f, 1.0f));
	NightFill->VolumetricScatteringIntensity = 0.f;
	NightFill->SetVisibility(false);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);
	Atmosphere->SetMobility(EComponentMobility::Movable);
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
	// More ray-march steps: the air is a thin shell and its haze layer is only metres deep.
	Atmosphere->TraceSampleCountScale = 2.f;

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);
	Clouds->SetVisibility(false);
	// Trace far enough to see a world's whole cloud deck from orbit.
	Clouds->TracingStartMaxDistance = 600.f;
	Clouds->TracingMaxDistance = 120.f;
	// The layer is only a couple of hundred metres thick: shadow rays need not reach far, and
	// keeping them short puts their few samples where the cloud is.
	Clouds->ShadowTracingDistance = 1.2f;
	Clouds->ViewSampleCountScale = 1.5f;
	Clouds->ShadowViewSampleCountScale = 1.5f;
	// Cloud bases see little sky: they are lit from below only by what the ground throws back.
	Clouds->SkyLightCloudBottomOcclusion = 0.45f;
	// Sunlight reaching each bit of cloud is filtered by the air it has crossed, and stopped by the
	// planet: clouds redden toward dusk and go dark on the night side. (This is the per-sample
	// option on the clouds; the sun's own per-pixel option stays off, as it leaves clouds unlit.)
	Clouds->bUsePerSampleAtmosphericLightTransmittance = true;

	// Mist you can see into. The engine draws fog as cells of air in front of the camera, each lit
	// by the sun through its shadows (the land's and the clouds'), by the sky and by a lamp: so mist
	// lies in layers that distance steps through, a cliff throws a shadow into it, and a beam shows.
	// Its own fog thins along the world's Z axis, which on a round world is "up" at one place only;
	// so it is given next to no density and is here only to switch the cells on. The mist itself
	// comes from MistVolume: a box that goes with the camera, whose material (built by the slice
	// build) says how thick the air is at each point from its height above the world's sea level.
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Fog->SetMobility(EComponentMobility::Movable);
	Fog->FogDensity = 0.0000002f;
	Fog->FogHeightFalloff = 0.001f;
	Fog->SecondFogData.FogDensity = 0.f;
	// (And it must never be seen. Thin as it is, it thickens without limit down the world's Z axis,
	// and a ray to the sky in that direction is long enough to gather all of it: half the sky went
	// black. So it may hide next to nothing, and stops well short of the sky.)
	Fog->FogMaxOpacity = 0.0001f;
	Fog->FogCutoffDistance = MistReachM * 300.f;
	Fog->bEnableVolumetricFog = true;
	Fog->VolumetricFogDistance = MistReachM * 100.f;
	Fog->VolumetricFogScatteringDistribution = 0.3f;
	Fog->VolumetricFogAlbedo = FColor(230, 235, 242);
	Fog->VolumetricFogExtinctionScale = 1.f;
	Fog->SetVisibility(false);

	MistVolume = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MistVolume"));
	MistVolume->SetupAttachment(Root);
	MistVolume->SetMobility(EComponentMobility::Movable);
	MistVolume->SetUsingAbsoluteRotation(true);
	MistVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MistVolume->SetCastShadow(false);
	MistVolume->bAffectDistanceFieldLighting = false;
	MistVolume->bAffectDynamicIndirectLighting = false;
	MistVolume->bVisibleInRayTracing = false;
	MistVolume->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		MistVolume->SetStaticMesh(Cube.Object);
	}
	// The engine cube is a metre across; the box reaches as far as the fog cells do, every way.
	MistVolume->SetWorldScale3D(FVector(MistReachM * 2.f));
	MistVolume->SetVisibility(false);

	Motes = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Motes"));
	Motes->SetupAttachment(Root);
	Motes->SetMobility(EComponentMobility::Movable);
	Motes->SetUsingAbsoluteRotation(true);
	Motes->SetUsingAbsoluteScale(true);
	Motes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Motes->SetCastShadow(false);
	Motes->bAffectDistanceFieldLighting = false;
	Motes->bAffectDynamicIndirectLighting = false;
	Motes->bVisibleInRayTracing = false;
	Motes->SetCanEverAffectNavigation(false);
	Motes->SetVisibility(false);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	// "Down" is toward a planet, not world -Z: the engine's flat-coloured lower hemisphere would cut
	// every reflection in half along an arbitrary plane.
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetCastShadows(true);

	Stars = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Stars"));
	Stars->SetupAttachment(Root);
	Stars->SetMobility(EComponentMobility::Movable);
	Stars->SetUsingAbsoluteRotation(true);
	Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stars->SetCastShadow(false);
	Stars->bAffectDistanceFieldLighting = false;
	Stars->bAffectDynamicIndirectLighting = false;
	Stars->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Stars->SetStaticMesh(Sphere.Object);
	}
	// 5,000 km: beyond every body in the system (the far plane is infinite). The engine sphere is 50 cm in radius.
	Stars->SetWorldScale3D(FVector(5.0e8 / 50.0));

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
	FPostProcessSettings& S = PostProcess->Settings;
	// Eye adaptation across a physical range: noon ground (~EV 15) down to a moonless night.
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Histogram;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = GroundExposureFloorEV;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = GroundExposureCeilingEV;
	S.bOverride_AutoExposureSpeedUp = true;
	S.AutoExposureSpeedUp = 2.5f;
	S.bOverride_AutoExposureSpeedDown = true;
	S.AutoExposureSpeedDown = 1.0f;
	// A real lens: the sun blooms and throws a faint flare; nothing else is bright enough to.
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = GroundBloom;
	S.bOverride_BloomMethod = true;
	S.BloomMethod = BM_SOG;
	// No ghost flares: Unreal's are large soft blobs that read as a video-game effect, not a lens.
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.f;
	// Meter on the bright part of the frame, as a camera does: a sunlit planet against black space
	// is exposed for the planet, not averaged up into a white ball.
	S.bOverride_AutoExposureLowPercent = true;
	S.AutoExposureLowPercent = SpaceMeterLow;
	S.bOverride_AutoExposureHighPercent = true;
	S.AutoExposureHighPercent = SpaceMeterHigh;
	// No motion blur: the sky turns with the reference frame and the stars smear into dashes.
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.f;
	S.bOverride_FilmGrainIntensity = true;
	S.bOverride_VignetteIntensity = true;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = GroundExposureBias;
	S.bOverride_FilmSlope = true;
	S.bOverride_FilmToe = true;
	S.bOverride_FilmShoulder = true;
	S.bOverride_FilmBlackClip = true;
	S.FilmBlackClip = 0.f;
	S.bOverride_FilmWhiteClip = true;
	S.FilmWhiteClip = 0.04f;
	S.bOverride_ColorContrast = true;
	S.bOverride_LocalExposureHighlightContrastScale = true;
	S.LocalExposureHighlightContrastScale = DayHighlightContrast;
	S.bOverride_LocalExposureShadowContrastScale = true;
	S.LocalExposureShadowContrastScale = DayShadowContrast;
	S.bOverride_ColorSaturation = true;
	S.bOverride_ColorSaturationShadows = true;
	S.bOverride_ColorSaturationHighlights = true;
	S.bOverride_ColorGainHighlights = true;
	S.bOverride_ColorGammaShadows = true;
	S.bOverride_ColorGain = true;
	S.bOverride_ColorGainShadows = true;
	S.bOverride_ColorCorrectionShadowsMax = true;
	// Sky light is stopped by whatever stands in its way: none is let leak into hollows, joints and
	// the lee of rocks to keep them readable. Occluded shade is what gives a grey day its form.
	S.bOverride_LumenSkylightLeaking = true;
	S.LumenSkylightLeaking = 0.f;
	// The numbers themselves are set in ApplyGrade, where a render can try others.
	ApplyGrade();
}

void AEphemeraSkyRig::ApplyGrade()
{
	FPostProcessSettings& S = PostProcess->Settings;
	// The film curve: a firm toe and a little contrast for depth in the shade (with matt ground
	// there is no black between grass blades left to protect, and a soft curve reads as haze over
	// the lens), and a soft shoulder so a bright sky rolls off under white with its clouds intact.
	S.FilmSlope = Tuned(TEXT("slope"), 0.92f);
	S.FilmToe = Tuned(TEXT("toe"), 0.58f);
	S.FilmShoulder = Tuned(TEXT("shoulder"), 0.26f);
	const float Contrast = Tuned(TEXT("contrast"), 1.08f);
	S.ColorContrast = FVector4(Contrast, Contrast, Contrast, 1.0);
	// Colour: dark ground gives up some of its colour (shade on moss is green-grey, not olive), the
	// sky keeps its own; highlights lean a little cool and shadows a little cool-green, which is
	// what separates lit cloud, haze and shade into different hues instead of one.
	const float Saturation = Tuned(TEXT("sat"), GroundSaturation);
	S.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0);
	const float InShadows = Tuned(TEXT("satshadow"), 0.85f);
	S.ColorSaturationShadows = FVector4(InShadows, InShadows, InShadows, 1.0);
	const float InHighlights = Tuned(TEXT("sathigh"), 1.15f);
	S.ColorSaturationHighlights = FVector4(InHighlights, InHighlights, InHighlights, 1.0);
	S.ColorGainHighlights = FVector4(Tuned(TEXT("highr"), 0.97f), 1.0, Tuned(TEXT("highb"), 1.04f), 1.0);
	S.ColorGammaShadows = FVector4(Tuned(TEXT("shadowr"), 0.96f), 1.0, Tuned(TEXT("shadowb"), 1.03f), 1.0);
	// The lens: edges a third of a stop down, grain fine enough to be felt and not seen.
	// (How far bounce light is followed is left at the engine's own 200 m. Six times that was tried,
	// for valley sides to shade valley floors, and went out to play untested in motion: the owner
	// saw the lighting "flickering and roughly switching". It comes back only with a moving test.)
	S.LumenSkylightLeaking = Tuned(TEXT("skyleak"), 0.f);
	S.VignetteIntensity = Tuned(TEXT("vignette"), 0.38f);
	S.FilmGrainIntensity = Tuned(TEXT("grain"), 0.14f);
	S.BloomIntensity = Tuned(TEXT("bloom"), GroundBloom);
}

float AEphemeraSkyRig::Tuned(const TCHAR* Key, const float Default) const
{
	const float* Value = Tuning.Find(Key);
	return Value ? *Value : Default;
}

const TMap<FName, FString>& AEphemeraSkyRig::WeatherPresets()
{
	// Every cloudy sky thins its cloud out with distance (FarStart..FarEnd metres from the camera).
	// A deck thick enough to be an overcast sky from the ground would wrap the planet in solid
	// white from orbit, where what is wanted is weather: soft sheets with the land showing between
	// and through them. Everything seen from the ground lies inside FarStart.
	static const FString Far = TEXT(",cloud.FarStart=2500,cloud.FarEnd=9000,cloud.FarVeil=1.0,cloud.FarThin=0.06");
	static const TMap<FName, FString> Presets = {
		{TEXT("clear"), TEXT("clouds=0,cloudshadow=0,fog=0.10")},
		{TEXT("fronts"), TEXT("clouds=1,cloudshadow=1,fog=0.12,cloud.Coverage=0.5,cloud.Soft=0.22,cloud.Fill=1.2,cloudbase=300,cloudthick=260,cloud.Density=0.05") + Far},
		{TEXT("overcast"), TEXT("clouds=1,cloudshadow=1,fog=0.28,cloud.Coverage=1.05,cloud.Soft=0.25,cloud.Fill=1.2,cloudbase=420,cloudthick=450,cloud.Density=0.03") + Far},
		{TEXT("broken"), TEXT("clouds=1,cloudshadow=1,fog=0.18,cloud.Coverage=0.78,cloud.Soft=0.3,cloud.Fill=1.0,cloudbase=450,cloudthick=420,cloud.Density=0.028") + Far},
	};
	return Presets;
}

void AEphemeraSkyRig::ReadTuning(const FString& Spec)
{
	TArray<FString> Pairs;
	Spec.ParseIntoArray(Pairs, TEXT(","));
	for (const FString& Pair : Pairs)
	{
		FString Key, Value;
		if (Pair.Split(TEXT("="), &Key, &Value))
		{
			Tuning.Add(Key, FCString::Atof(*Value));
		}
	}
}

void AEphemeraSkyRig::ApplyTuning()
{
	Sun->bCastCloudShadows = Tuned(TEXT("cloudshadow"), 0.f) > 0.5f;
	Sun->bPerPixelAtmosphereTransmittance = Tuned(TEXT("perpixel"), 0.f) > 0.5f;
	Sun->MarkRenderStateDirty();
	Clouds->SetbUsePerSampleAtmosphericLightTransmittance(Tuned(TEXT("persample"), 1.f) > 0.5f);
	Sun->CloudShadowStrength = Tuned(TEXT("cloudshadowstrength"), 0.88f);
	ApplyGrade();
	Sun->SetIntensity(SunLux * Tuned(TEXT("sun"), 1.f));
	SkyLight->SetIntensity(Tuned(TEXT("skylight"), SkyLightScale));
	// How fast the land fades into the air with distance. This world's whole sky is packed into a
	// shell 1.4 km deep, so a cliff two kilometres off has as much air in front of it as the horizon
	// sky has altogether, and stands paler than the sky behind it. The fade on the land (not the sky
	// itself) can be drawn as if things were nearer than they are.
	Atmosphere->SetAerialPespectiveViewDistanceScale(Tuned(TEXT("airdepth"), AirDepthScale));
	Fog->SetVolumetricFogDistance(Tuned(TEXT("fogreach"), MistReachM) * 100.f);
	Fog->SetVolumetricFogScatteringDistribution(Tuned(TEXT("fogforward"), 0.3f));
	MistVolume->SetWorldScale3D(FVector(Tuned(TEXT("fogreach"), MistReachM) * 2.f));
	if (StarInstance && Tuning.Contains(TEXT("stars")))
	{
		StarInstance->SetScalarParameterValue(TEXT("Brightness"), Tuned(TEXT("stars"), 0.f));
	}
	// The air and the clouds are fitted to a world from the same settings: fit them again.
	if (const AEphemeraBody* Body = AtmosphereBody.Get())
	{
		FitAtmosphere(Body);
	}
}

void AEphemeraSkyRig::SetWeather(const FName Name)
{
	const FString* Preset = WeatherPresets().Find(Name);
	if (!Preset)
	{
		return;
	}
	Weather = Name;
	Tuning.Reset();
	ReadTuning(*Preset);
	ReadTuning(CommandLineTuning);
	ApplyTuning();
}

void AEphemeraSkyRig::BeginPlay()
{
	Super::BeginPlay();
	if (StarMaterial)
	{
		StarInstance = UMaterialInstanceDynamic::Create(StarMaterial, this);
		Stars->SetMaterial(0, StarInstance);
	}
	Stars->SetVisibility(StarMaterial != nullptr);
	if (MistMaterial)
	{
		MistInstance = UMaterialInstanceDynamic::Create(MistMaterial, this);
		MistVolume->SetMaterial(0, MistInstance);
	}
	if (UnderwaterMaterial)
	{
		UnderwaterInstance = UMaterialInstanceDynamic::Create(UnderwaterMaterial, this);
		PostProcess->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(0.f, UnderwaterInstance));
	}
	if (MotesMesh && MotesMaterial)
	{
		Motes->SetStaticMesh(MotesMesh);
		MotesInstance = UMaterialInstanceDynamic::Create(MotesMaterial, this);
		Motes->SetMaterial(0, MotesInstance);
	}
	FParse::Value(FCommandLine::Get(), TEXT("-EphemeraSky="), CommandLineTuning, false);
	// The weather: what the command line names (photographs always name one, so they do not depend
	// on anyone's settings), else what the player last chose.
	FString Named = UEphemeraUserSettings::Get()->Weather;
	FParse::Value(FCommandLine::Get(), TEXT("-EphemeraWeather="), Named);
	SetWeather(WeatherPresets().Contains(FName(*Named)) ? FName(*Named) : FName(TEXT("fronts")));
	Sun->SetLightColor(SunColor);
	Sun->SetLightSourceAngle(SunAngleDeg);
	if (UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>())
	{
		FrameHandle = Frames->OnFrameUpdated.AddUObject(this, &AEphemeraSkyRig::UpdateSky);
	}
}

void AEphemeraSkyRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UEphemeraFrameSubsystem* Frames = World->GetSubsystem<UEphemeraFrameSubsystem>())
		{
			Frames->OnFrameUpdated.Remove(FrameHandle);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AEphemeraSkyRig::UpdateSky()
{
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	if (!Frames)
	{
		return;
	}
	FVector Eye = FVector::ZeroVector;
	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		Eye = Camera->GetCameraLocation();
	}
	// The stars stay centred on the eye and turn with the inertial sky.
	Stars->SetWorldLocation(Eye);
	// The sky light captures the sky where the eye is (it sat at the rig's origin, the planet's centre, and
	// captured the sky from inside the world).
	SkyLight->SetWorldLocation(Eye);
	Stars->SetWorldRotation(Frames->SkyRotation());
	// Light travels from the star: the light's forward axis is the opposite of the way to it.
	const FVector ToStar = Frames->DirectionToStar(Eye);
	Sun->SetWorldRotation((-ToStar).ToOrientationQuat());

	// The atmosphere wraps the nearest world that has air, also when you look back at it from space.
	const AEphemeraBody* Body = Frames->NearestWithAtmosphere(Eye);
	const bool bHasAir = Body != nullptr;
	if (bHasAir)
	{
		if (AtmosphereBody.Get() != Body)
		{
			FitAtmosphere(Body);
		}
		Atmosphere->SetWorldLocation(Body->GetActorLocation());
	}
	else if (AtmosphereBody.IsValid())
	{
		AtmosphereBody = nullptr;
		Clouds->SetVisibility(false);
	}
	if (bHasAir)
	{
		Clouds->SetWorldLocation(Body->GetActorLocation());
	}
	if (Atmosphere->IsVisible() != bHasAir)
	{
		Atmosphere->SetVisibility(bHasAir);
	}
	// Stars are always there in space. Under a lit sky they fade: fully hidden by day at the
	// ground, back again as the sun sinks or as you climb out of the air.
	float StarVisibility = 1.f;
	float InSpace = 1.f;
	// How near the sun is to the horizon of the world the eye is at: 1 as it crosses, 0 once it is a hand high or well under.
	float Twilight = 0.f;
	// And how low it stands while still up: 1 in the hour after dawn and before dusk, 0 by mid-morning.
	float LowSun = 0.f;
	if (bHasAir)
	{
		const FVector Centre = Body->GetActorLocation();
		const FVector Up = (Eye - Centre).GetSafeNormal();
		const double AltitudeM = (FVector::Dist(Eye, Centre) - Body->RadiusCm()) / 100.0;
		const float InAir = 1.f - FMath::SmoothStep(0.35f, 1.0f, static_cast<float>(AltitudeM / Body->Definition->AtmosphereHeightM));
		const float SunHeightDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(ToStar, Up), -1.0, 1.0)));
		const float Day = FMath::SmoothStep(-9.f, 3.f, SunHeightDeg);
		Twilight = InAir * (1.f - FMath::SmoothStep(0.f, 11.f, FMath::Abs(SunHeightDeg + 2.f)));
		LowSun = InAir * FMath::SmoothStep(-4.f, 2.f, SunHeightDeg) * (1.f - FMath::SmoothStep(6.f, 30.f, SunHeightDeg));
		StarVisibility = 1.f - InAir * Day;
		// The camera's fixed daylight setting takes over as soon as most of the air is below it:
		// looking down on dark lava and sea, an automatic meter would lift them to mid grey.
		InSpace = FMath::SmoothStep(0.4f, 1.2f, static_cast<float>(AltitudeM / Body->Definition->AtmosphereHeightM));
	}
	if (bHasAir)
	{
		// Cloud shadows are drawn into a square map centred on the planet (the engine snaps its centre to
		// 20 km, and the world you are on rests at the origin). It must always hold the whole shell of
		// cloud (it used to shrink to 3 km after a flight, and then most of the world's cloud shadows
		// were smeared from the map's edge), and grows for the view from high up.
		const float AltitudeKm = static_cast<float>((FVector::Dist(Eye, Body->GetActorLocation()) - Body->RadiusCm()) / 100000.0);
		// It is changed in few, large steps and only once well past one: every change remakes the sun's
		// light and all its shadows, which shows as the lighting jumping.
		const float ShellKm = static_cast<float>(Body->RadiusCm() / 100000.0 + 1.0) * 1.02f;
		const float Needed = FMath::Max(ShellKm, FMath::Clamp(AltitudeKm * 1.2f + 3.f, 3.f, 96.f));
		const float Step = ShellKm * FMath::Pow(2.f, FMath::CeilToFloat(FMath::Log2(Needed / ShellKm) - 0.001f));
		if (Step > Sun->CloudShadowExtent * 1.01f || Step < Sun->CloudShadowExtent * 0.3f)
		{
			Sun->CloudShadowExtent = Step;
			Sun->MarkRenderStateDirty();
		}
	}
	// Mist belongs to the view from the ground: it is let in as the camera comes down into the air
	// and gone again by the time the whole planet is in frame.
	// The mist you can see into belongs to the ground too. Its box goes with the eye; its material
	// is told where the world's centre is and how big the world is, and thins the mist with height.
	// (By night it is thinned to a quarter: what little light there is should fall on the land.)
	const float MistNow = bHasAir && MistInstance ? Tuned(TEXT("fog"), MistPerKm) * (1.f - InSpace) * FMath::Lerp(1.f, 0.25f, NightNow) : 0.f;
	// The fog cells are on for as long as the camera is in the air at all, however thin the mist:
	// switching them with the mist's own thickness made the lighting step as it crossed the line.
	// Under the sea: is the eye below the water (a sphere about the world's centre, raised by the tide)?
	bool bUnder = false;
	if (bHasAir && Body->Definition && Body->Definition->OceanMesh)
	{
		const double SeaCm = (Body->Definition->RadiusM + Body->SeaSurfaceM(Eye)) * 100.0;
		const FVector Centre = Body->GetActorLocation();
		bUnder = FVector::Dist(Eye, Centre) < SeaCm;
		if (UnderwaterInstance && bUnder)
		{
			UnderwaterInstance->SetVectorParameterValue(TEXT("SeaCentre"), FLinearColor(static_cast<float>(Centre.X), static_cast<float>(Centre.Y), static_cast<float>(Centre.Z), 0.f));
			UnderwaterInstance->SetScalarParameterValue(TEXT("SeaRadiusCm"), static_cast<float>(SeaCm));
			UnderwaterInstance->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(static_cast<float>(ToStar.X), static_cast<float>(ToStar.Y), static_cast<float>(ToStar.Z), 0.f));
			// The light falling on the sea here, lux: the sun's and the bright sky's by day, the moon's and the
			// night sky's by night (the water's glow is lit by it, as a real brightness).
			const float SunDot = static_cast<float>(FVector::DotProduct(ToStar, (Eye - Centre).GetSafeNormal()));
			const float DayLux = SunLux * (FMath::Max(SunDot, 0.f) + 0.15f * FMath::SmoothStep(-0.1f, 0.15f, SunDot));
			UnderwaterInstance->SetScalarParameterValue(TEXT("Light"), DayLux + ShineLuxNow + FillLuxNow + 0.3f);
		}
	}
	if (bUnder != bUnderNow && UnderwaterInstance)
	{
		bUnderNow = bUnder;
		for (FWeightedBlendable& Blendable : PostProcess->Settings.WeightedBlendables.Array)
		{
			if (Blendable.Object == UnderwaterInstance)
			{
				Blendable.Weight = bUnder ? 1.f : 0.f;
			}
		}
	}
	// (Under the sea there is no mist: the water is its own.)
	const bool bMist = bHasAir && MistInstance && InSpace < 0.995f && !bUnderNow;
	if (bMist)
	{
		MistVolume->SetWorldLocation(Eye);
		const FVector Centre = Body->GetActorLocation();
		MistInstance->SetVectorParameterValue(TEXT("Centre"), FLinearColor(static_cast<float>(Centre.X), static_cast<float>(Centre.Y), static_cast<float>(Centre.Z), 0.f));
		MistInstance->SetScalarParameterValue(TEXT("RadiusCm"), static_cast<float>(Body->RadiusCm()));
		MistInstance->SetScalarParameterValue(TEXT("PerKm"), MistNow);
		MistInstance->SetScalarParameterValue(TEXT("HeightM"), Tuned(TEXT("fogheight"), MistHeightM));
		MistInstance->SetScalarParameterValue(TEXT("Banks"), Tuned(TEXT("fogbanks"), 1.f));
	}
	if (Fog->IsVisible() != bMist)
	{
		Fog->SetVisibility(bMist);
		MistVolume->SetVisibility(bMist);
	}
	// Motes hang in the air near the ground. Their cloud goes with the eye (its material keeps each
	// one where it is in the world as the eye moves through them). Past a run they are let go: at
	// flying speed they would be streaks, and then a flicker.
	const float Delta = GetWorld()->GetDeltaSeconds();
	if (Delta > 1.e-4f)
	{
		const float Speed = static_cast<float>(FVector::Dist(Eye, EyeBefore)) / Delta;
		EyeSpeed = Speed > 20000.f ? 0.f : FMath::Lerp(EyeSpeed, Speed, FMath::Min(1.f, Delta * 4.f));     // a jump of place is not a speed
		EyeBefore = Eye;
	}
	// (Not under the sea: the motes are lit as the air is, and there they shone like stars.)
	const float MotesNow = bHasAir && MotesInstance && !bUnderNow
		? Tuned(TEXT("motes"), MoteAmount) * (1.f - FMath::SmoothStep(0.f, 0.3f, InSpace)) * (1.f - FMath::SmoothStep(900.f, 2600.f, EyeSpeed)) : 0.f;
	const bool bMotes = MotesNow > 0.01f;
	if (bMotes)
	{
		Motes->SetWorldLocation(Eye);
		const FVector Centre = Body->GetActorLocation();
		MotesInstance->SetVectorParameterValue(TEXT("Centre"), FLinearColor(static_cast<float>(Centre.X), static_cast<float>(Centre.Y), static_cast<float>(Centre.Z), 0.f));
		MotesInstance->SetScalarParameterValue(TEXT("Amount"), MotesNow);
	}
	if (Motes->IsVisible() != bMotes)
	{
		Motes->SetVisibility(bMotes);
	}
	const float Haze = ClearHaze + Mist * (1.f - InSpace);
	if (bHasAir && FMath::Abs(Haze - HazeNow) > FMath::Max(HazeNow, 0.001f) * 0.04f)
	{
		HazeNow = Haze;
		Atmosphere->SetMieScatteringScale(Haze);
	}
	// The camera's fixed daylight setting is for a view with sunlit ground in it. Behind a world,
	// in its shadow, there is none: the eye opens up as it does on the ground at night, and the
	// night side is seen by its moon and its stars instead of being a black hole in the picture.
	const float Night = UpdateNight(*Frames, Eye, ToStar);
	NightNow = Night;
	// Materials that change with the night (the sea's glowing surf) read it from the world's shared numbers.
	if (bHasAir && Body->Definition && Body->Definition->TideCollection && FMath::Abs(Night - SkyStateSent) > 0.005f)
	{
		SkyStateSent = Night;
		UKismetMaterialLibrary::SetVectorParameterValue(this, Body->Definition->TideCollection, TEXT("SkyState"), FLinearColor(Night, 0.f, 0.f, 0.f));
	}
	// At night the picture holds a lamp's pool beside ground lit a thousand times more faintly. The
	// eye sees into both; so highlights and shadows are each drawn in toward the middle (the
	// engine's local exposure), by day not at all.
	PostProcess->Settings.LocalExposureHighlightContrastScale = FMath::Lerp(Tuned(TEXT("localhigh"), DayHighlightContrast), 0.6f, Night);
	// (At dusk and dawn the sky is still day-bright over land the sun has left: there the shade is
	// lifted toward the middle, as the eye does, or the whole land is a black cut-out under the sky.)
	PostProcess->Settings.LocalExposureShadowContrastScale = FMath::Lerp(Tuned(TEXT("localshadow"), DayShadowContrast), 0.9f, Night) - 0.4f * Twilight;
	// What the meter reads. In space, only the brighter half of the frame, so black sky cannot drag
	// the exposure up. Under a sky, most of the frame: read the bright half there and the meter is
	// reading the sky alone, and sets it to middle grey with the land in the dark beneath it.
	// By night it reads the bright half again, as in space: moonlit cloud and a lamp's pool then set
	// the exposure, instead of being left to burn white over ground the meter has lifted to grey.
	// (Only deep into night: at dusk the sky is still bright, and read by its bright half the land
	// under it would be a black cut-out.)
	const float BrightHalf = FMath::Max(InSpace, FMath::SmoothStep(0.6f, 1.f, Night));
	PostProcess->Settings.AutoExposureLowPercent = FMath::Lerp(Tuned(TEXT("meterlow"), GroundMeterLow), SpaceMeterLow, BrightHalf) - 20.f * Twilight * (1.f - BrightHalf);
	PostProcess->Settings.AutoExposureHighPercent = FMath::Lerp(Tuned(TEXT("meterhigh"), GroundMeterHigh), SpaceMeterHigh, BrightHalf);
	PostProcess->Settings.AutoExposureBias = FMath::Lerp(FMath::Lerp(Tuned(TEXT("bias"), GroundExposureBias), Tuned(TEXT("spacebias"), SpaceExposureBias), InSpace),
		Tuned(TEXT("nightbias"), NightExposureBias), Night);
	// And the eye opens for the dark slowly (a third of a stop a second): coming down onto a world's
	// night side, the land should come up out of black over some seconds, not snap to grey.
	PostProcess->Settings.AutoExposureSpeedDown = FMath::Lerp(1.0f, 0.35f, Night);
	// The sky's own light is nearly all taken away by night. What the engine's picture of the night
	// sky holds is the neighbour world in full sun (some thirty lux of it over Dawnhollow), and
	// left in, that lights the land like an overcast afternoon: it was the night's real lamp, and
	// the reason night was never dark. A fortieth of it is kept: enough for hollows and the lee of
	// rocks to be darker than open ground, which the two night lights by themselves cannot do.
	// (0.012 of 2.5 by day: the same night sky light as 0.025 of the old 1.2, so night is as dark as it was.)
	const float SkyNow = Tuned(TEXT("skylight"), SkyLightScale) * FMath::Lerp(1.f, Tuned(TEXT("nightsky"), 0.012f), Night);
	if (FMath::Abs(SkyNow - SkyLight->Intensity) > 0.02f * FMath::Max(SkyLight->Intensity, 0.01f))
	{
		SkyLight->SetIntensity(SkyNow);
	}
	// Night is seen with the eye's rods: colour drains out of it and what is left leans blue.
	const float NightColour = FMath::SmoothStep(0.5f, 1.f, Night);
	// The day has a colour script, as a film's does. With the sun low, what it lights goes to gold
	// and what it does not goes blue (the two are further apart than at noon, and a little richer);
	// as it crosses the horizon the whole picture takes the rose and violet of this world's dusk;
	// and night is the eye's rods, colour drained and leaning blue. Noon is left as it comes.
	const float Script = Tuned(TEXT("script"), 1.f);
	const float Gold = LowSun * Script;
	const float Rose = Twilight * (1.f - NightColour) * Script;
	// Night colour belongs to the dark: rods see the shade grey-blue, but a lamp, a fire or the moon
	// stays its own colour ("night vision is not black and white", Ghost of Tsushima). It used to be a
	// blue layer over the whole picture, the sky and the clouds included.
	const float Saturation = Tuned(TEXT("sat"), GroundSaturation) * (1.f + 0.10f * Gold);
	PostProcess->Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0);
	const float InShade = FMath::Lerp(Tuned(TEXT("satshadow"), 0.85f), 0.5f, NightColour);
	PostProcess->Settings.ColorSaturationShadows = FVector4(InShade, InShade, InShade, 1.0);
	PostProcess->Settings.ColorGainShadows = FVector4(FMath::Lerp(1.f, 0.90f, NightColour), FMath::Lerp(1.f, 0.97f, NightColour), FMath::Lerp(1.f, 1.10f, NightColour), 1.0);
	PostProcess->Settings.ColorCorrectionShadowsMax = FMath::Lerp(0.09f, 0.25f, NightColour);
	// Grain is a daylight film's: by night, on dark frames, it is only noise.
	PostProcess->Settings.FilmGrainIntensity = Tuned(TEXT("grain"), 0.14f) * (1.f - 0.7f * Night);
	PostProcess->Settings.ColorGainHighlights = FVector4(Tuned(TEXT("highr"), 0.97f) + 0.10f * Gold + 0.03f * Rose, 1.0 - 0.01f * Rose,
		Tuned(TEXT("highb"), 1.04f) - 0.13f * Gold + 0.02f * Rose, 1.0);
	PostProcess->Settings.ColorGammaShadows = FVector4(Tuned(TEXT("shadowr"), 0.96f) - 0.03f * Gold + 0.02f * Rose, 1.0 - 0.01f * Rose,
		Tuned(TEXT("shadowb"), 1.03f) + 0.05f * Gold + 0.04f * Rose, 1.0);
	PostProcess->Settings.ColorGain = FVector4(1.f + 0.02f * Rose, 1.f - 0.02f * Rose, 1.f + 0.03f * Rose, 1.0);
	const float Fixed = InSpace * (1.f - Night);
	const float SpaceEV = Tuned(TEXT("floor"), SpaceExposureEV);
	// (An experiment, off unless a render asks: "keyspan" holds the meter by day within that many
	// stops of "key", so that turning to a shaded cliff leaves it dark instead of lifting it to grey.)
	float FloorEV = FMath::Lerp(GroundExposureFloorEV, Tuned(TEXT("nightev"), NightExposureFloorEV), Night);
	// Nor, by night, does the eye close down far for a lamp: three stops at most. Free to follow the lamp's pool all
	// the way, the meter turned the pool grey and everything round it black, and the lamp seemed weak.
	// (Three stops let a bright thing in frame, then the clouds, close the eye until the land was black.)
	float CeilingEV = FMath::Lerp(GroundExposureCeilingEV, Tuned(TEXT("nightev"), NightExposureFloorEV) + Tuned(TEXT("nightspan"), 1.f), Night);
	// By day the exposure is a photographer's, not a free meter's: held within KeySpan of what daylight of
	// this strength calls for ("sunny 16": about EV 14 in full sun), lower as the sun goes down. Left free,
	// the meter saw a frame of black sand, dark moss and dark sea, judged it dim and opened two or three
	// stops, and every sunlit thing went pale: basalt like concrete, the land evenly over-lit, "a blender
	// render with bad lighting" (owner, 14 October). Measured: key 13.5 at a 32 degree sun matched the look of
	// real black-sand coasts; the key follows half of the change in sunlight with the sun's height (the eye
	// itself makes up the rest). Night keeps its own range.
	const float KeySpan = Tuned(TEXT("keyspan"), 0.6f);
	if (KeySpan > 0.f && bHasAir)
	{
		const float SunSin = static_cast<float>(FVector::DotProduct(ToStar, (Eye - Body->GetActorLocation()).GetSafeNormal()));
		const float Key = FMath::Clamp(Tuned(TEXT("key"), 13.5f) + 0.5f * FMath::Log2(FMath::Max(SunSin, 0.06f) / 0.53f), 10.5f, 14.2f);
		const float NightFloor = Tuned(TEXT("nightev"), NightExposureFloorEV);
		FloorEV = FMath::Lerp(Key - KeySpan, NightFloor, Night);
		CeilingEV = FMath::Lerp(Key + KeySpan, NightFloor + Tuned(TEXT("nightspan"), 1.f), Night);
	}
	PostProcess->Settings.AutoExposureMinBrightness = FMath::Lerp(FloorEV, SpaceEV, Fixed);
	PostProcess->Settings.AutoExposureMaxBrightness = FMath::Lerp(CeilingEV, SpaceEV, Fixed);
	if (StarInstance)
	{
		StarInstance->SetScalarParameterValue(TEXT("Visibility"), StarVisibility);
		// Under a daylit sky the dome is not drawn at all: left in, every star and the galaxy's clouds are
		// worked out for each pixel of sky only to be multiplied by nothing.
		const bool bStars = StarVisibility > 0.003f;
		if (Stars->IsVisible() != bStars)
		{
			Stars->SetVisibility(bStars);
		}
	}
	// Tides: every sea is told where its moon is.
	for (TActorIterator<AEphemeraBody> It(GetWorld()); It; ++It)
	{
		AEphemeraBody* Sea = *It;
		if (!Sea->Definition || Sea->Definition->TideAmplitudeM <= 0.f)
		{
			continue;
		}
		if (const AEphemeraBody* Moon = Frames->FindBody(Sea->Definition->TideBody))
		{
			Sea->SetTide(Sea->GetActorTransform().InverseTransformVectorNoScale((Moon->GetActorLocation() - Sea->GetActorLocation()).GetSafeNormal()));
		}
	}
	if (CloudInstance && bHasAir)
	{
		// The cloud material draws weather in the planet's own space; it needs to know where and
		// how the planet is in the current frame.
		const FVector C = Body->GetActorLocation();
		CloudInstance->SetVectorParameterValue(TEXT("PlanetCentre"), FLinearColor(C.X, C.Y, C.Z, 0.f));
		const FQuat Q = Body->GetActorQuat().Inverse();
		CloudInstance->SetVectorParameterValue(TEXT("PlanetRotation"), FLinearColor(Q.X, Q.Y, Q.Z, Q.W));
		// Wind: the billows cross one repeat of their texture (a few kilometres) in about a
		// quarter of an hour. Wrapped here, in double precision, so the shader gets a small number.
		CloudInstance->SetScalarParameterValue(TEXT("Drift"), static_cast<float>(FMath::Fmod(Frames->GetTime() * 0.0012, 1.0)));
		// On the night side a cloud is lit by the night's light, not the sun's (see CLOUD_LIT_CODE): their ratio.
		// (At dusk the clouds the sun has left are still lit by the bright sky, some hundreds of lux: grey-blue,
		// not dark shapes against it.)
		CloudInstance->SetScalarParameterValue(TEXT("Unlit"), FMath::Max((ShineLuxNow + FillLuxNow + 0.02f + 1200.f * Twilight) / SunLux, 1.e-6f) * Tuned(TEXT("cloudnight"), 1.f));
	}
}

float AEphemeraSkyRig::UpdateNight(const UEphemeraFrameSubsystem& Frames, const FVector& Eye, const FVector& ToStar)
{
	// The world the eye is at: the nearest that is not the star.
	const AEphemeraBody* Home = nullptr;
	double HomeHeight = TNumericLimits<double>::Max();
	for (TActorIterator<AEphemeraBody> It(GetWorld()); It; ++It)
	{
		const AEphemeraBody* Body = *It;
		if (!Body->Definition || Body->Definition->bIsStar)
		{
			continue;
		}
		const double Height = FVector::Dist(Eye, Body->GetActorLocation()) - Body->RadiusCm();
		if (Height < HomeHeight)
		{
			Home = Body;
			HomeHeight = Height;
		}
	}
	float Night = 0.f;
	float NearGround = 0.f;
	float ShineLux = 0.f;
	FVector Down = FVector::DownVector;
	FVector ShineFrom = FVector::DownVector;
	float ShineAngleDeg = 1.f;
	bool bShineHasAir = false;
	AEphemeraBody* ShineSource = nullptr;
	auto AngleDeg = [](const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0)));
	};
	if (Home)
	{
		const FVector ToCentre = Home->GetActorLocation() - Eye;
		const double Distance = FMath::Max(ToCentre.Size(), 1.0);
		Down = ToCentre / Distance;
		// How much of the view the world's bulk blocks, as an angle from straight down: 90 degrees
		// standing on it. The sun is gone once it is inside that angle, which on the ground is
		// sunset and in orbit is passing into the world's shadow.
		const double Bulk = FMath::RadiansToDegrees(FMath::Asin(FMath::Min(Home->RadiusCm() / Distance, 1.0)));
		Night = 1.f - FMath::SmoothStep(-6.f, 2.f, static_cast<float>(AngleDeg(ToStar, Down) - Bulk));
		NearGround = 1.f - FMath::SmoothStep(0.3f, 0.8f, static_cast<float>(HomeHeight / Home->RadiusCm()));
		// The brightest neighbour: a sphere lit by the star and seen at some phase (Lambert's law).
		double Best = 0.0;
		for (TActorIterator<AEphemeraBody> It(GetWorld()); It; ++It)
		{
			AEphemeraBody* Other = *It;
			if (Other == Home || !Other->Definition || Other->Definition->bIsStar)
			{
				continue;
			}
			const FVector FromOther = Eye - Other->GetActorLocation();
			const double Far = FMath::Max(FromOther.Size(), Other->RadiusCm());
			const double Phase = FMath::Acos(FMath::Clamp(FVector::DotProduct(Frames.DirectionToStar(Other->GetActorLocation()), FromOther / Far), -1.0, 1.0));
			const double Lit = (FMath::Sin(Phase) + (UE_DOUBLE_PI - Phase) * FMath::Cos(Phase)) / UE_DOUBLE_PI;
			const bool bAir = Other->Definition->AtmosphereHeightM > 0.0;
			double Lux = SunLux * (bAir ? 0.30 : 0.12) * (2.0 / 3.0) * FMath::Square(Other->RadiusCm() / Far) * Lit;
			// From the ground, a neighbour under the horizon lights nothing here. (From orbit the
			// light is left to fall on whichever side of the world faces it.)
			const float Up = FMath::SmoothStep(-3.f, 4.f, static_cast<float>(AngleDeg(-FromOther / Far, Down) - Bulk));
			Lux *= FMath::Lerp(1.f, Up, NearGround);
			if (Lux > Best)
			{
				Best = Lux;
				ShineFrom = FromOther / Far;
				ShineAngleDeg = static_cast<float>(2.0 * FMath::RadiansToDegrees(FMath::Asin(FMath::Min(Other->RadiusCm() / Far, 1.0))));
				bShineHasAir = bAir;
				ShineSource = Other;
			}
		}
		const float Scaled = static_cast<float>(Best) / ShineHalfLux;
		ShineLux = ShineCapLux * Scaled / (1.f + Scaled) * Night * Tuned(TEXT("shine"), 1.f);
	}
	const bool bShine = ShineLux > 0.0005f;
	// Shadows where there is something near to throw them; from orbit the round of the world is enough.
	const bool bShineShadows = bShine && NearGround > 0.5f && Tuned(TEXT("shineshadow"), 1.f) > 0.5f;
	// The light is drawn as coming from the neighbour's direction, from infinitely far: the neighbour
	// itself would stand in that light and shadow the very ground it is lighting. While its light
	// throws shadows it throws none itself. (Nothing is lost: the only shadow it could cast is an
	// eclipse of the sun, and this is night.)
	AEphemeraBody* Unshadowing = bShineShadows ? ShineSource : nullptr;
	if (ShineUnshadowed.Get() != Unshadowing)
	{
		if (AEphemeraBody* Was = ShineUnshadowed.Get())
		{
			Was->SetCastsShadows(true);
		}
		if (Unshadowing)
		{
			Unshadowing->SetCastsShadows(false);
		}
		ShineUnshadowed = Unshadowing;
	}
	if (bShine)
	{
		Shine->SetWorldRotation(ShineFrom.ToOrientationQuat());
		Shine->SetIntensity(ShineLux);
		Shine->SetLightColor(bShineHasAir ? FLinearColor(0.72f, 0.85f, 1.0f) : FLinearColor(0.84f, 0.90f, 1.0f));
		if (FMath::Abs(ShineAngleDeg - Shine->LightSourceAngle) > Shine->LightSourceAngle * 0.1f)
		{
			Shine->SetLightSourceAngle(ShineAngleDeg);
		}
		if (static_cast<bool>(Shine->CastShadows) != bShineShadows)
		{
			Shine->SetCastShadows(bShineShadows);
		}
	}
	if (Shine->IsVisible() != bShine)
	{
		Shine->SetVisibility(bShine);
	}
	ShineLuxNow = ShineLux;
	const float FillLux = Tuned(TEXT("nightfill"), NightFillLux) * Night;
	FillLuxNow = FillLux;
	const bool bFill = FillLux > 0.0005f;
	if (bFill)
	{
		NightFill->SetWorldRotation(Down.ToOrientationQuat());
		NightFill->SetIntensity(FillLux);
	}
	if (NightFill->IsVisible() != bFill)
	{
		NightFill->SetVisibility(bFill);
	}
	// Said now and then while it is night, so a dark picture can be told from a broken one afterwards.
	if (Night > 0.f && FPlatformTime::Seconds() - NightSaidAt > 15.0)
	{
		NightSaidAt = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Log, TEXT("Ephemera night: %.2f into night at %s, neighbour's light %.3f lux, starlight %.3f lux"), Night,
			Home ? *Home->BodyId().ToString() : TEXT("nowhere"), ShineLux, FillLux);
	}
	return Night;
}

void AEphemeraSkyRig::FitAtmosphere(const AEphemeraBody* Body)
{
	AtmosphereBody = Body;
	const UEphemeraBodyDefinition& D = *Body->Definition;
	const float HeightKm = FMath::Max(static_cast<float>(D.AtmosphereHeightM / 1000.0), 0.1f);
	const float Pack = EarthAtmosphereKm / HeightKm;
	// The atmosphere's floor lies below the deepest sea: with the eye under it (diving), the engine
	// draws no sky at all, and the sky seen up through the water went black. (80 m lower changes the air
	// at the surface by under one percent.)
	// The ground the atmosphere sees under it is this land's own dark: about 6% (moss, basalt, black sand),
	// not the engine's 40%. At 40% the captured sky's lower half was a bright sunlit floor, and the sky light
	// lit everything from below ("lighting up ... even though the light is not hitting them").
	Atmosphere->SetGroundAlbedo(FColor(68, 70, 64));      // sRGB: about 0.06 linear
	Atmosphere->SetBottomRadius(FMath::Max(static_cast<float>((D.RadiusM - (D.OceanMesh ? 80.0 : 0.0)) / 1000.0), 0.1f));
	Atmosphere->SetAtmosphereHeight(HeightKm);
	Atmosphere->SetRayleighScattering(D.RayleighScattering);
	Atmosphere->SetRayleighScatteringScale(D.RayleighScale * Pack * Tuned(TEXT("rayleigh"), 1.f));
	Atmosphere->SetRayleighExponentialDistribution(EarthRayleighFalloffKm / Pack);
	ClearHaze = D.MieScale * Pack * Tuned(TEXT("mie"), 1.f);
	Mist = Tuned(TEXT("mist"), D.MistPerKm);
	HazeNow = -1.f;
	Atmosphere->SetMieAbsorptionScale(EarthMieAbsorption * Pack);
	// Haze hugs the ground: a layer a few metres deep gives distance its soft fade when you look
	// along the land, and is too thin to veil the sky above or the world seen from orbit.
	Atmosphere->SetMieExponentialDistribution(Mist > 0.f ? FMath::Max(Tuned(TEXT("mistheight"), D.MistHeightM) / 1000.f, 0.01f)
		: FMath::Max(EarthMieFalloffKm / Pack * 3.0f, 0.006f));
	// Haze scatters less tightly forward than Earth's: on a world this small the sun shines through
	// the limb at grazing angles from almost anywhere in orbit, and a tight forward lobe whites out
	// the whole horizon.
	Atmosphere->SetMieAnisotropy(0.7f);
	// The absorbing layer (Earth's ozone) is what gives a sky its depth of colour overhead and its
	// violet dusk; each world sets how much it has and what it absorbs.
	Atmosphere->SetOtherAbsorptionScale(0.001881f * D.AbsorptionScale * Pack * Tuned(TEXT("absorption"), 1.f));
	Atmosphere->SetOtherAbsorption(D.Absorption);
	// Whether there is cloud at all, and what kind, is the weather's to say (WeatherPresets).
	const bool bClouds = Tuned(TEXT("clouds"), 0.f) > 0.5f && D.CloudThicknessM > 0.0 && D.CloudMaterial != nullptr;
	if (bClouds)
	{
		CloudInstance = UMaterialInstanceDynamic::Create(D.CloudMaterial, this);
		CloudInstance->SetScalarParameterValue(TEXT("PlanetRadiusCm"), static_cast<float>(D.RadiusM * 100.0));
		for (const TPair<FString, float>& Entry : Tuning)
		{
			FString Parameter;
			if (Entry.Key.Split(TEXT("cloud."), nullptr, &Parameter))
			{
				CloudInstance->SetScalarParameterValue(FName(*Parameter), Entry.Value);
			}
		}
		Clouds->SetMaterial(CloudInstance);
		Clouds->SetPlanetRadius(static_cast<float>(D.RadiusM / 1000.0));
		Clouds->SetLayerBottomAltitude(Tuned(TEXT("cloudbase"), static_cast<float>(D.CloudBaseM)) / 1000.f);
		Clouds->SetLayerHeight(Tuned(TEXT("cloudthick"), static_cast<float>(D.CloudThicknessM)) / 1000.f);
	}
	Clouds->SetVisibility(bClouds);

}
