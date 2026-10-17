# Footsteps, movement and the sense of space: research and design brief

Research date: 2026-10-14. Web research, reading the repo and searching the Sonniss index only. No source file was edited, no Unreal run, nothing downloaded, no ssh.

Scope: bring the player's footsteps and movement sound of "Ephemera" (UE 5.8, C++, first person, Iceland-like planet) to the bar of The Last of Us Part II / Uncharted 4 / Alan Wake 2, using only real recordings from the Sonniss GDC bundles.

## Status legend

| Tag | Meaning |
|---|---|
| [V] | Verified: read on the page (or in the repo) this session. Words in quotation marks are verbatim; other lines on a [V] item are paraphrase of the same page. |
| [S] | Secondary: only a search-result snippet, a forum post, or a page that was only partly readable. Source named, not fully read. |
| [D] | Derived or designed here: arithmetic from stated inputs, or a starting value I chose. Tune by ear and by meter. |
| [K] | Engine API recalled from knowledge of UE headers. Epic's API-reference pages mostly did not render for the fetch tool, so check each name against the 5.8 headers before relying on it. |
| [U] | Unverified or conflicting. Do not rely on it. |

The player's verdict that this brief answers: "walking steps sound so bad and generic", "not diverse and real", "sound effects in a loop ... doesn't give depth in volume, diversity", "it sounds like minecraft".

---

## 0. Summary (the twelve things that matter)

1. The Minecraft sound is structural, not a matter of better samples. One recording per step, one loudness tier, a metronome cadence, played dry and mono-in-the-ears, picked from 8 takes with only "not the same as last time". Every one of those is in the code (section 1).
2. Every AAA reference builds a step from several layers (surface, shoe/body, cloth, gear, debris) and picks each layer from a different pool, so the combinations rarely repeat. Sucker Punch's per-character set is "gear, cloth, body, and surface" in hard / medium / light variants [V]. Naughty Dog records "50 to 60 assets per locomotion tag" and ships 8 to 15 of them per tag [V] (section 2).
3. Intensity is not a gain knob. A creep, a walk, a jog, a run and a sprint are different recordings. Sucker Punch blends footstep types "based on the velocity of the character" [V]. Our steps currently differ by 7 dB at most (section 1) and each take is normalised to the same RMS, which erases the natural spread between soft and hard steps.
4. Adopt five speed tiers measured in m/s, not named after `WalkSpeed`. The default 4.8 m/s "walk" is, by cadence (2.9 steps/s), a jog. The tier the player hears most should get the most takes (section 6).
5. A step is more than the footfall. Add start-from-rest, settle (stop), turn pivot, skid, slope slide, take-off and landing tiers, wet and wading states. Sucker Punch ships "scuffs, plants, turns, drags, slides, and settles" for exactly this reason [V].
6. Surface is a weighted mix, not a switch. Naughty Dog "implemented surface blending and additives" and "surface probes are now multi-sampling" [V]; Sucker Punch cross-fades dirt to stone over roughly a second [V]. We already have the terrain fields (Cover, Ice, Flow, Moisture, height against tide); turn them into weights.
7. Depth comes from the space, not from louder samples. Uncharted 4's biggest audio advance was "switching to Impulse Response Reverbs" so sounds happen "in the actual environment on the fly" [V]. In UE 5.8 this is achievable in C++: convolution reverb on submixes, per-voice submix sends driven by a cheap ray probe of the surroundings, plus a computed slap-back echo (section 3).
8. Do not spatialise the player's own foley with HRTF. Sucker Punch routes almost all of Atsu's footsteps, clothing and sheaths "through a 3D passthrough buss" because "HRTF processing can have a negative effect on the fidelity of sound", worst in the low end [V]. Keep them 2D, keep the recorded stereo width, bake a small left/right foot bias.
9. Do not adopt Steam Audio, MetaSound graphs, Audio Modulation or Quartz for this. Reasons in section 3.4. Plain `UAudioComponent` voices (already used here), a few submixes, convolution reverb and C++ control are enough.
10. Mixing: give footsteps about 20 to 24 dB of dynamic range between creep and sprint, keep the whole mix at a console-style -24 LUFS integrated with a wide loudness range, and let quiet be quiet. Naughty Dog rejected HDR-style "make quiet things audible" because "things are scarier when you're straining to hear them" [V] (section 4).
11. Repetition over hours is beaten by layer combinatorics, shuffle bags, per-layer jitter, loops with analog control for cloth and brush (Naughty Dog: "nearly infinite variation with a minimal memory footprint" [V]), rare one-off events, and culling the quietest layers when loud things happen (section 4.4).
12. The bundles hold no recording of dry black volcanic sand steps and nothing of footsteps on bare basalt as such. These are real gaps (section 8). Everything else in this brief has at least three candidates in the index (section 7).

---

## 1. What the current system does and why it reads as "Minecraft"

From `unreal/Tools/audio/steps.py`, `EphemeraAudio.cpp/.h`, and `AEphemeraPlayer::Footstep / GroundUnderfoot` (read today; note that between my first and second reading someone added underwater handling to the audio, see the end of this section).

| Finding | Where | Effect on the ear |
|---|---|---|
| Each surface is one pool of 8 takes (water 6), one intensity, `step_<surface>_NN`. | `steps.py` `SETS` | A creep and a sprint pull from the same takes. 8 takes with "not the last one" repeats a given take every 8 steps on average. |
| Every take is rescaled to `level` = -25 dB RMS plus a random +-1 dB. | `steps.py` lines 139 to 141 | Real steps differ by 6 to 12 dB between soft and hard. This throws that away; every step has the same weight. |
| Each take is collapsed to mono and copied to both channels ("a step is under you"). | `steps.py` `shaped()` | A dead-centre, in-head tap with no width and no foot-to-foot difference. |
| Loudness = `GetMappedRangeValueClamped(150..SprintSpeed -> 0.45..1)` as linear gain. | `EphemeraPlayer.cpp` `Footstep` call in the stride code | 0.45 to 1.0 is only 6.9 dB. This is the "doesn't give depth in volume" complaint, exactly. |
| Pitch +-6% random; one layer ("suit_step") always at 0.6 x loudness. | `Footstep()` | Pitch jitter alone does not hide that the sample set is small; the cloth layer is the same quiet loop under every step. |
| `Pick()` only avoids the previous take. | `EphemeraAudio.cpp` `Pick` | Audible repeats at 8 takes. Wwise users run "Shuffle" with "Avoid repeating last 10" on 15 wood-footstep variations [S, Audiokinetic Q&A]. |
| No left/right foot, no heel/toe, no start, stop, turn, slide, wet, slope, vegetation. | `Footstep()` | One event type. |
| Cadence is a perfect metronome: `Stride += Speed * dt / (55 + 0.23 * Speed)`, a footfall each wrap. | `EphemeraPlayer.cpp` stride code | Constant stride length means constant interval at constant speed. Real strides vary a few percent. |
| Surface from hard thresholds on `Cover`: moss > 0.3, sand < -0.6, gravel < -0.2, else rock; `Ice` > 0.5 snow. | `GroundUnderfoot()` | Surface flips abruptly at borders; no sedge/grass class; no mixture. |
| All voices are `bAllowSpatialization = false`, no submix sends, no reverb. | `MakeVoice()` | Every step is perfectly dry whether on open moor, beside a cliff or in a lava tube. This is the "no depth" cue. |
| 12 voices in a ring. | `VoiceCount = 12` | A layered step system at 3.3 steps/s needs about 24 to 32. |

Stride arithmetic ([D], from the stride formula): at 0.8 m/s the step interval is 0.91 s (1.1 steps/s); at 1.5 m/s 0.60 s; at 3.0 m/s 0.41 s (2.4/s); at 4.8 m/s (`WalkSpeed`) 0.34 s (2.9/s, 174 steps a minute, a jog); at 8 m/s (`SprintSpeed`) 0.30 s (3.35/s).

Since the first read, `EphemeraAudio` gained an `Under` scene value (eye under water: per-voice low-pass sliding toward 520 Hz, the sea loop heard from beneath) and `SenseSea` plays a `splash` key that falls back to `step_water` [V, repo, 05:36 today]. The `splash` asset does not exist yet; candidates are in section 7.9. This brief builds on that rather than replacing it.

---

## 2. Findings: how AAA footstep and foley systems are built

### 2.1 Naughty Dog (The Last of Us Part II, Uncharted 4, The Last of Us)

