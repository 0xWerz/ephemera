# Flight, jetpack, zero-g and free-fly controls: research notes

Compiled 2026-10-13 for the UE5 space game (star, 5 km planet at 10.5 m/s² with atmosphere, 1.4 km airless moon at 3.2 m/s² 24 km away).

Conventions:
- Every number has its source URL next to it.
- **Unverified** = came only from a search-result summary, a fan or student project, a mod page, or a page I could not open in full.
- **Not found** = no accessible source gave the number. I did not guess.
- Sections marked **Derived** are my arithmetic on sourced numbers, not claims from any source.
- Fetch problems: the NMS fandom wiki returned HTTP 402, nomanssky.wiki.gg returned 401, the Valve wiki returned 403, the Blender manual page returned 404 or a navigation sidebar only, the EA Anthem tips page redirected to an unrelated news item, and the GDC 2019 Anthem session page did not cover Anthem. Where a fetch failed, the figure is taken from a search summary and marked as such.

---

## Most useful findings

1. **Minecraft creative flight:** base flight 10.92 m/s, sprint-flight 21.6 m/s (no edition tag on the wiki). Bedrock 0.7.2 alpha: stopping after release went from about 4 s to about 1 s. Java drift after release is unresolved (8–10 blocks vs 1–2 blocks). Source: https://minecraft.wiki/w/Flight
2. **Apex Valkyrie (post Season 27 numbers):** max jet burst 6 s; refill starts 2.5 s after the last jet use and only while on the ground; full refill from empty 3 s. Hover fuel was cut to about 1/10 of flight burn, then changed to parity. Sources: https://apexlegends.wiki.gg/wiki/Valkyrie and https://esports.gg/news/apex-legends/apex-legends-season-27-patch-notes-the-skies-belong-to-valkyrie/
3. **KSP EVA jetpack:** 5 units of propellant, about 600 m/s delta-v, about 270 N of thrust (wiki). A forum measurement gives about 3.2 m/s² acceleration for at least 195 s. The pack cannot lift a suited Kerbal above 3.2 m/s² of gravity, which is the same failure mode as our 6 m/s² pack on the 10.5 m/s² planet. Sources: https://wiki.kerbalspaceprogram.com/wiki/EVA and https://wiki.kerbalspaceprogram.com/wiki/EVA_Jetpack
4. **Anthem:** hover is a separate state (you can fire while hovering). Take-off gets an impulse from a jump (Ranger double jump, Interceptor triple jump). Overheat: the gauge fills, thrusters cut, you fall. Diving straight down drains the gauge; water cools the jets. **No seconds or speeds were published in any accessible source.** Sources: https://devtrackers.gg/anthem/p/686e4c20-tips-on-traversal-in-anthem and https://www.pcgamesn.com/anthem/anthem-interview-flight
5. **Destiny 2 glides:** Burst Glide = big early speed kick; Strafe Glide = directional control and more horizontal acceleration; Balanced = blend. Quick double-tap jump gives height; waiting longer between jumps gives distance. **No velocity numbers found.** Sources: https://destinytracker.com/article/462/weekly-reset-for-march-27th---go-fast-update (patch note, year not shown) and player-forum tips (unverified)
6. **Deep Rock Galactic Hover Boots:** hover 2 / 2.5 / 3 / 3.5 s, cooldown 120 / 105 / 90 / 75 s. The fandom wiki says the top tier is unlimited, which conflicts. Source: https://deeprockgalactic.wiki.gg/wiki/Hover_Boots
7. **Source engine noclip:** sv_noclipspeed default 5 and sv_noclipaccelerate default 5 (two sources agree on the values, not on what the speed multiplies). sv_specspeed 1200 is questionable. Sources: https://developer.valvesoftware.com/wiki/Sv_noclipspeed (search summary) and https://totalcsgo.com/commands/svnoclipspeed
8. **Quake-style friction:** sv_friction 4, sv_stopspeed 100 units/s, sv_spectatormaxspeed 500 units/s (ezQuake manual, Quake-era). Source: https://www.quaddicted.com/webarchive/ezquake.sourceforge.net_20121129/docs/index.html%3Fvars-serverside-physics.html
9. **Unity Scene view:** Acceleration ON ramps speed up until movement stops; OFF = constant speed. Min speed range 0.01–98, max 0.02–99. Scroll wheel changes speed in Flythrough. Source: https://docs.unity3d.com/6000.4/Documentation/Manual/SceneViewCamera.html
10. **Unreal Editor:** the mouse wheel changes camera speed while holding RMB. The only documented auto-scaling option is "distance-scaled camera speed" (Advanced section). The 1–8 speed scale is not verified. Sources: https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine and https://www.versluis.com/2023/12/changing-camera-speed-depending-on-object-size-in-unreal-engine
11. **SpaceEngine:** keys 1/2/3 select Free / Spacecraft / Aircraft (2017 forum, archived). Aircraft = motion follows the view; Spacecraft = keeps heading when you turn. Mouse wheel or +/− changes speed; hold Ctrl for velocity presets. No numeric speeds or distance-scaling rule found. Sources: https://forum.spaceengine.org/viewtopic.php%3Fp=13462.html and https://spaceengine.org/manual/ (FAQ text)
12. **Celestia:** Follow (default for Go-to), Sync Orbit, Phase Lock, Chase. Speed keys (secondary guide): A up, Z down, Q reverse, F1/S stop, F2 = 1 km/s. Source: https://skymarvels.com/learningctr.htm
13. **Zero-g assists:** Elite Dangerous Flight Assist ON auto-corrects pitch/yaw/roll and brakes on throttle cut; OFF is Newtonian drift. Breathedge has a dedicated stabilise button (L2 / LT). Hardspace makes the player counter drift by hand. Sources: https://forums.frontier.co.uk/goto/post?id=3656788 (forum), https://outsidergaming.com/breathedge-controls-guide-for-ps4-ps5-xbox-one-and-xbox-series-xs/, https://www.playstationcountry.com/hardspace-shipbreaker-ps5-review/
14. **Small round worlds:** Super Mario Galaxy 2 changed the camera so Mario's head stays mostly upright on planet undersides (the first game showed him upside down, with left/right reversed). Outer Wilds reportedly uses a gentler-than-inverse-square gravity falloff (PC Gamer, search snippet only). Source: https://iwataasks.nintendo.com/interviews/wii/supermariogalaxy2/1/3
15. **Motion sickness:** constant speed in a straight line is gentler than changing speed; yaw turns from gamepad or keyboard are the most provoking; frame rates below 60 fps raise risk. Sources: https://doc.stride3d.net/4.1/en/Manual/virtual-reality/vr-sickness.html (VR docs, secondary) and https://uploadvr.com/five-ways-to-reduce-motion-sickness-in-vr/

