#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EphemeraAudio.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * Every sound the player makes or hears, as imported by Tools/build_audio.py from
 * SourceAssets/audio. A sound's name is its meaning: "step_moss_03" is the third take of a
 * footstep on moss, "loop_wind" something that plays round and round. The game asks for a sound
 * by the name without the take number and is given one of the takes, never one of the last few
 * it was given.
 */
UCLASS(BlueprintType)
class EPHEMERA_API UEphemeraAudioSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") TArray<TObjectPtr<USoundBase>> Sounds;
};

/** Which foot is coming down: the recordings are cut in order and kept apart by foot, so each keeps its own character. */
enum class EEphemeraFoot : uint8 { Left, Right };
/**
 * The five gaits a step is recorded in, by speed along the ground: creep to 1.5 m/s, walk to 3,
 * jog to 5.2, run to 6.8, sprint beyond. They are different recordings, not one played louder.
 */
enum class EEphemeraGait : uint8 { Creep, Walk, Jog, Run, Sprint };
/** What the feet are doing: a footfall, the first one from rest, a stop, a pivot on the spot, a skid to a stop, a jump, a landing. */
enum class EEphemeraStepKind : uint8 { Step, Start, Settle, Turn, Skid, Takeoff, Land };
/** What a step can sound like. Count is the number of them, for arrays. */
enum class EEphemeraSurface : uint8 { Moss, Sedge, Ash, Gravel, Basalt, Snow, Water, Count };

/**
 * Everything the sound needs to know about one footfall (or take-off, or landing), built by the
 * player from what the body and the ground are doing (docs/research/footsteps_and_space_audio.md,
 * 6.3 to 6.5). The sound decides from it which recordings, how loud, how bright and when.
 */
struct FEphemeraStep
{
	EEphemeraStepKind Kind = EEphemeraStepKind::Step;
	EEphemeraFoot Foot = EEphemeraFoot::Left;
	/** Speed along the ground, m/s, and the same as 0..1 between a creep and a sprint. */
	float SpeedMS = 0.f;
	float Intensity = 0.f;
	EEphemeraGait Gait = EEphemeraGait::Walk;
	/** A landing: how fast the ground came up, m/s. */
	float FallMS = 0.f;
	/** The ground's slope along the way the body is going, degrees: uphill positive. */
	float SlopeDeg = 0.f;
	/** How fast the body is turning about its up, degrees a second. */
	float YawRateDegS = 0.f;
	/** 0 dry .. 1 soaked: rises at once in water and dries over some forty seconds after. */
	float Wetness = 0.f;
	/** Depth of water at the feet, m. */
	float WaterDepthM = 0.f;
	/** 0..1: how thick the plants are about the legs. */
	float Vegetation = 0.f;
	/** 0..1: how hard the body has been working (sprints, climbs, jets, jumps). */
	float Exertion = 0.f;
	/** Seconds to the next footfall at this pace (0: none coming). */
	float StrideSeconds = 0.f;
	/** The ground's shares of the surfaces, summing to 1, smoothed over the last 0.4 s of walking. */
	float Mix[static_cast<int32>(EEphemeraSurface::Count)] = {};
};

/**
 * The place around the player as a step's echo and reverberation will need it: how much it is open
 * moor, a valley among cliffs, scrub or a cave, how far off the nearest wall that would throw a step
 * back is, and how much the surroundings soak up the highs. Nothing measures this yet (the probe of
 * docs/research/footsteps_and_space_audio.md 3.5 is still to come), so every place is open air; the
 * steps are already handed it, so the probe has only to fill it in.
 */
struct FEphemeraSpace
{
	float Open = 1.f;
	float Valley = 0.f;
	float Forest = 0.f;
	float Cave = 0.f;
	/** Seconds until a step comes back off the nearest large wall; 0 with none near enough to hear. */
	float SlapSeconds = 0.f;
	/** 0 bare rock round about .. 1 snow, moss and foliage. */
	float Damping = 0.f;
};