Sources: Jennifer Walden, "How The Last of Us Part II's incredible sound was made", A Sound Effect, 30 June 2020 (https://www.asoundeffect.com/the-last-of-us-part-ii-sound/); Jeremy Rogers on Uncharted 4 (https://www.asoundeffect.com/uncharted-4-sound); Kenny Young's interview with Phil Kovats and Jonathan Lanier, "The Mix in The Last of Us", GANG, 2014 (https://www.audiogang.org/?p=2615); Jonathan Lanier, "Aural Immersion: Audio Technology in The Last of Us", GDC 2014 (https://www.gdcvault.com/play/1020444/Aural-Immersion-Audio-Technology-in); Kill Screen on The Last of Us (https://www.killscreen.com/how-sound-designers-last-us-built-quiet-armageddon/).

- Asset scale. Neil Uchitel: "Generally we would record 50 to 60 assets per locomotion tag"; up to 16 tags across standing, crouch and prone; about 30 initial selects, from which 8 to 15 discrete assets are implemented per tag. "In the end, 40 distinct surface types ended up in the game", each with on the order of 1,000 to 2,000 assets; Foley banks grew from 300 to 400 sound IDs on Uncharted 4 to 1,500 to 1,800 on Part II; "in many levels, more than half the in-game memory used is for Foley" [V, A Sound Effect]. Takeaway: even Naughty Dog implements only 8 to 15 takes per locomotion tag; the richness comes from tags, surfaces and layers, not from a huge pool per tag.
- Motion matching. "Anywhere between 1 to 6 animations are simultaneously playing on every game frame", so no single animation can own a sound; foley is driven by locomotion tags and gameplay state [V]. Our equivalent is the speed/intensity state, not an animation notify.
- Surfaces. "Surface probes are now multi-sampling" (broken glass sounds change with how much glass is under the foot); "we implemented surface blending and additives", so a blend of two surfaces can have a third surface on top (glass or moss over something else); feet and hands probe independently [V].
- Loops for analog movement. For prone crawling, Uchitel replaced intensity buckets with loops modulated by ADSR scripts and gameplay variables: "Using loops was a judicious way to get nearly infinite variation with a minimal memory footprint" [V]. Use this idea for our cloth and vegetation-brush layers.
- Water. Water is "depth-aware" for the first time in their games, with "six different depths of water footsteps", splashes for player, NPCs, horses and dogs; wet socks play after ankle-deep water until they visually dry out; blood puddles leave sticky steps that fade out [V]. Snow was recorded in fresh, icy and packed states [V, gamemusic.net write-up of the same interviews, https://gamemusic.net/?p=6073].
- Breathing. Naughty Dog built a detailed breathing system for both games and talked about it at GDC 2023 (Jesse Garcia, Beau Jimenez, "Breathing Life Into The Last of Us: Part I and II", https://gdcvault.com/play/1028973/Breathing-Life-Into-The-Last) [V, abstract only].
- Space. Rogers (Uncharted 4): "I think the biggest advance came from switching to Impulse Response Reverbs ... No more baked gun tails that don't fit the environment! ... the audio is happening in the actual environment on the fly" [V]. Part II shares its impulse responses with the cinematic mixers (Formosa) so gameplay and cutscene reverbs match [V]. Sound designer Justin Mullens: "Reverbs played a big role in further supporting these designed contrasts of dense versus open or active versus dead" [V]. The Last of Us (2013) added reverb-portal propagation on top of existing occlusion, obstruction and reverb environments so you can "tell if someone is in the next room", and a "high SPL hear-through" flag so a fence muffles a voice but not a gunshot [V, GANG].
- Mix. Rob Krekel: "We shoot for a very high dynamic range mix" [V]. Lanier on The Last of Us: HDR audio (make quiet sounds audible) was rejected because "things are scarier when you're straining to hear them!"; instead "Parametric Dynamic Range" ties range to tension and mood. Kovats: the world is "naturally quiet", silence "a powerful narrative tool"; the mix is built from the first placed sound, and the final mix of a 16-hour game took about 20 hours [V]. The footsteps themselves were recorded raw, "no processing on the incoming signal" (Kill Screen), including in real puddles [V].

### 2.2 Sucker Punch (Ghost of Yotei, Ghost of Tsushima), the closest published description of a modern footstep system

Sources: Jennifer Walden, "Crafting Captivating Game Audio for 'Ghost of Yotei'", A Sound Effect, 6 Nov 2025 (https://asoundeffect.com/ghost-of-yotei-game-audio/); Walden on Ghost of Tsushima, 2020 (https://www.asoundeffect.com/ghost-of-tsushima-sound/).

- Foley architecture (Josh Lord): "Each costume and character in the game uses a set of four core categories: gear, cloth, body, and surface", each in hard, medium and light variants, assembled by "combin[ing] the precision of linear editing with the flexibility of a modular system"; runtime control of pitch, volume, EQ, fades and asset seeking [V]. Brad Meyer: simple, "nine non-hand/foot sounds per character" [V].
- Footsteps (Chris Walasek): "a system that functioned on speed, blending the footstep types based on the velocity of the character"; an idle footstep type "more focused on the texture of the surface" than the heel thump; a separate toe-lift sound with its own walk/jog/run variations, blended in with a delay; and new types: scuffs, plants, turns, drags, slides, settles; an extra layer that can be switched on during combat [V].
- Surfaces (Adam Lidbetter): a script "blends current and previous surfaces together" when crossing between them (dirt fading out over a second or so as stone fades in); the surface comes from a ray cast down from the sky onto whatever collision it hits [V].
- Spatialisation: foley goes "through a 3D passthrough buss" and is not HRTF-spatialised, because "HRTF processing can have a negative effect on the fidelity of sound ... Low end is an area that really suffers" [V].
- Mix: strict loudness rules on every asset so dynamics processing could be tuned by script; "we were clocking around -18LKFS after 6-8 hours of gameplay" (mostly combat and UI, which Meyer called "exceptionally loud"); dynamic EQ on SFX and music buses to carve space for dialogue; in dense combat "there's no need for the quietest foley sounds, or subtle tree creaks to play", so voice limits were lowered at runtime [V].
- Tsushima: wind adapts to terrain ("tall grass rustling on your one side and clacking bamboo on the other"); snow was researched as a sound absorber and given subtle EQ; during infiltration "we bring down the Foley and ambience a bit so the player can feel the tension"; mix states "constantly balance" SFX, music and dialogue; in exploration "the wildlife and ambience is loudest in the mix" [V].

### 2.3 Remedy (Alan Wake 2)

Sources: A Sound Effect, Alan Wake 2 audio interview (https://asoundeffect.com/alan-wake-2-game-audio); Audiokinetic, "Inside the Mind of Alan Wake 2: The Role of Audio QA" (https://www.audiokinetic.com/blog/alan-wake-2-audio-qa, page returned 403; content from search snippet [S]); Alan Wake fan site index of Remedy's series (https://www.alanwake.info/2025/04/21st-april-2025-audiokinetic-explores.html).

- Quiet is built from detail: "there aren't any truly silent moments at all"; the team wanted the environment quiet enough to "hear a pin drop", and named the detail of Saga's footsteps and breathing as part of that; rain wets the footsteps and the character's clothing foley [V].
- Reverb: Gunnarsson and Schiesari needed "semi-realistic reverbs" because "if nothing feels real then nothing really matters, and the player would detach"; Petri Alanko kept sources "acoustic and dry as possible" [V]. Takeaway: a believable but not exaggerated space.
- Mix: moving between rain and no rain changes the noise floor so dialogue, foley, enemy sounds and reverbs "sit differently in the mix", which needed "a fair amount of tweaking" [V]. For us: wind level is our noise floor; footstep level must be authored against the wind's level states.
- Water: a QA bug had footsteps not triggering in a large flooded space, because water is a major surface in the game [S]. A reminder to test every depth class, and to make water a first-class surface, not an afterthought.
- Foley is a stated priority: "foley and breathing ... bring players closer to the characters" (Gunnarsson, Remedy "Sound of Fear" video) [S].

### 2.4 Other published footstep architectures (what the middleware people and GDC say)

- Insomniac / Obsidian, GDC 2015 "All Style, All Substance" (Kristen Quebe, Bryan Higa), Sunset Overdrive: a footstep system with nine behaviours across many materials and six shoe types, data-driven so asset count did not explode the workload [V, abstract; talk is members-only] (https://gdcvault.com/play/1021783/All-Style-All-Substance-The).
- CRI (ADX2 / AtomCraft) "Advanced Footsteps" (https://blog.criware.com/?p=3604) [V]: samples split into heel and toe sets, each its own random-no-repeat cue, played together in a polyphonic cue with random volume and pitch ranges; a speed parameter shortens the heel-to-toe delay and raises heel volume and low end as speed rises ("at top speed ... almost no delay between Heel and Toe"); water is a separate cue layered over the surface (not a surface), its volume driven by its own parameter so wetness tracks how much water the character walks in.
- Krotos, "How to record and mix usable footsteps sound effects" (https://krotos.studio/blog/footsteps-sound-effects) [V]: think of each step as attack (heel or toe impact), body (mid-range weight), space (room tone or reflections); align transients between layers; check in mono; "small changes in level and ambience are often more convincing than heavy processing"; close shots want minimal reverb, wide shots lower direct level and more ambience.
- Audiokinetic, "Footsteps Material Management" (https://blog.audiokinetic.com/footsteps-material-management-using-wwise-unreal-engine-4-unity-3d/): a Switch Container per material holding a Random Container per surface, with a default switch [S; page returned 403, from search snippet].
- Damian Kastbauer, "Footsteps: Informal Game Sound Study", Lost Chocolate Lab / Game Developer, 30 March 2010 (https://www.gamedeveloper.com/audio/footsteps-informal-game-sound-study) [V, teaser only]: footsteps are "heard for countless hours across every level", so repetition is the main enemy.
- Opinion worth keeping in mind: over-engineered layering can itself distract ("The Overengineered Footstep", republished at https://001.arktimes.com/?p=19989) [S, page timed out]. Hence the rule in section 6.1: every layer must earn its place by being audible in the mix.

### 2.5 Gait facts the design leans on

- Loading response (initial contact to foot flat) is the first 0 to 10% of the gait cycle (Boston O&P clinical gait terminology, https://www.bostonoandp.com/Customer-Content/www/CMS/files/GaitTerminology.pdf) [S, snippet]. A gait cycle is two steps, so heel-to-flat is 10% of 2/cadence: 118 ms at 1.7 steps/s (a slow walk), 83 ms at 2.4/s, 67 ms at 3.0/s [D]. Running with a mid- or forefoot strike removes most of the gap. This is why the heel/roll offset ranges in section 6.3 shrink from about 120 ms to zero as speed rises. (CRI's own numbers, 400 to 600 ms at slow speed, belong to their sample structure, not to anatomy; do not copy them.)
- Stance is about 60% of the cycle, swing 40% [S]. Not needed for triggers, but it tells you toe-off (the "toe lift" sound Sucker Punch adds) comes roughly half a step interval after the heel strike at a walk.

---

## 3. Sound in a space: distance, reverb, occlusion and what UE 5.8 can do from C++

### 3.1 What actually places a footstep in an environment

A first-person footstep is a near-field, ground-coupled source about 1.6 m below the ears. The direct sound and the ground bounce are essentially one event, which is why close-miked foley sits well "dry". What the space adds, in order of how audible it is:

1. The direct-to-reverberant ratio and the tail's length and colour. Open moor and snow: almost none. Forest or scrub: short, dark. Valley with cliffs: long, with discrete echoes. Lava tube or cave: dense, long, bright. Carnes (Production Expert): brick is "slappy", jagged cliffs "slappy, but less so", trees "surprisingly diffuse", leaf litter and underbrush take away still more highs; "in an exterior [first reflections] might take longer" than the first 100 to 150 ms of a hall [V] (https://www.production-expert.com/production-expert-1/creating-believable-outdoor-reverbs-in-post). A box canyon has a long tail, a forest a short one.
2. Discrete slap-back from the nearest large wall. Sound covers 1 m in 2.9 ms (343 m/s), so an echo off a wall of distance d returns after 2d/343 s [D]: 20 m = 117 ms, 50 m = 292 ms, 100 m = 583 ms, 200 m = 1.2 s. (Carnes gives "a foot is a bit less than a millisecond"; his "a meter is about four" is loose, 2.9 is right.) On a quiet moor beside a basalt escarpment, a faint echo of your own gravel steps is exactly the "vast" cue.
3. High-frequency damping by the surface (snow absorbs; Sucker Punch researched it and EQ'd the northern snow area [V]).
4. Air absorption. For distant sounds only. Using the 10 C / 70% RH table in the windPRO manual [S] (3.7 / 9.7 / 32.8 / 117 dB per km at 1 / 2 / 4 / 8 kHz): per 100 m that is about 0.4 / 1.0 / 3.3 / 11.7 dB. So 4 to 8 kHz roll-off becomes audible from about 100 m. It is irrelevant for your own feet (<2 m) and matters for rockfalls, creature calls, a distant stream. Use it where it applies: world sounds, not the player's body.
5. Medium. Through water the picture changes completely (section 5).

### 3.2 What the references did

- Uncharted 4: runtime impulse responses instead of baked tails [V] (section 2.1).
- The Last of Us: reverb portals, occlusion and obstruction; a wide-band filter on the reflected path so a muffled voice stays intelligible [V, GANG].
- Alan Wake 2: "semi-realistic" reverbs, with the dry sources kept dry [V].
- Ghost of Yotei: sound classes routed through a 3D passthrough bus for player foley, with environmental processing applied elsewhere [V].
- Audiokinetic's literature on rooms and portals ("Reverb Needs Spatialization Too", https://www.audiokinetic.com/fr/blog/rooms-and-portals-with-wwise-spatial-audio [S]) says the same thing in middleware terms: late reverb is an aux send; spatialising it through rooms and portals is the extra step. For a mostly outdoor planet with few rooms, the aux-send level driven by the surroundings is the useful 80%.

### 3.3 The UE 5.8 toolbox, with what is verified

| Feature | What it is | Verified | C++ reachable without editor graph-authoring? |
|---|---|---|---|
| Sound Attenuation, air absorption | `bAttenuateWithLPF`, `LPFRadiusMin/Max`, `LPFFrequencyAtMin/Max`, `HPFFrequencyAtMin/Max`, `AbsorptionMethod`, custom curves [V, `FSoundAttenuationSettings`] | [V] | Yes. Build an `FSoundAttenuationSettings` in code or a transient `USoundAttenuation`; `UAudioComponent` can take overrides [K]. |
| Occlusion | `bEnableOcclusion`, `OcclusionLowPassFilterFrequency`, `OcclusionVolumeAttenuation`, `OcclusionInterpolationTime`, `bUseComplexCollisionForOcclusion`, `OcclusionTraceChannel` [V]. One line trace per source. | [V] | Yes. Needed only for world sources (rockfall, creatures), not the player's own body. |
| Reverb send by distance | `bEnableReverbSend`, `ReverbSendMethod` (Linear, Custom Curve, Manual), min/max wet level and distance; Manual "doesn't change relative to distance", suits ambient and non-spatial sounds [V] | [V] | Yes. |
| Submix sends | Per-sound `bEnableSubmixSends` / `SubmixSendSettings` on the attenuation [V]. Per-component `UAudioComponent::SetSubmixSend(USoundSubmixBase*, float)` | attenuation [V]; component call [K] | Yes. This is the control point for the player's 2D voices. |
| Master Reverb Submix | A Sound Class can "Send to Master Reverb Submix" with a "Default 2DReverb Send Amount" [V, submix overview] | [V] | Yes (asset setting; could also be set at startup). |
| Submix effect chains | A submix is "a DSP graph that runs continuously"; effects via the Synthesis and DSP Effects plugin; presets in Audio > Effects > Submix Effect Preset; `SetSubmixEffectChainOverride(World, Submix, TArray<USoundEffectSubmixPreset*>, FadeTimeSec)` cross-fades to a new chain; `ClearSubmixEffectChainOverride` reverts [V] | [V] (signature from Epic's page for `UAudioMixerBlueprintLibrary`, header `AudioMixerBlueprintLibrary.h`) | Yes. The right tool for underwater and thin-air/vacuum states. |
| Convolution reverb | Synthesis and DSP Effects plugin. Any imported sound wave becomes an IR via "Create Impulse Response" (`_IR` asset), per-IR "Normalization Volume Db" and "True Stereo"; `SubmixEffectConvolutionReverbPreset`; "usually more expensive than traditional algorithmic reverbs", cost grows with IR size; Block Size and Hardware Acceleration advanced options [V, Epic's UE 5.8 page https://dev.epicgames.com/documentation/unreal-engine/convolution-reverb-in-unreal-engine] | [V] | IR assets are created once in the editor (right-click). Everything after that (which IR, how much send) is C++. Swapping an IR at run time: `SetImpulseResponse` existed in the 4.26 API [S]; prefer fixed IRs per submix and cross-fade the sends. |
| Algorithmic reverb | `USubmixEffectReverbPreset` (settings: decay, density, diffusion, gains, air absorption) | [K] | Yes. Good enough for the underwater and "thin-air" cases. |
| Audio Volumes | Reverb Effect with Priority, Volume, Fade Time; Ambient Zone interior/exterior volume and LPF with fade times [V] | [V] | Placed by hand in a level; not suited to a procedural planet. |
| Audio Modulation | Control buses, bus mixes, parameter patches, LFO/envelope generators; volume is additive, HPF takes the max, LPF takes the min; custom parameters by subclassing `USoundModulationParameter` [V] | [V] | Possible but unnecessary here. |
| MetaSounds | Builder API: "public Blueprint and C++ API to author MetaSounds procedurally", Beta; no variables; "use caution when shipping" [V]. Templates (footsteps are a named use case): experimental, signatures may change [V] | [V] | The builder can create sources in memory; templates are editor-first. Not needed for anything in this brief. |
| Binaural | `SpatializationAlgorithm` Panning or Binaural; binaural "requires playback over headphones" and an enabled plugin [V, UE 5.8 attenuation page] | [V] | Plugin choices exist (Steam Audio, Meta XR Audio, others) [S]. Not used for the player's own foley. |

### 3.4 Decisions

| Tool | Decision | Reason |
|---|---|---|
| Plain `UAudioComponent` voices with C++ selection (as now) | Keep, extend to ~28 voices | Fully controllable: gain, pitch, LPF, submix sends, delayed second voices. Already verified working in this repo. |
| Convolution reverb submixes | Adopt: 3 IR submixes always running (Open, Valley, Cave) plus one algorithmic reverb for forest/scrub and underwater | It is the Uncharted 4 technique [V]. Budget CPU on the Windows PC (RTX 3060 machine); trim IRs (open about 1 s, valley about 2.5 s, cave about 3 s). A fourth convolution (forest) only if the measurement allows. |
| Computed slap-back echo | Adopt | Cheap, strong "vastness" cue, no plugin. A second one-shot voice after 2d/343 s, low-passed, quieter. |
| Ray-probe environment classifier | Adopt | 16 horizontal rays at chest height plus 5 upward ones, one or two per frame, smoothed over 1.5 s. Feeds reverb sends, slap delay and absorption. |
| Submix effect-chain override for underwater / vacuum | Adopt | Bus-level LPF covers all sounds, not just the ones that remembered to ask. Replaces the per-voice 520 Hz trick in `DullNow()`, which cannot reach the reverb returns. |
| Attenuation air absorption + occlusion | Adopt for world sources only | Realistic, cheap, standard. |
| Steam Audio | Do not adopt now | Needs UE-side geometry export, replaces the engine's spatializer/occlusion/reverb plugins, its Unreal page says "does not ship with macOS binaries" and requires deleting the bundled plugin [V, https://valvesoftware.github.io/steam-audio/doc/unreal/getting-started.html]; dynamic-geometry support only arrived in v4.8.0 [S]; this planet is runtime-generated terrain plus runtime-grown ground cover. Risk is high for a benefit (reflections) that a ray probe and convolution IRs deliver at this game's scale. Revisit only for HRTF of world sources. |
| MetaSound graphs / templates | Do not adopt | Beta/experimental APIs [V]; nothing here needs sample-accurate graph logic; layer offsets of 15 to 120 ms are fine at frame granularity (a 16 ms jitter on a fused heel-roll pair is not heard as a flam jitter). |
| Audio Modulation | Do not adopt | Our global states (underwater, stillness, thin air) are few and C++-driven. |
| Quartz | Not relevant | It quantises to a musical clock; footsteps lock to the stride, not to a beat. |

### 3.5 The concrete space design

Environment probe (`UEphemeraPlayerAudio` or a small `FEphemeraAcoustics` helper, run from the player's tick, 2 rays a frame, full refresh about 0.25 s):

```
Inputs per refresh (rays on the collision channel the player already traces on):
  16 horizontal rays at chest height, length 150 m      -> HitDist[i], HitNormal[i], HitSurface[i]
  4 rays up at 45 degrees (80 m) + 1 straight up (60 m) -> SkyOpen = fraction that hit nothing
Derived:
  Enclosure   = fraction of horizontal hits within 60 m              (0 open ... 1 boxed in)
  MeanFree    = mean(min(HitDist, 150 m))
  WallM       = nearest hit with a steep normal (> 60 degrees from up) beyond 8 m
  SlapSeconds = clamp(2 * WallM / 343, 0.04, 0.9)   (skip if no wall within 150 m)
  Damping     = from the ground and hits: snow/moss/foliage -> high, bare basalt -> low
  Class weights (sum to 1), smoothed with a 1.5 s time constant (reverb must never be heard to "change"):
     Open   = (1 - Enclosure) * SkyOpen
     Valley = Enclosure_far (hits 30..150 m) * SkyOpen
     Forest = Enclosure_near (hits < 30 m) * SkyOpen         (scrub, birch, gully)
     Cave   = (1 - SkyOpen) * Enclosure
```

Sends, starting values [D] (wet level relative to the dry voice):

| Class | IR source (real, section 7.8) | Tail | Wet send | Notes |
|---|---|---|---|---|
| Open moor / plain / snowfield | `AK47_big_open_area_...` tail | about 1 s, thin | -30 dB | The tail is nearly absent: openness is the lack of reverb plus distant ambience, as in Production Expert's field/forest comparison [V]. |
| Valley / cliffs / basalt walls | `AK5_valley_field_forest_50m_...` tail | 2 to 3 s with echoes | -18 dB, plus computed slap | The "vast" cue. |
| Forest / scrub / gully | Algorithmic reverb preset (short, dark) in the first pass; `MP5_forest_road_10m_...` tail as the IR if a fourth convolution fits the CPU budget | 0.4 to 0.8 s, dark | -24 dB | High cut at 3 to 5 kHz (foliage takes the highs [V]). |
| Cave / lava tube | `AK47_catacombs_...` (or the long corridor shot for a longer tube) | 3 s, bright | -10 dB | Dense early reflections; the highest wet level of the set. |

Each IR is made offline (section 6.6): cut the blank shot's tail from about 15 ms after the muzzle blast, high-pass at 80 Hz, fade the head in over 2 ms, normalise, trim to length, import, "Create Impulse Response". Real recordings of real places; no synthesis.

Slap-back echo: for impulsive layers only (heel, landing, debris), schedule a duplicate of the same take after `SlapSeconds`, gain `-20 dB - 6 * log2(WallM / 30)` (clamped), LPF at 3 kHz. Skip below 40 ms (it would just colour the step).

Player voices use `SetSubmixSend` to the four reverb submixes (class weight times the wet table) and nothing else. Their dry output stays 2D.

World sounds (rockfall, creatures, distant streams) use a shared attenuation asset: Natural Sound falloff, air absorption LPF from 20 kHz at 3 m to about 3.5 kHz at 600 m with log frequency scaling, reverb send ramped by distance, occlusion on with a 900 Hz cut and -6 dB, 0.25 s interpolation [D, from the property list in section 3.3].

Thin air / vacuum: keep the existing `Through()` and `DullHz()`; move them to a submix effect-chain override for the "steps heard through the suit" path (a Filter at 380 Hz) and remove per-voice filters from the new layered steps so all layers agree.

---

## 4. Mixing for depth

### 4.1 Dynamic range of the steps themselves [D]

Natural footstep levels differ by well over 20 dB between a careful creep and a hard landing. We currently deliver 7. Target: about 24 dB from a creep to a sprint landing, achieved by choosing different recordings per tier (so spectrum changes with force: more low thump and more HF snap as force rises) and then applying gain.

| Event | Peak at the surface layer before the bus, dBFS [D] | Relative to a walk step |
|---|---|---|
| Creep step | -34 | -10 dB |
| Walk step | -24 | 0 |
| Jog step | -20 | +4 dB |
| Run step | -16 | +8 dB |
| Sprint step | -14 | +10 dB |
| Soft landing | -18 | +6 dB |
| Hard landing | -8 (all layers summed) | +16 dB |

Per-step random gain +-1.5 dB. Do not renormalise individual takes to one RMS: normalise per tier pool to its tier reference and keep each take's natural +-2 dB (Sucker Punch's pipeline did the opposite of this: "strict loudness rules on assets" so dynamics could be tuned by script [V], which is a rule per tier, not per take).

### 4.2 Loudness targets

- Whole game: -24 LUFS integrated (+-2 LU) is the PlayStation console target, measured for at least 30 minutes of representative play, with a max true peak -1 dBTP and loudness range up to 20 LU (Audiokinetic, "Mastering a Game with Wwise, Part 1" https://audiokinetic.com/en/mastering-a-game-with-wwise-part1; page returned 403, figures from the search snippet [S]). Sucker Punch measured -18 LKFS after 6 to 8 hours in a combat game [V]; this game is a quiet exploration game, so aim at the console number, with a wide loudness range, and treat the lower end as a feature.
- Bus relationships, starting points [D], short-term loudness while walking on open ground in light wind: world/wind bed -38 to -32 LUFS-S; footsteps and foley -34 to -28; music -34 to -26. The wind is the noise floor: set it first, then author steps so a jog sits 6 to 10 dB above the bed and a creep just clears it. Remedy hit exactly this when rain changed the noise floor and "everything sits differently in the mix" [V].
- Measure, do not guess: a small offline script using BS.1770 loudness (for example the `pyloudnorm` package, run with `uv`) over a recorded 5-minute walk capture from the PC build.

### 4.3 Ducking and mix states

- Light bus-level sidechain from the player foley submix onto the world/wind submix: -2 dB, attack 20 ms, release 700 ms (Epic's Dynamics Processor submix effect accepts a submix or audio bus as key input [S]). Subtle on purpose.
- Mix states, driven from the player (all C++): Stillness (speed < 0.3 m/s for 5 s: world +2 dB over 3 s, steps off, breath recovery fades), Exertion (sprint/jets: breath up 6 dB, cloth up), Underwater (section 5), Thin air/vacuum (existing). Sucker Punch uses mix states to bring down foley and ambience for tension and to let ambience be loudest in exploration [V].
- Do not follow Naughty Dog's tension-driven range literally; borrow the principle: range follows mood, quiet moments are allowed to be audibly quiet [V].

### 4.4 Keeping footsteps from repeating over hours

What the references do, and the matching rule here:

| Reference practice | Rule in this design |
|---|---|
| 8 to 15 takes per tag, many tags, many surfaces, banks of 1,500+ IDs [V] | 5 intensity tiers x 7 surfaces; 8 to 14 takes per tier per layer (section 6.2). |
| Layers from separate pools: gear, cloth, body, surface [V] | Five independent pools per step (heel, roll, debris, cloth, gear), each with its own random gain, pitch, timing. 10 heels x 8 rolls x 12 debris x 8 cloth x 10 gear is over 76,000 distinct steps before any parameter jitter. |
| Loops for analog movement [V] | Cloth, backpack and vegetation-brush are looped beds with gain/pitch/filter following speed, not one-shots per step. |
| Hundreds of events in play at once, culling the quietest [V] | Layers whose computed gain is below -48 dB are not started; at high voice use, drop cloth/gear first. |
| Wwise "Shuffle" with "Avoid repeating last N" [S] | Per-pool shuffle bag, N = min(4, floor(size / 2)); the same source step is never used for heel and roll in one event. |
| Rare, noticeable one-off events | Kicked pebble (1 step in 35 on gravel/basalt), dry-stem snap (1 in 70 on sedge), crust break (1 in 25 on snow), a rolling stone down a slope, none repeating inside 8 steps. |
| Context splits the pool so the same sample is not the answer to every question | Pools keyed by surface, tier, foot parity, slope sign, wetness class. |
| Speed-based blending between tiers [V] | Choose between neighbouring tiers stochastically from the continuous intensity so pool changes are never heard as a switch. |

---

## 5. Water, wading and underwater

References: Naughty Dog's six depth classes, wet-sock persistence, sticky blood steps [V]; CRI's water-as-layer with its own parameter [V]; Remedy's flooded-space footstep bug as a test case [S]; Epic's submix effect-chain override for global state changes [V].

### 5.1 Depth classes and layers

| Class | Depth at the foot | Layers [D] |
|---|---|---|
| 0 Wet ground | wetness only, no standing water | surface step with wet damping (LPF -2 kHz), quiet squelch layer at probability = wetness |
| 1 Film / puddle | < 5 cm | surface step + puddle splash single (`0014_Footsteps_water_puddle_single_splashes`), droplet at toe-off |
| 2 Ankle | 5 to 15 cm | splash step pool replaces the heel, surface step at -8 dB underneath, slosh sweetener |
| 3 Shin / knee | 15 to 60 cm | wading step pool (slower, heavier, longer), continuous "wash" loop gain = speed x depth, drag-out drips |
| 4 Thigh / waist | 60 to 110 cm | slow slosh pool, wash loop louder, steps almost inaudible, buoyancy swell |
| 5 Swim | > about 1.1 m (player's `SeaFullDepthCm`) | existing swim logic; stroke/splash layer, bubbles on exhalation |

Wetness w (0 to 1) rises with time in water or on saturated ground and decays over about 40 s after leaving, as Naughty Dog's wet socks persist "until they visually dry out" [V]; while w > 0.15 add the wet layer with probability w, and darken surface layers.

### 5.2 Water entry

Three parts, sized by the entry speed (the repo already derives `Into`):
1. Impact slap/plop at the moment of contact: small / medium / large pool (section 7.9), pitch down with size.
2. Air-to-water bubble burst, 30 to 120 ms later, fades in over 0.3 s, then a muffled low "thump" when the head goes under.
3. When the eye goes under (`bEyeUnder`), apply the underwater state with a 0.15 to 0.3 s cross-fade; on surfacing, a drip/pour layer and a breath.

### 5.3 Underwater state

- Physics: sound crosses an air-water boundary with about 30 dB loss because of the impedance mismatch (water/air impedance ratio near 3,600, about 0.1% of the energy transmitted) [S, arXiv 2208.12999 https://arxiv.org/pdf/2208.12999; see also DOSITS, "How does sound propagate from air into water?" https://dosits.org/science/movement/how-does-sound-propagate-from-air-into-water/, page returned 403]. So: everything airborne arrives quiet and low-passed.
- Implementation [V mechanism]: a `SetSubmixEffectChainOverride` on the world/effects submix with a low-pass at about 500 to 700 Hz and -12 dB for airborne sources, fade 0.2 s, cleared on surfacing. The existing 520 Hz per-voice value in `DullNow()` is a good number; moving it to the bus makes it cover the reverb returns and the new layers.
- Do not muffle what the body hears from inside the suit: breath, suit hum, jets through the suit. In `Play()` terms those are the `bThroughAir = false` sounds.
- Underwater beds and movement: real hydrophone and scuba recordings (section 7.9) rather than any synthesised bed.
- Reverb underwater: switch off the outdoor IR sends; use the algorithmic reverb with a long dark tail (decay 2 to 3 s, high cut 1.2 kHz, wet -20 dB) for the closed, ringing quality.
- Test case straight from Remedy's QA bug: walk, wade, swim, and exit through every depth class; a footstep must sound at every depth.

---

## 6. Design for Ephemera

### 6.1 Layers per event

Rule: a layer exists only if it is audible in the mix at its intended level; otherwise do not start it (voice and CPU saving, and it avoids the "over-engineered footstep" trap [S]).

| Event | Layers (all with independent pitch/gain/time jitter) |
|---|---|
| Step (any tier) | L1 Impact (heel/ball contact on the surface, stereo, tier- and foot-specific). L2 Roll/toe (surface "release", delayed 0 to 120 ms by tier). L3 Debris sweetener (grit, pebble, twig, crust; probability by surface and tier). L4 Cloth swish (tier; probability 0.6 to 1). L5 Gear tick (suit hardware, clink, strap; probability by tier). L6 Vegetation brush (a looped bed, not per step; see below). L7 Wet layer (by wetness/depth). L8 Slap-back duplicate of L1 (by environment). |
| Start from rest | cloth weight-shift (soft), first step at -4 dB, no debris. |
| Settle (stop after striding) | 1 to 2 soft "feet-together" steps at +120 ms and +240 ms, -10 dB, cloth settle, breath if exertion is high. |
| Turn pivot | surface scuff + cloth, when yaw rate > 120 degrees/s with speed < 1.5 m/s, rate-limited to one per 0.6 s, gain by yaw rate. |
| Skid | sprint to stop in < 0.25 s: surface drag + debris burst. |
| Slope slide / scree | looped scree bed when sliding or when slope > 30 degrees and moving, gain by speed; downhill steps +2 dB heel, +20% debris probability. |
| Take-off (jump) | L1 push-off at -3 dB (run tier), cloth swish at +20 ms, gear tick, breath if exertion > 0.4. Replaces the single generic "jump" + `Footstep(0.7)`. |
| Landing | Soft (fall speed 1.2 to 3 m/s): one step-like impact + cloth. Medium (3 to 6): two feet 25 to 45 ms apart + gear thump + debris puff. Hard (> 6): plus low body-weight layer + breath + crouch cloth + low-passed sub thump. |
| Wading / splash | section 5. |
| Breath | not per step: a state machine over an exertion value (rises with sprint, jets, jumps; falls over 8 to 12 s); four states (calm, working, heavy, recovery) as looped or long one-shots at low level, in the helmet/suit ("inside"), so not high-passed by distance. |
| Vegetation brush | looped beds (soft grass/sedge, harder reeds/stalks) with gain = density x smoothstep(1.5, 5 m/s) x (1 - snow) and pitch/filter following speed; add per-step "swish" one-shots at foot swing when density > 0.4. Density from the actual plants near the legs (6.4). |

### 6.2 Takes per layer per tier

Counts are what to cut and ship. The first column of each is realistic from the walks already in the bundles; the sand column is limited by the gap in section 8. Tiers are speed bands in m/s (6.3). Heel and roll are cut from the same recorded steps but recombined, so they multiply.

| Layer | Creep | Walk | Jog | Run | Sprint | Notes |
|---|---|---|---|---|---|---|
| L1 Impact, per surface | 6 | 10 | 14 | 12 | 8 | x7 surfaces (moss, sedge/grass, ash/sand, gravel/scree, basalt, snow, shallow water). Jog gets the most: it is where the player lives (4.8 m/s). Naughty Dog implements 8 to 15 per tag [V]. |
| L2 Roll/toe, per surface class | 6 | 8 | 8 | 6 | 4 | 3 classes (soft ground, hard grit, snow). |
| L3 Debris, per surface class | 6 | 10 | 12 | 12 | 12 | Hard grit (gravel, basalt, ash) 12; snow crust 8; moss/sedge twig/stem 6. |
| L4 Cloth | 8 | 8 | 8 | 8 | 8 | Light / medium / hard sets across tiers; shared by surface. Plus 2 loops (walk, jog) for beds. |
| L5 Gear tick | 6 | 10 | 10 | 10 | 10 | Shared, plus 1 backpack-bounce loop for jog/run. |
| L6 Vegetation | 2 loops (soft, hard) + 10 swish one-shots per vegetation type | | | | | Sedge/grass; moss gets none. |
| L7 Wet | 8 splashes per depth class 1 to 3; 2 wash loops | | | | | |
| Start / settle / turn / skid | 6 each per surface class | | | | | 3 surface classes. |
| Take-off | 6 | | | | | Cloth + push-off. |
| Landing | soft 6, medium 6, hard 6 per surface class | | | | | 4 classes (soft ground, hard grit, snow, water). |
| Breath | 3 per state | | | | | |

Rough total if every cell is filled: about 900 short assets (impacts 350, roll 96, debris 156, cloth 40, gear 46, vegetation 22, wet 26, start/settle/turn/skid 72, take-off 6, landing 72, breath 12). At 0.4 s average, 48 kHz stereo 16-bit, that is about 70 MB before compression and perhaps 10 to 15 MB compressed [D]. Naughty Dog spends "more than half the in-game memory" on foley [V]; this is small by comparison. The first pass will not fill every cell (sand especially, section 8); fill the jog and run tiers first.

### 6.3 Tiers, selection rules and parameter ranges

Tiers in m/s along the ground (`Speed / 100`). Use a 0.2 m/s hysteresis and treat the continuous intensity I = smoothstep(0.8, 8.0, speed) as the real input: pick the neighbouring tier with probability equal to the fractional position between tier centres, so pools cross-fade statistically.

| | Creep | Walk | Jog | Run | Sprint |
|---|---|---|---|---|---|
| Speed (m/s) | 0.8 to 1.5 | 1.5 to 3.0 | 3.0 to 5.2 | 5.2 to 6.8 | above 6.8 |
| Steps per second (from the stride formula) | 1.1 to 1.7 | 1.7 to 2.4 | 2.4 to 3.0 | 3.0 to 3.2 | 3.2 to 3.4 |
| Level relative to walk (dB) | -10 | 0 | +4 | +8 | +10 |
| Roll delay after impact (ms) | 100 to 130 | 70 to 110 | 40 to 70 | 15 to 40 | 0 to 15 |
| Cloth layer relative to L1 (dB) | -16 | -12 | -9 | -7 | -6 |
| Gear tick probability | 0.2 | 0.35 | 0.5 | 0.7 | 0.8 |
| Debris probability (hard grit surfaces) | 0.25 | 0.40 | 0.50 | 0.65 | 0.75 |
| Impact pitch jitter (+-%) | 3 | 3 | 3 | 3 | 2.5 |

Common rules [D]:
- Gain jitter +-1.5 dB on impacts, +-2.5 dB on cloth, gear, debris. Pitch +-3% (never beyond +-6%: pitch also changes duration and timbre and is the first thing the ear flags as fake). Per-layer timing jitter +-6 ms; stride length jitter +-3% (applied to the camera bob too so sound and visible footfall stay locked).
- Foot: alternate Left/Right from `Strides % 2`; cut the steps from recordings in order and tag them by parity at cut time so each foot keeps its natural character; the right foot +0.5 dB and a bias of 0.10 toward its side baked into the asset at build time (stereo balance of +-1 dB; no runtime pan needed).
- Selection: per-pool shuffle bag with last-N avoidance (6.4); heel and roll from different source steps; debris never twice in a row from the same take; Left/Right pools separate.
- Anti-flam: for run and sprint, where the roll delay is below 12 ms, ship impact and roll as one asset and skip the separate L2 voice.
- Culling: layers computed below -48 dB are not started; under voice pressure drop gear, then cloth, then debris, never the impact.
- LPF: surface wetness lowers the surface cut-off by up to 2 kHz; snow and moss sit around 6 to 8 kHz; hard grit stays open.

### 6.4 Runtime logic in C++ terms

What follows is a sketch: names that already exist in the repo (`Strides`, `Along`, `AirNow`, `SeaDepthCm`, `LandedAt`, `SenseSea`, `KeyOf`, `Pick`, `PlayAt`, `Follow`) are used as they are; everything else is a proposal.

New data (header `EphemeraAudio.h`):

```cpp
enum class EEphemeraFoot : uint8 { Left, Right };
enum class EEphemeraGait : uint8 { Creep, Walk, Jog, Run, Sprint };
enum class EEphemeraStepKind : uint8 { Step, Start, Settle, Turn, Skid, Takeoff, Land };
enum class EEphemeraSurface : uint8 { Moss, Sedge, Ash, Gravel, Basalt, Snow, Water, Count };

struct FEphemeraStep
{
	EEphemeraStepKind Kind = EEphemeraStepKind::Step;
	EEphemeraFoot Foot = EEphemeraFoot::Left;      // Strides % 2
	float SpeedMS = 0.f;                           // along the ground
	float Intensity = 0.f;                         // smoothstep(0.8, 8, SpeedMS)
	EEphemeraGait Gait = EEphemeraGait::Walk;      // with hysteresis
	float FallMS = 0.f;                            // landing speed
	float SlopeDeg = 0.f;                          // signed: + uphill along travel
	float YawRateDegS = 0.f;
	float Wetness = 0.f;                           // 0..1
	float WaterDepthM = 0.f;
	float Vegetation = 0.f;                        // 0..1, plants at the legs
	float Exertion = 0.f;                          // 0..1 (breath, cloth level)
	float Mix[(int32)EEphemeraSurface::Count] = {};// smoothed weights, sum to 1
	float AirAmount = 1.f;                         // existing Air
};

struct FEphemeraSpace                              // refreshed ~4 Hz by the probe
{
	float Open = 1.f, Valley = 0.f, Forest = 0.f, Cave = 0.f;   // class weights, sum 1
	float WallM = 0.f;                             // 0 = none within 150 m
	float SlapSeconds = 0.f;
	float Damping = 0.f;                           // 0 bare rock .. 1 snow/moss/foliage
	float Under = 0.f;                             // existing
};
```

Entry point: `void UEphemeraPlayerAudio::PlayStep(const FEphemeraStep& Step, const FEphemeraSpace& Space)`. `AEphemeraPlayer::Footstep(float Loudness)` becomes a builder:

```cpp
// In AEphemeraPlayer, replaces Footstep(float):
FEphemeraStep AEphemeraPlayer::MakeStep(EEphemeraStepKind Kind) const
{
	FEphemeraStep S;
	S.Kind = Kind;
	S.Foot = (Strides & 1) ? EEphemeraFoot::Right : EEphemeraFoot::Left;
	S.SpeedMS = Along.Size() / 100.0;                       // as already computed in the stride code
	S.Intensity = FMath::SmoothStep(0.8f, 8.f, S.SpeedMS);
	S.Gait = GaitWithHysteresis(S.SpeedMS);                 // member, keeps last tier
	SampleSurfaceMix(S.Mix);                                // 6.5: replaces GroundUnderfoot's hard thresholds
	S.SlopeDeg = SlopeAlongTravelDeg();                     // FEphemeraTerrain::Normal, dot with travel dir
	S.Wetness = Wetness;                                    // integrator: up in/near water, decay 40 s
	S.WaterDepthM = SeaDepthCm / 100.f;                     // SenseSea; add streams via FlowAsShaded
	S.Vegetation = GroundCover ? GroundCover->PlantsNear(GetFeetLocation(), 80.f) : 0.f;   // 6.4 note
	S.Exertion = Exertion;                                  // integrator
	S.AirAmount = AirNow;
	return S;
}
```

`PlayStep` in order:

```
1. Choose the surface: smoothed weights W[s]. primary = sample one by weight; if the runner-up weight
   > 0.25 play it too at gain runnerUp / (primary + runnerUp) (the "blend"), and apply additives
   (snow over gravel, water film over anything with Wetness).
2. Choose the tier pool from I (stochastic neighbour crossfade).
3. Impact: pool = (surface, tier, foot); bag pick; gain = tierLevelDb + jitter + slopeAdjust; pitch jitter.
   LPF from Wetness/Damping/Air.
4. Roll: pool = (surfaceClass, tier); bag pick from a different source step; delay = tier range
   (+/- slope: uphill -15 ms); schedule in a small timeline queue (below). Skip if delay < 12 ms.
5. Debris: probability by surface weights and tier; pool bag; delay 0..30 ms.
6. Cloth: probability and level by tier and Exertion; delay -20..+40 ms.
7. Gear: probability by tier.
8. Wet: if Wetness > 0.15 or WaterDepth > 0: depth class pool per section 5.
9. Slap: if Space.SlapSeconds > 0.04 and the layer is impulsive: duplicate of the impact, delay Slap,
   gain per 3.5, LPF 3 kHz.
10. Sends: SetSubmixSend for Open/Valley/Forest/Cave with the class weights x wet table.
11. Cull anything below -48 dB; under voice pressure drop gear, cloth, debris (never the impact).
```

Timeline queue: `UEphemeraPlayerAudio::Follow()` already runs every frame; add a small array of `{DueClock, Key, GainDb, Pitch, LowPassHz, Sends}`; due entries are launched in the same call. Frame granularity (8 to 16 ms) is fine for 15 to 120 ms offsets [D]. Exact sample-accuracy would need a MetaSound or baked pre-roll, which is not warranted.

Other changes:
- `Pick()`: replace "not the last one" with a per-key shuffle bag: refill a shuffled index array when empty, never return any of the last `N = min(4, Num/2)` picks. (Keys carry tier and foot: `step_gravel_jog_L_heel`, via the existing `KeyOf()` that strips the trailing take number.)
- `VoiceCount`: 12 to about 28. At 3.3 steps/s x 5 layers x 0.3 to 0.6 s tails the worst case is about 8 to 10 live voices plus landings, which leaves headroom.
- `Play()` gains optional pan bias and sends; `PlayAt()` accepts a `DelaySeconds`.
- The `Under` state moves from per-voice low-pass to a submix chain override (3.4); the per-voice `SetLowPassFilterFrequency` stays for the thin-air and wetness cases.
- New events wire in: stop detection (settle), yaw-rate (turn), skid, slope slide, take-off in `DoJump()`, landing tiers in the existing `LandedAt` block (it already has `Hard`), water entry in `SenseSea` (already plays `splash` or `step_water`).
- Vegetation density: ground cover is grown at run time in a ring around the camera by `UEphemeraGroundCoverComponent` from rules (Moisture, Cover, slope, patch noise); add `PlantsNear(WorldPosition, RadiusCm)` that counts live instances of the grass/sedge rules around the feet, so the sound agrees with the plants you can see. Same single-source-of-truth idea as the comment in `GroundUnderfoot` ("the terrain already knows"). Until then, estimate from `Moisture`/`Cover`.

### 6.5 Surface weights from the terrain fields

Replace the hard thresholds in `GroundUnderfoot()` with weights that sum to 1, computed from the same fields the shader and ground cover read (no second source of truth). Starting formulas [D] (verify against what the material actually paints; `Cover` is -1 bare sand/gravel .. 0 lichened lava .. 1 deep moss; `Moisture` 0 dry .. 1 lush; `Ice` 0..1; `FlowAsShaded` flow; sea level from `TideM`):

```
Water  = smoothstep(0.0, 0.15, (Tide + 0.1) - HeightM)          // at the foot; depth class from SeaDepthCm
        or smoothstep(0.55, 0.75, FlowAsShaded) when the body has streams
Snow   = smoothstep(0.35, 0.65, Ice)                              // additive: leaves the layer below at 30%
Moss   = smoothstep(0.15, 0.55, Cover)
Sedge  = smoothstep(0.35, 0.7, Moisture) * (1 - Moss) * (1 - smoothstep(-0.2, 0.0, -Cover))
Ash    = smoothstep(-0.9, -0.55, -Cover) * (1 - Gravel)           // fine black sand
Gravel = band(Cover, -0.6 .. -0.2) + slope>25deg scree
Basalt = remainder
normalise the non-additive ones to sum 1; apply Snow and Water as additives; then low-pass the whole
vector in time with a 0.4 s time constant (Sucker Punch fades ~1 s between surfaces [V]).
```

Sampling: the foot position at heel strike, plus one sample 0.5 m ahead along travel (ND "multi-sampling" [V]); average.

### 6.6 Asset pipeline changes (`steps.py` and a new `foley.py`)

1. Stop equalising every take to -25 dB RMS (`steps.py` lines 139 to 141). Measure each cut step (RMS, peak, spectral centroid) and assign it to a tier by its own energy and by the speed of the recording it came from; then normalise per tier pool, preserving each take's natural +-2 dB.
2. Keep stereo. Remove the mono fold in `shaped()`; high-pass 55 Hz stays; sum below 120 Hz to mono so the thump is centred; apply the baked L/R balance.
3. Cut by parity (walk recordings alternate feet), keep the order, tag `L`/`R`.
4. Split each step at its second onset into impact (to about 3 ms before the toe contact) and roll (the rest); keep both with their source step ID so the game can recombine from different steps.
5. Cut debris and scuffs from the same recordings' non-step events (pebbles, drags) and from the debris libraries (section 7.5).
6. Build cloth and gear sets by the same onset routine with a lower threshold (as `suit_step` does already); build the looped beds with the existing `make_loop` cross-fade from `pro.py`.
7. IRs: cut the tail of each outdoor shot (6.7), high-pass 80 Hz, trim, export as 48 kHz stereo; import and run "Create Impulse Response" once per IR in the editor.
8. Name by tier, foot and layer so `KeyOf()` still works: `step_gravel_jog_L_heel_07.wav`.
9. Loudness audit script: print per-pool true peak, RMS and tier reference deltas, so tier spacing is checked by number, not by ear alone.

Licence reminder from `pro.py`: the Sonniss recordings may be used in the game, not passed on as sound files and not used to train AI. IRs and cut steps derived inside the game are within that.

### 6.7 Rollout order (smallest change with the biggest audible gain first)

1. Pipeline: per-tier pools from the recordings already in use, no per-take normalisation, stereo kept, parity tagged. Shuffle bags and 28 voices. 5 tiers with the 20+ dB range. This alone removes the "loop" feel.
2. Layers: cloth swish, gear ticks, debris, roll with tier delay, plus start/settle/turn/skid. Landing tiers and take-off.
3. Surface weights, sedge/ash/water classes, vegetation brush loops.
4. Wetness and depth classes; splash and underwater state as a submix override.
5. Space: environment probe, reverb submixes with IRs, slap-back. Do this last because it needs the most tuning; auditioning on the PC build, not the Mac.
6. Mix pass against the wind floor with a meter.

---

## 7. Candidate recordings from the Sonniss index

All names exactly as in `.scratch/audio/audition/index.tsv`. Library, then bundle year in brackets. Where a layer has no good source in the bundles it is said plainly in section 8. "Used" means already used by `steps.py`/`pro.py`.

### 7.1 Moss / soft turf and sedge-grass impact (L1, L2)

| File | Library [year] | Why it fits |
|---|---|---|
| `169_Foley_Footsteps_Grass_Sneaker_Walk_Fast_Run_Jog_Close.wav` (24.6 s) | Tovusound - Edward – Foleyart Collection Add-On Extended Footsteps [2017] | A foley artist on grass at walk, fast, run and jog, close: one file feeds four tiers with natural level spread. Used. |
| `PM_SDGS_113 Footstep Step Dry Grass Shrubs Pine Needles Meadow .wav` (0.8 s) | PMSFX - STEPS Dry Grass & Shrubs [2020] | Isolated single step with a clean tail; sedge/meadow texture. Used. |
| `PM_SDGS_14 Footstep Step Dry Grass Shrubs Pine Needles Meadow .wav` (1.4 s) | PMSFX - STEPS Dry Grass & Shrubs [2020] | Same series, a heavier step: a jog/run take. Used. |
| `PM_SDGS_186 Footstep Step Dry Grass Shrubs Pine Needles Meadow .wav` (0.5 s) | PMSFX - STEPS Dry Grass & Shrubs [2020] | Short light step: creep/walk. Used. |
| `grass 3 single step 3.wav` (0.8 s) | RYK-Sounds - Footstep [2023] | A further single grass step to widen the walk pool. Used. |
| `mud 1 loop.wav` (3.1 s) | RYK-Sounds - Footstep [2023] | Wet soft ground steps: source for the "saturated moss/bog" wet-class impacts. |
| `Bare_Feet_Grass_RUN.R.wav` (19.8 s) | Joshua Reinhardt - Ultimate Bare Feet Expansion [2018] | Run on grass; bare feet, so use only for the soft body-thump component under shoed steps. |
| `ICEBrk_FROZEN FARMLAND Footstep on frozen field_Eneas Mentzel_ _39.wav` (0.6 s) | Eneas Mentzel - Frozen Farmlands [2023] | "Walking on a snowy frozen field with grass": crust over turf, for cold-morning moss. |

Moss is quiet and damp: expect to low-pass the dry-grass sources to about 4 to 6 kHz and add a soft body layer; the moss itself makes little sound.

### 7.2 Black volcanic sand / ash impact (L1) and fine-grit sweetener (L3)

This is the biggest gap (section 8): the bundles contain no recording of footsteps on dry sand or ash.

| File | Library [year] | Why it fits |
|---|---|---|
| `PM_SDNG_Stereo_Walk_Seamless_Loop_1.wav` (26.4 s) | PMSFX - STEPS Dirt & Gravel [2019] | Dirt-and-gravel walk; select the dirt-dominated steps, low-pass about 3.5 kHz, pitch down 2 to 3 semitones to approximate fine grit. |
| `Small Crushed Rock - DROP - Thick - Stereo.wav` (62.9 s) | Pole Position Production - The Stone & Sand Library [2023] | Granular crushed rock movement: the grain layer under the dirt steps. |
| `Pebbles - DROP - Dense - Mono.wav` (119.6 s) | Pole Position Production - The Stone & Sand Library [2023] | Dense small-stone sound for scatter and fine debris. |
| `DIRTCrsh_Stone Sand Trickle Reverberant 01_JSE_SD.wav` (29.9 s) | Justsoundeffects - Stones and Debris [2023] | Sand and stone trickle; reverberant, so use only the driest passages. |
| `Tatak_ROCKS_Pebbles Dust Tumbling Rolling Debris.wav` (8.9 s) | Tatak Audio - Rocks and Debris [2020] | Dusty pebbles: debris layer with a fine-ash quality. |
| earlier CC0 sand takes (`step_sand_01..06`) | OpenGameArt (see CREDITS.md) | Keep as the base until a real recording exists. |

### 7.3 Gravel / scree impact (L1, L2)

| File | Library [year] | Why it fits |
|---|---|---|
| `PM_SDNG_Single_Step_Footstep_19.wav` (0.9 s) | PMSFX - STEPS Dirt & Gravel [2019] | Clean isolated gravel step. Used. |
| `PM_SDNG_Single_Step_Footstep_46.wav` (1.1 s) | PMSFX - STEPS Dirt & Gravel [2019] | A heavier isolated step, ideal for a run take. Used. |
| `PM_SDNG_Stereo_Walk_Seamless_Loop_1.wav` (26.4 s) | PMSFX - STEPS Dirt & Gravel [2019] | A long walk to cut dozens of steps from. Used. |
| `S23_SFX_Footsteps_Gravel_Loafers_Loops_Walk_Normal.wav` (13.4 s) | Studio 23 - Ultimate Footstep Collection [2019] | Soft shoes on gravel at normal pace: a walk-tier source with a different shoe from the PMSFX set. Used. |

### 7.4 Bare basalt rock and hard ground impact (L1, L2)

| File | Library [year] | Why it fits |
|---|---|---|
| `BOOT_CONC CLEAN_WALK.R.wav` (30.0 s) | Joshua Reinhardt - Ultimate Bare Feet Expansion [2018] | Boots on clean hard ground, long and consistent; closest to basalt in the bundles. Used. |
| `015_Foley_Footsteps_Asphalt_Boot_Walk_Fast_Run_Jog_Close.wav` (22.9 s) | Tovusound - Edward – Foleyart Collection Add-On Extended Footsteps [2017] | Boots at walk, fast, run and jog on asphalt: hard dry strikes across tiers. Used. |
| `FS Ground Civilian Walk N03.wav` / `FS Ground Civilian Walk N05.wav` (0.7 s each) | Levan Nadashvili - Civilian Footsteps [2016] | Isolated walking steps on firm ground, game-ready. |
| `FS Concrete Soldier Crouch N02.wav` / `FS Concrete Soldier Crouch N05.wav` (0.8 s each) | Levan Nadashvili - Soldier Footsteps [2016] | Crouched, slow steps on hard ground: the creep tier. |
| `289_Foley_Footsteps_Rocks_Sneaker_Jump_Land_On_Two_Feet_Close.wav` (9.7 s) | Tovusound - Edward – Foleyart Collection Add-On Extended Footsteps [2017] | Landings on rocky ground (see 7.9). Used for `land`. |
| `Tatak_ROCKS_Single Rock Impact on Stones.wav` (6.5 s) | Tatak Audio - Rocks and Debris [2020] | Stone-on-stone for the clack of a hard step on loose stones. |

### 7.5 Debris / grit / kicked-stone sweeteners and scree (L3, slide bed)

| File | Library [year] | Why it fits |
|---|---|---|
| `Tatak_ROCKS_Pebbles Dust Tumbling Rolling Debris.wav` (8.9 s) | Tatak Audio - Rocks and Debris [2020] | Pebbles rolling with dust: cut to short grit bursts for gravel/basalt. |
| `Rock Hit and Bounce Dirt_RBDII 06.wav` (1.1 s) | Mechanical Wave - Rock,Brick and Dirt - Part 02 [2017] | A stone kicked and bouncing on dirt: the rare "kicked pebble" event. |
| `Hit Rock into Gravel_RBDII 06.wav` (1.0 s) | Mechanical Wave - Rock,Brick and Dirt - Part 02 [2017] | A stone landing in gravel: scatter after a hard step. |
| `Scree Rock_RBD 06.wav` (9.8 s) | Mechanical Wave - Rock Brick and Dirt [2015] | Scree tumble: the loop/bed for slope slides. |
| `rockslide_02.wav` (5.8 s) | Soundrangers - Foley Elements Rocks, Dirt And Stone [2018] | "Small rocks and debris sliding down stone surface": slope slide. |
| `Crumbling Rock Slide_RBDII 07.wav` (4.5 s) | Mechanical Wave - Rock,Brick and Dirt - Part 02 [2017] | A heavier slide for steep scree. |
| `SBvfe2_Medium Rock Dropping 011.wav` (0.6 s) | Sonic Bat - Videogame Foley Essentials Vol. I [2024] | Short stone drop for small clack sweeteners. |
| `SBvfe2_Dry Tree Branches Contact 024.wav` (2.3 s) | Sonic Bat - Videogame Foley Essentials Vol. I [2024] | Dry twig contact for the rare stem snap on sedge. |
| `PM_SDGS_213 Footstep Step Dry Grass Skid Drag.wav` (1.5 s) | PMSFX - STEPS Dry Grass & Shrubs [2020] | A foot dragging over dry grass: scuff/skid for soft ground. |

### 7.6 Snow impact, crust, slide and stomp (L1, L3, landings)

| File | Library [year] | Why it fits |
|---|---|---|
| `0002_Footsteps_Walking_Crusty_Snow_Male_Shoes_Normal_Pace.wav` (25.4 s) | Wav Junction Sound Effects - Footsteps [2020] | A long crusty-snow walk at normal pace. Used. |
| `Bluezone_BC0264_snow_footsteps_walk_light_008.wav` | Bluezone - Snow Footsteps [2020] | Light walk: creep/walk tier. Used. |
| `Bluezone_BC0264_snow_footsteps_walk_013.wav` | Bluezone - Snow Footsteps [2020] | Normal walk. Used. |
| `Bluezone_BC0264_snow_footsteps_walk_deep_008.wav` | Bluezone - Snow Footsteps [2020] | Deep snow: a distinct pool for deep drifts. Used. |
| `Bluezone_BC0264_snow_footsteps_run_004.wav` | Bluezone - Snow Footsteps [2020] | Run tier. |
| `Snow Footstep Light Slow.wav` (10.4 s) | Alexander Fokshtein - Snow Steps [2018] | Slow light steps: creep. |
| `Ice Footsteps Hard Packed.wav` (8.7 s) | Alexander Fokshtein - Snow Steps [2018] | Hard packed / icy steps. |
| `Snow Impact Stomp Hard.wav` (4.8 s) | Alexander Fokshtein - Snow Steps [2018] | Hard landing in snow. |
| `Snow Slide Medium .wav` (4.5 s) | Alexander Fokshtein - Snow Steps [2018] | Slide/skid on snow. |
| `Footsteps_Icy_Normal_Single_Fienup_006_SIS2.wav` (1.0 s) | Soundopolis - Icy Snow 2 HD [2019] | Single icy step with crust. |
| `footstep_multiple_snow_boot_run_01_var03.wav` (16.5 s) | Soundrangers - Foley Elements Ice and Snow [2018] | Run in snow boots. |
| `snow_foley_walking_on_snow_02.wav` (13.5 s) | Pole Position Production - The Snow & Ice Texture Library [2016] | Well-recorded (MKH 8040) walk. |
| `S23_SFX_Footsteps_Snow_Singles_Sequence.wav` (12.3 s) | Studio 23 - Ultimate Footstep Collection [2019] | Singles with natural spacing. Used. |
| `footstep_ice_crunchy_run_01.wav` / `footstep_ice_crunchy_run_02.wav` | Gamemaster Audio - Footstep and Foley Sounds [2018] | Crunchy ice crust at a run. |

### 7.7 Vegetation brush (L6) and wind-in-grass beds

| File | Library [year] | Why it fits |
|---|---|---|
| `RUSTLE Studio performed green leaves twigs brush moves.LR.wav` (65.2 s) | Articulated Sounds - Rustle Tones [2018] | "Pass through, slither, plant interaction": a performer moving through leaves and twigs, i.e. brushing past plants. |
| `RUSTLE Studio performed broadleaf reeds manipulation LOOP.LR.wav` (56.2 s) | Articulated Sounds - Rustle Tones [2018] | Reeds and brittle leaves: the nearest thing to sedge. |
| `SON_The_Rustle_004 - Grass Rustling Heavy.wav` (78.2 s) | Sonocaine - The Rustle [2016] | Heavy grass rustling: the "hard" brush bed at run. |
| `VEGEMisc_CornStalkMovements20_InMotionAudio_SoundoftheCorn.wav` (11.9 s) | InMotionAudio - Sound of the Corn [2024] | Tall stalks pushed aside: the swish when pushing through tall grass. |
| `VEGELeaf_CornLeafMovements02_InMotionAudio_SoundoftheCorn.wav` (2.0 s) | InMotionAudio - Sound of the Corn [2024] | A single leaf movement for per-step swish. |
| `91 Wind_winter grass_2_MS stereo.wav` (140.6 s) | Ivo Vicic - Winter textures vol.1 [2019] | Wind through winter grass, to drive rustle with the existing wind gusts (Tsushima does this with terrain-aware wind [V]). |

### 7.8 Cloth, gear, backpack (L4, L5) and the take-off swish

| File | Library [year] | Why it fits |
|---|---|---|
| `PR LEATHER BACKPACK_JOG_416.L.wav` (20.0 s) | Joshua Reinhardt - Ultimate Cloth and Prop Collection [2017] | A leather pack on a jogging walker: the gear-bounce loop for jog/run. |
| `0025_BackPack_Running.wav` (33.5 s) | Wav Junction Sound Effects - Zippers, Buckles & Clothing [2020] | A backpack at a run: bounce and strap movement. |
| `PR ARMY GEAR ROOM_WALK_C414.wav` (31.2 s) | Joshua Reinhardt - Ultimate Cloth and Prop Collection [2017] | A walk with equipment: the gear-tick source. |
| `SFX CLOTH Foley Jacket Synthetic Soft Shell Whoosh Flutter.wav` (15.3 s) | Systematic-Sound - Sound Themes - Modern Cloth Foley 01 [2020] | Modern technical fabric swishing: a better match for a spacesuit/field suit than leather. |
| `SFX CLOTH Foley Jacket Leather Rough Movement Slow Rustle.wav` (22.8 s) | Systematic-Sound - Sound Themes - Modern Cloth Foley 01 [2020] | Heavier rustle for the hard cloth set. |
| `HERO - CK - ARMOR Leather Walk.wav` (18.3 s) | Rock The Speakerbox - Hero [2019] | The current `suit_step` source; keep as one of the sets, but it is fantasy armour and should not be the only cloth voice. Used. |
| `FOLYClth_ClothMovement24_InMotionAudio_FoleyT-Shirt.wav` / `FOLYClth_ClothMovement29_InMotionAudio_FoleyT-Shirt.wav` (2.3 s / 1.5 s) | InMotionAudio - Foley T-Shirt [2026] | Single cloth movements: the light cloth tier and the start-from-rest weight shift. |
| `LeatherArmor_Rustle_03.wav` (0.9 s) | Double Trouble Audio - Medieval Armor and Impacts [2017] | Short leather rustle for the hard tier. |
| `SBvfe1_Jiggling Belt Buckle 004.wav` (1.4 s) | Sonic Bat - Videogame Foley Essentials Vol. I [2023] | Buckle jingle for gear ticks. |
| `EQUISprt_Climbing Gear, Carabiner, Screwing Lock 01_344 Audio_Climbing Gear Foley Vol 1.wav` (3.2 s) | 344 Audio - Climbing Gear Foley Vol. 1 [2026] | Carabiner and clip clicks: believable hung hardware on a field suit. |
| `Whoosh,Organic,Fabric,Glove,Airy,Breathy,Punchy.wav` (0.5 s) / `Whoosh,Organic,Fabric,Strap,Zip,High,Stagger,Pushy.wav` (0.8 s) | Airborne Sound - Organic Whooshes [2019] | Arm and cloth swish for take-off and turns. |
| `WEATHER WIND Mountain Slope, Strong, Inside Jacket Hood, Sustained Blustery, Gusty, Binaural, Patagonia, LOOP.wav` (48.1 s) | Articulated Sounds - Rare Winds [2020] | Wind flapping a hood: cloth flutter in a strong wind state. |

### 7.9 Landing, take-off, body weight

| File | Library [year] | Why it fits |
|---|---|---|
| `289_Foley_Footsteps_Rocks_Sneaker_Jump_Land_On_Two_Feet_Close.wav` (9.7 s) | Tovusound - Edward – Foleyart Collection Add-On Extended Footsteps [2017] | Two-footed landings on rocks, several takes in one file. Used. |
| `RL_bodyfall_Dirt_M4_Close_Stereo_Hard_Impact_10.wav` (1.1 s) | Red Libraries - Bodyfall [2019] | Body weight onto dirt: the low "hard landing" layer. |
| `RL_bodyfall_Concrete_Generic_Feet_Mid_Mono_Med_Impact_02.wav` (1.1 s) | Red Libraries - Bodyfall [2019] | A feet-first medium impact: the weight layer for medium landings on rock. |
| `Snow Impact Stomp Hard.wav` (4.8 s) | Alexander Fokshtein - Snow Steps [2018] | Hard landing in snow. |
| `ARMOR Body Drop Chain Leather Short 02.wav` (2.0 s) | SmartSoundFX – Medieval [2020] | Gear thump on a hard landing (use low-passed; trim the chain jingle). |
| `Female 3 - Grunt - Jumping and Landing - Long - Ehrlund M1.wav` (84.7 s) | Pole Position Production - The Grunts Library [2023] | Optional effort vocals; the player has no stated voice, and a quiet game may prefer breath only. |

### 7.10 Breath (state machine)

| File | Library [year] | Why it fits |
|---|---|---|
| `HUMAN BREATH Male_Deep Mouth Inhale and Powerful Strong Exhale with Acceleration_A.wav` (12.6 s) | Articulated Sounds - Human Male Breathe [2020] | Effortful breathing building in pace: the "working" and "heavy" states. |
| `HUMAN BREATH Male_Deep Opened Mouth Breath with S Formed Lips Long Exhale_F.wav` (21.0 s) | Articulated Sounds - Human Male Breathe [2020] | Long exhales: recovery. |
| `HMNBrth_Human, Adult, Male, Breathe, Mouth, Slow,_UberDuo_INHL.wav` (23.5 s) | UberDuo - Inhale – Owen – Character Playset [2024] | Calm slow breathing: the idle state. |
| `Breathe Male Fast Shuddering ORTF MKH8040.wav` (78.8 s) | Pole Position - The Gut-Wrenching Gore Library [2020] | Fast hard breathing: exertion peak (cut short, low in the mix). |
| `Male 1 - Breathing - Gasping - Ehrlund M1.wav` (41.2 s) | Pole Position Production - The Grunts Library [2023] | Gasps for hard landings and surfacing. |
| `HMNBrth_Human, Adult, Female, Panting, Nose, Loop_UberDuo_INHL.wav` (10.4 s) | UberDuo - Inhale – Tanja – Character Playset [2024] | A panting loop if a female voice is chosen. |

### 7.11 Shallow water, wading and mud (L7)

| File | Library [year] | Why it fits |
|---|---|---|
| `0014_Footsteps_water_puddle_single_splashes.wav` (25.9 s) | Wav Junction Sound Effects - Footsteps [2020] | Single steps into a puddle: classes 1 and 2. Used. |
| `Bluezone_BC0256_water_splash_small_008.wav` (1.9 s) | Bluezone - Splash [2020] | Small splash for ankle-depth steps. |
| `Bluezone_BC0256_water_splash_drop_006.wav` (1.3 s) | Bluezone - Splash [2020] | Droplet impact: toe-off drips. |
| `mud_splat_heavy_03.wav` (1.2 s) | Soundrangers - Hydrology Bubbles and Splashes [2018] | "Heavy watery splat": a foot pulled from a bog. |
| `LIQMvmt_Liquid Movement Plop Slosh Single 07_HZBIT_Microbes_MKH60.Wav` (2.8 s) | Hzandbits - Microbes [2023] | A single plop/slosh to vary wading steps. |
| `EFX EXT GROUP Marching Puddle Mud 01.wav` (19.3 s) | Coll Anderson - Battle Crowd [2015] | A group marching through puddles and mud: use as a texture source for wash loops, not as single steps. |
| `Water,Inlet,Wash,Bubbling,Between,Rocks,Liquid,Bubbles,Loop_01.wav` (96.4 s) | Mattia Cellotto - Water Volumes [2017] | Water pushing between rocks: the knee-deep wash loop. |

### 7.12 Water entry and splash

| File | Library [year] | Why it fits |
|---|---|---|
| `Bluezone_BC0256_water_splash_008.wav` (5.4 s) | Bluezone - Splash [2020] | A full splash: the medium entry. |
| `Bluezone_BC0256_water_splash_distant_007.wav` (3.2 s) | Bluezone - Splash [2020] | Distant splash: the underwater-heard version. |
| `LIQUID HIT Lake, Branch, Splash, Lake, Seaweed in Water, Small, CU 04.wav` (2.1 s) | Articulated Sounds - Magic Elements vol.2 [2020] | A close real lake splash: small entries. |
| `Glacial Lake - Large Size Rock Splashes - 5 Meters. Water,Splash,Large,Rock,Impact,Various_06.wav` (26.2 s) | Mattia Cellotto - Rocks Momentum [2016] | Heavy splashes in a glacial lake: large entries, low pitch. |
| `Splash_Dive_Fienup_002.wav` (20.7 s) | Soundopolis - Water 01 [2015] | A dive splash with its settle. |
| `underwater_splash_large_06.wav` (3.8 s) | Soundrangers - Hydrology Underwater [2018] | "Deep, low frequency underwater splash": the entry as heard from below. |
| `water_bubble_19.wav` (1.0 s) | Soundrangers - Hydrology Bubbles and Splashes [2018] | Heavy bubble burst: the air release after entry. |

### 7.13 Underwater beds and movement

| File | Library [year] | Why it fits |
|---|---|---|
| `UWM032.wav` (135.1 s, "Underwater Movement Heavy") | TheWorkRoom Audio Post - Underwater Movement [2023] | Heavy swimming movement. |
| `UWM027.wav` (61.5 s, "Underwater Ambience Medium Heavy With Bubbling") | TheWorkRoom Audio Post - Underwater Movement [2023] | Real underwater ambience with bubbles. |
| `Underwater_Motion_Fienup_024.wav` (10.4 s) | Soundopolis - Underwater [2015] | Swim/dive movement. |
| `60 Dive_mediterranean sea 1_stereo.wav` (160.7 s) | Ivo Vicic - Scuba Diving equipment and underwater [2018] | A real dive: long ambient bed. |
| `24 Bubbles_regulator_submerged 4_stereo_AKG.wav` (41.6 s) | Ivo Vicic - Scuba Diving equipment and underwater [2018] | Bubbles for breath exhalation underwater. |
| `STFX Underwater Liquid very slow and low bubbles.wav` (68.9 s) | Charlie Atanasyan - Sonic Transmission by Faunet [2018] | Slow, low bubbles for a calm, vast underwater bed. |
| `underwater_object_movement_quick_deep_15.wav` (2.9 s) | Soundrangers - Hydrology Underwater [2018] | Deep object/body movement through water. |

### 7.14 Space: real outdoor and enclosed impulse sources (for the IRs in section 3.5)

These are blank-shot recordings made to document environments; the shot's tail is the impulse response. All real field recordings.

| File | Library [year] | Environment, and why it fits |
|---|---|---|
| `AK47_big_open_area_2m_above_behind_gun_RSM191_M.wav` (35.2 s) | Pole Position - The Outdoor Gun Acoustics Library [2018] | "Open area medium long tail field forest": the Open IR. |
| `AK5_valley_field_forest_50m_behind_gun_off_axis_behind_buildings_M10.wav` (71.2 s) | Pole Position - The Outdoor Gun Acoustics Library [2018] | "Very long tail echo slapback" in a valley: the Valley IR and the reference for the computed slap. |
| `MP5_forest_road_10m_behind_gun_pointing_at_forest_off_axis_MKH8060.wav` (17.7 s) | Pole Position - The Outdoor Gun Acoustics Library [2018] | "Medium long dense tail" inside a forest: the Forest/scrub IR. |
| `AK47_catacombs_burst_blanks_in_front_of_gun_MKH8020_wide_stereo.wav` (7.1 s) | Pole Position - The Indoor Gun Acoustics Library [2018] | "Catacombs medium tail indoor metallic": the Cave / lava-tube IR. |
| `AK4_long_corridor_single_shots_blanks_behind_gun_in_corner_M10.wav` (79.4 s) | Pole Position - The Indoor Gun Acoustics Library [2018] | Long stone corridor: a longer tube IR. |
| `Hämmerli 280 - FIRING - 170m In Front - DR05.wav` (121.4 s) | Pole Position - Hammerli 280 22LR [2024] | A shot heard from 170 m: reference for far-field loss and arrival delay (0.5 s). |
| `FRWKComr_InsJ_Fireworks-Display_Multi_02-01.wav` (48.2 s) | InspectorJ - Essentials 03 Fireworks [2023] | Distant bangs "echoing across the landscape": a check that the Valley IR reads right. |
| `Patria 6x6 - IMPULSE RESPONSE - Passenger Compartment - Altiverb 3 - INTERIOR - Middle - MKH8020.wav` (18.7 s) | Pole Position - The Vehicle Impulse Response Library [2024] | A genuine IR of a small metal cabin: candidate for the ship interior. |

The only designed "impulse response" set in the bundles (`Lexi Back Double Slap.wav` etc., Navi Retlav Studio - Impulse Reverbism [2018]) is made with a Lexicon reverb, not recorded in a place; do not use it.

---

## 8. Gaps and risks

Real gaps in the bundles (verified by the index searches described above):

1. No dry sand / volcanic ash footsteps. Workaround in 7.2 (low-passed gravel plus grit layers) is acceptable for a first pass; a short field recording (boots, 20 minutes, a sandy or gravelly shore, close mic, calm day) would close it for good.
2. No bare-basalt footsteps. Hard-ground boot walks (7.4) are the closest; add stone clack debris.
3. No dedicated scuff/turn/settle library for natural surfaces. Cut from the pace gaps and tails of the walks, plus the few named drags and slides (7.5, 7.6).
4. No recorded outdoor IR sets (only shot tails, 7.14). These make good IRs but contain the shot's spectrum; flatten the tail's long-term spectrum and rely on the ear.
5. Wading in thigh-deep water: only a rock-wash texture and the Bluezone splashes; expect to build it from layers.
6. Breath is human voice: confirm the player's helmet/gender before choosing a state set.

Risks:
- Convolution CPU. Three convolution effects plus an algorithmic one running continuously on the Windows machine need a measurement (Epic: convolution is "usually more expensive than traditional algorithmic reverbs" [V]). Fallback: one convolution submix whose IR is swapped by class, with an algorithmic reverb for the rest.
- Epic's API pages for `UAudioComponent` and `UGameplayStatics` did not render for the fetch tool, so the [K] items (`SetSubmixSend`, per-component attenuation override, `USubmixEffectReverbPreset`, `SetImpulseResponse`) need a check against the 5.8 headers before coding.
- The ray probe must run on the same collision the player uses; ground-cover scatter has `bCollision` false for some batches, so trees and plants will not block rays unless the probe uses a channel they respond to.
- Tuning is by ear: the level and delay values in sections 4 and 6 are starting points. Keep the first listening session short and on the PC build with the final wind bed running.

---

## 9. Sources

Naughty Dog, Sony, Sucker Punch, Remedy
- Walden, "How The Last of Us Part II's incredible sound was made", A Sound Effect, 2020-06-30: https://www.asoundeffect.com/the-last-of-us-part-ii-sound/ [V]
- gamemusic.net, "Sound design in The Last of Us Part 2": https://gamemusic.net/?p=6073 [V]
- Rogers on Uncharted 4, A Sound Effect: https://www.asoundeffect.com/uncharted-4-sound [V]
- Young, Kovats, Lanier, "The Mix in The Last of Us", GANG, 2014: https://www.audiogang.org/?p=2615 [V]
- Lanier, GDC 2014, "Aural Immersion: Audio Technology in The Last of Us": https://www.gdcvault.com/play/1020444/Aural-Immersion-Audio-Technology-in [V abstract]
- Naughty Dog, 2014 GDC talks: https://www.naughtydog.com/blog/2014_naughty_dog_gdc_talks_and_the_last_of_us_accolades [V]
- Garcia, Jimenez, GDC 2023, "Breathing Life Into The Last of Us": https://gdcvault.com/play/1028973/Breathing-Life-Into-The-Last [V abstract]
- Lemmon, Kill Screen (2013): https://www.killscreen.com/how-sound-designers-last-us-built-quiet-armageddon/ [V]
- Walden, "Crafting Captivating Game Audio for 'Ghost of Yotei'", A Sound Effect, 2025-11-06: https://asoundeffect.com/ghost-of-yotei-game-audio/ [V]
- Walden on Ghost of Tsushima, A Sound Effect: https://www.asoundeffect.com/ghost-of-tsushima-sound/ [V]
- A Sound Effect on Alan Wake 2: https://asoundeffect.com/alan-wake-2-game-audio [V]
- Audiokinetic, "Inside the Mind of Alan Wake 2: The Role of Audio QA": https://www.audiokinetic.com/blog/alan-wake-2-audio-qa [S]
- Alan Wake news index to Remedy's Audiokinetic series: https://www.alanwake.info/2025/04/21st-april-2025-audiokinetic-explores.html [V]
- Quebe, Higa, GDC 2015, Sunset Overdrive: https://gdcvault.com/play/1021783/All-Style-All-Substance-The [V abstract]
- Kastbauer, "Footsteps: Informal Game Sound Study": https://www.gamedeveloper.com/audio/footsteps-informal-game-sound-study [V teaser]

Middleware and craft
- CRI, "Advanced Footsteps": https://blog.criware.com/?p=3604 [V]
- Krotos, footsteps guide: https://krotos.studio/blog/footsteps-sound-effects [V]
- Audiokinetic, "Footsteps Material Management": https://blog.audiokinetic.com/footsteps-material-management-using-wwise-unreal-engine-4-unity-3d/ [S]
- Audiokinetic Q&A, a Random Container of 15 wood-footstep variations in Shuffle with "Avoid repeating last 10": https://www.audiokinetic.com/qa/3617/routing-puzzle-impossible-to-solve (search snippet only; the page returned 403, so the attribution of the snippet to this thread is not confirmed) [S]
- Audiokinetic, "Reverb Needs Spatialization Too": https://www.audiokinetic.com/fr/blog/rooms-and-portals-with-wwise-spatial-audio [S]
- Audiokinetic, "Mastering a Game with Wwise, Part 1": https://audiokinetic.com/en/mastering-a-game-with-wwise-part1 [S]
- Carnes, "Creating believable outdoor reverbs in post", Production Expert: https://www.production-expert.com/production-expert-1/creating-believable-outdoor-reverbs-in-post [V]
- Boston O&P, gait terminology: https://www.bostonoandp.com/Customer-Content/www/CMS/files/GaitTerminology.pdf [S]
- windPRO manual, atmospheric absorption table (10 C, 70% RH): https://help.emd.dk/knowledgebase/content/windPRO4.1/Appendix_A_DECIBEL.pdf [S]
- Water-to-air eavesdropping, arXiv 2208.12999: https://arxiv.org/pdf/2208.12999 [S]; DOSITS: https://dosits.org/science/movement/how-does-sound-propagate-from-air-into-water/ [S]

Unreal Engine 5.8
- Convolution Reverb: https://dev.epicgames.com/documentation/unreal-engine/convolution-reverb-in-unreal-engine [V]
- Sound Attenuation: https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-attenuation-in-unreal-engine [V]
- `FSoundAttenuationSettings`: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FSoundAttenuationSettings [V]
- Submixes overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-submixes-in-unreal-engine [V]
- `SetSubmixEffectChainOverride`: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AudioMixer/UAudioMixerBlueprintLibrary/SetSubmixEffectChainOverride [V]
- Audio Volume actor: https://dev.epicgames.com/documentation/unreal-engine/audio-volume-actor-in-unreal-engine [V]
- Audio Modulation: https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-modulation-reference-guide-in-unreal-engine [V]
- MetaSound Builder API: https://dev.epicgames.com/documentation/en-us/unreal-engine/metasound-builder-api-in-unreal-engine [V]
- MetaSound Templates quick start: https://dev.epicgames.com/documentation/en-us/unreal-engine/metasound-templates-quick-start-in-unreal-engine [V]
- Steam Audio, Unreal getting started: https://valvesoftware.github.io/steam-audio/doc/unreal/getting-started.html [V]; releases: https://github.com/ValveSoftware/steam-audio/releases [V]

Repo files read: `unreal/Tools/audio/steps.py`, `unreal/Tools/audio/pro.py`, `unreal/Source/Ephemera/Private/Player/EphemeraAudio.cpp`, `unreal/Source/Ephemera/Public/Player/EphemeraAudio.h`, `unreal/Source/Ephemera/Private/Player/EphemeraPlayer.cpp` (`Footstep`, `GroundUnderfoot`, stride code, `DoJump`, landing, `SenseSea`), `unreal/Source/Ephemera/Public/Frame/EphemeraBodyDefinition.h` (`FEphemeraTerrain`, `FEphemeraGroundCoverRule`), index `.scratch/audio/audition/index.tsv`.
