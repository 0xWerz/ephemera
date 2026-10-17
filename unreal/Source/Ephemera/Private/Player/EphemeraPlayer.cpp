#include "Player/EphemeraPlayer.h"

#include "AudioDevice.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Frame/EphemeraSkyRig.h"
#include "Frame/EphemeraBody.h"
#include "Frame/EphemeraGroundCover.h"
#include "Frame/EphemeraHeightField.h"
#include "GameFramework/PlayerController.h"
#include "ContentStreaming.h"
#include "Misc/App.h"
#include "PipelineStateCache.h"
#include "Misc/CommandLine.h"
#include "Player/EphemeraAudio.h"
#include "Player/EphemeraUserSettings.h"
#include "UI/SEphemeraHud.h"
#include "Widgets/SWeakWidget.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Frame/EphemeraBody.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Misc/CommandLine.h"
#include "Player/EphemeraInputConfig.h"

namespace
{
	constexpr float CapsuleRadius = 30.f;
	constexpr float CapsuleHalfHeight = 66.f;
	// How long a puff of dust hangs before it is gone, seconds (its material fades it over the same time).
	constexpr float PuffLife = 1.7f;
	// In the sea, Outer Wilds' own rules (docs/research/ow_mechanics.md, 1.6): wholly under, the water
	// holds the body up with 1.1 times the world's pull, so it rises slowly; it drags as 0.5 rho v^2
	// with rho 30 (twenty times the air); the jets work under water, burn nothing unless boosted, and
	// push a third as hard; on the bottom you walk at half pace. Afloat, the body settles with the eye
	// a hand above the water. Swimming is steering at up to SwimSpeed: along the look under water,
	// along the surface on it.
	constexpr float SeaFloat = 1.1f;
	constexpr float SeaFullDepthCm = 121.f;
	constexpr double SeaDragPerCm = 5.88e-4;
	constexpr float SwimSpeed = 190.f;
	constexpr float SwimSprint = 320.f;
	constexpr float SwimAcceleration = 280.f;
	constexpr float SeaJetShare = 0.35f;
	// Unreal's world gravity, which GravityScale multiplies.
	constexpr float WorldGravity = 980.f;
	// Air resists a body in proportion to the square of its speed: this much of the speed is lost
	// each second for every cm/s of it, at sea level. A long fall settles at about 60 m/s.
	constexpr double AirDragPerCm = 2.9e-5;
	const FName PlacedTag(TEXT("EphemeraPlaced"));

	/** Unreal's camera FOV is horizontal at 16:9; the design value is vertical, as in Outer Wilds. */
	float HorizontalFov(float VerticalDeg)
	{
		return FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalDeg) * 0.5f) * 16.f / 9.f));
	}

	// Where each gait gives way to the next, m/s along the ground: creep, walk, jog, run (and a sprint beyond).
	constexpr float GaitTop[] = {1.5f, 3.f, 5.2f, 6.8f};
	constexpr int32 SurfaceKinds = static_cast<int32>(EEphemeraSurface::Count);
	constexpr int32 WaterIndex = static_cast<int32>(EEphemeraSurface::Water);

	/** The ground at a point as a step hears it (see GroundSoundAt). */
	struct FGroundSound
	{
		float Mix[SurfaceKinds] = {};
		float Plants = 0.f;
		float Stalks = 0.f;
		/** How much of it is a stream's bed. */
		float Stream = 0.f;
		/** World space; zero where there is no terrain to ask. */
		FVector Normal = FVector::ZeroVector;
	};

	/**
	 * The ground at a point as a step hears it (docs/research/footsteps_and_space_audio.md 6.5):
	 * shares of the seven surfaces, summing to 1, from the same fields that decide the ground's
	 * material and what grows on it, so that what is heard underfoot is what is seen there. Shares
	 * and not one answer, so that a border is a blend and not a line. These are starting values, to
	 * be held against what the material paints.
	 */
	FGroundSound GroundSoundAt(const UEphemeraFrameSubsystem* Frames, const FVector& WorldPosition)
	{
		FGroundSound Ground;
		auto Share = [&Ground](const EEphemeraSurface Surface) -> float& { return Ground.Mix[static_cast<int32>(Surface)]; };
		const AEphemeraBody* Body = Frames ? Frames->NearestBody(WorldPosition) : nullptr;
		const UEphemeraBodyDefinition* D = Body ? Body->Definition.Get() : nullptr;
		if (!D)
		{
			Share(EEphemeraSurface::Basalt) = 1.f;
			return Ground;
		}
		if (D->AtmosphereHeightM <= 0.0)
		{
			// An airless world's ground is its dust: fine, dry and loose, the nearest thing to ash.
			Share(EEphemeraSurface::Ash) = 1.f;
			return Ground;
		}
		const FTransform& Frame = Body->GetActorTransform();
		const FVector Direction = Frame.InverseTransformPosition(WorldPosition).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FEphemeraTerrainSettings& Terrain = D->Terrain;
		const float Cover = static_cast<float>(FEphemeraTerrain::Cover(Terrain, Direction));
		const float Ice = static_cast<float>(FEphemeraTerrain::Ice(Terrain, Direction));
		const double AltitudeM = FEphemeraTerrain::HeightM(Terrain, D->RadiusM, Direction);
		const FVector Normal = FEphemeraTerrain::Normal(Terrain, D->RadiusM, Direction);
		const float SlopeDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Normal, Direction), -1.0, 1.0))));
		Ground.Normal = Frame.TransformVectorNoScale(Normal);
		// What grows follows the cover alone on a baked world (whose "moisture" is only where water
		// runs); elsewhere the moisture field decides meadow from bare ground.
		const bool bBaked = Terrain.HeightField && Terrain.HeightField->IsUsable();
		const float Lush = bBaked ? 1.f : FMath::SmoothStep(0.35f, 0.7f, static_cast<float>(FEphemeraTerrain::Moisture(Terrain, Direction)));
		// Cover runs from bare black sand (-1) through gravel to lichened lava (0) and on to deep moss
		// (1). Sedge and grass grow from just above the bare lava into the moss; steep bare ground is scree.
		const float Moss = FMath::SmoothStep(0.15f, 0.55f, Cover);
		const float Sedge = FMath::SmoothStep(-0.05f, 0.2f, Cover) * (1.f - Moss) * Lush;
		const float Ash = FMath::SmoothStep(0.5f, 0.8f, -Cover);
		const float Gravel = FMath::Min(1.f, FMath::SmoothStep(0.1f, 0.3f, -Cover) + 0.7f * FMath::SmoothStep(25.f, 35.f, SlopeDeg) * (1.f - Moss)) * (1.f - Ash);
		const float Basalt = FMath::Max(0.f, 1.f - Moss - Sedge - Ash - Gravel);
		// Snow and standing water lie over whatever is beneath them, which is still heard a little.
		const float Snow = FMath::SmoothStep(0.35f, 0.65f, Ice);
		const float Sea = D->OceanMesh ? FMath::SmoothStep(0.f, 0.15f, static_cast<float>(Body->TideM(WorldPosition) + 0.1 - AltitudeM)) : 0.f;
		Ground.Stream = D->StreamMesh ? FMath::SmoothStep(0.55f, 0.75f, static_cast<float>(FEphemeraTerrain::FlowAsShaded(Terrain, Direction))) * (1.f - Snow) : 0.f;
		const float Water = FMath::Max(Sea, Ground.Stream);
		const float Beneath = (1.f - 0.7f * Snow) * (1.f - 0.75f * Water) / (Moss + Sedge + Ash + Gravel + Basalt);
		Share(EEphemeraSurface::Moss) = Moss * Beneath;
		Share(EEphemeraSurface::Sedge) = Sedge * Beneath;
		Share(EEphemeraSurface::Ash) = Ash * Beneath;
		Share(EEphemeraSurface::Gravel) = Gravel * Beneath;
		Share(EEphemeraSurface::Basalt) = Basalt * Beneath;
		Share(EEphemeraSurface::Snow) = 0.7f * Snow * (1.f - 0.75f * Water);
		Share(EEphemeraSurface::Water) = 0.75f * Water;
		// The plants about the legs, as the ground cover grows them (until it can be asked how many
		// it has grown there): heath from the edge of the bare lava, sedge and meadow grass over the
		// lichen, shorter turf among the moss cushions; none below the tide line, on steep ground or
		// on the ice. Heath is stiff stalks, the rest soft grass.
		Ground.Plants = FMath::SmoothStep(-0.25f, 0.1f, Cover) * (1.f - 0.5f * FMath::SmoothStep(0.35f, 0.8f, Cover)) * Lush
			* FMath::SmoothStep(5.f, 7.f, static_cast<float>(AltitudeM)) * (1.f - FMath::SmoothStep(34.f, 42.f, SlopeDeg))
			* (1.f - FMath::SmoothStep(0.3f, 0.45f, Ice)) * (1.f - Water);
		Ground.Stalks = 1.f - FMath::SmoothStep(-0.15f, 0.1f, Cover);
		return Ground;
	}
}

AEphemeraPlayer::AEphemeraPlayer()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, EyeHeight - CapsuleHalfHeight));
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(HorizontalFov(FieldOfView));

	Lamp = CreateDefaultSubobject<USpotLightComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Camera);
	Lamp->SetRelativeLocation(FVector(10.f, 18.f, -20.f));
	Lamp->SetVisibility(false);
	Lamp->IntensityUnits = ELightUnits::Lumens;
	// By night a strong hand lamp (LampLumensNight); as the day comes up it is given more and more
	// (UpdateLamp), because a lamp that is true to life is lost at dusk and gone by day, and a key
	// that does nothing reads as a fault (the owner: "its light is too low").
	Lamp->Intensity = LampLumensNight;
	// Sixty metres: past that its light is lost in the night anyway, and its shadows cost by the area it reaches.
	Lamp->AttenuationRadius = 6000.f;
	Lamp->OuterConeAngle = 40.f;
	Lamp->InnerConeAngle = 13.f;
	Lamp->SourceRadius = 2.f;
	Lamp->SetLightColor(FLinearColor(1.f, 0.95f, 0.85f));
	// In mist the lamp is a beam: the air it crosses is lit (three times what the bare physics
	// gives, as a torch in real night mist looks), and rocks and the land cut the beam.
	Lamp->VolumetricScatteringIntensity = 3.f;
	Lamp->bCastVolumetricShadow = true;

	// The jets burn: a blue-white glow under the body while they fire, on the ground beneath and in
	// any mist, flickering as a flame does. (Until now they pushed and were heard and nothing was seen.)
	JetGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("JetGlow"));
	JetGlow->SetupAttachment(GetCapsuleComponent());
	JetGlow->SetRelativeLocation(FVector(-25.f, 0.f, -70.f));
	JetGlow->IntensityUnits = ELightUnits::Lumens;
	JetGlow->Intensity = 0.f;
	JetGlow->AttenuationRadius = 2600.f;
	JetGlow->SourceRadius = 14.f;
	JetGlow->SetLightColor(FLinearColor(0.62f, 0.80f, 1.0f));
	JetGlow->SetCastShadows(false);
	JetGlow->VolumetricScatteringIntensity = 2.f;
	JetGlow->SetVisibility(false);

	// And are seen: a flame under each side of the pack, a cone of the engine's with its point turned
	// down (UpdateEffects sizes it to the thrust; its material, from the sky rig, is the fire).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		UStaticMeshComponent* Flame = CreateDefaultSubobject<UStaticMeshComponent>(Side == 0 ? TEXT("JetFlameLeft") : TEXT("JetFlameRight"));
		Flame->SetupAttachment(GetCapsuleComponent());
		if (Cone.Succeeded())
		{
			Flame->SetStaticMesh(Cone.Object);
		}
		Flame->SetRelativeRotation(FRotator(180.f, 0.f, 0.f));
		Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Flame->SetCastShadow(false);
		Flame->bAffectDistanceFieldLighting = false;
		Flame->bAffectDynamicIndirectLighting = false;
		Flame->bVisibleInRayTracing = false;
		Flame->SetCanEverAffectNavigation(false);
		Flame->SetVisibility(false);
		(Side == 0 ? JetFlameLeft : JetFlameRight) = Flame;
	}
	// Dust stays where it was thrown while the body goes on: these are placed in the world, not carried.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Puff = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Puff%d"), Index));
		Puff->SetupAttachment(GetCapsuleComponent());
		Puff->SetUsingAbsoluteLocation(true);
		Puff->SetUsingAbsoluteRotation(true);
		Puff->SetUsingAbsoluteScale(true);
		Puff->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Puff->SetCastShadow(false);
		Puff->bAffectDistanceFieldLighting = false;
		Puff->bAffectDynamicIndirectLighting = false;
		Puff->bVisibleInRayTracing = false;
		Puff->SetCanEverAffectNavigation(false);
		// The mesh is a metre of seeds; the dust its material draws from them runs out several metres.
		Puff->SetBoundsScale(8.f);
		Puff->SetVisibility(false);
		Puffs.Add(Puff);
	}

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = GroundAcceleration;
	// A change of direction swings the body's travel round eight times a second: a curve, not a skid.
	Move->GroundFriction = 8.f;
	// Stopping (see GroundGrip). The grip is the ground's alone: in the air and in flight the
	// engine is given none, so a jump keeps the speed it was made with.
	Move->bUseSeparateBrakingFriction = false;
	Move->BrakingFrictionFactor = GroundGrip / Move->GroundFriction;
	Move->BrakingDecelerationWalking = GroundBraking;
	Move->SetWalkableFloorAngle(45.f);
	Move->MaxStepHeight = 40.f;
	Move->bOrientRotationToMovement = false;
	// In the air the engine leaves the speed alone: steering and the jets are ApplyFlight's.
	Move->AirControl = 0.f;
	Move->BrakingDecelerationFalling = 0.f;
	Move->FallingLateralFriction = 0.f;
	Move->bApplyGravityWhileJumping = true;
	Move->MaxFlySpeed = 1.e9f;
	Move->BrakingDecelerationFlying = 0.f;
	Move->GetNavAgentPropertiesRef().bCanJump = true;

	Audio = CreateDefaultSubobject<UEphemeraPlayerAudio>(TEXT("Audio"));
}

