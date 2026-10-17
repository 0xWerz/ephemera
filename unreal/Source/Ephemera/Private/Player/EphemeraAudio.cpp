#include "Player/EphemeraAudio.h"

#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Player/EphemeraUserSettings.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

namespace
{
	// A layered step at three a second holds eight to ten voices at once, a hard landing more, and
	// the score's long notes ring on under them.
	constexpr int32 VoiceCount = 28;
	// -48 dB: a sound this quiet is not heard under anything else, and is not started.
	constexpr float InaudibleDb = -48.f;

	float DbToGain(const float Db)
	{
		return FMath::Pow(10.f, Db / 20.f);
	}

	float GainToDb(const float Gain)
	{
		return 20.f * FMath::LogX(10.f, FMath::Max(Gain, 1.e-6f));
	}

	/** A random amount up to Range either way (dB, seconds). */
	float Jitter(const float Range)
	{
		return FMath::FRandRange(-Range, Range);
	}

	/** A pitch up to Percent either way. Never more than six: past that the ear hears a different sound, not a different step. */
	float Detune(const float Percent)
	{
		return 1.f + FMath::FRandRange(-Percent, Percent) / 100.f;
	}

	/** The lower of two low-pass cut-offs, either of which may be 0 (none). */
	float Lower(const float A, const float B)
	{
		return A <= 0.f ? B : B <= 0.f ? A : FMath::Min(A, B);
	}

	/** How loud a sound from outside arrives: everything in air, a third through the suit alone. */
	float Through(const float Air)
	{
		return FMath::Lerp(0.32f, 1.f, FMath::Sqrt(FMath::Clamp(Air, 0.f, 1.f)));
	}

	/** And how much of it: the suit passes the thump of a step and none of its crunch. 0 = unfiltered. */
	float DullHz(const float Air)
	{
		return Air > 0.97f ? 0.f : FMath::Lerp(380.f, 16000.f, FMath::Pow(FMath::Clamp(Air, 0.f, 1.f), 0.6f));
	}

	/** "step_moss_03" -> "step_moss": the name without its take number. */
	FName KeyOf(const FString& Name)
	{
		int32 Underscore = INDEX_NONE;
		if (Name.FindLastChar(TEXT('_'), Underscore) && Underscore > 0 && Underscore < Name.Len() - 1 && Name.Mid(Underscore + 1).IsNumeric())
		{
			return FName(*Name.Left(Underscore));
		}
		return FName(*Name);
	}

	/** Every other index of a row of Count, nearest to Index first (the lower of two equally near). */
	template <typename FunctionType>
	void ForNeighbours(const int32 Index, const int32 Count, FunctionType&& Visit)
	{
		for (int32 Away = 1; Away < Count; ++Away)
		{
			if (Index - Away >= 0)
			{
				Visit(Index - Away);
			}
			if (Index + Away < Count)
			{
				Visit(Index + Away);
			}
		}
	}

	// ---- What a step is made of (docs/research/footsteps_and_space_audio.md, 6.1 to 6.3) ----

	constexpr int32 SurfaceCount = static_cast<int32>(EEphemeraSurface::Count);
	constexpr int32 WaterSurface = static_cast<int32>(EEphemeraSurface::Water);
	constexpr int32 GaitCount = static_cast<int32>(EEphemeraGait::Sprint) + 1;
	constexpr int32 CreepGait = static_cast<int32>(EEphemeraGait::Creep);
	constexpr int32 RunGait = static_cast<int32>(EEphemeraGait::Run);
	constexpr int32 SprintGait = static_cast<int32>(EEphemeraGait::Sprint);
	const TCHAR* const SurfaceNames[SurfaceCount] = {TEXT("moss"), TEXT("sedge"), TEXT("ash"), TEXT("gravel"), TEXT("basalt"), TEXT("snow"), TEXT("water")};
	const TCHAR* const GaitNames[GaitCount] = {TEXT("creep"), TEXT("walk"), TEXT("jog"), TEXT("run"), TEXT("sprint")};
	const TCHAR* const FootNames[2] = {TEXT("l"), TEXT("r")};
	// The roll of the foot, the scuffs and the landings are recorded on three kinds of ground (and
	// landings on a fourth, water), not on all seven surfaces.
	constexpr int32 SoftGround = 0;
	constexpr int32 GritGround = 1;
	constexpr int32 SnowGround = 2;
	constexpr int32 WaterGround = 3;
	const TCHAR* const GroundNames[4] = {TEXT("soft"), TEXT("grit"), TEXT("snow"), TEXT("water")};
	constexpr int32 GroundOf[SurfaceCount] = {SoftGround, SoftGround, GritGround, GritGround, GritGround, SnowGround, WaterGround};
	// Before the steps were recorded by gait and foot there was one pool per ground, nearest first.
	const TCHAR* const OldSteps[SurfaceCount][2] = {
		{TEXT("step_moss"), nullptr}, {TEXT("step_moss"), nullptr}, {TEXT("step_sand"), TEXT("step_gravel")}, {TEXT("step_gravel"), TEXT("step_rock")},
		{TEXT("step_rock"), TEXT("step_gravel")}, {TEXT("step_snow"), TEXT("step_sand")}, {TEXT("step_water"), nullptr}};

