#pragma once

#include "CoreMinimal.h"
#include "Frame/EphemeraFrameSubsystem.h"
#include "GameFramework/Character.h"
#include "Player/EphemeraAudio.h"
#include "EphemeraPlayer.generated.h"

class UCameraComponent;
class USpotLightComponent;
class UEphemeraInputConfig;
struct FInputActionValue;

/**
 * The Dayling on foot. The first version copied Outer Wilds to the number (a rigid camera, a
 * jump that waited for the key to be let go, jets too weak to lift you) and was stiff and slow
 * to answer. This one is built on what the best-feeling first-person games share
 * (docs/research/controls_feel.md): every key does something on the frame it goes down; speed
 * comes on and goes off in about a tenth of a second; the jump forgives a press that is a little
 * early or late; the jets lift, steer and refill; and the view has a body under it (it dips on
 * landing, sways with a stride, leans into a strafe, widens at speed), all of it scaled by one
 * setting down to nothing. "Down" comes from the frame subsystem, so the same code walks on any
 * world.
 */
UCLASS()
class EPHEMERA_API AEphemeraPlayer : public ACharacter, public IEphemeraFrameRider
{
	GENERATED_BODY()

public:
	AEphemeraPlayer();

	// ---- Tuning (cm, cm/s, degrees) ----
	/** A steady jog, and the run while Boost is held and you are heading forwards. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Walk") float WalkSpeed = 480.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Walk") float SprintSpeed = 800.f;
	/** Sideways and backwards, as shares of the forward speed. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Walk") float SidewaysShare = 0.92f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Walk") float BackwardShare = 0.8f;
	/** Full walking speed in a ninth of a second. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Walk") float GroundAcceleration = 4500.f;
	/**
	 * Letting go: speed falls away in proportion to itself (GroundGrip, per second) and by a steady
	 * amount on top (GroundBraking), which together stop a jog in a fifth of a second and 40 cm.
	 */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Walk") float GroundGrip = 5.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Walk") float GroundBraking = 1400.f;
	/** Leaves the ground at this speed the moment the key goes down: 1.3 m high on Dawnhollow, 4 m on Hush. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jump") float JumpSpeed = 520.f;
	/** Let go within JumpCutWindow seconds and the rise is cut to this share: a tap is a hop. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jump") float JumpCut = 0.55f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jump") float JumpCutWindow = 0.16f;
	/** A jump still counts this long after walking off an edge, and this long before touching down. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jump") float CoyoteTime = 0.13f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jump") float JumpBuffer = 0.13f;
	/** Steering in the air without the jets: up to this speed in the direction asked, gained this fast. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jump") float AirSteerSpeed = 600.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jump") float AirSteerAcceleration = 1400.f;
	/**
	 * The jets. Lift beats Dawnhollow's gravity by a comfortable margin (and on Hush will put you
	 * in orbit); sideways and downward thrust are gentler; adrift in space every direction has the
	 * same, lower thrust, for fine work. Boost multiplies whichever is firing.
	 */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float JetLift = 1700.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float JetSide = 1000.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float JetDown = 1200.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float JetAdrift = 800.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float JetBoost = 2.f;
	/** The jump key lifts once it has been held this long after a jump (in mid-air it lifts at once). */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float JetDelay = 0.15f;
	/** Lighting the jets while hanging or sinking slowly gives this much speed at once, so they are felt. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float JetKick = 120.f;
	/** Seconds of full lift in the tank, and seconds on the ground to fill it again. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float LiftSeconds = 7.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float RefillSeconds = 1.6f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Look") float MouseDegreesPerCount = 0.22f;
	/** The stick at full tilt, before the extra turn that builds while it is held against the edge. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Look") float StickYawRate = 190.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Look") float StickPitchRate = 130.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Look") float FieldOfView = 70.f;
	/** Free flight (toggle): cruise speed near the ground, and the boost multiplier while Boost is held. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Free Flight") float FreeFlightSpeed = 2500.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Free Flight") float FreeFlightBoost = 8.f;
	/**
	 * Free flight goes faster the further it is from any ground: cruise speed is multiplied by the
	 * height over the nearest world divided by this (cm), never less than 1 nor more than
	 * FreeFlightReachMax. Walking pace among the grass, a planet's width in seconds from orbit, and
	 * it eases off by itself on the way down to a surface.
	 */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Free Flight") float FreeFlightReachCm = 12000.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Free Flight") float FreeFlightReachMax = 400.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Body") float EyeHeight = 120.f;
	/** Below this gravity (cm/s²) you float: no "down", free flight. */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Body") float ZeroGThreshold = 35.f;

	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<USpotLightComponent> Lamp;
	/** The lamp's output by full night and by full day, lumens (it rises with the daylight so that it always shows). */
	UPROPERTY(EditAnywhere, Category = "Ephemera|Lamp") float LampLumensNight = 14000.f;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Lamp") float LampLumensDay = 1500000.f;
	/** The glow under the jets at full thrust by night, lumens (it rises with the daylight as the lamp does). */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<class UPointLightComponent> JetGlow;
	UPROPERTY(EditAnywhere, Category = "Ephemera|Jets") float JetGlowLumens = 26000.f;
	/** The jets' flames, one under each side of the pack: seen on looking down past your own feet while they burn. */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<class UStaticMeshComponent> JetFlameLeft;
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TObjectPtr<class UStaticMeshComponent> JetFlameRight;
	/** Dust off the ground: thrown out by a landing, and blown out by the jets while they burn close over it. A few at a time. */
	UPROPERTY(VisibleAnywhere, Category = "Ephemera") TArray<TObjectPtr<class UStaticMeshComponent>> Puffs;

	float GetFuel() const { return Fuel; }
	/** True while the tank is being drawn on (the fuel readout shows itself). */
	bool JetsLit() const { return JetThrust > 0.01f; }
	bool IsFreeFlight() const { return bFreeFlight; }
	/** 0 out of the sea .. 1 wholly under it (what the water holds up), and whether the eye is under. */
	float GetSubmerged() const { return Submerged; }
	bool IsEyeUnderwater() const { return bEyeUnder; }

	/** Opens or closes the settings menu (and hands the mouse to it or back to the view). */
	void ToggleMenu();
	/** Takes field of view and mouse sensitivity from the user's settings. */
	void ApplyUserSettings();
	/** After the ground's relief is switched between geometry and shading: everything standing on it is set down again. */
	void ReliefChanged();

	// IEphemeraFrameRider
	virtual FVector GetFrameVelocity() const override;
	virtual void RebaseToFrame(const FTransform& OldToNew, const FVector& NewVelocity) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void Landed(const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	/**
	 * A solar system has no floor: the engine's own guard against falling out of a level (KillZ, about 10 km
	 * under the origin, and the world's bounds) would destroy the player as soon as they flew that far from
	 * whatever body the frame is centred on, leaving the camera staring and every key dead, the menu's too.
	 * (Reported 17 October 2026: free flight from the star, then away from it.) Neither ever applies here.
	 */
	virtual void FellOutOfWorld(const class UDamageType& DamageType) override {}
	virtual void OutsideWorldBounds() override {}
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnMove(const FInputActionValue& Value);
	void OnMoveEnd(const FInputActionValue& Value);
	void OnLookMouse(const FInputActionValue& Value);
	void OnLookStick(const FInputActionValue& Value);
	void OnLookStickEnd(const FInputActionValue& Value);
	void OnJumpStart(const FInputActionValue& Value);
	void OnJumpEnd(const FInputActionValue& Value);
	void OnJets(const FInputActionValue& Value);
	void OnJetsEnd(const FInputActionValue& Value);
	void OnBoost(const FInputActionValue& Value);
	void OnBoostEnd(const FInputActionValue& Value);
	void OnLamp(const FInputActionValue& Value);
	void OnFly(const FInputActionValue& Value);
	void OnFlySpeed(const FInputActionValue& Value);

	void AlignToGravity(float DeltaSeconds);
	void ApplyLook(float DeltaSeconds);
	/** True when a planet or moon gives a clear "down" here. The star never does: open space is free look. */
	bool bOriented = false;
	void ApplyWalk(float DeltaSeconds);
	void ApplyFlight(float DeltaSeconds);
	void ApplyFreeFlight(float DeltaSeconds);
	void DoJump();
	/** The view's own movement on top of the body's: springs, stride, lean, field of view. */
	void UpdateView(float DeltaSeconds, bool bGrounded);
	/** What the ground under the feet is ("moss", "sand", "rock", "snow", "water", "dust"), from the terrain itself: what the dust a landing throws up looks like. */
	FName GroundUnderfoot() const;
	/**
	 * Each frame, what the sound needs of the feet (into Tread): the gait, the ground's mix, how wet
	 * and how worked the body is, and the moments between footfalls that are heard too: setting off,
	 * coming to a stop, a pivot on the spot, a skid out of a run.
	 */
	void FollowFeet(float DeltaSeconds, bool bGrounded, float Speed);
	/** Looks at the ground at the foot and half a metre on and moves Tread's mix that share of the way toward it (1: all of it). */
	void SampleGround(float Alpha);
	/** A footfall (or take-off, landing, stop) as Tread has it now, on the foot the stride has come to. */
	FEphemeraStep MakeStep(EEphemeraStepKind Kind) const;
	/** How much air there is here (AirNow) and how that air is itself moving in the current frame (AirVelocity). */
	void SenseAir();
	/** For tests (-EphemeraDrive=w:2,w+shift:2,space:0.1,...): holds keys down by a script and logs what the body does. */
	void RunDrive(float DeltaSeconds);
	/** Looks round for running water and the shore (a few terrain look-ups; called twice a second). */
	void SenseWater();
	/** How deep in the sea the body is, each tick; a splash on going in. */
	void SenseSea(float DeltaSeconds);
	float SeaDepthCm = 0.f;
	float Submerged = 0.f;
	bool bEyeUnder = false;
	void UpdateSound(float DeltaSeconds);
	void UpdateLamp();
	TWeakObjectPtr<class AEphemeraSkyRig> Sky;
	/** The flames' size and the dust's age, each frame; and the jets' wash looking for ground to blow on. */
	void UpdateEffects(float DeltaSeconds);
	/** Throws a ring of the ground's dust out from a point on it. Strength 0..1; SpreadCm: how far it runs. */
	void ThrowDust(const FVector& At, const FVector& Up, float Strength, float SpreadCm);
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FlameInstance;
	UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> PuffInstances;
	/** Seconds since each puff was thrown (negative: not in the air). */
	TArray<float> PuffAge;
	int32 NextPuff = 0;
	float SinceWash = 0.f;
	bool bEffectsAsked = false;
	/** Free flight only: moves the camera along with the body it is nearest, each time the bodies move. */
	void RideNearestBody();
	/** Turns the body's up toward a direction by a share of the way; bKeepView leaves what is being looked at where it is. */
	void TurnUpToward(const FVector& TargetUp, float Alpha, bool bKeepView);
	/** Free flight: the key that levels the horizon, and the third of a second it takes. */
	void OnLevel();
	void LevelView(float DeltaSeconds);
	void SetFreeFlight(bool bOn);
	void PlaceAtStart();

	UPROPERTY() TObjectPtr<UEphemeraInputConfig> Input;

	FVector Gravity = FVector(0.0, 0.0, -1050.0);
	FVector2D MoveInput = FVector2D::ZeroVector;
	FVector2D StickLook = FVector2D::ZeroVector;
	FVector2D MouseLook = FVector2D::ZeroVector;   // degrees accumulated since the last tick
	float Pitch = 0.f;
	float JetInput = 0.f;
	bool bBoost = false;
	bool bJumpHeld = false;
	/** Seconds the jump key has been down this press, and whether this press began with a jump. */
	float JumpHeldFor = 0.f;
	bool bPressJumped = false;
	/** Seconds since the key went down with nothing to jump from (a jump is owed if ground comes soon). */
	float SinceJumpAsked = 1000.f;
	float SinceJump = 1000.f;
	float SinceGrounded = 0.f;
	float GroundedFor = 0.f;
	bool bWasGrounded = false;
	/** Speed into the ground at the last touchdown, cm/s; taken up by the next tick. */
	float LandedAt = -1.f;
	/** The walking speed asked for now: it rises to a run rather than jumping there. */
	float GroundSpeed = 0.f;
	float Fuel = 1.f;
	/** 0..1: how hard the jets are firing this tick, how much of it is boost, and seconds since they last fired. */
	float JetThrust = 0.f;
	float JetBoosted = 0.f;
	float SinceJets = 1000.f;
	float SinceJetKick = 1000.f;
	bool bFuelOutSaid = false;
	// ---- the view ----
	/** How far the eye sits below (-) or above its place, and how fast it is moving there: a spring. */
	float ViewDrop = 0.f;
	float ViewDropSpeed = 0.f;
	/** A step up or down that the eye has not caught up with yet. */
	float StepLag = 0.f;
	float ViewRoll = 0.f;
	float FovKick = 0.f;
	/** 0..1 through the current stride, how many have been taken, and how much the stride shows (0 standing or airborne). */
	float Stride = 0.f;
	int32 Strides = 0;
	float StrideShow = 0.f;
	FVector LastPlace = FVector::ZeroVector;
	bool bHaveLastPlace = false;
	/** Seconds the look stick has been held against its edge. */
	float StickAtEdge = 0.f;
	/** 0 in a vacuum .. 1 at sea level, and the air's own velocity here in the current frame, cm/s. */
	float AirNow = 1.f;
	FVector AirVelocity = FVector::ZeroVector;
	// ---- sound ----
	UPROPERTY() TObjectPtr<UEphemeraPlayerAudio> Audio;
	int32 StepsHeard = 0;
	/** What the feet are doing, kept up each frame by FollowFeet; every step played is a copy of it. */
	FEphemeraStep Tread;
	/** The last footfall of a run of strides: a stop or a skid is heard at the pace that came before it. */
	FEphemeraStep LastStride;
	/** The surroundings as the steps will hear them. Nothing measures them yet, so they are open air. */
	FEphemeraSpace Space;
	/** How much of the plants about the legs is stiff stalk rather than grass, and how much of the ground under the foot is a stream's bed. */
	float Stalks = 0.f;
	float StreamUnderfoot = 0.f;
	float SinceGroundSampled = 1000.f;
	/** This stride's length against what the pace gives: a few per cent either way, drawn afresh at each footfall. */
	float StrideScale = 1.f;
	/** The way the body faced last frame, for how fast it is turning. */
	FVector LastFacing = FVector::ZeroVector;
	/** Seconds standing (or near enough), footfalls since setting off, and whether the next footfall is the first from rest. */
	float StillFor = 0.f;
	int32 StepsInRun = 0;
	bool bFromRest = false;
	float SinceTurn = 1000.f;
	/** Seconds since the pace was a run's, for telling a skid to a stop from slowing down. */
	float SinceFast = 1000.f;
	/** -EphemeraMute: tests run the sound for real with the output held at nothing. */
	bool bMuted = false;
	/** 0..1: how near running water and the sea's edge are, and seconds since that was last looked into. */
	float Stream = 0.f;
	float Shore = 0.f;
	float SinceWaterSensed = 1000.f;
	// ---- scripted keys, for tests ----
	TArray<TPair<float, FString>> Drive;
	int32 DriveAt = -1;
	float DriveLeft = 0.f;
	float DriveClock = 0.f;
	float DriveSaid = 0.f;
	float DriveHighest = 0.f;
	/** -EphemeraRecord=<folder>: every frame of the scripted run saved there as f_00000.png..., from
	 * -EphemeraRecordFrom=<seconds> into the script; the game quits when the script ends. With -benchmark
	 * -fps=30 the game steps exactly 1/30 s a frame however long one takes to draw: a smooth film. */
	FString RecordDir;
	float RecordFrom = 0.f;
	int32 RecordFrame = 0;
	bool bFreeFlight = false;
	float FreeFlightScale = 1.f;                    // mouse wheel
	void ToggleFps();
	/** Keeps the loading screen up until the world is ready to be seen, and says what it is waiting for. */
	void UpdateLoading();
	bool bLoading = true;
	float LoadSeconds = 0.f;
	float ReadySeconds = 0.f;
	float QuietSeconds = 0.f;
	int32 PlaceTries = 0;
	/** The body free flight is riding with, and where that body was when the camera last caught up with it. */
	TWeakObjectPtr<const class AEphemeraBody> RideBody;
	FTransform RidePose;
	bool bRideReset = true;
	/** Seconds left of levelling the horizon (free flight, on request) and of righting the body (just out of free flight). */
	float LevelLeft = 0.f;
	float RightingLeft = 0.f;
	FDelegateHandle RideHandle;
	float WarmTurnDeg = 0.f;
	TSharedPtr<class SEphemeraHud> Hud;
	float BaseMouseDegreesPerCount = 0.22f;
};