void AEphemeraPlayer::BeginPlay()
{
	Super::BeginPlay();
	BaseMouseDegreesPerCount = MouseDegreesPerCount;
	UEphemeraUserSettings::Get()->BringUpToDate();
	UEphemeraUserSettings::Get()->ApplyRendering();
	ApplyUserSettings();
	if (UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>())
	{
		Frames->RegisterRider(this);
		Frames->SetObserver(this);
		RideHandle = Frames->OnFrameUpdated.AddUObject(this, &AEphemeraPlayer::RideNearestBody);
	}
	if (GEngine && GEngine->GameViewport && !Hud.IsValid())
	{
		Hud = SNew(SEphemeraHud).Player(this);
		GEngine->GameViewport->AddViewportWidgetContent(Hud.ToSharedRef(), 50);
	}
	// The engine's own notices (red and yellow lines across the screen) are for developers: they
	// still go to the log, where they are counted and read (Tools/winsync.py logs), but a player
	// does not see them.
	GAreScreenMessagesEnabled = false;
	if (Audio)
	{
		Audio->Start(GetDefault<UEphemeraSettings>()->AudioSet.LoadSynchronous());
	}
	bMuted = FParse::Param(FCommandLine::Get(), TEXT("EphemeraMute"));
	// A photograph has its own camera and waits in its own way; everything else starts behind the
	// loading screen until there is ground to stand on and the world round it has grown in.
	bLoading = !FCString::Strifind(FCommandLine::Get(), TEXT("-EphemeraPhoto"));
	if (Hud.IsValid())
	{
		Hud->SetLoading(bLoading, NSLOCTEXT("Ephemera", "Starting", "Starting"));
	}
}

void AEphemeraPlayer::UpdateLoading()
{
	// What "ready" means, in order, each stage behind the loading screen:
	//   1. the engine has nothing left to compile and the player stands on a world;
	//   2. everything that grows within sight has been grown;
	//   3. the player has been turned once right round, so that whatever any direction needs
	//      (textures at full detail, fine geometry, the shaders' pipeline states) has been asked
	//      for while nobody can see it arriving;
	//   4. nothing is still streaming in, and the light has had a few seconds to settle.
	// Without 3 and 4 the screen lifted on a world still sharpening and popping in.
	const float Seconds = FMath::Min(static_cast<float>(FApp::GetDeltaTime()), 0.1f);
	LoadSeconds += static_cast<float>(FApp::GetDeltaTime());
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const AEphemeraBody* Ground = Frames ? Frames->GetReference() : nullptr;
	const int32 Streaming = IStreamingManager::Get().GetRenderAssetStreamingManager().GetNumWantingResources()
		+ static_cast<int32>(PipelineStateCache::NumActivePrecacheRequests());
	FText What;
	float Progress = 0.f;
	if (Hud.IsValid() && Hud->IsPreparing())
	{
		What = NSLOCTEXT("Ephemera", "LoadShaders", "Preparing shaders and meshes");
		Progress = 0.05f;
	}
	else if (!Tags.Contains(PlacedTag) || !Ground)
	{
		What = NSLOCTEXT("Ephemera", "LoadGround", "Finding the ground");
		Progress = 0.1f;
	}
	else if (!Ground->GroundReady())
	{
		What = NSLOCTEXT("Ephemera", "LoadLand", "Growing the land");
		Progress = 0.25f;
	}
	else if (WarmTurnDeg < 360.f)
	{
		What = NSLOCTEXT("Ephemera", "LoadLook", "Loading the view in every direction");
		// A steady 60 degrees a second, never more than a few degrees a frame however slow the frame.
		const float Step = FMath::Min(60.f * Seconds, 360.f - WarmTurnDeg);
		AddActorLocalRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(Step)), false, nullptr, ETeleportType::TeleportPhysics);
		WarmTurnDeg += Step;
		Progress = 0.4f + 0.4f * WarmTurnDeg / 360.f;
	}
	else
	{
		What = NSLOCTEXT("Ephemera", "LoadLast", "Finishing textures and light");
		// Quiet for two seconds on end, and at least three in this stage.
		QuietSeconds = Streaming == 0 ? QuietSeconds + Seconds : 0.f;
		ReadySeconds += Seconds;
		Progress = 0.8f + 0.2f * FMath::Clamp(FMath::Min(ReadySeconds / 3.f, QuietSeconds / 2.f), 0.f, 1.f);
		if ((ReadySeconds > 3.f && QuietSeconds > 2.f) || ReadySeconds > 25.f)
		{
			bLoading = false;
		}
	}
	if (LoadSeconds > 300.f)
	{
		bLoading = false;
	}
	if (!bLoading)
	{
		UE_LOG(LogTemp, Display, TEXT("Ephemera: ready to play after %.1f s (%d things still streaming)"), LoadSeconds, Streaming);
		// For tests: -EphemeraLamp starts with the lamp lit; -EphemeraFly=<body>,<metres> starts in
		// free flight that high over a body, on the side facing where the player stood, looking at it.
		if (FParse::Param(FCommandLine::Get(), TEXT("EphemeraLamp")))
		{
			Lamp->SetVisibility(true);
		}
		// -EphemeraPitch=-80 starts looking that far up (or, negative, down): to photograph what is at the feet.
		float StartPitch = 0.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraPitch="), StartPitch))
		{
			Pitch = FMath::Clamp(StartPitch, -89.f, 89.f);
		}
		FString Fly;
		TArray<FString> Parts;
		if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraFly="), Fly, false) && Fly.ParseIntoArray(Parts, TEXT(",")) == 2 && Frames)
		{
			if (const AEphemeraBody* Body = Frames->FindBody(FName(*Parts[0])))
			{
				const FVector Out = (GetActorLocation() - Body->GetActorLocation()).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
				SetFreeFlight(true);
				SetActorLocationAndRotation(Body->GetActorLocation() + Out * (Body->RadiusCm() + FCString::Atod(*Parts[1]) * 100.0),
					(-Out).ToOrientationQuat(), false, nullptr, ETeleportType::TeleportPhysics);
				Pitch = 0.f;
				Camera->SetRelativeRotation(FRotator::ZeroRotator);
				GetCharacterMovement()->Velocity = FVector::ZeroVector;
				UE_LOG(LogTemp, Display, TEXT("Ephemera test: free flight %.0f m over %s"), FCString::Atod(*Parts[1]), *Body->BodyId().ToString());
				// -EphemeraFlyUp: as if the rise key were held, which from this pose is flight along the ground.
				if (FParse::Param(FCommandLine::Get(), TEXT("EphemeraFlyUp")))
				{
					JetInput = 1.f;
				}
			}
		}
		// -EphemeraDrive=w:2,w+shift:2,space:0.1,:1 holds keys down for so many seconds each (RunDrive).
		FString Script;
		if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraDrive="), Script, false))
		{
			TArray<FString> Steps;
			Script.ParseIntoArray(Steps, TEXT(","));
			for (const FString& Step : Steps)
			{
				FString Keys, HowLong;
				if (Step.Split(TEXT(":"), &Keys, &HowLong))
				{
					Drive.Emplace(FCString::Atof(*HowLong), Keys.ToLower());
				}
			}
		}
	}
	if (Hud.IsValid())
	{
		Hud->SetLoading(bLoading, What, Progress);
	}
}

void AEphemeraPlayer::ApplyUserSettings()
{
	const UEphemeraUserSettings* S = UEphemeraUserSettings::Get();
	FieldOfView = FMath::Clamp(S->FieldOfView, 40.f, 110.f);
	Camera->SetFieldOfView(HorizontalFov(FieldOfView));
	MouseDegreesPerCount = BaseMouseDegreesPerCount * FMath::Clamp(S->MouseSensitivity, 0.05f, 10.f);
}

void AEphemeraPlayer::ToggleMenu()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!Hud.IsValid() || !PC)
	{
		return;
	}
	const bool bOpen = !Hud->IsMenuOpen();
	if (Audio)
	{
		Audio->Play(bOpen ? TEXT("ui_open") : TEXT("ui_close"), 0.6f, 1.f, false);
	}
	Hud->SetMenuOpen(bOpen);
	PC->SetShowMouseCursor(bOpen);
	if (bOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(Hud);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		MoveInput = FVector2D::ZeroVector;
		StickLook = FVector2D::ZeroVector;
		JetInput = 0.f;
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void AEphemeraPlayer::ToggleFps()
{
	UEphemeraUserSettings* S = UEphemeraUserSettings::Get();
	S->bShowFps = !S->bShowFps;
	S->Save();
}

void AEphemeraPlayer::ReliefChanged()
{
	// The ground has just moved under everything (its relief became geometry, or stopped being):
	// each world tells its terrain, and what grows on it is grown again at the new height.
	for (TActorIterator<AEphemeraBody> It(GetWorld()); It; ++It)
	{
		It->ReliefChanged();
	}
}

void AEphemeraPlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Hud.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
	}
	Hud.Reset();
	if (UWorld* World = GetWorld())
	{
		if (UEphemeraFrameSubsystem* Frames = World->GetSubsystem<UEphemeraFrameSubsystem>())
		{
			Frames->UnregisterRider(this);
			Frames->OnFrameUpdated.Remove(RideHandle);
		}
	}
	Super::EndPlay(EndPlayReason);
}

FVector AEphemeraPlayer::GetFrameVelocity() const
{
	return GetCharacterMovement()->Velocity;
}

void AEphemeraPlayer::RebaseToFrame(const FTransform& OldToNew, const FVector& NewVelocity)
{
	FTransform Pose = GetActorTransform() * OldToNew;
	Pose.SetScale3D(FVector::OneVector);
	SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
	// Free flight is a camera, not a body: it carries no momentum from one world's frame into the
	// next (which would be the two worlds' whole relative speed, hundreds of metres a second).
	GetCharacterMovement()->Velocity = bFreeFlight ? FVector::ZeroVector : NewVelocity;
	GetCharacterMovement()->UpdateComponentVelocity();
	// Every body is somewhere else in the new frame's numbers: where the ridden one "was" means nothing now.
	bRideReset = true;
	bHaveLastPlace = false;
}