	// The gaits, by speed along the ground (m/s): the middle of each, and how loud it is against a
	// walk. Over 20 dB from a creep to a sprint, as real feet are; the old steps spanned 7.
	constexpr float GaitCentre[GaitCount] = {1.15f, 2.25f, 4.1f, 6.f, 7.6f};
	constexpr float GaitDb[GaitCount] = {-10.f, 0.f, 4.f, 8.f, 10.f};
	// The walk step's own gain, from which the gaits are counted: a jog's footfall comes out where
	// the old jog step was (0.73), a sprint's 2 dB over the old one's. The mix pass against the wind
	// (section 4.2) sets it.
	constexpr float WalkDb = -8.f;
	// The heel strikes and the foot rolls onto its toes this many ms later: a tenth of the gait
	// cycle, so long at a creep and gone at a sprint, where a runner lands on the ball of the foot.
	constexpr float RollMs[GaitCount][2] = {{100.f, 130.f}, {70.f, 110.f}, {40.f, 70.f}, {15.f, 40.f}, {0.f, 15.f}};
	// The cloth under the footfall (dB), and how often the pack's hardware and loose grit are heard.
	constexpr float ClothDb[GaitCount] = {-16.f, -12.f, -9.f, -7.f, -6.f};
	constexpr int32 ClothWeightOf[GaitCount] = {0, 0, 1, 2, 2};
	const TCHAR* const ClothNames[3] = {TEXT("light"), TEXT("medium"), TEXT("hard")};
	constexpr float GearChance[GaitCount] = {0.2f, 0.35f, 0.5f, 0.7f, 0.8f};
	constexpr float DebrisChance[GaitCount] = {0.25f, 0.4f, 0.5f, 0.65f, 0.75f};
	constexpr float PitchPercent[GaitCount] = {3.f, 3.f, 3.f, 3.f, 2.5f};
	// How much loose stuff each surface has to kick up against hard grit (the chances above are
	// grit's): stems in the sedge, a crust on the snow, little in the moss, nothing in the water.
	constexpr float DebrisShare[SurfaceCount] = {0.15f, 0.35f, 0.8f, 1.f, 0.7f, 0.6f, 0.f};
	const TCHAR* const DebrisNames[3] = {TEXT("debris_stem"), TEXT("debris_grit"), TEXT("debris_snow")};
	// Moss and snow swallow the top of a step; hard ground keeps it all (0: open).
	constexpr float SurfaceLowPassHz[SurfaceCount] = {7000.f, 0.f, 0.f, 0.f, 0.f, 6500.f, 0.f};
	const TCHAR* const LandNames[3] = {TEXT("soft"), TEXT("medium"), TEXT("hard")};
	const TCHAR* const WadeNames[3] = {TEXT("ankle"), TEXT("shin"), TEXT("knee")};
	const TCHAR* const BreathNames[4] = {TEXT("calm"), TEXT("working"), TEXT("heavy"), TEXT("recovery")};
	// The breath in the helmet: how loud each state is, and the pause after each breath before the next (s).
	constexpr float BreathDb[4] = {-36.f, -25.f, -19.f, -22.f};
	constexpr float BreathRest[4][2] = {{4.f, 8.f}, {0.2f, 0.6f}, {0.f, 0.15f}, {0.4f, 1.f}};

	const FName GearTick(TEXT("gear_tick"));
	const FName SwishGrass(TEXT("swish_grass"));
	const FName Bubbles(TEXT("bubbles"));
	const FName SuitStep(TEXT("suit_step"));
	const FName OldLand(TEXT("land"));
}

UEphemeraPlayerAudio::UEphemeraPlayerAudio()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UAudioComponent* UEphemeraPlayerAudio::MakeVoice()
{
	UAudioComponent* Voice = NewObject<UAudioComponent>(GetOwner());
	Voice->bAutoActivate = false;
	Voice->bAutoDestroy = false;
	Voice->bAllowSpatialization = false;
	Voice->bStopWhenOwnerDestroyed = true;
	Voice->SetupAttachment(GetOwner()->GetRootComponent());
	Voice->RegisterComponent();
	return Voice;
}