/** What the continuous sounds follow, gathered once a frame by the player. */
struct FEphemeraSoundScene
{
	/** 0 in a vacuum .. 1 in air at sea level: how much there is to carry a sound. */
	float Air = 1.f;
	/** Speed through that air, m/s. */
	float AirSpeed = 0.f;
	/** 0..1: how hard the jets are firing, and how much of that is boost. */
	float Jets = 0.f;
	float Boost = 0.f;
	/** 0 (still) .. 1 (a gale): the weather's wind. */
	float Wind = 0.4f;
	/** 0..1: how near running water and the shore are. */
	float Stream = 0.f;
	float Shore = 0.f;
	/** 0 by day .. 1 by night, where the player stands: the score darkens with it. */
	float Night = 0.f;
	/** 0 far from the star .. 1 close over it: its hum is heard. */
	float Star = 0.f;
	/** 0 with the ears in air .. 1 under water: everything comes through dull and close. */
	float Under = 0.f;
	/** Speed along the ground while on it, m/s (0 in the air or afloat): the cloth, the pack and the plants against the legs follow it. */
	float Pace = 0.f;
	/** 0..1: how thick the plants about the legs are, and how much of them is stiff stalk rather than soft grass. */
	float Vegetation = 0.f;
	float Stalks = 0.f;
	/** Depth of water at the feet, m, and the body's speed through it, m/s. */
	float WaterM = 0.f;
	float WaterSpeed = 0.f;
	/** 0..1: how hard the body has been working; the breath follows it. */
	float Exertion = 0.f;
	/** Loading or in the menu: everything is faded down. */
	bool bQuiet = false;
};

/**
 * The player's ears. Sounds are not placed in the world: they are the player's own (steps, suit,
 * jets, lamp) or surround the player (wind, water), so each is simply played, louder or softer.
 *
 * What a sound passes through matters more than where it is. In air everything is heard as it is.
 * In a vacuum nothing arrives from outside at all, and what the body itself does (a step, the
 * jets) is heard only through the suit: quiet and dull, all thump and no detail. Every sound that
 * travels through air is played through that one rule (Through), so a walk on the moon sounds
 * like one without a single moon-specific recording.
 *
 * A step is not one recording but several, each drawn from its own pool with its own small
 * differences of level, pitch and timing: the foot striking the ground, the roll onto the toes a
 * moment later, grit kicked up, the cloth, the pack's hardware, water. Between them they are a
 * different step every time, and the gait decides which recordings, so a sprint is not a walk
 * played louder (docs/research/footsteps_and_space_audio.md, section 6).
 */
UCLASS()
class EPHEMERA_API UEphemeraPlayerAudio : public UActorComponent
{
	GENERATED_BODY()

public:
	UEphemeraPlayerAudio();

	/** Takes the sounds and starts the loops (silent until Follow says otherwise). */
	void Start(UEphemeraAudioSet* Set);
	bool Has(FName Key) const { return Takes.Contains(Key); }
	/** For tests: how many loops are sounding, how many single sounds have been played, and the last of them. */
	FString Report() const;
	/**
	 * Plays one take of a sound once. bThroughAir: it is a sound out in the world (a step, a
	 * landing) and is dulled and quietened as the air thins; otherwise it is inside the suit or
	 * the interface and always sounds the same.
	 */
	void Play(FName Key, float Volume = 1.f, float Pitch = 1.f, bool bThroughAir = true);
	/** Plays what the feet just did, as all the layers it is made of (some of them a moment later). */
	void PlayStep(const FEphemeraStep& Step, const FEphemeraSpace& Space);
	/** Keeps the loops (wind, air rushing past, jets, water, cloth, plants, breath) and the score in step with what is happening. */
	void Follow(const FEphemeraSoundScene& Scene, float DeltaSeconds);

private:
	/** What may be left out of a step when the voices run short (the pack's ticks first, then the cloth, then the grit); never the footfall itself. */
	enum class EShed : uint8 { Never, Debris, Cloth, Gear };
	/** A layer waiting for its moment: the roll after the heel, the cloth after the foot. */
	struct FDue
	{
		double At = 0.0;
		FName Key;
		float GainDb = 0.f;
		float Pitch = 1.f;
		float LowPassHz = 0.f;
		EShed Shed = EShed::Never;
	};
	/** Takes not yet drawn this round, in a shuffled order, and the last few drawn (none of which comes again yet). */
	struct FBag
	{
		TArray<int32> Left;
		TArray<int32> Recent;
	};
	enum class EBreath : uint8 { Calm, Working, Heavy, Recovery };