void AEphemeraPlayer::PlaceAtStart()
{
	const UEphemeraSettings* Settings = GetDefault<UEphemeraSettings>();
	UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	// The day begins where the settings say (a test or a photograph that names its own time sets it after this).
	FString Unused;
	if (Frames && !FParse::Value(FCommandLine::Get(), TEXT("EphemeraTime="), Unused) && !FParse::Value(FCommandLine::Get(), TEXT("EphemeraPhotoList="), Unused))
	{
		Frames->SetTime(Settings->StartTimeSeconds);
	}
	const AEphemeraBody* Body = Frames ? Frames->FindBody(Settings->StartBody) : nullptr;
	if (!Body || !Body->Definition)
	{
		return;
	}
	FVector StartDirection = Settings->StartDirection;
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraStart="), Override, false))      // (a list: do not stop at its commas)
	{
		TArray<FString> Parts;
		Override.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			StartDirection = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
		}
	}
	const FVector LocalDirection = StartDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const FTransform BodyPose = Body->GetActorTransform();
	const FVector Up = BodyPose.TransformVectorNoScale(LocalDirection);
	const FVector Ground = BodyPose.TransformPosition(FEphemeraTerrain::SurfacePointCm(*Body->Definition, LocalDirection));
	FVector Forward = FVector::VectorPlaneProject(BodyPose.GetUnitAxis(EAxis::X), Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(BodyPose.GetUnitAxis(EAxis::Y), Up).GetSafeNormal();
	}
	SetActorLocationAndRotation(Ground + Up * (CapsuleHalfHeight + 30.f), FRotationMatrix::MakeFromZX(Up, Forward).ToQuat(),
		false, nullptr, ETeleportType::TeleportPhysics);
	// At rest on the ground means moving with the ground. On the first frame the world is still in
	// the star's frame, where Dawnhollow's surface goes by at some 300 m/s: a player set down with no
	// velocity was left behind by the planet and found themselves kilometres up within seconds.
	GetCharacterMovement()->Velocity = Frames->PointVelocity(Body, BodyPose.InverseTransformPosition(GetActorLocation()));
	GetCharacterMovement()->UpdateComponentVelocity();
	UE_LOG(LogTemp, Display, TEXT("Ephemera player: placed on %s, %.1f m above its datum, frame reference %s"), *Body->BodyId().ToString(),
		Body->AltitudeCm(GetActorLocation()) / 100.0, Frames->GetReference() ? *Frames->GetReference()->BodyId().ToString() : TEXT("none"));
}

// ------------------------------------------------------------------------------------- input ---

void AEphemeraPlayer::PawnClientRestart()
{
	Super::PawnClientRestart();
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Input || !Input->OnFootContext)
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(Input->OnFootContext, 0);
	}
}

void AEphemeraPlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	Input = GetDefault<UEphemeraSettings>()->InputConfig.LoadSynchronous();
	UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Enhanced || !Input)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera: no input config (set [/Script/Ephemera.EphemeraSettings] InputConfig in DefaultGame.ini)."));
		return;
	}
	auto Bind = [&](const UInputAction* Action, ETriggerEvent Event, void (AEphemeraPlayer::*Handler)(const FInputActionValue&))
	{
		if (Action)
		{
			Enhanced->BindAction(Action, Event, this, Handler);
		}
	};
	// The menu and the frame-rate readout are plain keys, the same on every layout.
	PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AEphemeraPlayer::ToggleMenu);
	PlayerInputComponent->BindKey(EKeys::F1, IE_Pressed, this, &AEphemeraPlayer::ToggleMenu);
	PlayerInputComponent->BindKey(EKeys::F3, IE_Pressed, this, &AEphemeraPlayer::ToggleFps);
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AEphemeraPlayer::OnLevel);
	Bind(Input->Move, ETriggerEvent::Triggered, &AEphemeraPlayer::OnMove);
	Bind(Input->Move, ETriggerEvent::Completed, &AEphemeraPlayer::OnMoveEnd);
	Bind(Input->LookMouse, ETriggerEvent::Triggered, &AEphemeraPlayer::OnLookMouse);
	Bind(Input->LookStick, ETriggerEvent::Triggered, &AEphemeraPlayer::OnLookStick);
	Bind(Input->LookStick, ETriggerEvent::Completed, &AEphemeraPlayer::OnLookStickEnd);
	Bind(Input->Jump, ETriggerEvent::Started, &AEphemeraPlayer::OnJumpStart);
	Bind(Input->Jump, ETriggerEvent::Completed, &AEphemeraPlayer::OnJumpEnd);
	Bind(Input->Jets, ETriggerEvent::Triggered, &AEphemeraPlayer::OnJets);
	Bind(Input->Jets, ETriggerEvent::Completed, &AEphemeraPlayer::OnJetsEnd);
	Bind(Input->Boost, ETriggerEvent::Started, &AEphemeraPlayer::OnBoost);
	Bind(Input->Boost, ETriggerEvent::Completed, &AEphemeraPlayer::OnBoostEnd);
	Bind(Input->Lamp, ETriggerEvent::Started, &AEphemeraPlayer::OnLamp);
	Bind(Input->Fly, ETriggerEvent::Started, &AEphemeraPlayer::OnFly);
	Bind(Input->FlySpeed, ETriggerEvent::Triggered, &AEphemeraPlayer::OnFlySpeed);
}

void AEphemeraPlayer::OnMove(const FInputActionValue& Value) { MoveInput = Value.Get<FVector2D>().GetClampedToMaxSize(1.0); }
void AEphemeraPlayer::OnMoveEnd(const FInputActionValue&) { MoveInput = FVector2D::ZeroVector; }
void AEphemeraPlayer::OnLookMouse(const FInputActionValue& Value) { MouseLook += Value.Get<FVector2D>() * MouseDegreesPerCount; }
void AEphemeraPlayer::OnLookStick(const FInputActionValue& Value) { StickLook = Value.Get<FVector2D>(); }
void AEphemeraPlayer::OnLookStickEnd(const FInputActionValue&) { StickLook = FVector2D::ZeroVector; }
void AEphemeraPlayer::OnJets(const FInputActionValue& Value) { JetInput = Value.Get<float>(); }
void AEphemeraPlayer::OnJetsEnd(const FInputActionValue&) { JetInput = 0.f; }
void AEphemeraPlayer::OnBoost(const FInputActionValue&) { bBoost = true; }
void AEphemeraPlayer::OnBoostEnd(const FInputActionValue&) { bBoost = false; }
void AEphemeraPlayer::OnLamp(const FInputActionValue&)
{
	Lamp->ToggleVisibility();
	if (Audio)
	{
		Audio->Play(Lamp->IsVisible() ? TEXT("lamp_on") : TEXT("lamp_off"), 0.8f, 1.f, false);
	}
	if (Hud.IsValid())
	{
		Hud->Note(Lamp->IsVisible() ? NSLOCTEXT("Ephemera", "LampOn", "Lamp on") : NSLOCTEXT("Ephemera", "LampOff", "Lamp off"));
	}
}

void AEphemeraPlayer::OnFly(const FInputActionValue&)
{
	SetFreeFlight(!bFreeFlight);
	if (Audio)
	{
		Audio->Play(bFreeFlight ? TEXT("fly_on") : TEXT("fly_off"), 0.6f, 1.f, false);
	}
	if (Hud.IsValid())
	{
		Hud->Note(bFreeFlight ? NSLOCTEXT("Ephemera", "FlyOn", "Free flight   (V to land, wheel for speed, Shift to go fast, G to level the horizon)") : NSLOCTEXT("Ephemera", "FlyOff", "On foot"));
	}
}

void AEphemeraPlayer::SetFreeFlight(const bool bOn)
{
	if (bFreeFlight == bOn)
	{
		return;
	}
	bFreeFlight = bOn;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bFreeFlight)
	{
		// A look-around mode: no gravity, no fuel, stops when you let go.
		Move->SetMovementMode(MOVE_Flying);
		RideBody = nullptr;
	}
	else
	{
		// Let go beside a body the camera was riding with: you leave with that body's speed, not at
		// rest in a frame it is crossing at hundreds of metres a second.
		const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
		if (const AEphemeraBody* Body = RideBody.Get(); Body && Frames)
		{
			Move->Velocity = Frames->PointVelocity(Body, Body->GetActorTransform().InverseTransformPosition(GetActorLocation()));
			Move->UpdateComponentVelocity();
		}
		RideBody = nullptr;
		Move->SetMovementMode(MOVE_Falling);
		RightingLeft = 0.35f;
		LevelLeft = 0.f;
		if (!bOriented)
		{
			// Back to a body adrift in space: the view's tilt goes into the body, as when leaving a world.
			AddActorLocalRotation(FRotator(Pitch, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			Pitch = 0.f;
			Camera->SetRelativeRotation(FRotator::ZeroRotator);
		}
	}
}

void AEphemeraPlayer::OnFlySpeed(const FInputActionValue& Value)
{
	FreeFlightScale = FMath::Clamp(FreeFlightScale * FMath::Pow(1.25f, Value.Get<float>()), 0.1f, 40.f);
}

void AEphemeraPlayer::OnJumpStart(const FInputActionValue&)
{
	bJumpHeld = true;
	JumpHeldFor = 0.f;
	bPressJumped = false;
	if (bFreeFlight || bLoading)
	{
		return;
	}
	// The jump happens now, on the way down of the key. With nothing to jump from, the press is
	// remembered for a moment: touch down within JumpBuffer and it is taken then.
	if (bOriented && SinceJump > 0.2f && (GetCharacterMovement()->IsMovingOnGround() || SinceGrounded < CoyoteTime))
	{
		DoJump();
	}
	else
	{
		SinceJumpAsked = 0.f;
	}
}

void AEphemeraPlayer::OnJumpEnd(const FInputActionValue&)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	// Let go early and the rise is cut short: a tap is a hop, a held key the whole jump (and then the jets).
	if (bPressJumped && SinceJump < JumpCutWindow && !bFreeFlight && !Move->IsMovingOnGround())
	{
		const FVector Up = GetActorUpVector();
		const double Rising = FVector::DotProduct(Move->Velocity, Up);
		if (Rising > 0.0)
		{
			Move->Velocity -= Up * Rising * (1.0 - JumpCut);
		}
	}
	bJumpHeld = false;
	bPressJumped = false;
	JumpHeldFor = 0.f;
}

void AEphemeraPlayer::DoJump()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const FVector Up = GetActorUpVector();
	// The jump sets the speed off the ground, whatever it was: the same on a slope as on the flat.
	Move->Velocity = FVector::VectorPlaneProject(Move->Velocity, Up) + Up * JumpSpeed;
	Move->SetMovementMode(MOVE_Falling);
	SinceGrounded = CoyoteTime;
	SinceJump = 0.f;
	SinceJumpAsked = 1000.f;
	bPressJumped = bJumpHeld;
	// The eye is left behind for an instant, as a head is.
	ViewDropSpeed -= 55.f;
	// A jump is work, and is heard as the push off the ground, the suit swinging up, the pack.
	Tread.Exertion = FMath::Min(1.f, Tread.Exertion + 0.04f);
	if (Audio)
	{
		++StepsHeard;
		Audio->PlayStep(MakeStep(EEphemeraStepKind::Takeoff), Space);
	}
}

void AEphemeraPlayer::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	// Called while the speed of the fall is still there to be read; Tick answers it.
	LandedAt = FMath::Max(0.f, static_cast<float>(-FVector::DotProduct(GetCharacterMovement()->Velocity, GetActorUpVector())));
}

// -------------------------------------------------------------------------------------- tick ---