void UEphemeraPlayerAudio::Start(UEphemeraAudioSet* Set)
{
	Sounds = Set;
	Takes.Reset();
	Bags.Reset();
	Chosen.Reset();
	Due.Reset();
	if (!Set || !GetOwner())
	{
		return;
	}
	for (USoundBase* Sound : Set->Sounds)
	{
		if (!Sound)
		{
			continue;
		}
		const FName Key = KeyOf(Sound->GetName());
		Takes.FindOrAdd(Key).Add(Sound);
		if (Key.ToString().StartsWith(TEXT("loop_")))
		{
			// A loop goes on while it cannot be heard (the wind does not start again from its first
			// second each time the jets drown it), which the engine otherwise does not allow.
			Sound->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
			if (USoundWave* Wave = Cast<USoundWave>(Sound))
			{
				Wave->bLooping = true;
			}
		}
	}
	for (int32 Index = Voices.Num(); Index < VoiceCount; ++Index)
	{
		Voices.Add(MakeVoice());
	}
	for (const TPair<FName, TArray<USoundBase*>>& Pair : Takes)
	{
		if (Pair.Key.ToString().StartsWith(TEXT("loop_")))
		{
			StartLoop(Pair.Key);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Ephemera sound: %d sounds under %d names"), Set->Sounds.Num(), Takes.Num());
}

USoundBase* UEphemeraPlayerAudio::Pick(const FName Key)
{
	const TArray<USoundBase*>* Found = Takes.Find(Key);
	if (!Found || Found->IsEmpty())
	{
		return nullptr;
	}
	// A shuffled round of every take, and none of the last few again until others have been heard
	// (up to four, never more than half the takes): eight takes chosen only "not the last one"
	// repeated audibly within a few steps.
	const int32 Count = Found->Num();
	const int32 Avoid = FMath::Min(4, Count / 2);
	FBag& Bag = Bags.FindOrAdd(Key);
	int32 At = INDEX_NONE;
	for (int32 Round = 0; Round < 2 && At == INDEX_NONE; ++Round)
	{
		// A fresh round when this one is spent, or when all that is left of it was heard just now.
		if (Bag.Left.IsEmpty() || Round == 1)
		{
			Bag.Left.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Bag.Left[Index] = Index;
			}
			for (int32 Index = Count - 1; Index > 0; --Index)
			{
				Bag.Left.Swap(Index, FMath::RandHelper(Index + 1));
			}
		}
		At = Bag.Left.IndexOfByPredicate([&Bag](const int32 Take) { return !Bag.Recent.Contains(Take); });
	}
	const int32 Take = Bag.Left[At];
	Bag.Left.RemoveAt(At);
	Bag.Recent.Add(Take);
	if (Bag.Recent.Num() > Avoid)
	{
		Bag.Recent.RemoveAt(0);
	}
	return (*Found)[Take];
}

void UEphemeraPlayerAudio::Play(const FName Key, const float Volume, const float Pitch, const bool bThroughAir)
{
	Launch(Key, GainToDb(Volume), Pitch, 0.f, bThroughAir, EShed::Never);
}

void UEphemeraPlayerAudio::Layer(const FName Key, const float GainDb, const float Pitch, const float DelaySeconds, const float LowPassHz, const EShed Shed)
{
	if (Key.IsNone() || !Takes.Contains(Key))
	{
		return;
	}
	if (DelaySeconds <= 0.f)
	{
		Launch(Key, GainDb, Pitch, LowPassHz, true, Shed);
		return;
	}
	FDue& Later = Due.AddDefaulted_GetRef();
	Later.At = Clock + DelaySeconds;
	Later.Key = Key;
	Later.GainDb = GainDb;
	Later.Pitch = Pitch;
	Later.LowPassHz = LowPassHz;
	Later.Shed = Shed;
}

USoundBase* UEphemeraPlayerAudio::Launch(const FName Key, const float GainDb, const float Pitch, const float LowPassHz, const bool bThroughAir, const EShed Shed)
{
	if (Key.IsNone() || Voices.IsEmpty())
	{
		return nullptr;
	}
	// Out in the world a sound is quietened and dulled by thin air and by water; in the suit it is not.
	const float Out = bThroughAir ? Through(Air) * FMath::Lerp(1.f, 0.75f, Under) : 1.f;
	const float HeardDb = GainDb + GainToDb(Master * Out);
	if (HeardDb < InaudibleDb)
	{
		return nullptr;
	}
	if (Shed != EShed::Never)
	{
		// Short of voices, what is least missed goes first: the pack's ticks, then the cloth, then the grit.
		int32 Busy = 0;
		for (const UAudioComponent* Voice : Voices)
		{
			Busy += Voice && Voice->IsPlaying() ? 1 : 0;
		}
		const int32 Spare = Shed == EShed::Gear ? 10 : Shed == EShed::Cloth ? 7 : 4;
		if (Busy > Voices.Num() - Spare)
		{
			return nullptr;
		}
	}
	return PlayAt(Key, DbToGain(HeardDb), Pitch, bThroughAir ? Lower(LowPassHz, DullNow()) : LowPassHz);
}

USoundBase* UEphemeraPlayerAudio::PlayAt(const FName Key, const float Gain, const float Pitch, const float LowPassHz)
{
	USoundBase* Sound = Gain > DbToGain(InaudibleDb) && !Voices.IsEmpty() ? Pick(Key) : nullptr;
	if (!Sound)
	{
		return nullptr;
	}
	// The voice that has been silent longest. A long note (one of the score's) may still be
	// ringing in the next one round; with this many voices it is by then far down its tail.
	UAudioComponent* Voice = Voices[NextVoice];
	for (int32 Tried = 0; Tried < Voices.Num() && Voice->IsPlaying(); ++Tried)
	{
		NextVoice = (NextVoice + 1) % Voices.Num();
		Voice = Voices[NextVoice];
	}
	NextVoice = (NextVoice + 1) % Voices.Num();
	++Played;
	LastPlayed = Key;
	Voice->Stop();
	Voice->SetSound(Sound);
	Voice->SetVolumeMultiplier(Gain);
	Voice->SetPitchMultiplier(Pitch);
	Voice->SetLowPassFilterEnabled(LowPassHz > 0.f);
	if (LowPassHz > 0.f)
	{
		Voice->SetLowPassFilterFrequency(LowPassHz);
	}
	Voice->Play();
	return Sound;
}

FString UEphemeraPlayerAudio::Report() const
{
	int32 Sounding = 0;
	int32 Heard = 0;
	for (const TPair<FName, TObjectPtr<UAudioComponent>>& Pair : Loops)
	{
		Sounding += Pair.Value && Pair.Value->IsPlaying() ? 1 : 0;
		Heard += Pair.Value && Pair.Value->IsPlaying() && Pair.Value->VolumeMultiplier > 0.01f ? 1 : 0;
	}
	int32 Ringing = 0;
	for (const UAudioComponent* Voice : Voices)
	{
		Ringing += Voice && Voice->IsPlaying() ? 1 : 0;
	}
	return FString::Printf(TEXT("%d of %d loops running (%d audible), %d single sounds played (last %s, %d still ringing)"),
		Sounding, Loops.Num(), Heard, Played, *LastPlayed.ToString(), Ringing);
}

void UEphemeraPlayerAudio::StartLoop(const FName Key)
{
	USoundBase* Sound = Pick(Key);
	if (!Sound || Loops.Contains(Key))
	{
		return;
	}
	UAudioComponent* Loop = MakeVoice();
	Loop->SetSound(Sound);
	Loop->SetVolumeMultiplier(0.f);
	Loop->Play();
	Loops.Add(Key, Loop);
}

void UEphemeraPlayerAudio::SetLoop(const FName Key, const float Volume, const float Pitch, const float LowPassHz)
{
	const TObjectPtr<UAudioComponent>* Found = Loops.Find(Key);
	UAudioComponent* Loop = Found ? Found->Get() : nullptr;
	if (!Loop)
	{
		return;
	}
	if (!Loop->IsPlaying())
	{
		Loop->Play();
	}
	Loop->SetVolumeMultiplier(FMath::Max(Volume, 0.f));
	Loop->SetPitchMultiplier(FMath::Clamp(Pitch, 0.4f, 2.5f));
	Loop->SetLowPassFilterEnabled(LowPassHz > 0.f);
	if (LowPassHz > 0.f)
	{
		Loop->SetLowPassFilterFrequency(LowPassHz);
	}
}

float UEphemeraPlayerAudio::DullNow() const
{
	// Under water what reaches the ear is the low end, as through a wall (sound passes from water to
	// the air in an ear, or a helmet, poorly at every pitch and worst at the high ones).
	const float InAir = DullHz(Air);
	return Under <= 0.01f ? InAir : FMath::Lerp(InAir > 0.f ? InAir : 16000.f, 520.f, FMath::Clamp(Under, 0.f, 1.f));
}

// ------------------------------------------------------------------------------------ steps ---

FName UEphemeraPlayerAudio::Choose(const FString& Wanted, const TFunctionRef<void(TArray<FString>&)> StandIns)
{
	const FName Name(*Wanted);
	if (const FName* Known = Chosen.Find(Name))
	{
		return *Known;
	}
	FName Found = Takes.Contains(Name) ? Name : NAME_None;
	if (Found.IsNone())
	{
		TArray<FString> Others;
		StandIns(Others);
		for (const FString& Other : Others)
		{
			const FName OtherName(*Other);
			if (Takes.Contains(OtherName))
			{
				Found = OtherName;
				break;
			}
		}
	}
	Chosen.Add(Name, Found);
	return Found;
}

FName UEphemeraPlayerAudio::StepKey(const int32 Surface, const int32 Gait, const int32 Foot)
{
	auto Name = [Surface](const int32 Tier, const int32 Side) { return FString::Printf(TEXT("step_%s_%s_%s"), SurfaceNames[Surface], GaitNames[Tier], FootNames[Side]); };
	return Choose(Name(Gait, Foot), [&](TArray<FString>& Also)
	{
		// The other foot, then the nearest gaits (this foot first), then the old pool of this ground
		// or its nearest relation; and moss, which every ground fell back to before.
		Also.Add(Name(Gait, 1 - Foot));
		ForNeighbours(Gait, GaitCount, [&](const int32 Tier)
		{
			Also.Add(Name(Tier, Foot));
			Also.Add(Name(Tier, 1 - Foot));
		});
		for (const TCHAR* Old : OldSteps[Surface])
		{
			if (Old)
			{
				Also.Add(Old);
			}
		}
		Also.Add(TEXT("step_moss"));
	});
}

FName UEphemeraPlayerAudio::RollKey(const int32 Ground, const int32 Gait)
{
	auto Name = [Ground](const int32 Tier) { return FString::Printf(TEXT("roll_%s_%s"), GroundNames[Ground], GaitNames[Tier]); };
	// No stand-in from before: the old steps hold their roll in the one recording.
	return Choose(Name(Gait), [&](TArray<FString>& Also) { ForNeighbours(Gait, GaitCount, [&](const int32 Tier) { Also.Add(Name(Tier)); }); });
}

FName UEphemeraPlayerAudio::ClothKey(const int32 Weight)
{
	return Choose(FString::Printf(TEXT("cloth_%s"), ClothNames[Weight]), [Weight](TArray<FString>& Also)
	{
		ForNeighbours(Weight, 3, [&](const int32 Near) { Also.Add(FString::Printf(TEXT("cloth_%s"), ClothNames[Near])); });
		Also.Add(SuitStep.ToString());
	});
}

FName UEphemeraPlayerAudio::LandKey(const int32 Ground, const int32 Weight)
{
	auto Name = [Ground](const int32 Near) { return FString::Printf(TEXT("land_%s_%s"), GroundNames[Ground], LandNames[Near]); };
	return Choose(Name(Weight), [&](TArray<FString>& Also)
	{
		ForNeighbours(Weight, 3, [&](const int32 Near) { Also.Add(Name(Near)); });
		if (Ground == WaterGround)
		{
			Also.Add(TEXT("splash"));
			Also.Add(TEXT("step_water"));
		}
		Also.Add(OldLand.ToString());
	});
}

FName UEphemeraPlayerAudio::WadeKey(const int32 Depth)
{
	return Choose(FString::Printf(TEXT("wade_%s"), WadeNames[Depth]), [Depth](TArray<FString>& Also)
	{
		ForNeighbours(Depth, 3, [&](const int32 Near) { Also.Add(FString::Printf(TEXT("wade_%s"), WadeNames[Near])); });
		Also.Add(TEXT("step_water"));
	});
}

FName UEphemeraPlayerAudio::BreathKey(const EBreath State)
{
	// Each state stands in for the one nearest it in effort; calm, the quietest, is left out rather than played as another.
	const int32 Index = static_cast<int32>(State);
	return Choose(FString::Printf(TEXT("breath_%s"), BreathNames[Index]), [State](TArray<FString>& Also)
	{
		switch (State)
		{
		case EBreath::Working: Also.Add(TEXT("breath_recovery")); Also.Add(TEXT("breath_heavy")); break;
		case EBreath::Heavy: Also.Add(TEXT("breath_working")); break;
		case EBreath::Recovery: Also.Add(TEXT("breath_working")); Also.Add(TEXT("breath_calm")); break;
		default: break;
		}
	});
}

void UEphemeraPlayerAudio::Cloth(const int32 Weight, const float StepDb, const float BelowDb, const float DelaySeconds)
{
	const FName Key = ClothKey(Weight);
	// The old suit recording was mixed to sit at 0.6 of every footfall (4.4 dB under it) and keeps
	// that place until the cloth recordings arrive.
	const bool bOld = Key == SuitStep;
	Layer(Key, StepDb + (bOld ? -4.4f : BelowDb + Jitter(2.5f)), Detune(3.f), DelaySeconds, 0.f, EShed::Cloth);
}

void UEphemeraPlayerAudio::PlayStep(const FEphemeraStep& Step, const FEphemeraSpace& Space)
{
	if (Takes.IsEmpty())
	{
		return;
	}
	// The ground: one surface drawn by its share, so that a border between two is crossed a step at a
	// time rather than at a line, and the runner-up heard under it while it holds a fair share.
	// (Bare rock where the ground is not known at all.)
	float Total = 0.f;
	int32 Surface = static_cast<int32>(EEphemeraSurface::Basalt);
	for (int32 Index = 0; Index < SurfaceCount; ++Index)
	{
		Total += FMath::Max(Step.Mix[Index], 0.f);
		Surface = Step.Mix[Index] > Step.Mix[Surface] ? Index : Surface;
	}
	float Draw = FMath::FRand() * Total;
	for (int32 Index = 0; Index < SurfaceCount && Draw > 0.f; ++Index)
	{
		Draw -= FMath::Max(Step.Mix[Index], 0.f);
		if (Draw <= 0.f)
		{
			Surface = Index;
		}
	}
	int32 Second = INDEX_NONE;
	for (int32 Index = 0; Index < SurfaceCount; ++Index)
	{
		if (Index != Surface && Step.Mix[Index] > 0.25f * Total && (Second == INDEX_NONE || Step.Mix[Index] > Step.Mix[Second]))
		{
			Second = Index;
		}
	}
	const int32 Ground = GroundOf[Surface];
	// Water has no scuff or start of its own: underfoot it is soft ground.
	const int32 Footing = Ground == WaterGround ? SoftGround : Ground;
	const int32 Foot = static_cast<int32>(Step.Foot);

	// The gait's recordings or, by chance, its neighbour's on the side the speed lies toward, as
	// often as the speed has gone toward that neighbour's middle: the change from one gait to the
	// next is spread over many steps instead of falling on one. The level follows the speed itself.
	const int32 Gait = static_cast<int32>(Step.Gait);
	const int32 Toward = Step.SpeedMS >= GaitCentre[Gait] ? FMath::Min(Gait + 1, GaitCount - 1) : FMath::Max(Gait - 1, 0);
	const float Across = Toward == Gait ? 0.f : FMath::Clamp((Step.SpeedMS - GaitCentre[Gait]) / (GaitCentre[Toward] - GaitCentre[Gait]), 0.f, 1.f);
	const int32 Tier = FMath::FRand() < Across ? Toward : Gait;
	const float PaceDb = WalkDb + FMath::Lerp(GaitDb[Gait], GaitDb[Toward], Across);

	// Standing water (section 5): from ankle depth a wading step is the footfall, and the ground is
	// heard under it, less the deeper; to the thigh, hardly at all.
	const float Depth = FMath::Max(Step.WaterDepthM, 0.f);
	const int32 Wade = Depth < 0.15f ? 0 : Depth < 0.35f ? 1 : 2;
	const float SunkDb = Depth < 0.05f ? 0.f : Depth < 0.15f ? -8.f : Depth < 0.6f ? -14.f : -24.f;
	// Wet ground and soft surroundings take the top off what the ground says.
	const float Wet = FMath::Clamp(Step.Wetness, 0.f, 1.f);
	const float Damp = 2000.f * Wet + 3000.f * FMath::Clamp(Space.Damping, 0.f, 1.f);
	auto GroundHz = [Damp](const int32 Which)
	{
		const float Own = SurfaceLowPassHz[Which];
		return Damp < 50.f ? Own : (Own > 0.f ? Own : 16000.f) - Damp;
	};
	// Downhill each foot lands harder and sends more down the slope.
	const float Downhill = FMath::SmoothStep(3.f, 12.f, -Step.SlopeDeg);

	// The water at the feet, as a layer of its own: the wading step, a film's splash, or (on ground
	// still wet from it) now and then a quiet one. Never the very recording the footfall just used.
	auto Splash = [&](const float Db, const float DelaySeconds, const FName Besides)
	{
		float Level = Db;
		if (Depth >= 0.05f)
		{
			Level += static_cast<float>(Wade);
		}
		else if (Depth > 0.f)
		{
			Level -= 6.f;
		}
		else if (Wet > 0.15f && FMath::FRand() < Wet)
		{
			Level -= 16.f;
		}
		else
		{
			return;
		}
		const FName Key = WadeKey(Depth >= 0.05f ? Wade : 0);
		if (Key != Besides)
		{
			Layer(Key, Level + Jitter(1.5f), Detune(3.f), DelaySeconds);
		}
	};

	switch (Step.Kind)
	{
	case EEphemeraStepKind::Step:
	case EEphemeraStepKind::Start:
	{
		// The first step from rest is a careful one: no grit, no rattle, the suit shifting its weight.
		const bool bFirst = Step.Kind == EEphemeraStepKind::Start;
		const float Db = PaceDb + (bFirst ? -4.f : 0.f) + 2.f * Downhill;
		const float Pitch = Detune(PitchPercent[Tier]);
		const FName Impact = StepKey(Surface, Tier, Foot);
		Layer(Impact, Db + SunkDb + Jitter(1.5f), Pitch, 0.f, GroundHz(Surface));
		if (Second != INDEX_NONE)
		{
			const FName Blend = StepKey(Second, Tier, Foot);
			if (Blend != Impact)
			{
				Layer(Blend, Db + SunkDb + GainToDb(Step.Mix[Second] / (Step.Mix[Surface] + Step.Mix[Second])) + Jitter(1.5f), Detune(PitchPercent[Tier]), 0.f, GroundHz(Second));
			}
		}
		// The roll onto the toes, sooner uphill (the ground meets the forefoot early). Closer than
		// 12 ms it would only smear the strike, and the run and sprint recordings hold it already.
		if (Ground != WaterGround)
		{
			const float RollAtMs = FMath::FRandRange(RollMs[Tier][0], RollMs[Tier][1]) - 15.f * FMath::Clamp(Step.SlopeDeg / 15.f, -1.f, 1.f) + Jitter(6.f);
			if (RollAtMs >= 12.f)
			{
				Layer(RollKey(Ground, Tier), Db + SunkDb - 3.f + Jitter(1.5f), Pitch, RollAtMs / 1000.f, GroundHz(Surface));
			}
		}
		if (bFirst)
		{
			Layer(Choose(FString::Printf(TEXT("start_%s"), GroundNames[Footing]), [](TArray<FString>&) {}), Db + SunkDb - 2.f + Jitter(1.5f), Detune(3.f), 0.f, GroundHz(Surface));
		}
		else if (FMath::FRand() < DebrisChance[Tier] * DebrisShare[Surface] * (1.f + 0.2f * Downhill))
		{
			Layer(FName(DebrisNames[Footing]), Db + SunkDb - 10.f + Jitter(2.5f), Detune(3.f), FMath::FRandRange(0.f, 0.03f), GroundHz(Surface), EShed::Debris);
		}
		// The cloth moves a little before the foot lands as often as after: what would come early comes with it.
		if (bFirst || FMath::FRand() < FMath::Lerp(0.6f, 1.f, Step.Intensity))
		{
			Cloth(bFirst ? 0 : ClothWeightOf[Tier], Db, ClothDb[Tier] + 3.f * Step.Exertion, FMath::Max(0.f, FMath::FRandRange(-0.02f, 0.04f) + Jitter(0.006f)));
		}
		if (!bFirst && FMath::FRand() < GearChance[Tier])
		{
			Layer(GearTick, Db - 14.f + Jitter(2.5f), Detune(3.f), FMath::FRandRange(0.02f, 0.06f), 0.f, EShed::Gear);
		}
		Splash(Db, 0.f, Impact);
		// Through thick plants the leg swinging forward brushes them, half way to the next footfall.
		if (Step.StrideSeconds > 0.f && FMath::FRand() < FMath::SmoothStep(0.4f, 0.9f, Step.Vegetation) * FMath::SmoothStep(1.f, 3.f, Step.SpeedMS))
		{
			Layer(SwishGrass, PaceDb - 10.f + 4.f * Step.Vegetation + Jitter(2.5f), Detune(3.f), 0.45f * Step.StrideSeconds, 0.f, EShed::Debris);
		}
		break;
	}
	case EEphemeraStepKind::Settle:
	{
		// Coming to a stop after striding: the feet find each other, a soft step and often a second,
		// and the suit settles. Without its own recordings, the ground's creep step stands in.
		const float Db = PaceDb - 10.f;
		const FName Scuff = Choose(FString::Printf(TEXT("settle_%s"), GroundNames[Footing]), [](TArray<FString>&) {});
		const int32 Feet = FMath::FRand() < 0.6f ? 2 : 1;
		FName Placed;
		for (int32 Index = 0; Index < Feet; ++Index)
		{
			Placed = Scuff.IsNone() ? StepKey(Surface, CreepGait, (Foot + 1 + Index) % 2) : Scuff;
			Layer(Placed, Db + SunkDb - 2.f * Index + Jitter(1.5f), Detune(3.f), 0.12f * (Index + 1) + Jitter(0.006f), GroundHz(Surface));
		}
		Cloth(0, Db, -3.f, FMath::FRandRange(0.06f, 0.1f));
		Splash(Db, 0.12f, Placed);
		if (Step.Exertion > 0.5f)
		{
			BreatheSoon(0.4f);
		}
		break;
	}
	case EEphemeraStepKind::Turn:
	{
		// A pivot on the spot: the sole twisting on the ground and the suit swinging round, louder the faster the turn.
		const float Db = WalkDb - 8.f + 6.f * FMath::SmoothStep(120.f, 400.f, Step.YawRateDegS);
		Layer(Choose(FString::Printf(TEXT("turn_%s"), GroundNames[Footing]), [](TArray<FString>&) {}), Db + SunkDb + Jitter(1.5f), Detune(3.f), 0.f, GroundHz(Surface));
		Cloth(0, Db, -4.f, FMath::FRandRange(0.02f, 0.05f));
		break;
	}
	case EEphemeraStepKind::Skid:
	{
		// Pulled up from a run within a stride: the sole drags and the ground sprays from under it.
		const float Db = PaceDb - 2.f;
		Layer(Choose(FString::Printf(TEXT("skid_%s"), GroundNames[Footing]), [](TArray<FString>&) {}), Db + SunkDb + Jitter(1.5f), Detune(3.f), 0.f, GroundHz(Surface));
		if (DebrisShare[Surface] > 0.f)
		{
			const FName Spray(DebrisNames[Footing]);
			Layer(Spray, Db + SunkDb - 6.f + Jitter(2.5f), Detune(3.f), FMath::FRandRange(0.03f, 0.07f), GroundHz(Surface), EShed::Debris);
			if (FMath::FRand() < DebrisShare[Surface])
			{
				Layer(Spray, Db + SunkDb - 10.f + Jitter(2.5f), Detune(3.f), FMath::FRandRange(0.08f, 0.14f), GroundHz(Surface), EShed::Debris);
			}
		}
		Cloth(2, Db, ClothDb[Tier], FMath::FRandRange(0.f, 0.03f));
		Splash(Db, 0.f, NAME_None);
		break;
	}
	case EEphemeraStepKind::Takeoff:
	{
		// The push off: a run's footfall a little down, the suit swinging up after it, the pack's hardware.
		const float Db = WalkDb + GaitDb[RunGait] - 3.f;
		const FName Impact = StepKey(Surface, RunGait, Foot);
		Layer(Impact, Db + SunkDb + Jitter(1.5f), Detune(3.f), 0.f, GroundHz(Surface));
		Layer(Choose(TEXT("takeoff"), [](TArray<FString>& Also) { Also.Add(TEXT("jump")); }), Db - 1.5f + Jitter(1.5f), Detune(3.f), 0.02f + Jitter(0.006f));
		Layer(GearTick, Db - 12.f + Jitter(2.5f), Detune(3.f), FMath::FRandRange(0.03f, 0.07f), 0.f, EShed::Gear);
		Splash(Db, 0.f, Impact);
		if (Step.Exertion > 0.4f)
		{
			BreatheSoon(0.3f);
		}
		break;
	}
	case EEphemeraStepKind::Land:
	{
		// Touchdown, by how fast the ground came up. Under 3 m/s one footfall and the suit settling;
		// from 3 the feet arrive one after the other, the pack thumps and grit flies; from 6 the
		// whole weight goes into the ground, felt as much as heard, and the breath goes out.
		const float Fall = Step.FallMS;
		const int32 Weight = Fall < 3.f ? 0 : Fall < 6.f ? 1 : 2;
		const float Db = WalkDb + FMath::Lerp(5.f, 11.f, FMath::Clamp((Fall - 1.2f) / 8.8f, 0.f, 1.f));
		const int32 Into = Depth >= 0.05f || Surface == WaterSurface ? WaterGround : Ground;
		const FName Key = LandKey(Into, Weight);
		const float LandHz = Into == WaterGround ? 0.f : GroundHz(Surface);
		const float Heavy = FMath::Lerp(1.04f, 0.94f, FMath::SmoothStep(1.2f, 10.f, Fall));
		Layer(Key, Db + Jitter(1.5f), Heavy * Detune(3.f), 0.f, LandHz);
		// The old landing was one recording of a whole landing for every ground, played with that
		// ground's footfall; the second foot is in it already.
		if (Key == OldLand)
		{
			Layer(StepKey(Surface, SprintGait, Foot), Db + SunkDb - 2.f + Jitter(1.5f), Detune(3.f), 0.f, GroundHz(Surface));
		}
		if (Weight >= 1)
		{
			if (Key != OldLand)
			{
				Layer(Key, Db - 3.f + Jitter(1.5f), Heavy * Detune(3.f), FMath::FRandRange(0.025f, 0.045f), LandHz);
			}
			Layer(GearTick, Db - 10.f + Jitter(2.5f), 0.94f * Detune(3.f), FMath::FRandRange(0.03f, 0.06f), 0.f, EShed::Gear);
			if (DebrisShare[Surface] > 0.f)
			{
				Layer(FName(DebrisNames[Footing]), Db + SunkDb - 8.f + Jitter(2.5f), Detune(3.f), FMath::FRandRange(0.02f, 0.06f), GroundHz(Surface), EShed::Debris);
			}
		}
		if (Weight == 2)
		{
			// The body's weight: the same landing a good way down in pitch, with only its lowest notes left.
			Layer(Key, Db - 4.f + Jitter(1.5f), FMath::FRandRange(0.6f, 0.66f), 0.f, 220.f);
			BreatheSoon(0.35f);
		}
		Cloth(Weight, Db, Weight == 0 ? -10.f : Weight == 1 ? -9.f : -8.f, Weight == 2 ? FMath::FRandRange(0.06f, 0.11f) : FMath::FRandRange(0.f, 0.03f));
		Splash(Db - 4.f, 0.f, Key);
		break;
	}
	}
}

// ------------------------------------------------------------------------------------ loops ---

void UEphemeraPlayerAudio::Follow(const FEphemeraSoundScene& Scene, const float DeltaSeconds)
{
	Clock += DeltaSeconds;
	Air = FMath::Clamp(Scene.Air, 0.f, 1.f);
	Under = FMath::FInterpTo(Under, FMath::Clamp(Scene.Under, 0.f, 1.f), DeltaSeconds, 6.f);
	const float Dry = 1.f - Under;
	// Up gently when the loading screen lifts, down quickly for the menu.
	const float Loud = FMath::Clamp(UEphemeraUserSettings::Get()->Volume, 0.f, 1.f);
	Master = FMath::FInterpTo(Master, Scene.bQuiet ? 0.f : Loud, DeltaSeconds, Scene.bQuiet ? 7.f : 1.6f);

	// The layers of steps that were to come a moment after their footfall. A frame is 8 to 16 ms,
	// fine enough for offsets of 15 to 130.
	for (int32 Index = Due.Num() - 1; Index >= 0; --Index)
	{
		if (Due[Index].At <= Clock)
		{
			const FDue Now = Due[Index];
			Due.RemoveAtSwap(Index);
			Launch(Now.Key, Now.GainDb, Now.Pitch, Now.LowPassHz, true, Now.Shed);
		}
	}

	// Wind is never steady: three slow swells of different lengths, which between them do not
	// repeat for some minutes, make its gusts.
	const float Gust = FMath::Clamp(0.5f + 0.25f * FMath::Sin(static_cast<float>(Clock * UE_TWO_PI / 7.3))
		+ 0.15f * FMath::Sin(static_cast<float>(Clock * UE_TWO_PI / 3.1 + 1.3)) + 0.10f * FMath::Sin(static_cast<float>(Clock * UE_TWO_PI / 17.9 + 4.0)), 0.f, 1.f);
	const float Blow = (0.18f + 0.82f * FMath::Clamp(Scene.Wind, 0.f, 1.f)) * (0.5f + 0.5f * Gust);
	WindLevel = FMath::FInterpTo(WindLevel, FMath::Sqrt(Air) * Blow, DeltaSeconds, 1.2f);
	SetLoop(TEXT("loop_wind"), WindLevel * 0.6f * Master * Dry, 0.94f + 0.12f * Gust);
	// High up, where the air is half gone and less, the wind of the ground gives way to a thinner
	// one (a recording of wind on Mars, which is what wind is with almost nothing to carry it).
	SetLoop(TEXT("loop_thin"), FMath::Clamp(2.6f * FMath::Sqrt(Air) * FMath::Square(1.f - Air), 0.f, 1.f) * Blow * 0.7f * Master, 0.96f + 0.08f * Gust);

	// Air going past: nothing at a run, a roar in a long fall. Thin air is quieter but still rushes.
	RushLevel = FMath::FInterpTo(RushLevel, FMath::Sqrt(Air) * FMath::SmoothStep(7.f, 60.f, Scene.AirSpeed), DeltaSeconds, 4.f);
	SetLoop(TEXT("loop_rush"), RushLevel * 0.85f * Master * Dry, 0.8f + 0.5f * RushLevel);

	// The jets come on at once and die away a little slower, as a burner does.
	const bool bWasLit = JetLevel > 0.04f;
	const float Jets = FMath::Clamp(Scene.Jets, 0.f, 1.f);
	JetLevel = FMath::FInterpTo(JetLevel, Jets, DeltaSeconds, Jets > JetLevel ? 30.f : 9.f);
	BoostLevel = FMath::FInterpTo(BoostLevel, FMath::Clamp(Scene.Boost, 0.f, 1.f) * Jets, DeltaSeconds, 10.f);
	const bool bLit = JetLevel > 0.04f;
	if (bLit != bWasLit)
	{
		Play(bLit ? TEXT("jet_ignite") : TEXT("jet_stop"), bLit ? 0.8f : 0.55f, FMath::FRandRange(0.96f, 1.05f));
	}
	// A gas jet's roar grows louder and brighter with thrust, not higher: its pitch stays within 2% of the recording's (a hum
	// sliding 24% with thrust is what was heard as a lightsaber). The brightness is a low-pass opening, from a muffled 1.5 kHz as the jets
	// catch or die to 9 kHz at full thrust and wide open on boost; thin air or water dull it further.
	const float JetOpenHz = FMath::Lerp(FMath::Lerp(1500.f, 9000.f, FMath::Pow(JetLevel, 1.2f)), 16000.f, BoostLevel);
	SetLoop(TEXT("loop_jet"), FMath::Pow(JetLevel, 0.7f) * 0.75f * Through(Air) * Master, 0.98f + 0.03f * JetLevel + 0.01f * BoostLevel, Lower(JetOpenHz, DullNow()));
	SetLoop(TEXT("loop_jet_boost"), BoostLevel * 0.7f * Through(Air) * Master, 0.99f + 0.03f * BoostLevel, DullNow());

	StreamLevel = FMath::FInterpTo(StreamLevel, FMath::Clamp(Scene.Stream, 0.f, 1.f) * Air, DeltaSeconds, 1.5f);
	ShoreLevel = FMath::FInterpTo(ShoreLevel, FMath::Clamp(Scene.Shore, 0.f, 1.f) * Air, DeltaSeconds, 1.5f);
	SetLoop(TEXT("loop_stream"), StreamLevel * 0.6f * Master * Dry, 1.f);
	// The sea is heard from under it too: the surf becomes a low wash all round.
	SetLoop(TEXT("loop_sea"), FMath::Max(ShoreLevel, Under) * 0.6f * Master, FMath::Lerp(1.f, 0.8f, Under), DullNow());
	// Where there is no air there is still the suit: its faint hum is all the quiet there is.
	SetLoop(TEXT("loop_suit"), (1.f - Air) * 0.4f * Master, 1.f);
	FollowBody(Scene, DeltaSeconds);
	FollowBreath(Scene, DeltaSeconds);
	FollowScore(Scene, DeltaSeconds);
}

void UEphemeraPlayerAudio::FollowBody(const FEphemeraSoundScene& Scene, const float DeltaSeconds)
{
	// These are the body's sounds out in the air, quietened and dulled as its steps are.
	const float Out = Through(Air) * FMath::Lerp(1.f, 0.75f, Under) * Master;
	const float Dull = DullNow();
	const float Pace = FMath::Max(Scene.Pace, 0.f);
	const float Wading = FMath::Max(Scene.WaterM, 0.f);
	// The cloth as a bed that follows the legs: a walk's slow brush giving way to a jog's quicker
	// swish, both louder for a body working hard and gone once the water is over the knees. The
	// pack bounces from a jog up. One-shots alone, one per step, are where the "loop" feel came from.
	const float Dressed = Out * DbToGain(3.f * FMath::Clamp(Scene.Exertion, 0.f, 1.f)) * (1.f - FMath::SmoothStep(0.4f, 0.9f, Wading));
	const float Swing = 0.92f + 0.12f * FMath::SmoothStep(0.8f, 8.f, Pace);
	SetLoop(TEXT("loop_cloth_walk"), DbToGain(-30.f) * FMath::SmoothStep(0.8f, 1.8f, Pace) * (1.f - FMath::SmoothStep(3.f, 4.5f, Pace)) * Dressed, Swing, Dull);
	SetLoop(TEXT("loop_cloth_jog"), DbToGain(-26.f) * FMath::SmoothStep(3.f, 4.5f, Pace) * Dressed, Swing, Dull);
	SetLoop(TEXT("loop_gear_bounce"), DbToGain(-28.f) * FMath::SmoothStep(3.f, 5.2f, Pace) * Dressed, Swing, Dull);
	// Plants against the legs: as thick as they are, from a brisk walk up, brighter the faster
	// they are pushed through; stiff stalks (heath) and soft grass are different recordings.
	BrushLevel = FMath::FInterpTo(BrushLevel, FMath::Clamp(Scene.Vegetation, 0.f, 1.f) * FMath::SmoothStep(1.5f, 5.f, Pace), DeltaSeconds, 4.f);
	const float Stalks = FMath::Clamp(Scene.Stalks, 0.f, 1.f);
	const float Pushed = 0.9f + 0.2f * FMath::SmoothStep(1.5f, 8.f, Pace);
	const float Brushed = Lower(FMath::Lerp(3500.f, 14000.f, FMath::SmoothStep(1.5f, 6.f, Pace)), Dull);
	SetLoop(TEXT("loop_brush_soft"), DbToGain(-17.f) * BrushLevel * (1.f - Stalks) * Out, Pushed, Brushed);
	SetLoop(TEXT("loop_brush_hard"), DbToGain(-19.f) * BrushLevel * Stalks * Out, Pushed, Brushed);
	// Water pushed aside: as much as the body is in it and moving through it, heavier the deeper.
	WadeLevel = FMath::FInterpTo(WadeLevel, FMath::SmoothStep(0.08f, 0.5f, Wading) * FMath::SmoothStep(0.2f, 1.6f, FMath::Max(Scene.WaterSpeed, 0.f)), DeltaSeconds, 3.f);
	SetLoop(TEXT("loop_wade"), DbToGain(-14.f + 4.f * FMath::SmoothStep(0.5f, 1.1f, Wading)) * WadeLevel * Out, 0.9f + 0.15f * FMath::SmoothStep(0.2f, 2.5f, Scene.WaterSpeed), Dull);
	// With the head under, the water's own sound closes round it (over the fifth of a second Under takes to come).
	SetLoop(TEXT("loop_underwater"), DbToGain(-12.f) * Under * Master, 1.f);
	// Going under, the air comes off the helmet in a burst; coming up, the first thing is a breath.
	const bool bUnder = Scene.Under > 0.5f;
	if (bUnder && !bHeadUnder)
	{
		Layer(Bubbles, -12.f + Jitter(1.5f), Detune(3.f), FMath::FRandRange(0.03f, 0.12f));
	}
	else if (!bUnder && bHeadUnder)
	{
		BreatheSoon(0.3f);
	}
	bHeadUnder = bUnder;
}

void UEphemeraPlayerAudio::FollowBreath(const FEphemeraSoundScene& Scene, const float DeltaSeconds)
{
	// Each state is held until the exertion is well past the line into the next, so a body working
	// at the edge of one does not flicker between two kinds of breathing. Getting the breath back
	// comes between hard work and calm, whichever way it went.
	const float Effort = FMath::Clamp(Scene.Exertion, 0.f, 1.f);
	switch (Breath)
	{
	case EBreath::Calm: Breath = Effort > 0.4f ? EBreath::Working : Breath; break;
	case EBreath::Working: Breath = Effort > 0.72f ? EBreath::Heavy : Effort < 0.28f ? EBreath::Recovery : Breath; break;
	case EBreath::Heavy: Breath = Effort < 0.55f ? EBreath::Recovery : Breath; break;
	case EBreath::Recovery: Breath = Effort > 0.72f ? EBreath::Heavy : Effort > 0.5f ? EBreath::Working : Effort < 0.12f ? EBreath::Calm : Breath; break;
	}
	NextBreath -= DeltaSeconds;
	if (NextBreath > 0.f)
	{
		return;
	}
	// In the helmet, so never dulled by thin air or water; each breath is one take, and the next
	// comes after it has finished, sooner the harder the body is working.
	const int32 State = static_cast<int32>(Breath);
	const USoundBase* Took = Launch(BreathKey(Breath), BreathDb[State] + Jitter(1.5f), Detune(3.f), 0.f, false, EShed::Never);
	const float Seconds = Took ? FMath::Min(Took->GetDuration(), 8.f) : 0.f;
	NextBreath = Seconds + FMath::FRandRange(BreathRest[State][0], BreathRest[State][1]);
	// Under water the breath out leaves the helmet as bubbles.
	if (Took && Under > 0.5f)
	{
		Layer(Bubbles, -16.f + Jitter(2.5f), Detune(3.f), 0.45f * Seconds);
	}
}

void UEphemeraPlayerAudio::FollowScore(const FEphemeraSoundScene& Scene, const float DeltaSeconds)
{
	// It plays under the loading screen (so the first thing met is the mood, not a silence) and, lower, behind the menu.
	const float Wanted = FMath::Clamp(UEphemeraUserSettings::Get()->MusicVolume, 0.f, 1.f) * (Scene.bQuiet ? 0.7f : 1.f);
	Tune = FMath::FInterpTo(Tune, Wanted, DeltaSeconds, 0.8f);
	// Which chord: space once the air is nearly gone, otherwise day or night. The change takes
	// most of ten seconds, so it is never heard as a change.
	const float Space = 1.f - FMath::SmoothStep(0.02f, 0.3f, Air);
	const float Night = FMath::Clamp(Scene.Night, 0.f, 1.f);
	DayLevel = FMath::FInterpTo(DayLevel, (1.f - Space) * (1.f - Night), DeltaSeconds, 0.3f);
	NightLevel = FMath::FInterpTo(NightLevel, (1.f - Space) * Night, DeltaSeconds, 0.3f);
	SpaceLevel = FMath::FInterpTo(SpaceLevel, Space, DeltaSeconds, 0.3f);
	SetLoop(TEXT("loop_music_day"), DayLevel * 0.9f * Tune, 1.f);
	SetLoop(TEXT("loop_music_night"), NightLevel * 0.9f * Tune, 1.f);
	SetLoop(TEXT("loop_music_space"), SpaceLevel * 0.9f * Tune, 1.f);
	// The star has a voice of its own (the Sun's, as SOHO recorded it): nothing until you are well
	// on the way there, everything when it fills the sky.
	StarLevel = FMath::FInterpTo(StarLevel, FMath::Clamp(Scene.Star, 0.f, 1.f), DeltaSeconds, 0.5f);
	SetLoop(TEXT("loop_star"), FMath::Square(StarLevel) * Tune, 1.f);
	if (Scene.bQuiet)
	{
		return;
	}
	// Leaving the air: the one moment the score steps forward. Not again for some minutes.
	SinceSwell += DeltaSeconds;
	const bool bAbove = Space > 0.9f;
	if (bAbove && !bAboveTheAir && SinceSwell > 240.f && Clock > 20.0)
	{
		SinceSwell = 0.f;
		NextNote = 45.f;
		PlayAt(TEXT("awe"), 0.9f * Tune, 1.f, 0.f);
	}
	bAboveTheAir = bAbove;
	// Notes that drift over the chord: one, two or three a few seconds apart, then most of a minute of nothing.
	NextNote -= DeltaSeconds;
	if (NextNote <= 0.f)
	{
		if (NotesLeft <= 0)
		{
			// Out in space, one time in three, it is not a note at all but something heard: a real
			// recording of a planet's radio voice, from very far off. Then a long quiet.
			if (Space > 0.5f && FMath::FRand() < 0.34f && Has(TEXT("voice_space")))
			{
				PlayAt(TEXT("voice_space"), 0.55f * Tune, 1.f, 0.f);
				NextNote = FMath::FRandRange(60.f, 110.f);
				return;
			}
			NotesLeft = FMath::RandRange(1, 3);
		}
		PlayAt(Space > 0.5f ? TEXT("mote_space") : TEXT("mote_ground"), FMath::FRandRange(0.45f, 0.75f) * Tune, 1.f, 0.f);
		--NotesLeft;
		NextNote = NotesLeft > 0 ? FMath::FRandRange(2.2f, 5.5f) : FMath::FRandRange(35.f, 85.f);
	}
}