---

## 1. Jetpacks and aerial movement

### 1a. Per-game table

| Game | Thrust / speed | Fuel / energy and recharge | Buttons and special rules | What players and critics say | Sources |
|---|---|---|---|---|---|
| **No Man's Sky** jetpack | **Not found** (no thrust or speed values in any accessible source) | Drains fast during the initial burst (fandom wiki, via search). Deuterium-rich plant: 10 s boost with unlimited fuel for the duration (wiki, via search). Water refills tanks; underwater use costs life support, not fuel. Airburst Engine recycles falling energy into mid-air recharge. **Tank length and ground recharge rate: not found.** | Melee-then-jetpack boost: fire the pack while the strike animation is still playing (guide). Forum timing: about 0.25–0.5 s after the melee hit (forum, URL not resolved). Patch 3.37 (April 2021) fixed a melee-boost animation bug (search summary, unverified). | Steam (2018) found no fuel difference between holding the jetpack and tapping it; rocket boots add a jump but no efficiency. Players alternate short bursts. Players advise keeping 10–20% fuel to slow down at the end of a boost. | https://gamerguides.com/no-mans-sky/guide/walkthrough/tips-and-tricks/how-to-melee-boost ; https://nomansskyrecipes.com/technology/tech142 ; https://nomanssky.fandom.com/wiki/Jetpack (fetch 402, search snippet only) ; https://steamcommunity.com/app/275850/discussions/0/2149847423929249969/?l=latam |
| **Anthem** (javelin) | **Not found** (no speeds or seconds in any accessible source). All javelins share the same flight speed; Ranger and Interceptor are more agile. | Heat gauge builds while sprinting, hovering, flying. Gauge full: thrusters cut, you fall, and you must "wait a while" before flying again (lockout not timed). Diving straight down drains the gauge. Waterfalls and skimming water cool the jets; fully submerged, the jets do not overheat. Thermal Cooling (patch 1.05) raises heat capacity and cuts overheated time. | Hover: stop mid-air and hover; you can fire and use most gear. Full flight: most abilities locked out (search summary, unverified). Take-off: Ranger double jump, Interceptor triple jump, Storm seals give extra float, Colossus thruster adds height on take-off and extra lift on the double jump. | BioWare: "a sense of mastery" (verbatim); flight is easy to start and deep to master (paraphrase). QA found players could win without touching the ground, so enemies were changed. Players reported shorter flight and faster overheat after patch 1.06 (unverified, no fix confirmed). | https://devtrackers.gg/anthem/p/686e4c20-tips-on-traversal-in-anthem ; https://www.pcgamesn.com/anthem/anthem-interview-flight ; https://mp1st.com/news/anthem-patch-1-05-included-unannounced-changes-and-here-they-are (patch 1.05, search snippet) ; https://twinfinite.net/guides/anthem-fly-javelin-how/ (search snippet) |
| **Destiny 2** jumps | **Not found.** Qualitative only. | No meter (jump-based). | Lift / Glide variants: **Burst Glide** = strong initial speed; **Strafe Glide** = strong directional control; **Balanced** = middle. Patch note: Burst top speed raised above Strafe's; Strafe got higher top speed and more horizontal acceleration; initial vertical kick from Glide raised. Glide starts by pressing jump in the air. Blink = short teleport; HUD disabled for 1–2 s after (old review). Tips (forum, unverified): quick double-tap jump for max height; wait longer for distance; at the apex, press jump again and hold for lift, then steer. | Burst and Strafe described as different tools; Balanced described as "some of the properties of both without being as potent as either". | https://destinytracker.com/article/462/weekly-reset-for-march-27th---go-fast-update ; Destinypedia glide descriptions (search summary, page not confirmed) ; https://www.avforums.com/threads/destiny-2.2092647/post-26199488 (forum tips) |
| **Titanfall 2** | **Not found** from official sources. Student project only (below). | None (no meter). | Jump kit gives double jump, wall run, wall jump. Prima: wait until you start descending before the double jump. Slide keeps speed after a landing or wall run. | Chaining wall runs described as the fast way to travel. | https://primagames.com/eguides/titanfall-2-eguide/single-player-campaign |
| *Titanfall 2 student-project constants (**unverified, not Respawn**)* | Walk / sprint / crouch caps 320 / 530 / 200 units/s. Ground jump 380 units/s up; air jump 340 units/s up. Wall jump 640 up / 350 sideways. | Double-jump minimum delay 0.10 s; coyote 0.15 s. Wall run: gravity zero for 1.0 s, then ramps to full over 2.0 s. Grapple launch ×1.15; auto-detach about 2 m from anchor. | Tuned to match the "TMS" feel. Quake units. | n/a | https://cse125.ucsd.edu/2026/cse125g2/docs/TitanfallConstants_8hpp.html |
| **Halo: Reach** jetpack | **Not found.** | Cooldown is changeable in custom games (Halopedia). **Jetpack numbers not found.** | With infinite armour abilities, the jetpack lifts you to a set altitude and then slowly sinks back. It can be used while firing. Halo 4 let you hover indefinitely (fan wiki). Active camo refills from empty in 15 s; Armor Lock lasts up to 5 s, then 3 s wait, then 11 s recharge (for comparison). | n/a | https://www.halopedia.org/Armour_abilities ; https://halopedia.org/Jet_Pack (search result, attribution unconfirmed) |
| **Halo Infinite** thruster | **Not found.** Horizontal dash in any direction, "notably faster than sprinting" (guide). | One guide: 3 default uses, 5 max, 5 s cooldown. Wiki upgrade text says Afterburner adds a second charge (implies 1 base). **Conflict, unresolved.** Thermal Control: cooldown −20%. | Upgrades: Impulse (more distance and speed), Escape Velocity (4 s personal cloak). | n/a | https://www.halopedia.org/Thruster_%28Halo_Infinite%29 ; https://support.halowaypoint.com/hc/en-us/articles/26608497528596-Equipment-in-Halo-Infinite |
| **Starfield** boost packs | **Not found.** Relative only: Balanced ≈ 2× the height of Basic (guide). | Power: one use drains the whole meter. Balanced: about two boosts before the meter empties. Skip: sources conflict (one says recharges about 50% faster; another says it pushes forward; another says several small jumps). Needs Boost Pack Training perk. | Four pack types: Basic, Balanced, Power, Skip. | n/a | https://www.gamerevolution.com/guides/945049-starfield-boost-pack-comparison-basic-vs-balanced-power-skip-capacity-boostpack ; https://www.keengamer.com/articles/guides/starfield-boost-pack-tips-and-tricks/ ; https://primagames.com/?p=172649 |
| **Apex Valkyrie** jets | Horizontal speed and strafing raised in Season 27 (magnitude not given). Normal flight goes up in the facing direction; holding ADS gives level flight. | **Max burst 6 s** (was 8 s). **Refill delay 2.5 s** (was 8 s). **Full refill 3 s** (was about 10.9 s). Refill only on the ground. Hover fuel: was about 1/10 of flight burn; changed to parity so hover is no longer free. | 1 s weapon delay after release of jets. Slowing effects take a 20% debuff on aerial boost. | Respawn said Valkyries lingered over final fights in scrims; the change was made as the ALGS ramped up. | https://apexlegends.wiki.gg/wiki/Valkyrie ; https://esports.gg/news/apex-legends/apex-legends-season-27-patch-notes-the-skies-belong-to-valkyrie/ ; https://egmnow.com/apex-legends-update-makes-an-adjustment-to-valkyrie/ |
| **Deep Rock Galactic** Hover Boots (perk) | Hover is an airborne hold; reduces momentum "very quickly" but does not stop it instantly, so a fast fall can still hurt. | **Hover 2 / 2.5 / 3 / 3.5 s; cooldown 120 / 105 / 90 / 75 s** (wiki.gg, updated for U36). Fandom says top tier = unlimited hover. **Conflict.** | Long-press the call key in the air to start; press again or jump to end. Deals 20 heat per tick to creatures below; slows them to 0.8× speed. | n/a | https://deeprockgalactic.wiki.gg/wiki/Hover_Boots ; https://deeprockgalactic.fandom.com/wiki/Hover_Boots |
| **Deep Rock Galactic** Jet Boots MKII | **Max vertical speed 17 m/s.** | Heating 12/s; cooling delay 0.5 s; cooling 45/s; overheat threshold estimated about 47 (unconfirmed). Overheat described as "pretty rapid" (Steam, 2020/2023). | Found only on extraction in Deep Scan missions. Mutually exclusive with Hover Boots perk. | n/a | https://deeprockgalactic.wiki.gg/wiki/Jet_Boots (attribution in the search summary is ambiguous) |
| **Tribes (Ascend)** | **Not found.** Players describe skiing plus jetpack as the speed technique. | Jetpack spends an energy bar that refills over time up to a class-set ceiling. Pathfinder class: "Energy Recharge Pack" speeds recovery. Skiing costs nothing; jetpack drains energy. | Space toggles skiing (frictionless slide); RMB fires the jetpack (tribes wiki beginner's guide, flagged obsolete). Jetpack can add momentum when fired while dropping onto a slope. | Reviewers: skiing "removes friction"; "pick up speed down hills, jet up inclines". | https://tribes.fandom.com/wiki/Beginner%27s_Guide ; https://www.expertreviews.co.uk/technology/gaming/tribes-ascend-review |
| **Star Wars Battlefront II** Jet Trooper | **Not found** for Jet Trooper. | Fuel and thrust numbers for Boba Fett only (unverified, source not identified): burn 20% per second while held, refuel 8.3% per second. | Boost (LB / L1) thrusts up; dash (RB / R1) forward. | n/a | https://twinfinite.net/guides/star-wars-battlefront-2-jetpack-how/ (controls) |
| **Astroneer** | **Not found.** | **Hydrazine pack:** sustained flight while fuel lasts. Spare tanks auto-swap in. Low fuel = blinking lights and a pitch drop in the thrust sound. Partly used tanks must be vented to refill. **Solid-Fuel Jump Jet:** fuel-limited bursts; ring meter fills while in use and resets on landing. Two jets = faster boost. | Hydrazine: each thruster fires separately, or both by holding jump in the air. | n/a | https://astroneer.wiki.gg/wiki/Hydrazine_Jet_Pack ; https://astroneer.wiki.gg/wiki/Solid-Fuel_Jump_Jet |
| **Just Cause 4** wingsuit | **Not found.** | Not found. | PC: wingsuit E, airbrake Ctrl, parachute Space. Grapple then wingsuit carries grapple momentum into flight. Parachute to climb, then back to wingsuit. Steering "very sensitive"; altitude costs speed. | Guides: small, regular corrections; wide sweeps send you off course. | https://www.nerdsandscoundrels.com/just-cause-4-how-to-use-wingsuit/ ; https://www.nerdsandscoundrels.com/just-cause-4-controls-ps4-xb1-pc/ |
| **Outer Wilds** jetpack (feel only; you have the numbers) | Not given in sources. | Not given in sources. | Thrust up / down on Shift / Ctrl (PC). Jetpack used to boost over gaps and solve puzzles. | **Game Informer (2019)** (search summary): control scheme called a drag on the game; semi-realistic flight compared with KSP; after hours the reviewer managed only a handful of graceful landings. **Outside Is Overrated**: jetpack boosting was fun; it blamed its own frustration on user error. **itch.io commenter**: jetpack should work more like a double jump. **Steam (2021)**: jetpack physics "extremely vexing"; advice was to slow down, use autopilot and the landing camera. | https://gameinformer.com/review/outer-wilds/wonder-and-frustration-intertwined ; https://outsideisoverrated.com/?p=7415 ; https://itch.io/post/1395431 ; https://steamcommunity.com/app/753640/discussions/0/3110266679785540459 |

### 1b. Patterns the well-liked systems share (from the sources above)

Each pattern is tied to the games that show it. "Pattern in" lists examples; these are observations from sources, not endorsements.

| Pattern | Where it shows up | Numbers |
|---|---|---|
| **Impulse on activation** (a kick, then sustained thrust) | Anthem take-off boost (Colossus, Ranger/Interceptor jumps); Destiny Burst Glide's initial kick; Battlefront II boost | Destiny initial kick raised in patch (no number). Anthem: none. |
| **Hover as a separate, named state** | Anthem hover vs full flight; DRG Hover Boots; Valkyrie level-flight with ADS; Halo Reach lift to altitude then sink | DRG hover 2–3.5 s; cooldown 75–120 s |
| **Fuel refills after a fixed delay, fast, and only while grounded** | Valkyrie (2.5 s delay, 3 s refill from empty, ground only); NMS Airburst is the exception (mid-air recharge via upgrade) | Valkyrie: burst 6 s, refill 3 s from empty (refill twice as fast as burn, derived) |
| **Overheat or lockout that a terrain feature cools** | Anthem (water and waterfalls cool jets; submersion stops overheat); DRG (heat per tick) | Anthem lockout: not timed |
| **Momentum carried in from a ground action** | NMS melee-then-jetpack; Tribes ski-then-jet; JC4 grapple-then-wingsuit; Titanfall wall-run chains | NMS: 0.25–0.5 s window (forum, unverified) |
| **Tap-timing rules** | Destiny double-tap for height, long wait for distance; KSP forum "short taps, not holding" to avoid overshoot (unverified) | Not timed |
| **Thrust follows facing direction (or stick)** | Valkyrie: normal flight goes up in the facing direction. SpaceEngine aircraft mode: motion follows the view. JC4 wingsuit: steering by stick | Not timed |
| **Two variants trade speed for control** | Destiny Burst vs Strafe vs Balanced; Starfield Power vs Balanced | Not timed |
| **Feathering (short taps)** | NMS: holding vs tapping no fuel difference (Steam). KSP EVA: short taps (forum) | n/a |

### 1c. Summary of praise and criticism

- **Praised:** Anthem (mastery curve; "feels really natural" on first use); Destiny glide variants (distinct tools); Tribes ski-plus-jet (momentum from terrain); Starfield pack types (clear tiers).
- **Criticised:** Outer Wilds (semi-realistic control scheme; long learning curve); Hardspace-style drift (player must counter it); Valkyrie (hover was too cheap; changed); Anthem (heat gauge and patch 1.06, unverified complaints); Starfield skip pack (unclear; sources conflict).

---

## 2. Zero-gravity "EVA" and space-flight controls

### 2a. Table

| Game | Thrust / speed numbers | Assists and damping | Roll and rotation | Feel notes | Sources |
|---|---|---|---|---|---|
| **Outer Wilds** ship | Crash damage: "below about 200 m/s cosmetic, about 80 m/s or less no damage" (one Steam poster, anecdotal). | **No flight assist.** Speed holds until you thrust the other way. **Match Velocity** (PC Space, gamepad A) holds you still relative to a locked target; gravity re-engages when you stop matching. **Landing Camera** (C). **Autopilot** (X) can overshoot; advice is to accelerate halfway, then decelerate by boosting away. | Roll Mode (R, PC); players sometimes miss the roll input. Up/down thrust on Shift / Ctrl. | Reviews: "a drag" (Game Informer). Players: ship drifts and spins on landing; advice is to use autopilot and landing camera. | https://www.magicgameworld.com/outer-wilds-controls/ ; https://steamcommunity.com/app/753640/discussions/0/3194737075878041444 (search summary) ; https://gameinformer.com/review/outer-wilds/wonder-and-frustration-intertwined |
| **Hardspace: Shipbreaker** | **Not found.** | **No automatic braking** as far as reviews go. Brake brings you to a full stop. A Steam user says it is "like flight-assist-off but without the automatic breaking" (anecdotal, unverified). | Rotate clockwise / anticlockwise (PS5 L3 / R3). Float up and down relative to the suit. | Reviewers: you must counter the frictionless drift yourself; "rarely completely still". Grapple: manual move, shove, pull. | https://www.playstationcountry.com/hardspace-shipbreaker-ps5-review/ ; https://xboxaddict.com/Staff-Review/14733/Hardspace:-Shipbreaker.html |
| **Elite Dangerous** (ship) | **Not found** (no thrust or cruise numbers from official sources). | **Flight Assist ON:** thrusters correct pitch, yaw, roll; brake on throttle cut. **OFF:** Newtonian drift until you thrust against it. Rotational damping was reduced in Beta 2 (player claim). Forum request for rotation-only damping was declined. | Roll is damped with FA on; free with FA off. | Forum posts only, no official source found. | https://forums.frontier.co.uk/goto/post?id=3656788 ; https://forums.frontier.co.uk/goto/post?id=8467705 |
| **Star Citizen** (ship; EVA not found) | **Not found.** | **Coupled:** thrusters actively stop lateral movement (behaves like an aircraft). **Decoupled:** no forces; you keep moving on the last vector. CitizenCon change: forward throttle now accelerates rather than targets a speed (PCInvasion). | n/a | Fan and student sources; **EVA not covered by any source.** | https://starcitizen.tools/Guide:Controls ; https://pcinvasion.com/?p=179865 |
| **Kerbal Space Program** EVA | **Thrust about 270 N** (wiki). Forum measurement: about 3.2 m/s² acceleration, at least 195 s of thrust in one direction, at least 624 m/s delta-v. Propellant 5 units; about 600 m/s delta-v per pack (wiki). | Impossible to lift off above 3.2 m/s² gravity (Kerbin, Eve, Laythe, Tylo excluded; Duna is largest where it works). **SAS for EVA: not found.** | Q / E spin. | Forum advice: **short taps, not holds**, because holding overshoots. Wiki notes an older bug where propellant was not deducted (may be outdated). | https://wiki.kerbalspaceprogram.com/wiki/EVA ; https://wiki.kerbalspaceprogram.com/wiki/EVA_Jetpack |
| **Adr1ft** | **Not found.** Search returned only Lone Echo results. | n/a | n/a | n/a | n/a |
| **Shattered Horizon** (2009) | **Not found.** | **No damping mechanic found.** PlayPile (aggregator): momentum keeps carrying you; turns must be timed. | A Giant Bomb user review says WASD-style controls get confusing once a third axis is added, but the game handles it well. | GamesRadar: controls "well thought out"; heavy deliberate movement "intentional". | https://www.gamesradar.com/uk/shattered-horizon-review ; https://playpile.gg/games/shattered-horizon |
| **Breathedge** | **Not found.** | **Stabilise button:** L2 (PS) / LT (Xbox). Acceleration R2 / RT. Flight Square / X. | n/a | A reviewer found it "simply too exhausting" and said keyboard precision limits space movement. | https://outsidergaming.com/breathedge-controls-guide-for-ps4-ps5-xbox-one-and-xbox-series-xs/ ; https://caniplaythat.com/?p=2373 |
| **Lone Echo** (VR) | **Not found.** | Glove jets plus grab; you must balance them to avoid drifting. Developer moderated "slow acceleration", a main motion-sickness trigger (details cut off). | n/a | Nausea risk named by reviewer; Lone Echo 2 review called zero-g "one of the coolest mechanics". | https://roadtovr.com/hands-on-lone-echo-nails-zero-gravity-in-vr-with-oculus-touch/ ; https://www.techradar.com/reviews/lone-echo-2-review |
| **No Man's Sky** ship (for comparison) | **Not found.** | **Vanilla has no real flight assist**: a low speed hold only (player claim). Boost disabled within a set distance of stations. Tap Square/X to slow, press again to land. | Pitch up and hold boost to climb through atmosphere. | Players ask for a true flight-assist-off mode; "the ship still stops itself" (player). | https://twinfinite.net/guides/no-mans-sky-how-to-fly-takeoff-space-land/ ; https://steamcommunity.com/app/275850/discussions/0/595140258825356267 |

### 2b. What the sources say makes zero-g pleasant

- **Explicit braking on a key with fast stop (Breathedge, Hardspace brake, Elite FA-on):** players describe the brake or FA as the thing that makes the game usable; Hardspace reviewers say the manual drift is the core challenge.
- **One-button "match" (Outer Wilds Match Velocity):** lets you stop relative to a target; gravity re-engages when released.
- **Auto-stabilise (Elite FA ON):** removes rotation drift; players describe the ship as behaving more like an aircraft.
- **Short taps not holds (KSP EVA):** forum advice to avoid overshoot.

These are sources' observations, not measured preferences. No source gave an acceleration or stop time for a zero-g "feels good" comparison.

---

## 3. Free-fly, creative and spectator cameras

### 3a. Numbers and rules

| Tool / game | Speed numbers | Acceleration and stop | Scaling with distance or height | Toggles and other keys | Sources |
|---|---|---|---|---|---|
| **Minecraft** creative flight (Java and Bedrock) | **Base 10.92 m/s** (about 250% of walking 4.36 m/s); **sprint 21.6 m/s** (about 1.98× base). Vertical: not given. Wiki says no numbers for vertical speed. | **Bedrock 0.7.2 alpha: full stop about 1 s, was about 4 s.** Java: "friction reduced to the level of ice" (no per-tick number on page). **Java drift after release: 8–10 blocks (NoDrift mod page) vs 1–2 blocks (Planet Minecraft forum). Conflict, unresolved.** Per-tick 0.91 multiplier: **unverified** (search found no official source). | None; flight can start at any altitude. | Double-tap jump toggles flight in Creative. Java: flight turns off on landing. Spectator (Java 1.8): scroll wheel changes speed. Flying gives +10% FOV. | https://minecraft.wiki/w/Flight ; https://modrinth.com/mod/no-drift (mod page); https://www.planetminecraft.com/forums/help/javaedition/blocks-wont-get-placed-properly-in-java-687127/ (forum) |
| **Source engine noclip** (CS / Source) | **sv_noclipspeed default 5** (Valve wiki: "maximum speed... Default 5"). TotalCSGO calls it a multiplier of sv_maxspeed. **Interpretation conflicts; neither source gives the absolute number.** | **sv_noclipaccelerate default 5** (Valve wiki: "speed at which the player accelerates to sv_noclipspeed"). TotalCSGO: "5 times as fast" as normal acceleration. | None. | **sv_specspeed 1200** (TotalCSGO; text conflicts), **sv_specaccelerate 5** (TotalCSGO), **sv_specnoclip** and Valve wiki page on noclip: **Valve pages returned 403; values via search summary only.** | https://developer.valvesoftware.com/wiki/Sv_noclipspeed (search summary) ; https://totalcsgo.com/commands/svnoclipspeed ; https://totalcsgo.com/commands/svspecspeed ; https://totalcsgo.com/commands/svspecaccelerate |
| **Quake-era friction (spectators and movement)** | **sv_friction 4** (CS:S default; Valve wiki). **sv_stopspeed 100 units/s** and **sv_spectatormaxspeed 500 units/s** (ezQuake manual). Quake units: 1 unit = 1.905 cm (common figure, **unverified here**). | Friction term scales with speed; below stopspeed it is constant (blog analysis of Quake code). **No spectator-specific stop time found.** | n/a | n/a | https://developer.valvesoftware.com/wiki/Sv_friction ; https://www.quaddicted.com/webarchive/ezquake.sourceforge.net_20121129/docs/index.html%3Fvars-serverside-physics.html ; https://blog.neon.moe/2020/04/03/fps-movement |
| **Unreal Editor** viewport fly (UE 5) | Speed scale **1–8 and scalar: not verified.** Old Epic forum: slider max hard-coded to 8; scroll above it possible; middle-mouse scroll gives a hidden multiplier 1/10 to 10× (old forum, unverified). | **No acceleration or inertia option found in docs.** | **Distance-scaled camera speed** (Editor Preferences > Level Editor > Viewports > Advanced) scales speed by distance from look-at point (bUseDistanceScaledCameraSpeed). This is the only documented auto-scale. | RMB + W/A/S/D; Q/E (global) and R/F (local) up/down; Z/C FOV. Mouse Scroll Camera Speed setting in Controls. Bug: setting not applied in non-Level-Editor viewports (5.8). | https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine ; https://www.versluis.com/2023/12/changing-camera-speed-depending-on-object-size-in-unreal-engine ; https://answers.unrealengine.com/questions/404631/view.html (old forum) ; https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/ULevelEditorViewportSettings |
| **Unity** Scene view fly | **Camera Speed** (no default given). Min range 0.01–98; Max range 0.02–99. | **Acceleration ON:** starts at speed value, keeps increasing until movement stops. **OFF:** constant speed. **Easing:** ease in and out (duration set via API; no default). | n/a | Flythrough: scroll wheel or two-finger drag changes speed. **Shift multiplier: not found** in the docs fetched. | https://docs.unity3d.com/6000.4/Documentation/Manual/SceneViewCamera.html |
| **Blender** walk / fly | **Not found** (manual page could not be read in full; defaults not verified). Wheel or Numpad +/− changes speed. | **No acceleration setting found.** Shift = temporary faster, Alt = temporary slower. | n/a | Preferences: walk speed, mouse sensitivity. 2013 commit lists walk base speed in m/s, a boost multiplier, jump height, gravity, teleport duration (historical). | https://www.blender.org/manual/en/editors/3dview/navigate/walk_fly.html (redirects; text not reached) ; https://lists.blender.org/pipermail/bf-blender-cvs/2013-December/060704.html (historical) |
| **SpaceEngine** (free / spacecraft / aircraft) | **Not found** (no numeric speeds in accessible docs). Speed set by mouse wheel or +/−; Ctrl switches velocity presets. | **Inertia:** middle-mouse look "without changing the camera's fixed aim or inertia" (implies inertia exists). Aircraft mode: motion follows the view; Spacecraft: keeps heading when you turn. Free: **not found.** | **Speed scaling with distance: not found.** Auto-horizon: **not found.** | Keys 1/2/3 = Free / Spacecraft / Aircraft (2017 forum). G = go to selected; G twice = faster. Shift+G / Ctrl+G = land. Space = unpause. Follow / rotate-with-object / synchronous rotation options: **not found.** | https://spaceengine.org/manual/ (FAQ text) ; https://forum.spaceengine.org/viewtopic.php%3Fp=13462.html ; https://gamepretty.com/spaceengine-controls-guide-mouse-keys/ |
| **Celestia** | Speed keys (secondary guide): A increases, Z decreases, Q reverses, F1 or S stops, **F2 = 1 km/s** preset. (Official help page does not list speed keys.) | Not found. | Not found. | Modes: **Free flight** (Esc), **Follow** (keeps offset; default for Go-to), **Sync Orbit** (Follow + matches the object's rotation), **Phase Lock** (stationary above two moving objects), **Chase** (one object; stays with its velocity vector). C centres, G goes to, T tracks, Esc cancels. | https://celestia.mobi/help/flight-mode ; https://skymarvels.com/learningctr.htm ; https://celestiaproject.space/forum/viewtopic.php?p=8481 |
| **Kerbal Space Program** camera modes | Not applicable. | Not applicable. | Not applicable. | **Free** (body at bottom, "up" away from surface; recommended for planes); **Orbital** (up toward body's north pole); **Chase** (follows surface-prograde horizontally, not vertical or roll); **Locked** (fixed to craft). V cycles views. | https://wiki.kerbalspaceprogram.com/wiki/Camera_view |
| **Photo / drone cameras** (Death Stranding, Forza Horizon) | **Not found** for speed or smoothing values. | **Not found.** | Forza FH6 guide: camera detaches up to about 12 m from the car when you stop (guide, unverified). | Death Stranding (older article): camera angle and roll; aperture, focus, exposure, depth of field. | https://siliconera.com/?p=826751 (older, self-flagged) ; https://primagames.com/gaming/forza-horizon-5-photo-mode |
| **Outer Wilds** mod free-cam, **Gaia Sky**, **Universe Sandbox**, **Google Earth**, **Elite Dangerous** external camera, **Ansel** | **Not searched** (out of time budget). | | | | |

### 3b. Acceleration and stopping: what the sources say

- **Instant stop vs glide:** Minecraft Bedrock shortened its stop from about 4 s to about 1 s (wiki). No source gives a player preference for either.
- **Acceleration on or off:** Unity's acceleration mode ramps speed the longer you hold; off = constant speed. No preference data.
- **Eased start and stop:** Unity offers an easing option; no numbers given.
- **Quake friction model:** friction 4 and stopspeed 100 units/s give an e-folding time of 0.25 s above stopspeed (see section 3c). Sources do not give an observed feel.
- **Motion sickness:** a steady speed in a straight line is gentler than changing speed (Stride VR docs, secondary).

### 3c. Speed across scales (centimetres to thousands of km/s)

- **Sourced patterns:** Celestia has a 1 km/s preset on one key (F2). Unity uses a min/max range of 0.01–98 and 0.02–99 (units not stated in the docs). Unreal has an optional distance-scaled speed. SpaceEngine uses a wheel plus presets with Ctrl (exponential steps: **unverified**).
- **Derived rule (my synthesis):** a wheel step that multiplies speed by a fixed factor covers the whole range in a few dozen notches; the sources do not give the factor. Tie the step to the current speed or to the distance from the nearest body.

### 3d. Camera "up" near a round body vs open space

- **KSP:** Free mode keeps "up" away from the body's surface; Orbital keeps "up" toward the north pole; Chase keeps horizon-level on the surface frame (no roll); Locked follows the craft.
- **Super Mario Galaxy 2 (Iwata Asks):** camera keeps Mario's head mostly upright on planet undersides; the first game's upside-down view reversed left and right and confused players. Some sequel stages are flat to simplify the camera.
- **Derived rule:** on a round body, use "away from the surface" as up and avoid roll; in open space, keep the last up vector or align to a reference body. No source gave an angular speed limit.

---

## 4. Small round worlds

| Game | Up direction and camera | Gravity and leaving the ground | Notes on nausea and feel | Sources |
|---|---|---|---|---|
| **Super Mario Galaxy** (GDC / Iwata Asks) | Galaxy 2 camera keeps head upright on planets; first game reversed left and right on undersides. Some sequel stages flat. Camera can get blocked under a planet (Yale Daily News review). | Miyamoto chose gravity because it made the Mario world distinctive among 3D games (paraphrase). **GDC talk on camera or gravity: not found.** | Mushroom Kingdom review: the automatic camera is a strong system that is far from perfect, and it adapts slowly to fast movement. | https://iwataasks.nintendo.com/interviews/wii/supermariogalaxy2/1/3 ; https://nintendo.co.uk/Iwata-Asks/Iwata-Asks-Super-Mario-Galaxy/Volume-4-Shigeru-Miyamoto/2-It-s-Fun-Just-Playing-Around/2-It-s-Fun-Just-Playing-Around-220674.html ; https://themushroomkingdom.net/smg_re.shtml |
| **Outer Wilds** | Gravity from planets; alignment fields turn the player toward nearby masses without input (PC Gamer, search snippet). | Gravity falls off more linearly than inverse-square. Beachum reportedly found inverse-square falloff made escape velocity too easy to reach (search snippet paraphrase). Exaggerated Newtonian physics (Wikipedia). | Players found ship and jetpack physics hard (see section 1 and 2). | https://pcgamer.com/how-to-build-a-black-hole (snippet only; fetch truncated) ; https://en.wikipedia.org/wiki/Outer_Wilds ; https://gdconf.com/news/see-4d-level-design-outer-wilds-deconstructed-gdc-2020 (talk page, no content) |
| **Exo One** | Disc (hover) and sphere (rolls, builds speed downhill); no HUD. | One button raises gravity about 10× (downhill speed); another flattens to hover (source mapping conflicts; another source says trigger). Two jumps before landing. | Handling "precise and responsive"; speed control has a gentle curve; no death, only crashes restart. | https://www.thesixthaxis.com/2022/01/05/exo-one-review/ ; https://primagames.com/reviews/exo-one-review |
| **Ratchet & Clank** (Nexus) | Ship flies between planets; land on spherical moons with side quests. | Gravity manipulation for Clank sections; zero-g space in Nexus. | Review of the first game (search summary): camera adequate but can get erratic; re-centring needs Ratchet to pause. | https://en.wikipedia.org/wiki/Ratchet_%26_Clank:_Into_the_Nexus ; https://www.thesixthaxis.com/2013/11/11/ratchet-clank-nexus-review/ |
| **Astroneer** | **Not researched beyond jetpack pages.** | | | |
| **Planet Nomads** | **Not researched.** | | | |
| **KSP on Minmus / Gilly** | **Not found.** Search returned nothing on these bodies. | Jetpack limit of 3.2 m/s² is the only sourced gravity threshold (wiki EVA). | | https://wiki.kerbalspaceprogram.com/wiki/EVA |

### 4a. Jumping into orbit, alignment speed, nausea

- **Jumping into orbit:** no source gave a speed or rule. KSP's 3.2 m/s² jetpack limit is the closest sourced gravity threshold.
- **Alignment speed when gravity direction changes:** no source gave a number. Galaxy 2's camera uses a rotation that keeps the head upright (no rate given).
- **Nausea:** Stride VR docs say yaw turns from gamepad or keyboard are the most provoking. UploadVR calls yaw one of the fastest ways to cause sickness. Frame rates below 60 fps are linked to sickness. Vection alone is not the same as sickness (Kuiper, Bos and Diels, *Displays* 2019; cited in search summary, no URL). ACM TOG 2019 "perceptual camera control" paper models linear and rotational vection (cited in search summary, no URL, not read).
- Sources: https://doc.stride3d.net/4.1/en/Manual/virtual-reality/vr-sickness.html ; https://uploadvr.com/five-ways-to-reduce-motion-sickness-in-vr/ ; https://lavalle.pl/vr/node319.html ; https://research.vu.nl/ws/files/118981466/Vection_does_not_necessitate_visually_induced_motion_sickness.pdf

---

## 5. Flying a ship between small planets (brief)

| Game | What the ship automates | Numbers | Sources |
|---|---|---|---|
| **Outer Wilds** | Match Velocity (hold still relative to target); Landing Camera (speed control near high-gravity bodies); Autopilot (approach, overshoots). | Autopilot overshoot advice: accelerate to halfway, then decelerate by boosting away (player advice). No numbers. | https://www.magicgameworld.com/outer-wilds-controls/ ; https://steamcommunity.com/app/753640/discussions/0/3194737075878041444 |
| **No Man's Sky** ship | Boost (hold R2 / RT to climb, both shoulder buttons in space); tap to slow, press again to land; Pulse Drive plots a route; boost disabled near stations. No autopilot landing found. | Not found. | https://twinfinite.net/guides/no-mans-sky-how-to-fly-takeoff-space-land/ |
| **Elite Dangerous** | Flight Assist ON: auto-correct and braking. OFF: none. | Not found (forum). | https://forums.frontier.co.uk/goto/post?id=3656788 |
| **Star Citizen** | Coupled: active correction. Decoupled: no forces. | Not found. | https://starcitizen.tools/Guide:Controls |
| **Kerbal Space Program** | Not researched for ships in this pass. | | |
| **Ratchet & Clank** | Ship flies between planets; landings on moons. | Not found. | https://en.wikipedia.org/wiki/Ratchet_%26_Clank:_Into_the_Nexus |

---

## Derived numbers for our game (my synthesis, not sourced)

These follow from the sourced figures above. Verify each in the engine before relying on it.

- **Planet hover threshold.** Planet gravity is 10.5 m/s². The current 6 m/s² jetpack gives net −4.5 m/s² (falls). Boost 12.6 m/s² gives net +2.1 m/s² (climbs). Any pack must exceed local gravity to hover. This is the same failure mode as the KSP EVA pack (3.2 m/s² limit).
- **Moon.** 3.2 m/s² gravity. Current 6 m/s² jetpack gives net +2.8 m/s² (climbs). Boost gives net +9.4 m/s².
- **Minecraft ratio.** Sprint-flight 21.6 m/s is 1.98× base flight 10.92 m/s. Base flight is about 2.5× walking speed (wiki).
- **Valkyrie refill rate.** Burst 6 s, refill from empty 3 s: refill is about 2× burn rate (derived from the wiki numbers).
- **Quake-style stop.** With sv_friction 4 and sv_stopspeed 100 units/s (ezQuake manual): above stopspeed speed decays with time constant 1/4 = 0.25 s; below stopspeed, decel is 4 × 100 = 400 units/s². From 320 units/s (about 6.1 m/s, derived with 1 unit = 1.905 cm, unverified) to a stop: about 0.29 s + 0.25 s = 0.54 s. **Unverified for noclip and spectator movement.**
- **Source noclip speed.** If the default 5 multiplies sv_maxspeed 320 units/s (CS:S walk max), the noclip top speed is 1600 units/s, about 30.5 m/s (derived; the multiplier reading is disputed).
- **Current stop time.** Our 0.2 s stop is shorter than any sourced stop time (Bedrock about 1 s; Quake model about 0.54 s from 6 m/s). Whether that feels stiff is not answered by any source.

---

## Not found or unverified (summary)

- **NMS** jetpack thrust, tank duration, ground recharge rate (fandom wiki returned 402).
- **Anthem** speeds, heat seconds, lockout length, take-off impulse numbers.
- **Destiny 2** glide and jump velocities; Blink timing beyond the 1–2 s HUD note.
- **Halo: Reach** jetpack thrust, altitude, duration, recharge.
- **Halo Infinite** thruster charges (sources conflict).
- **Starfield** boost height and meter numbers beyond ratios.
- **Tribes** energy numbers (wiki flagged obsolete).
- **Battlefront II** Jet Trooper numbers (Boba Fett numbers unverified).
- **Astroneer** thrust and burn numbers.
- **Just Cause 4** grapple and wingsuit speeds.
- **Star Citizen** EVA coupled/decoupled (only ship sources found).
- **Adr1ft** all control details (search returned Lone Echo instead).
- **KSP SAS** on EVA (only a 2015 vessel description found).
- **Source noclip** absolute speed, sv_specnoclip, Valve wiki pages (403).
- **Blender** walk speeds and defaults (manual could not be read in full).
- **SpaceEngine** numeric speeds, horizon behaviour, distance scaling, follow/rotate options (official manual not reached beyond FAQ).
- **Unreal** 1–8 speed scale (not in official docs reached).
- **Unity** Shift multiplier (not in docs fetched).
- **Photo modes** speed and smoothing values (not found).
- **Mario Galaxy GDC** camera/gravity talk (not found).
- **Outer Wilds** gravity equations and alignment-field strength (PC Gamer article truncated at fetch).
- **Outer Wilds** mod free-cam, Gaia Sky, Universe Sandbox, Google Earth (not searched).
- **Minecraft** per-tick 0.91 drag multiplier (not on the official wiki page; appears in mod pages only).
- **Minecraft** Java stop distance (8–10 blocks vs 1–2 blocks; conflict).
- **KSP** Minmus / Gilly low-gravity rules (not found).
- **Astroneer** and **Planet Nomads** small-planet camera (not researched).
- **Anthem** 1.06 flight-time regression (player reports only).
- **Fortnite** jetpack (requested, not searched). **Destiny 2** jump numbers (see above).