void AEphemeraPlayer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	if (!Frames)
	{
		return;
	}
	if (bLoading)
	{
		UpdateLoading();
	}
	if (!HasActorBegunPlay() || Frames->GetTime() <= 0.0)
	{
		return;
	}
	if (!Tags.Contains(PlacedTag))
	{
		// Set down twice. The first time gets the player to the starting world, which makes that
		// world the frame at rest; only then, with the ground standing still, is the player set
		// down for good. Set down once, in the star's frame, the player and the ground each
		// travelled on for the rest of that frame at 300 m/s by their own arithmetic, and on a long
		// first frame (the PC's) came out of it a hundred metres apart: the game began with a fall.
		const AEphemeraBody* StartBody = Frames->FindBody(GetDefault<UEphemeraSettings>()->StartBody);
		PlaceAtStart();
		if (!StartBody || Frames->GetReference() == StartBody || ++PlaceTries > 30)
		{
			Tags.Add(PlacedTag);
		}
		return;
	}

	// Something is wrong when one frame takes a second: say so where it can be found afterwards.
	if (FApp::GetDeltaTime() > 1.0 && !bLoading)
	{
		UE_LOG(LogTemp, Warning, TEXT("Ephemera: a frame took %.1f s (frame of %s, %.0f m over the nearest world, moving at %.0f m/s)"),
			FApp::GetDeltaTime(), Frames->GetReference() ? *Frames->GetReference()->BodyId().ToString() : TEXT("open space"),
			Frames->HeightOverNearestBodyCm(GetActorLocation()) / 100.0, GetVelocity().Size() / 100.0);
	}
	if (bLoading || (Hud.IsValid() && Hud->IsMenuOpen()))
	{
		// The mouse and keys belong to the menu.
		MoveInput = FVector2D::ZeroVector;
		MouseLook = FVector2D::ZeroVector;
		StickLook = FVector2D::ZeroVector;
		JetInput = 0.f;
		bJumpHeld = false;
	}
	else if (!Drive.IsEmpty())
	{
		RunDrive(DeltaSeconds);
	}
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Gravity = Frames->GravityAt(GetActorLocation());
	const float G = Gravity.Size();
	// Only a planet or moon gives you a "down". The star's pull is felt by the ship, but it never
	// turns your body: open space is free look in every direction (as in Outer Wilds).
	const bool bWasOriented = bOriented;
	bOriented = G > ZeroGThreshold && Frames->DominantBody(GetActorLocation()) != nullptr;
	if (bWasOriented && !bOriented && !bFreeFlight)
	{
		// Leaving a world: fold the camera's pitch into the body so free look continues from the same view.
		AddActorLocalRotation(FRotator(Pitch, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Pitch = 0.f;
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
	}
	if (bOriented)
	{
		Move->SetGravityDirection(Gravity / G);
		Move->GravityScale = bFreeFlight ? 0.f : G / WorldGravity;
	}
	else
	{
		Move->GravityScale = 0.f;
		if (Move->IsMovingOnGround())
		{
			Move->SetMovementMode(MOVE_Falling);
		}
	}
	if (bFreeFlight && Move->MovementMode != MOVE_Flying)
	{
		Move->SetMovementMode(MOVE_Flying);     // landing would otherwise drop back to walking
	}
	AlignToGravity(DeltaSeconds);
	ApplyLook(DeltaSeconds);
	SenseAir();
	SenseSea(DeltaSeconds);

	if (bFreeFlight)
	{
		LevelView(DeltaSeconds);
		ApplyFreeFlight(DeltaSeconds);
		JetThrust = 0.f;
		JetBoosted = 0.f;
		LandedAt = -1.f;
		UpdateView(DeltaSeconds, false);
		UpdateSound(DeltaSeconds);
		bWasGrounded = false;
		return;
	}
	bool bGrounded = Move->IsMovingOnGround();
	SinceGrounded = bGrounded ? 0.f : SinceGrounded + DeltaSeconds;
	GroundedFor = bGrounded ? GroundedFor + DeltaSeconds : 0.f;
	SinceJump += DeltaSeconds;
	SinceJumpAsked += DeltaSeconds;
	SinceJets += DeltaSeconds;
	SinceJetKick += DeltaSeconds;
	if (bJumpHeld)
	{
		JumpHeldFor += DeltaSeconds;
	}
	if (LandedAt >= 0.f)
	{
		// Touchdown: the knees give by an amount that grows with the fall, and the ground is heard.
		if (!bLoading)
		{
			ViewDropSpeed -= FMath::Min(LandedAt * 0.5f, 520.f);
			if (Audio && LandedAt > 120.f)
			{
				// What is landed on is the ground here, not the mix the take-off left behind; and a
				// hard landing takes it out of the body.
				SampleGround(1.f);
				Tread.FallMS = LandedAt / 100.f;
				Tread.Exertion = FMath::Min(1.f, Tread.Exertion + 0.12f * FMath::SmoothStep(3.f, 12.f, Tread.FallMS));
				++StepsHeard;
				Audio->PlayStep(MakeStep(EEphemeraStepKind::Land), Space);
				// Both feet have just come down: the stride goes on from here, not from where it was
				// in the air, and the landing is the end of any run of strides before it.
				Stride = 0.3f;
				StepsInRun = 0;
			}
			// And seen: the harder the landing, the more of the ground goes up round the boots.
			if (LandedAt > 220.f && Submerged < 0.5f)
			{
				const float Hard = FMath::Clamp(LandedAt / 1300.f, 0.f, 1.f);
				ThrowDust(GetActorLocation() - GetActorUpVector() * CapsuleHalfHeight, GetActorUpVector(), FMath::Lerp(0.3f, 1.f, Hard), FMath::Lerp(150.f, 420.f, Hard));
			}
		}
		LandedAt = -1.f;
	}
	// A jump asked for just before touching down is taken on touching down.
	if (bGrounded && bOriented && SinceJumpAsked < JumpBuffer && SinceJump > 0.2f)
	{
		DoJump();
		bGrounded = false;
	}
	if (bGrounded)
	{
		JetThrust = 0.f;
		JetBoosted = 0.f;
		ApplyWalk(DeltaSeconds);
	}
	else
	{
		ApplyFlight(DeltaSeconds);
	}
	UpdateView(DeltaSeconds, bGrounded);
	UpdateSound(DeltaSeconds);
	bWasGrounded = bGrounded;
}

void AEphemeraPlayer::AlignToGravity(float DeltaSeconds)
{
	// Free flight is a camera, not a body: no world's pull ever turns it.
	RightingLeft = FMath::Max(0.f, RightingLeft - DeltaSeconds);
	if (!bOriented || bFreeFlight)
	{
		return;
	}
	const FVector TargetUp = -Gravity.GetSafeNormal();
	const float ErrorDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(GetActorUpVector(), TargetUp), -1.0, 1.0)));
	if (ErrorDeg < 0.01f)
	{
		return;
	}
	const bool bGrounded = GetCharacterMovement()->IsMovingOnGround();
	// Walking: the ground's curve is followed at once (it is the ground turning under the feet, and
	// is never seen). In the air a world may not swing the body round: the first version did, at
	// 150 degrees a second, and it was sickening. Now the body only leans toward feet-down, a small
	// error closing in a third of a second and a large one at no more than 30 degrees a second. The
	// one exception is the player's own act: for a third of a second after leaving free flight (the
	// key that says "put me down") the body rights itself at once.
	const float Rate = bGrounded ? 1000.f : RightingLeft > 0.f ? 720.f : FMath::Min(30.f, ErrorDeg * 3.f);
	TurnUpToward(TargetUp, FMath::Clamp(Rate * DeltaSeconds / ErrorDeg, 0.f, 1.f), !bGrounded);
}

void AEphemeraPlayer::TurnUpToward(const FVector& TargetUp, const float Alpha, const bool bKeepView)
{
	const FVector Up = GetActorUpVector();
	const FQuat Delta = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Up, TargetUp), Alpha);
	if (!bKeepView)
	{
		// On the ground the view turns with the body, like standing on any curved world.
		SetActorRotation((Delta * GetActorQuat()).GetNormalized(), ETeleportType::TeleportPhysics);
		return;
	}
	// Otherwise the body turns but the view stays on what it was looking at: only the horizon moves.
	const FVector View = Camera->GetForwardVector();
	const FVector NewUp = Delta.RotateVector(Up).GetSafeNormal();
	FVector Forward = FVector::VectorPlaneProject(View, NewUp).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(Delta.RotateVector(GetActorForwardVector()), NewUp).GetSafeNormal();
	}
	SetActorRotation(FRotationMatrix::MakeFromZX(NewUp, Forward).ToQuat(), ETeleportType::TeleportPhysics);
	Pitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(View, NewUp), -1.0, 1.0))), -89.f, 89.f);
	Camera->SetRelativeRotation(FRotator(Pitch, 0.f, ViewRoll));
}

void AEphemeraPlayer::OnLevel()
{
	// Free flight never levels itself, so a key does it when asked: the horizon of whatever world is
	// nearest is brought level over a third of a second, and what is being looked at stays put.
	if (bFreeFlight && !bLoading)
	{
		LevelLeft = 0.35f;
		if (Hud.IsValid())
		{
			Hud->Note(NSLOCTEXT("Ephemera", "Levelled", "Horizon levelled"));
		}
	}
}

void AEphemeraPlayer::LevelView(const float DeltaSeconds)
{
	if (LevelLeft <= 0.f)
	{
		return;
	}
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const AEphemeraBody* Body = Frames ? Frames->NearestBody(GetActorLocation(), RideBody.Get()) : nullptr;
	const FVector Away = Body ? (GetActorLocation() - Body->GetActorLocation()).GetSafeNormal() : FVector::ZeroVector;
	if (!Away.IsNearlyZero())
	{
		TurnUpToward(Away, FMath::Clamp(DeltaSeconds / LevelLeft, 0.f, 1.f), true);
	}
	LevelLeft = FMath::Max(0.f, LevelLeft - DeltaSeconds);
}

void AEphemeraPlayer::ApplyLook(float DeltaSeconds)
{
	// The mouse turns the view by exactly what the hand did: no smoothing, no acceleration. The
	// stick cannot do that, so it is shaped: gentle near the centre for small corrections (tilt
	// to the power 1.8), and held hard over the turn builds by half as much again over a third of
	// a second, which lets one stick both aim finely and spin round. Zoomed views turn slower.
	FVector2D Stick = StickLook;
	const float Tilt = FMath::Min(static_cast<float>(Stick.Size()), 1.f);
	if (Tilt > UE_KINDA_SMALL_NUMBER)
	{
		Stick = Stick.GetSafeNormal() * FMath::Pow(Tilt, 1.8f);
	}
	StickAtEdge = Tilt > 0.9f ? StickAtEdge + DeltaSeconds : 0.f;
	const float Spin = 1.f + 0.5f * FMath::SmoothStep(0.12f, 0.45f, StickAtEdge);
	const float FovScale = FieldOfView / 70.f;
	const float Flip = UEphemeraUserSettings::Get()->bInvertLook ? -1.f : 1.f;
	const float Yaw = MouseLook.X + Stick.X * StickYawRate * Spin * FovScale * DeltaSeconds;
	const float PitchDelta = Flip * (MouseLook.Y + Stick.Y * StickPitchRate * FovScale * DeltaSeconds);
	MouseLook = FVector2D::ZeroVector;
	// (Free flight keeps the ordinary look everywhere: turn about your own up, tilt the view. In
	// open space that up is simply the last one you had.)
	if (!bOriented && !bFreeFlight)
	{
		// Free look: there is no horizon, so the whole body turns with the view and nothing is clamped.
		if (!FMath::IsNearlyZero(Yaw) || !FMath::IsNearlyZero(PitchDelta))
		{
			AddActorLocalRotation(FRotator(PitchDelta, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
		return;
	}
	if (!FMath::IsNearlyZero(Yaw))
	{
		AddActorLocalRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw)), false, nullptr, ETeleportType::TeleportPhysics);
	}
	Pitch = FMath::Clamp(Pitch + PitchDelta, -89.f, 89.f);
	Camera->SetRelativeRotation(FRotator(Pitch, 0.f, ViewRoll));
}