	/** One layer of a step, gain in dB before the master and the air; with a delay it waits in Due. LowPassHz 0: none of its own. */
	void Layer(FName Key, float GainDb, float Pitch, float DelaySeconds, float LowPassHz = 0.f, EShed Shed = EShed::Never);
	/** Starts a sound now through the master, the air and the water (bThroughAir), unless that leaves it inaudible or the voices are needed for more. */
	USoundBase* Launch(FName Key, float GainDb, float Pitch, float LowPassHz, bool bThroughAir, EShed Shed);
	/** The cloth layer, at BelowDb under the step it goes with; the old suit recording keeps the place it was mixed for. */
	void Cloth(int32 Weight, float StepDb, float BelowDb, float DelaySeconds);
	void BreatheSoon(float Seconds) { NextBreath = FMath::Min(NextBreath, Seconds); }
	/**
	 * The name a layer is played under: Wanted if it has takes, otherwise the first of its stand-ins
	 * that has (a recording of the other foot, the next gait, the one pool per ground there was
	 * before), None if none has and the layer is left out. Remembered, as the takes never change.
	 */
	FName Choose(const FString& Wanted, TFunctionRef<void(TArray<FString>&)> StandIns);
	FName StepKey(int32 Surface, int32 Gait, int32 Foot);
	FName RollKey(int32 Ground, int32 Gait);
	FName ClothKey(int32 Weight);
	FName LandKey(int32 Ground, int32 Weight);
	FName WadeKey(int32 Depth);
	FName BreathKey(EBreath State);
	/** The beds under the steps (cloth, the pack, plants against the legs, wading, under water). */
	void FollowBody(const FEphemeraSoundScene& Scene, float DeltaSeconds);
	/** The breath in the helmet: calm, working, heavy, getting it back, as the body's exertion rises and falls. */
	void FollowBreath(const FEphemeraSoundScene& Scene, float DeltaSeconds);
	/**
	 * The score has no tune and no beat to be in or out of step with: three long chords (day, night,
	 * space) that are always playing and are faded between by where the player is, single notes
	 * that drift over them a few at a time with long silences between, and one swell the first
	 * time the air is left behind. It follows the player; it never announces anything.
	 */
	void FollowScore(const FEphemeraSoundScene& Scene, float DeltaSeconds);
	/** Puts one take on a free voice at a linear gain; returns it (null if nothing was played). */
	USoundBase* PlayAt(FName Key, float Gain, float Pitch, float LowPassHz);
	USoundBase* Pick(FName Key);
	UAudioComponent* MakeVoice();
	void StartLoop(FName Key);
	void SetLoop(FName Key, float Volume, float Pitch, float LowPassHz = 0.f);

	UPROPERTY() TObjectPtr<UEphemeraAudioSet> Sounds;
	UPROPERTY() TArray<TObjectPtr<UAudioComponent>> Voices;
	UPROPERTY() TMap<FName, TObjectPtr<UAudioComponent>> Loops;
	TMap<FName, TArray<USoundBase*>> Takes;
	TMap<FName, FBag> Bags;
	/** What each asked-for name was answered with (see Choose). */
	TMap<FName, FName> Chosen;
	TArray<FDue> Due;
	EBreath Breath = EBreath::Calm;
	float NextBreath = 4.f;
	bool bHeadUnder = false;
	/** The plants against the legs and the water round them, eased so that a new look at the ground is never heard as a step in level. */
	float BrushLevel = 0.f;
	float WadeLevel = 0.f;
	int32 NextVoice = 0;
	int32 Played = 0;
	FName LastPlayed;
	/** The air as last told (for one-shots played between two calls of Follow), and the water. */
	float Air = 1.f;
	float Under = 0.f;
	/** The low-pass every sound from outside goes through: the suit's in thin air, the water's under it. 0 = none. */
	float DullNow() const;
	/** Everything, faded down while loading or in the menu and by the volume setting. */
	float Master = 0.f;
	float JetLevel = 0.f;
	float BoostLevel = 0.f;
	float RushLevel = 0.f;
	float WindLevel = 0.f;
	float StreamLevel = 0.f;
	float ShoreLevel = 0.f;
	double Clock = 0.0;
	/** The score's own loudness (its setting, lower in the menu), and each chord's share of it. */
	float Tune = 0.f;
	float DayLevel = 0.f;
	float NightLevel = 0.f;
	float SpaceLevel = 0.f;
	float StarLevel = 0.f;
	/** Seconds to the next drifting note, and how many more belong to the phrase now playing. */
	float NextNote = 25.f;
	int32 NotesLeft = 0;
	float SinceSwell = 1000.f;
	bool bAboveTheAir = false;
};