void AEphemeraPlayer::ApplyWalk(const float DeltaSeconds)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	// Boost on the ground is a run, and only forwards: nobody sprints backwards.
	const bool bRun = bBoost && MoveInput.Y > 0.5f;
	const FVector Wish = GetActorForwardVector() * MoveInput.Y * (MoveInput.Y > 0.f ? 1.f : BackwardShare) + GetActorRightVector() * MoveInput.X * SidewaysShare;
	const float Tilt = FMath::Min(static_cast<float>(Wish.Size()), 1.f);
	// A run is worked up to over a third of a second; coming back down to a jog is the ground's braking.
	const float Top = bRun ? SprintSpeed : WalkSpeed;
	GroundSpeed = FMath::Max(Top > GroundSpeed ? FMath::Min(GroundSpeed + 1100.f * DeltaSeconds, Top) : Top, WalkSpeed);
	if (Tilt > 0.01f)
	{
		// Wading is slow, and slower the deeper: on the bottom it is half pace.
		Move->MaxWalkSpeed = GroundSpeed * Tilt * FMath::Lerp(1.f, 0.5f, Submerged);
		AddMovementInput(Wish.GetSafeNormal(), 1.f);
	}
	// Wholly under, the water lifts you off the bottom unless you are pushing down.
	if (Submerged > 0.97f && JetInput >= 0.f)
	{
		Move->SetMovementMode(MOVE_Falling);
	}
	// The tank fills while there is something to stand on.
	if (GroundedFor > 0.2f)
	{
		Fuel = FMath::Min(1.f, Fuel + DeltaSeconds / RefillSeconds);
	}
}

void AEphemeraPlayer::ApplyFlight(float DeltaSeconds)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	FVector Acceleration = Frames ? Frames->FictitiousAcceleration(GetActorLocation(), Move->Velocity) : FVector::ZeroVector;
	const FVector Up = GetActorUpVector();

	// The jump key is also the lift: after a jump, once it has been held a moment (so a tap stays
	// a hop and burns nothing); pressed in mid-air, at once.
	const bool bLiftKey = bJumpHeld && (!bPressJumped || JumpHeldFor >= JetDelay);
	const float Rise = FMath::Clamp(JetInput + (bLiftKey ? 1.f : 0.f), -1.f, 1.f);
	// Over a world the keys steer along its ground; adrift they steer where you look.
	const FVector Ahead = bOriented ? GetActorForwardVector() : Camera->GetForwardVector();
	const FVector Steer = (Ahead * MoveInput.Y + GetActorRightVector() * MoveInput.X).GetClampedToMaxSize(1.0);
	const bool bSteering = !Steer.IsNearlyZero(0.05);
	// Sideways the jets fire only while they are already lifting or lowering (a running jump with
	// the run key still down must not light them). Adrift they are all there is.
	const bool bSideJets = bSteering && (!bOriented || !FMath::IsNearlyZero(Rise));
	const bool bWanted = !FMath::IsNearlyZero(Rise) || bSideJets;
	if (bWanted && Fuel > 0.f)
	{
		FVector Thrust;
		float Burn;
		if (bOriented)
		{
			Thrust = Up * Rise * (Rise > 0.f ? JetLift : JetDown) + (bSideJets ? Steer * JetSide : FVector::ZeroVector);
			Burn = FMath::Abs(Rise) * (Rise > 0.f ? 1.f : 0.5f) + (bSideJets ? 0.4f * static_cast<float>(Steer.Size()) : 0.f);
			JetThrust = FMath::Clamp(FMath::Abs(Rise) + (bSideJets ? 0.6f * static_cast<float>(Steer.Size()) : 0.f), 0.35f, 1.f);
		}
		else
		{
			// Small, even thrust in every direction, and it sips the tank: this is for placing yourself.
			const FVector Push = (Up * Rise + Steer).GetClampedToMaxSize(1.0);
			Thrust = Push * JetAdrift;
			Burn = 0.25f * static_cast<float>(Push.Size());
			JetThrust = 0.5f * static_cast<float>(Push.Size());
		}
		Acceleration += Thrust * (bBoost ? JetBoost : 1.f) * FMath::Lerp(1.f, SeaJetShare, FMath::Clamp(Submerged * 2.f, 0.f, 1.f));
		// Lit while hanging or sinking slowly, the jets give a shove at once: the key is felt before the climb is seen.
		if (bOriented && Rise > 0.f && SinceJets > 0.25f && SinceJetKick > 0.8f && FVector::DotProduct(Move->Velocity, Up) < 250.0)
		{
			Move->Velocity += Up * JetKick;
			SinceJetKick = 0.f;
		}
		if (Submerged < 0.5f || bBoost)
		{
			Fuel = FMath::Max(0.f, Fuel - Burn * (bBoost ? 2.2f : 1.f) * DeltaSeconds / LiftSeconds);
		}
		JetBoosted = bBoost ? 1.f : 0.f;
		SinceJets = 0.f;
	}
	else
	{
		JetThrust = 0.f;
		JetBoosted = 0.f;
		// Left alone in the air the tank creeps back, so a long fall or a drift in space is never the end of the jets.
		if (SinceJets > 1.2f)
		{
			Fuel = FMath::Min(1.f, Fuel + DeltaSeconds * (bOriented ? 0.04f : 0.1f));
		}
		if (bWanted && !bFuelOutSaid)
		{
			bFuelOutSaid = true;
			if (Audio)
			{
				Audio->Play(TEXT("jet_empty"), 0.7f);
			}
			if (Hud.IsValid())
			{
				Hud->Note(bOriented ? NSLOCTEXT("Ephemera", "JetsOut", "Jets empty: they refill on the ground")
					: NSLOCTEXT("Ephemera", "JetsOutAdrift", "Jets empty: they refill slowly while you drift"));
			}
		}
	}
	if (Fuel > 0.25f)
	{
		bFuelOutSaid = false;
	}
	if (Submerged > 0.f)
	{
		// Afloat: the water holds the body up against the world's pull (which the movement itself applies).
		Acceleration += Up * (SeaFloat * static_cast<float>(Gravity.Size()) * Submerged);
		// Swimming: along the look under water, along the surface on it.
		if (bSteering && Submerged > 0.3f)
		{
			FVector Along = (bEyeUnder ? Camera->GetForwardVector() : Ahead) * MoveInput.Y + GetActorRightVector() * MoveInput.X;
			if (!bEyeUnder)
			{
				Along = FVector::VectorPlaneProject(Along, Up);
			}
			const FVector Direction = Along.GetSafeNormal();
			const float Wanted = (bBoost ? SwimSprint : SwimSpeed) * FMath::Min(1.f, static_cast<float>(Along.Size())) - static_cast<float>(FVector::DotProduct(Move->Velocity, Direction));
			Move->Velocity += Direction * FMath::Clamp(Wanted, 0.f, SwimAcceleration * DeltaSeconds);
		}
		// The water drags: hard at speed, and enough at the surface that a floating body settles
		// instead of bouncing on its own buoyancy.
		const FVector Through = Move->Velocity;
		Move->Velocity -= Through * FMath::Min(Through.Size() * SeaDragPerCm * Submerged * DeltaSeconds, 0.5);
		Move->Velocity -= Up * FVector::DotProduct(Move->Velocity, Up) * FMath::Min(1.8f * Submerged * (1.f - Submerged * 0.6f) * DeltaSeconds, 0.5f);
	}
	// Without the jets a body in the air can still be steered a little, as any jumper steers: up
	// to a walking pace in the direction asked, and no more however long the key is held.
	if (Submerged <= 0.3f && bOriented && bSteering && !(bSideJets && Fuel > 0.f))
	{
		const FVector Direction = Steer.GetSafeNormal();
		const float Wanted = AirSteerSpeed * static_cast<float>(Steer.Size()) - static_cast<float>(FVector::DotProduct(Move->Velocity - AirVelocity, Direction));
		Move->Velocity += Direction * FMath::Clamp(Wanted, 0.f, AirSteerAcceleration * DeltaSeconds);
	}
	// And the air pushes back (AirDragPerCm): nothing at a run, everything in a long fall.
	if (AirNow > 0.f)
	{
		const FVector Through = Move->Velocity - AirVelocity;
		Move->Velocity -= Through * FMath::Min(Through.Size() * AirDragPerCm * AirNow * DeltaSeconds, 0.5);
	}
	Move->Velocity += Acceleration * DeltaSeconds;
}

void AEphemeraPlayer::RideNearestBody()
{
	// Whatever the camera is nearest holds still for it. Only the frame's own world rests; every
	// other body crosses the frame, the star at nearly two kilometres a second as seen from
	// Dawnhollow's turning ground. A camera left at rest in the frame watched the thing it had flown
	// to slide away, or was run down by it. So each time the bodies are moved, the camera is moved
	// (and turned) exactly as the nearest one was.
	const UEphemeraFrameSubsystem* Frames = GetWorld() ? GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>() : nullptr;
	const AEphemeraBody* Body = bFreeFlight && Frames ? Frames->NearestBody(GetActorLocation(), RideBody.Get()) : nullptr;
	if (!Body)
	{
		RideBody = nullptr;
		return;
	}
	const FTransform Now = Body->GetActorTransform();
	if (RideBody.Get() == Body && !bRideReset && !Now.Equals(RidePose, 0.0))
	{
		// Its place goes with the body (so the ground it hangs over stays under it); where it looks
		// does not. Nothing but the player's own hand turns this camera.
		SetActorLocation(Now.TransformPosition(RidePose.InverseTransformPosition(GetActorLocation())), false, nullptr, ETeleportType::TeleportPhysics);
	}
	RideBody = Body;
	RidePose = Now;
	bRideReset = false;
}

void AEphemeraPlayer::ApplyFreeFlight(const float DeltaSeconds)
{
	// The star is not somewhere to be: inside it there is nothing to see and, moving at ground
	// speed, no quick way out. The camera stops just over its surface.
	if (const AEphemeraBody* Star = RideBody.Get(); Star && Star->Definition && Star->Definition->bIsStar)
	{
		const FVector Centre = Star->GetActorLocation();
		const double Least = Star->RadiusCm() * 1.04;
		const FVector Off = GetActorLocation() - Centre;
		if (Off.SizeSquared() < Least * Least)
		{
			SetActorLocation(Centre + Off.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector) * Least, false, nullptr, ETeleportType::TeleportPhysics);
			GetCharacterMovement()->Velocity = FVector::VectorPlaneProject(GetCharacterMovement()->Velocity, Off.GetSafeNormal());
		}
	}
	// Fly where you look; jump / jets go straight up and down. Boost for crossing a planet,
	// the mouse wheel for fine or very fast movement.
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const float Vertical = FMath::Clamp(JetInput + (bJumpHeld ? 1.f : 0.f), -1.f, 1.f);
	const FVector Wish = (Camera->GetForwardVector() * MoveInput.Y + Camera->GetRightVector() * MoveInput.X + GetActorUpVector() * Vertical).GetClampedToMaxSize(1.0);
	// Faster the further from any ground (see FreeFlightReachCm): the moon is seconds away from
	// orbit, and the approach slows of its own accord.
	const double Height = Frames ? Frames->HeightOverNearestBodyCm(GetActorLocation()) : 0.0;
	const float Reach = FMath::Clamp(static_cast<float>(Height / FMath::Max(FreeFlightReachCm, 1.f)), 1.f, FreeFlightReachMax);
	const float Top = FreeFlightSpeed * FreeFlightScale * Reach * (bBoost ? FreeFlightBoost : 1.f);
	// The speed asked for is eased into over a tenth of a second and out of over a little more,
	// whatever that speed is: no jolt at either end of a move, and nothing like a drift after it.
	// (The engine's own way was a straight ramp up and a straight ramp down, which is what a
	// machine does.) The view is never delayed, only the travel.
	const float Ease = Wish.IsNearlyZero() ? 0.13f : 0.09f;
	Move->Velocity = FMath::Lerp(Wish * Top, Move->Velocity, FMath::Exp(-DeltaSeconds / Ease));
	if (Wish.IsNearlyZero())
	{
		// Easing alone never quite arrives: the last of the speed would creep on for most of a
		// second. A steady brake on top brings it to a dead stop a fifth of a second after the keys.
		const double Speed = Move->Velocity.Size();
		Move->Velocity *= Speed > UE_SMALL_NUMBER ? FMath::Max(0.0, Speed - Top * 1.5 * DeltaSeconds) / Speed : 0.0;
	}
}

// ------------------------------------------------------------------------------------- view ---

void AEphemeraPlayer::UpdateView(const float DeltaSeconds, const bool bGrounded)
{
	// The eye rides on the body, and a body is not a tripod. Everything here is small, and all of
	// it but the step smoothing is scaled by the "camera motion" setting, down to nothing.
	const float Life = FMath::Clamp(UEphemeraUserSettings::Get()->CameraMotion, 0.f, 1.5f);
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const FVector Up = GetActorUpVector();
	const FVector Along = FVector::VectorPlaneProject(Move->Velocity, Up);
	const float Speed = bFreeFlight ? 0.f : static_cast<float>(Along.Size());

	// A step up or down moves the body at once and the eye a tenth of a second later. Walkable
	// ground is never steeper than 45 degrees, so anything that rose faster than it ran was a step.
	if (bGrounded && bWasGrounded && bHaveLastPlace)
	{
		const FVector Moved = GetActorLocation() - LastPlace;
		const float Rose = static_cast<float>(FVector::DotProduct(Moved, Up));
		const float Ran = static_cast<float>(FVector::VectorPlaneProject(Moved, Up).Size());
		if (FMath::Abs(Rose) > Ran * 1.2f + 1.5f && FMath::Abs(Rose) < 60.f)
		{
			StepLag = FMath::Clamp(StepLag - Rose, -45.f, 45.f);
		}
	}
	LastPlace = GetActorLocation();
	bHaveLastPlace = true;
	StepLag = FMath::FInterpTo(StepLag, 0.f, DeltaSeconds, 11.f);

	// Landings and take-offs push the eye down (ViewDropSpeed) and a spring brings it back: 2.6
	// swings a second, damped so it settles in one. Stepped so that a slow frame cannot blow it up.
	constexpr float Stiffness = 267.f;
	constexpr float Damping = 26.f;
	ViewDropSpeed = (ViewDropSpeed - DeltaSeconds * Stiffness * ViewDrop) / (1.f + DeltaSeconds * Damping + DeltaSeconds * DeltaSeconds * Stiffness);
	ViewDrop = FMath::Clamp(ViewDrop + ViewDropSpeed * DeltaSeconds, -32.f, 12.f);

	// The stride: its length grows with speed (0.9 m at a stroll, 1.7 m at a jog, 2.4 m flat out,
	// the three steps a second of someone actually running), each one is heard as the foot comes
	// down, and the eye sinks a centimetre onto it and sways a little from foot to foot. No two
	// strides are quite alike (StrideScale): a step every so many milliseconds exactly is a
	// metronome, not a walk. The bob runs on the same Stride, so what is seen keeps time with what
	// is heard.
	const bool bStriding = bGrounded && Speed > 80.f;
	StrideShow = FMath::FInterpTo(StrideShow, bStriding ? 1.f : 0.f, DeltaSeconds, bStriding ? 6.f : 10.f);
	FollowFeet(DeltaSeconds, bGrounded, Speed);
	if (bStriding)
	{
		Stride += Speed * DeltaSeconds / ((55.f + 0.23f * Speed) * StrideScale);
		if (Stride >= 1.f)
		{
			Stride = FMath::Fmod(Stride, 1.f);
			++Strides;
			StrideScale = FMath::FRandRange(0.97f, 1.03f);
			if (Audio)
			{
				FEphemeraStep Step = MakeStep(bFromRest ? EEphemeraStepKind::Start : EEphemeraStepKind::Step);
				Step.StrideSeconds = (55.f + 0.23f * Speed) * StrideScale / Speed;
				LastStride = Step;
				++StepsHeard;
				++StepsInRun;
				Audio->PlayStep(Step, Space);
			}
			bFromRest = false;
		}
	}
	const float Sway = FMath::GetMappedRangeValueClamped(FVector2f(WalkSpeed * 0.3f, SprintSpeed), FVector2f(0.5f, 1.6f), Speed) * StrideShow * Life;
	const float Pair = (static_cast<float>(Strides % 2) + Stride) * UE_PI;
	const float BobUp = -FMath::Cos(Stride * UE_TWO_PI) * Sway;
	const float BobSide = FMath::Sin(Pair) * 0.6f * Sway;
	const float BobRoll = FMath::Sin(Pair) * 0.12f * Sway;

	// A lean into sideways travel (under a degree), and a view that widens with speed: a run, a
	// boost, a long fall.
	const float Sideways = bFreeFlight ? 0.f : static_cast<float>(FVector::DotProduct(Along, GetActorRightVector())) / WalkSpeed;
	ViewRoll = FMath::FInterpTo(ViewRoll, FMath::Clamp(Sideways, -1.5f, 1.5f) * 0.9f * Life, DeltaSeconds, 9.f);
	float Wider;
	if (bFreeFlight)
	{
		Wider = bBoost && !Move->Velocity.IsNearlyZero(50.0) ? 6.f : 0.f;
	}
	else
	{
		Wider = 4.f * FMath::Clamp((Speed - WalkSpeed) / (SprintSpeed - WalkSpeed), 0.f, 1.f) * (bGrounded ? 1.f : 0.5f) + 3.5f * JetBoosted * JetThrust
			+ 5.f * FMath::SmoothStep(2500.f, 9000.f, static_cast<float>((Move->Velocity - AirVelocity).Size()));
	}
	FovKick = FMath::FInterpTo(FovKick, Wider * Life, DeltaSeconds, 5.f);

	Camera->SetFieldOfView(HorizontalFov(FieldOfView + FovKick));
	Camera->SetRelativeLocation(FVector(0.f, BobSide, EyeHeight - CapsuleHalfHeight + ViewDrop * Life + StepLag + BobUp));
	Camera->SetRelativeRotation(FRotator(Pitch, 0.f, ViewRoll + BobRoll));
}

// ------------------------------------------------------------------------------------ sound ---

FName AEphemeraPlayer::GroundUnderfoot() const
{
	// Nothing is painted on the ground to say what it is: the terrain already knows (it is what
	// the ground's material and what grows on it are decided by), so the feet ask it too.
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const AEphemeraBody* Body = Frames ? Frames->NearestBody(GetActorLocation()) : nullptr;
	const UEphemeraBodyDefinition* D = Body ? Body->Definition.Get() : nullptr;
	if (!D)
	{
		return TEXT("rock");
	}
	if (D->AtmosphereHeightM <= 0.0)
	{
		return TEXT("dust");
	}
	const FVector Direction = Body->GetActorTransform().InverseTransformPosition(GetActorLocation()).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	if (D->OceanMesh && FEphemeraTerrain::HeightM(D->Terrain, D->RadiusM, Direction) < Body->TideM(GetActorLocation()) + 0.1)
	{
		return TEXT("water");
	}
	if (FEphemeraTerrain::Ice(D->Terrain, Direction) > 0.5)
	{
		return TEXT("snow");
	}
	if (D->StreamMesh && FEphemeraTerrain::FlowAsShaded(D->Terrain, Direction) > 0.66)
	{
		return TEXT("water");
	}
	const double Cover = FEphemeraTerrain::Cover(D->Terrain, Direction);
	return Cover > 0.3 ? TEXT("moss") : Cover < -0.6 ? TEXT("sand") : Cover < -0.2 ? TEXT("gravel") : TEXT("rock");
}

void AEphemeraPlayer::SampleGround(const float Alpha)
{
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const FVector Up = GetActorUpVector();
	const FVector Foot = GetActorLocation() - Up * CapsuleHalfHeight;
	const FVector Travel = FVector::VectorPlaneProject(GetCharacterMovement()->Velocity, Up).GetSafeNormal();
	// Under the foot and half a metre on, where the next one lands: a border is heard coming.
	const FGroundSound Here = GroundSoundAt(Frames, Foot);
	const FGroundSound Ahead = Travel.IsZero() ? Here : GroundSoundAt(Frames, Foot + Travel * 50.0);
	// The first look at the ground is taken whole: there is nothing yet to ease from.
	float Known = 0.f;
	for (const float Share : Tread.Mix)
	{
		Known += Share;
	}
	const float Toward = Known > 0.f ? Alpha : 1.f;
	for (int32 Index = 0; Index < SurfaceKinds; ++Index)
	{
		Tread.Mix[Index] = FMath::Lerp(Tread.Mix[Index], 0.5f * (Here.Mix[Index] + Ahead.Mix[Index]), Toward);
	}
	Tread.Vegetation = FMath::Lerp(Tread.Vegetation, 0.5f * (Here.Plants + Ahead.Plants), Toward);
	Stalks = FMath::Lerp(Stalks, 0.5f * (Here.Stalks + Ahead.Stalks), Toward);
	StreamUnderfoot = Here.Stream;
	// The slope along the way the body is going: uphill where the ground leans back against it.
	Tread.SlopeDeg = Travel.IsZero() || Here.Normal.IsZero() ? 0.f
		: static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-FVector::DotProduct(Here.Normal, Travel), FVector::DotProduct(Here.Normal, Up))));
}

FEphemeraStep AEphemeraPlayer::MakeStep(const EEphemeraStepKind Kind) const
{
	FEphemeraStep Step = Tread;
	Step.Kind = Kind;
	// The recordings are kept apart by foot, and the feet take turns.
	Step.Foot = (Strides & 1) ? EEphemeraFoot::Right : EEphemeraFoot::Left;
	return Step;
}

void AEphemeraPlayer::FollowFeet(const float DeltaSeconds, const bool bGrounded, const float Speed)
{
	const float Pace = bGrounded ? Speed / 100.f : 0.f;
	Tread.SpeedMS = Pace;
	Tread.Intensity = FMath::SmoothStep(0.8f, 8.f, Pace);
	// The gait, with 0.2 m/s between going up into the next and coming back down out of it, so that
	// a pace on a border keeps one.
	if (bGrounded)
	{
		int32 Gait = static_cast<int32>(Tread.Gait);
		while (Gait < static_cast<int32>(EEphemeraGait::Sprint) && Pace > GaitTop[Gait] + 0.1f)
		{
			++Gait;
		}
		while (Gait > 0 && Pace < GaitTop[Gait - 1] - 0.1f)
		{
			--Gait;
		}
		Tread.Gait = static_cast<EEphemeraGait>(Gait);
	}
	// How fast the body is turning about its up (the look turns the body, so this is the look's yaw).
	const FVector Up = GetActorUpVector();
	const FVector Facing = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
	if (DeltaSeconds > 0.f && !LastFacing.IsZero() && !Facing.IsZero())
	{
		const float Turned = static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Facing, LastFacing), -1.0, 1.0))));
		Tread.YawRateDegS = FMath::FInterpTo(Tread.YawRateDegS, Turned / DeltaSeconds, DeltaSeconds, 25.f);
	}
	LastFacing = Facing;
	// The ground, ten times a second while on it, eased toward over 0.4 s: one ground fades into
	// the next over a stride or two, never at a line.
	SinceGroundSampled += DeltaSeconds;
	if (bGrounded && SinceGroundSampled >= 0.1f)
	{
		SampleGround(1.f - FMath::Exp(-SinceGroundSampled / 0.4f));
		SinceGroundSampled = 0.f;
	}
	// Water at the feet: the sea's depth, or a stream's bed, which is about ankle deep.
	Tread.WaterDepthM = FMath::Max(SeaDepthCm / 100.f, bGrounded ? 0.08f * StreamUnderfoot : 0.f);
	// Wet: soaked at once in water, on its way to it at the water's edge, and dry again some forty
	// seconds after (a wet boot does not dry with the first step out).
	const float Soak = FMath::Max(FMath::SmoothStep(0.f, 0.05f, Tread.WaterDepthM), bGrounded ? Tread.Mix[WaterIndex] : 0.f);
	Tread.Wetness = Soak > Tread.Wetness ? FMath::FInterpTo(Tread.Wetness, Soak, DeltaSeconds, 3.f) : FMath::Max(Soak, Tread.Wetness - DeltaSeconds / 40.f);
	// Exertion: what the body is doing sets where it is heading (nothing standing, a little at a
	// jog, most of it in a sprint, more uphill, with the jets, swimming). It gets there over some
	// seconds and comes back down over about ten.
	float Work = 0.35f * JetThrust;
	if (bGrounded)
	{
		Work += 0.9f * FMath::Pow(FMath::SmoothStep(2.f, 8.5f, Pace), 1.3f) + 0.25f * FMath::SmoothStep(4.f, 20.f, Tread.SlopeDeg) * FMath::SmoothStep(1.f, 3.f, Pace);
	}
	else if (Submerged > 0.5f)
	{
		Work += 0.45f * FMath::SmoothStep(0.3f, 1.8f, static_cast<float>(GetCharacterMovement()->Velocity.Size()) / 100.f);
	}
	Work = FMath::Min(Work, 1.f);
	Tread.Exertion += (Work - Tread.Exertion) * (1.f - FMath::Exp(-DeltaSeconds / (Work > Tread.Exertion ? 7.f : 4.f)));
	if (!bGrounded)
	{
		StillFor = 0.f;
		bFromRest = false;
		return;
	}

	// Stopping after striding: the feet come together, at the pace they were going. Setting off
	// after standing: the first footfall comes soon (not most of a stride later) and is a careful one.
	if (Pace < 0.3f)
	{
		if (StepsInRun >= 2 && Audio)
		{
			FEphemeraStep Settle = MakeStep(EEphemeraStepKind::Settle);
			Settle.SpeedMS = LastStride.SpeedMS;
			Settle.Gait = LastStride.Gait;
			Audio->PlayStep(Settle, Space);
		}
		StepsInRun = 0;
		StillFor += DeltaSeconds;
	}
	else
	{
		if (StillFor >= 0.4f)
		{
			bFromRest = true;
			Stride = 0.6f;
		}
		StillFor = 0.f;
	}
	// (No pivot scuff: in first person turning is the camera, and a sound for every look round was heard as
	// noise for nothing: "while I move only my camera, I hear sound effects".)
	// From a run to nearly standing inside a quarter of a second is a skid, heard at the run's pace.
	SinceFast = Pace >= GaitTop[3] ? 0.f : SinceFast + DeltaSeconds;
	if (Pace < 1.5f && SinceFast < 0.25f)
	{
		SinceFast = 1000.f;
		if (Audio)
		{
			FEphemeraStep Skid = MakeStep(EEphemeraStepKind::Skid);
			Skid.SpeedMS = LastStride.SpeedMS;
			Skid.Gait = LastStride.Gait;
			Audio->PlayStep(Skid, Space);
		}
	}
}

void AEphemeraPlayer::SenseAir()
{
	AirNow = 0.f;
	AirVelocity = FVector::ZeroVector;
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const AEphemeraBody* Body = Frames ? Frames->NearestWithAtmosphere(GetActorLocation()) : nullptr;
	if (!Body || !Body->Definition)
	{
		return;
	}
	// The air thins by a factor of e with every quarter of the atmosphere's height and is gone at
	// its top. It turns with its world, so over a world that is not the frame at rest it is moving.
	const double Top = Body->Definition->AtmosphereHeightM;
	const double AltitudeM = Body->AltitudeCm(GetActorLocation()) / 100.0;
	AirNow = static_cast<float>(FMath::Exp(-FMath::Max(AltitudeM, 0.0) / (0.25 * Top)) * (1.0 - FMath::SmoothStep(0.8 * Top, 1.1 * Top, AltitudeM)));
	if (AirNow > 0.f)
	{
		AirVelocity = Frames->PointVelocity(Body, Body->GetActorTransform().InverseTransformPosition(GetActorLocation()));
	}
}

void AEphemeraPlayer::SenseWater()
{
	Stream = 0.f;
	Shore = 0.f;
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const AEphemeraBody* Body = Frames ? Frames->GetReference() : nullptr;
	const UEphemeraBodyDefinition* D = Body ? Body->Definition.Get() : nullptr;
	if (!D || !D->Terrain.HeightField || D->AtmosphereHeightM <= 0.0)
	{
		return;
	}
	const FVector Local = Body->GetActorTransform().InverseTransformPosition(GetActorLocation());
	const double Distance = Local.Size();
	if (Distance < 1.0)
	{
		return;
	}
	const FVector Direction = Local / Distance;
	// Heard from the ground and from low over it, not from the sky.
	const double AboveM = Distance / 100.0 - D->RadiusM - FEphemeraTerrain::HeightM(D->Terrain, D->RadiusM, Direction);
	const float Low = 1.f - FMath::SmoothStep(15.f, 250.f, static_cast<float>(AboveM));
	if (Low <= 0.f)
	{
		return;
	}
	// The terrain is asked in eight directions at three distances whether water lies there: a
	// stream is heard from a couple of dozen metres, the sea's edge from further.
	FVector East, North;
	Direction.FindBestAxisVectors(East, North);
	const double Sea = Body->TideM(GetActorLocation());
	const double Reaches[] = {6.0, 25.0, 70.0};
	for (int32 Ring = 0; Ring < 3; ++Ring)
	{
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const double Angle = Step * UE_DOUBLE_PI / 4.0 + Ring * 0.39;
			const FVector There = (Direction + (East * FMath::Cos(Angle) + North * FMath::Sin(Angle)) * (Reaches[Ring] / D->RadiusM)).GetSafeNormal();
			const float Near = static_cast<float>(1.0 - Reaches[Ring] / 90.0);
			if (D->StreamMesh && Ring < 2 && FEphemeraTerrain::FlowAsShaded(D->Terrain, There) > 0.66 && FEphemeraTerrain::Ice(D->Terrain, There) < 0.5)
			{
				Stream = FMath::Max(Stream, Near * Low);
			}
			if (D->OceanMesh && FEphemeraTerrain::HeightM(D->Terrain, D->RadiusM, There) < Sea)
			{
				Shore = FMath::Max(Shore, Near * Low);
			}
		}
	}
}

void AEphemeraPlayer::SenseSea(const float DeltaSeconds)
{
	const float WasDepth = SeaDepthCm;
	SeaDepthCm = 0.f;
	Submerged = 0.f;
	bEyeUnder = false;
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const AEphemeraBody* Body = Frames ? Frames->GetReference() : nullptr;
	const UEphemeraBodyDefinition* D = Body ? Body->Definition.Get() : nullptr;
	if (!D || !D->OceanMesh || bFreeFlight)
	{
		return;
	}
	// The sea is a sphere about the world's centre, raised and lowered by the tide and by its waves (the
	// same waves its surface is drawn with, AEphemeraBody::SeaSurfaceM), so a floating body rides them.
	const FTransform& World = Body->GetActorTransform();
	const double SeaCm = (D->RadiusM + Body->SeaSurfaceM(GetActorLocation())) * 100.0;
	const float Swell = 0.f;
	const double Feet = World.InverseTransformPosition(GetActorLocation()).Size() - CapsuleHalfHeight;
	SeaDepthCm = FMath::Max(0.f, static_cast<float>(SeaCm - Feet) + Swell);
	Submerged = FMath::Clamp(SeaDepthCm / SeaFullDepthCm, 0.f, 1.f);
	bEyeUnder = World.InverseTransformPosition(Camera->GetComponentLocation()).Size() < SeaCm;
	// Going in: a splash, as big as the speed it was hit at.
	const float Into = static_cast<float>(-FVector::DotProduct(GetCharacterMovement()->Velocity, GetActorUpVector()));
	if (WasDepth <= 0.f && SeaDepthCm > 0.f && Into > 180.f && !bLoading)
	{
		const float Hard = FMath::Clamp(Into / 1500.f, 0.f, 1.f);
		const FVector Up = GetActorUpVector();
		ThrowDust(GetActorLocation() - Up * (CapsuleHalfHeight - SeaDepthCm), Up, FMath::Lerp(0.4f, 1.f, Hard), FMath::Lerp(160.f, 520.f, Hard));
		if (Audio)
		{
			Audio->Play(Audio->Has(TEXT("splash")) ? FName(TEXT("splash")) : FName(TEXT("step_water")), FMath::Lerp(0.5f, 1.f, Hard), FMath::Lerp(1.05f, 0.85f, Hard));
		}
	}
}

void AEphemeraPlayer::UpdateLamp()
{
	if (!Sky.IsValid())
	{
		for (TActorIterator<AEphemeraSkyRig> It(GetWorld()); It; ++It)
		{
			Sky = *It;
			break;
		}
	}
	// Between the night's lamp and the day's the steps are equal ratios, as the eye counts light: half way
	// into dusk it is the geometric mean of the two, not the average (which would glare).
	const float Night = Sky.IsValid() ? Sky->NightAmount() : 1.f;
	const float ByDay = FMath::Pow(LampLumensDay / LampLumensNight, 1.f - Night);
	if (Lamp->IsVisible())
	{
		const float Lumens = LampLumensNight * ByDay;
		if (FMath::Abs(Lumens - Lamp->Intensity) > Lamp->Intensity * 0.04f)
		{
			Lamp->SetIntensity(Lumens);
		}
	}
	// The jets' glow follows their thrust (and doubles under boost), with a flame's unrest in it.
	const float Burn = FMath::Clamp(JetThrust, 0.f, 1.f) * (1.f + JetBoosted);
	// The glow under the jets is off: lit from below the land looked lit by a stage lamp (the owner: "makes no
	// sense ... cartoonish"). bJetLight brings it back for trying again.
	constexpr bool bJetLight = false;
	const bool bBurning = bJetLight && Burn > 0.02f && !bFreeFlight;
	if (bBurning)
	{
		const float Seconds = static_cast<float>(GetWorld()->GetTimeSeconds());
		const float Unrest = 0.86f + 0.09f * FMath::Sin(Seconds * 31.f) + 0.07f * FMath::Sin(Seconds * 53.f + 1.7f) + 0.05f * FMath::Sin(Seconds * 11.f);
		// Brighter by day so it still shows, but by eight times at most (the lamp's own day factor, about a
		// hundred, put 2.8 million lumens a metre from the seabed when diving: past what the picture's numbers
		// can hold, which turned the whole screen black until the eye left the water). Under water, less.
		const float JetByDay = FMath::Pow(8.f, 1.f - Night) * FMath::Lerp(1.f, 0.3f, Submerged);
		JetGlow->SetIntensity(JetGlowLumens * JetByDay * Burn * Unrest);
	}
	if (JetGlow->IsVisible() != bBurning)
	{
		JetGlow->SetVisibility(bBurning);
	}
}

void AEphemeraPlayer::ThrowDust(const FVector& At, const FVector& Up, const float Strength, const float SpreadCm)
{
	if (!PuffInstances.IsValidIndex(NextPuff) || !Puffs.IsValidIndex(NextPuff) || Strength <= 0.02f)
	{
		return;
	}
	// The dust is the ground's own, in its colour: snow goes up white, the black sand dark, and
	// damp moss gives up little. Out of the sea comes spray.
	const FName Ground = GroundUnderfoot();
	FLinearColor Tint(0.085f, 0.085f, 0.090f);
	float Gives = 1.f;
	if (Ground == TEXT("snow")) { Tint = FLinearColor(0.80f, 0.84f, 0.90f); }
	else if (Ground == TEXT("water")) { Tint = FLinearColor(0.72f, 0.78f, 0.84f); Gives = 0.8f; }
	else if (Ground == TEXT("sand")) { Tint = FLinearColor(0.040f, 0.040f, 0.045f); }
	else if (Ground == TEXT("dust")) { Tint = FLinearColor(0.20f, 0.20f, 0.21f); }
	else if (Ground == TEXT("moss")) { Tint = FLinearColor(0.095f, 0.095f, 0.070f); Gives = 0.6f; }
	// (For tests, with -EphemeraJetTest: pale and at full strength whatever the ground, so that it shows in a photograph.)
	if (FParse::Param(FCommandLine::Get(), TEXT("EphemeraJetTest")))
	{
		Tint = FLinearColor(0.5f, 0.5f, 0.52f);
		Gives = 1.f;
	}
	UMaterialInstanceDynamic* Look = PuffInstances[NextPuff];
	Puffs[NextPuff]->SetWorldLocation(At);
	Look->SetVectorParameterValue(TEXT("Centre"), FLinearColor(static_cast<float>(At.X), static_cast<float>(At.Y), static_cast<float>(At.Z), 0.f));
	Look->SetVectorParameterValue(TEXT("Up"), FLinearColor(static_cast<float>(Up.X), static_cast<float>(Up.Y), static_cast<float>(Up.Z), 0.f));
	Look->SetVectorParameterValue(TEXT("Tint"), Tint);
	Look->SetScalarParameterValue(TEXT("Strength"), Strength * Gives);
	Look->SetScalarParameterValue(TEXT("Spread"), SpreadCm);
	Look->SetScalarParameterValue(TEXT("Age"), 0.f);
	PuffAge[NextPuff] = 0.f;
	Puffs[NextPuff]->SetVisibility(true);
	NextPuff = (NextPuff + 1) % Puffs.Num();
}

void AEphemeraPlayer::UpdateEffects(const float DeltaSeconds)
{
	// What the effects are made of is the sky rig's to give (it is in the map; see its header).
	if (!bEffectsAsked && Sky.IsValid())
	{
		bEffectsAsked = true;
		if (Sky->FlameMaterial)
		{
			FlameInstance = UMaterialInstanceDynamic::Create(Sky->FlameMaterial, this);
			JetFlameLeft->SetMaterial(0, FlameInstance);
			JetFlameRight->SetMaterial(0, FlameInstance);
		}
		if (Sky->PuffMesh && Sky->PuffMaterial)
		{
			for (UStaticMeshComponent* Puff : Puffs)
			{
				Puff->SetStaticMesh(Sky->PuffMesh);
				PuffInstances.Add(UMaterialInstanceDynamic::Create(Sky->PuffMaterial, this));
				Puff->SetMaterial(0, PuffInstances.Last());
				PuffAge.Add(-1.f);
			}
		}
	}
	// The flames: longer and brighter with the thrust, half as long again under boost, never still.
	// (For tests, -EphemeraJetTest burns them at full thrust wherever the body is, so they can be photographed.)
	static const bool bJetTest = FParse::Param(FCommandLine::Get(), TEXT("EphemeraJetTest"));
	const float Burn = bJetTest ? 1.f : FMath::Clamp(JetThrust, 0.f, 1.f) * (1.f + 0.5f * JetBoosted);
	// The flame cones are off with the glow (see UpdateLamp); the dust the jets blow up stays.
	constexpr bool bJetFlames = false;
	const bool bBurning = Burn > 0.02f && !bFreeFlight && FlameInstance && bJetFlames;
	if (bBurning)
	{
		const float Seconds = static_cast<float>(GetWorld()->GetTimeSeconds());
		const float Length = (0.35f + 0.55f * Burn) * (0.94f + 0.06f * FMath::Sin(Seconds * 47.f));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			UStaticMeshComponent* Flame = Side == 0 ? JetFlameLeft : JetFlameRight;
			// The engine's cone is a metre long about its middle: its mouth is kept at the nozzle.
			Flame->SetRelativeScale3D(FVector(0.085f, 0.085f, Length));
			Flame->SetRelativeLocation(FVector(-22.f, Side == 0 ? -15.f : 15.f, -24.f - 50.f * Length));
		}
		FlameInstance->SetScalarParameterValue(TEXT("Power"), Burn);
		// A flame is added to what is behind it: over sunlit ground at its night strength it is two white
		// bulbs. By day it is a pale blue hint, as a gas flame is in sunlight; by night it is the brightest thing there.
		FlameInstance->SetScalarParameterValue(TEXT("Bright"), FMath::Lerp(1.3f, 3.5f, Sky.IsValid() ? Sky->NightAmount() : 1.f));
	}
	if (JetFlameLeft->IsVisible() != bBurning)
	{
		JetFlameLeft->SetVisibility(bBurning);
		JetFlameRight->SetVisibility(bBurning);
	}
	// The jets' wash: burning within a few metres of the ground they blow its dust out from under them.
	SinceWash += DeltaSeconds;
	if (Burn > 0.02f && !bFreeFlight && SinceWash > 0.2f && !PuffInstances.IsEmpty())
	{
		const FVector Up = GetActorUpVector();
		const FVector Feet = GetActorLocation() - Up * CapsuleHalfHeight;
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(EphemeraJetWash), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Feet + Up * 20.f, Feet - Up * 550.f, ECC_Visibility, Query))
		{
			SinceWash = 0.f;
			const float Near = 1.f - FMath::Clamp((Hit.Distance - 20.f) / 550.f, 0.f, 1.f);
			ThrowDust(Hit.ImpactPoint, Up, 0.8f * Near * FMath::Min(Burn, 1.f), FMath::Lerp(380.f, 200.f, Near));
		}
	}
	for (int32 Index = 0; Index < PuffAge.Num(); ++Index)
	{
		if (PuffAge[Index] < 0.f)
		{
			continue;
		}
		PuffAge[Index] += DeltaSeconds;
		if (PuffAge[Index] > PuffLife)
		{
			PuffAge[Index] = -1.f;
			Puffs[Index]->SetVisibility(false);
			continue;
		}
		PuffInstances[Index]->SetScalarParameterValue(TEXT("Age"), PuffAge[Index]);
	}
}

void AEphemeraPlayer::UpdateSound(const float DeltaSeconds)
{
	UpdateLamp();
	UpdateEffects(DeltaSeconds);
	if (!Audio)
	{
		return;
	}
	if (bMuted)
	{
		// -EphemeraMute: a test that must run the sound for real and yet be heard by nobody. Every
		// sound still plays; the device's own output is held at nothing, every frame.
		FApp::SetVolumeMultiplier(0.f);
		FApp::SetUnfocusedVolumeMultiplier(0.f);
		if (FAudioDevice* Device = GetWorld()->GetAudioDeviceRaw())
		{
			Device->SetTransientPrimaryVolume(0.f);
		}
	}
	SinceWaterSensed += DeltaSeconds;
	if (SinceWaterSensed > 0.5f)
	{
		SinceWaterSensed = 0.f;
		SenseWater();
	}
	FEphemeraSoundScene Scene;
	Scene.Air = AirNow;
	// Free flight is a camera: it hears the air go by, but not the roar a body would make at such speeds.
	const float AirSpeed = static_cast<float>((GetCharacterMovement()->Velocity - AirVelocity).Size()) / 100.f;
	Scene.AirSpeed = bFreeFlight ? FMath::Min(AirSpeed, 30.f) : AirSpeed;
	Scene.Jets = JetThrust;
	Scene.Boost = JetBoosted;
	// The weather's wind, and more of it the higher you stand.
	const FString& Weather = UEphemeraUserSettings::Get()->Weather;
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	const AEphemeraBody* Ground = Frames ? Frames->GetReference() : nullptr;
	const float HighM = Ground ? static_cast<float>(Ground->AltitudeCm(GetActorLocation()) / 100.0) : 0.f;
	Scene.Wind = (Weather == TEXT("clear") ? 0.3f : Weather == TEXT("overcast") ? 0.75f : 0.5f) + 0.25f * FMath::SmoothStep(50.f, 600.f, HighM);
	Scene.Stream = Stream;
	Scene.Shore = Shore;
	Scene.Under = bEyeUnder ? 1.f : 0.f;
	// What the beds under the steps and the breath follow.
	Scene.Pace = Tread.SpeedMS;
	Scene.Vegetation = Tread.Vegetation;
	Scene.Stalks = Stalks;
	Scene.WaterM = Tread.WaterDepthM;
	Scene.WaterSpeed = Tread.WaterDepthM > 0.f ? static_cast<float>(GetCharacterMovement()->Velocity.Size()) / 100.f : 0.f;
	Scene.Exertion = Tread.Exertion;
	// Night where you stand: the star under the horizon of the world you are on.
	if (Ground)
	{
		const FVector Up = (GetActorLocation() - Ground->GetActorLocation()).GetSafeNormal();
		Scene.Night = 1.f - FMath::SmoothStep(-0.12f, 0.12f, static_cast<float>(FVector::DotProduct(Frames->DirectionToStar(GetActorLocation()), Up)));
	}
	if (const AEphemeraBody* Star = Frames ? Frames->GetStar() : nullptr)
	{
		// Heard from six of its radii off, in full from half of one.
		const float Radii = static_cast<float>((FVector::Dist(GetActorLocation(), Star->GetActorLocation()) - Star->RadiusCm()) / Star->RadiusCm());
		Scene.Star = 1.f - FMath::SmoothStep(0.5f, 6.f, Radii);
	}
	Scene.bQuiet = bLoading || (Hud.IsValid() && Hud->IsMenuOpen());
	Audio->Follow(Scene, DeltaSeconds);
}

// ------------------------------------------------------------------------------------ tests ---

void AEphemeraPlayer::RunDrive(const float DeltaSeconds)
{
	// A test cannot press keys, so a script holds them down: each step names the keys held (any of
	// w a s d shift space ctrl fly, joined by +, or none) and for how long. Ten times a second the
	// log says what the body is doing, which is how the feel is checked without hands: how long to
	// reach a run, how high a jump goes, whether the jets lift.
	const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
	UCharacterMovementComponent* Move = GetCharacterMovement();
	DriveLeft -= DeltaSeconds;
	DriveClock += DeltaSeconds;
	if (DriveLeft <= 0.f)
	{
		++DriveAt;
		TArray<FString> Held;
		if (Drive.IsValidIndex(DriveAt))
		{
			DriveLeft = Drive[DriveAt].Key;
			Drive[DriveAt].Value.ParseIntoArray(Held, TEXT("+"));
		}
		MoveInput = FVector2D((Held.Contains(TEXT("d")) ? 1.0 : 0.0) - (Held.Contains(TEXT("a")) ? 1.0 : 0.0),
			(Held.Contains(TEXT("w")) ? 1.0 : 0.0) - (Held.Contains(TEXT("s")) ? 1.0 : 0.0)).GetClampedToMaxSize(1.0);
		bBoost = Held.Contains(TEXT("shift"));
		JetInput = Held.Contains(TEXT("ctrl")) ? -1.f : 0.f;
		if (Held.Contains(TEXT("fly")) != bFreeFlight)
		{
			SetFreeFlight(!bFreeFlight);
		}
		if (Held.Contains(TEXT("space")) != bJumpHeld)
		{
			bJumpHeld ? OnJumpEnd(FInputActionValue()) : OnJumpStart(FInputActionValue());
		}
		if (!Drive.IsValidIndex(DriveAt))
		{
			UE_LOG(LogTemp, Display, TEXT("Ephemera drive: done after %.2f s, highest %.2f m, %d steps heard"), DriveClock, DriveHighest, StepsHeard);
			Drive.Reset();
			return;
		}
		DriveSaid = 0.f;
	}
	double HeightM = 0.0;
	if (const AEphemeraBody* Body = Frames ? Frames->GetReference() : nullptr; Body && Body->Definition)
	{
		const FVector Local = Body->GetActorTransform().InverseTransformPosition(GetActorLocation());
		HeightM = (Local.Size() - FEphemeraTerrain::SurfacePointCm(*Body->Definition, Local.GetSafeNormal()).Size() - CapsuleHalfHeight) / 100.0;
	}
	DriveHighest = FMath::Max(DriveHighest, static_cast<float>(HeightM));
	DriveSaid -= DeltaSeconds;
	if (DriveSaid <= 0.f)
	{
		DriveSaid = 0.1f;
		const FVector Up = GetActorUpVector();
		UE_LOG(LogTemp, Display, TEXT("Ephemera drive: %.2f s [%s] along %.2f m/s, rising %.2f m/s, %.2f m up, fuel %.0f%%, %s, jets %.2f, eye %.1f cm, wider %.1f deg, lean %.2f deg, %s | %s"),
			DriveClock, *Drive[DriveAt].Value, FVector::VectorPlaneProject(Move->Velocity, Up).Size() / 100.0, FVector::DotProduct(Move->Velocity, Up) / 100.0,
			HeightM, Fuel * 100.f, bFreeFlight ? TEXT("free flight") : Move->IsMovingOnGround() ? TEXT("on the ground") : TEXT("in the air"), JetThrust,
			ViewDrop + StepLag, FovKick, ViewRoll, *GroundUnderfoot().ToString(), Audio ? *Audio->Report() : TEXT("no sound"));
	}
}
