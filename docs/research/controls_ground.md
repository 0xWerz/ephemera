# First-person ground movement: numbers, formulas and sources

Research date: 2026-10-13. Web research only. No game code was written or run.

## Status legend and unit conventions

| Tag | Meaning |
|---|---|
| [V] | Verified: read in primary source code or official docs during this session |
| [S] | Secondary: wiki, forum, press, blog, or search-result snippet; primary source not opened |
| [D] | Derived: computed here from the inputs shown (formula given) |
| [U] | Unverified: from memory, conflicting, or not confirmed; do not rely on it |
| NOT FOUND | Searched; no usable number located |

Units:
- Source (Valve) unit = 1.905 cm (as given in the brief).
- Quake unit = 2.54 cm (assumed; the brief asked for a stated convention). This is not verified by any source here.
- Celeste values are in pixels per second with no metre scale in the code, so they are reported in px.
- Scale caveat: Quake 3 player box has MINS_Z = -24 [V] (bg_public.h). If MAXS_Z is 32 [U], the box is 56 u tall, which is 1.42 m at 2.54 cm/u. The 2.54 cm convention therefore makes Quake players smaller than human. Treat metre speeds from these games as approximate. Body-heights per second is the scale-free comparison.

Sources used repeatedly:
- Quake 3 bg_pmove.c: https://raw.githubusercontent.com/id-Software/Quake-III-Arena/master/code/game/bg_pmove.c
- Quake 3 bg_public.h: https://raw.githubusercontent.com/id-Software/Quake-III-Arena/master/code/game/bg_public.h
- Source SDK gamemovement.cpp, via a third-party Perforce mirror (GitHub path returned 404 this session; SDK version of the mirror unknown): https://swarm.workshop.perforce.com/files/guest/knut_wikstrom/ValveSDKCodegame_shared/gamemovement.cpp
- Celeste Player.cs: https://raw.githubusercontent.com/NoelFB/Celeste/master/Source/Player/Player.cs
- SM64 mario.c (n64decomp): https://raw.githubusercontent.com/n64decomp/sm64/master/src/game/mario.c
- ioquake3 cg_view.c and cg_local.h: https://raw.githubusercontent.com/ioquake/ioq3/master/code/cgame/cg_view.c and https://raw.githubusercontent.com/ioquake/ioq3/master/code/cgame/cg_local.h
- Apex movement (wiki.gg, revision oldid=121568, edited 18 Jul 2025): https://apexlegends.wiki.gg/wiki/Sliding (redirects to "Movement")

---

## Most useful findings (15 lines)

1. Quake 3 ground accel is linear: dv/dt = pm_accelerate x g_speed = 10 x 320 u/s^2 = 3200 u/s^2 = 81.3 m/s^2 [V code, S g_speed]. Friction is applied first, then accel, with the cap at wishspeed.
2. Quake 3 from standstill reaches 90% of top speed (8.13 m/s) in about 0.13 s, and stops from top speed in about 0.36 s [D].
3. CS:GO-style values (sv_accelerate 5.5, sv_friction 5.2, sv_stopspeed 80, top 320 u = 6.10 m/s) give 90% speed in about 0.37 s and a stop from top speed in about 0.46 s [D].
4. Source air move caps the projection of velocity on the wish direction at 30 u (0.57 m/s), not total speed; this is what allows air strafing [V code]. Quake 3 has no 30 cap; its air gain on the input axis is accel x 320 u/s^2 = 8.1 m/s^2, frame-rate independent [V code, D].
5. Source air strafe gain per second on a zero-projection axis is min(sv_airaccelerate x wishspeed, 30 x tickrate) [D; the clamp line is assumed, not shown in the fetched excerpt].
6. Quake 3 jump: JUMP_VELOCITY 270 u/s (6.86 m/s) gives 1.16 m apex and 0.675 s air time at g = 800 u/s^2 (20.3 m/s^2) [D; 270 is S, define not located].
7. Celeste (released Player.cs, verified): coyote window 0.10 s; variable jump hold 0.20 s; launch 105 px/s; gravity 900 px/s^2; apex half-gravity below 40 px/s while jump is held; max fall 160 px/s; fast fall 240 px/s [V].
8. Celeste has no separate heavier fall gravity; fall speed is capped (max fall, fast fall) and gravity is halved near the apex only while jump is held [V].
9. Celeste jumps on key press (Input.Jump.Pressed); holding extends the rise; releasing ends the hold [V].
10. SM64 single jump launch is 42 units/frame, double 52, triple 69, backflip 62; at 30 fps this is 1260 units/s for a single jump before scale is known [V code, unit scale NOT FOUND].
11. Apex (wiki.gg, current revision): walk 5.07 m/s, sprint 7.59 m/s (1.50x walk), crouch 2.34 m/s; a slide lasts about 1 s on flat ground [S].
12. Destiny 2 (pre-Edge of Fate): base sprint about 8.0 to 8.3 m/s; walk 5.0 m/s at mobility tier 0 and 7.0 m/s at tier 10; strafe about 85% of walk [S].
13. Quake 3 step smoothing: the view drops by stepChange x (200 ms - elapsed) / 200 ms, linear over STEP_TIME = 200 ms [V code].
14. Thumbstick dead zones (XInput): left 7849 and right 8689 on the +/-32767 scale, i.e. about 24% and 27% of travel; Microsoft's guidance is a circular (magnitude) test [V constants, S guidance].
15. Not found with usable numbers: Titanfall 2 slide/wall-run/double-jump/mantle values; Halo, Destiny, Mirror's Edge and Dying Light ledge heights; sprint FOV for Halo, Destiny, Apex and CoD; Tribes ski speeds; Journey/Exo One/BotW surfing numbers; Swink's ADSR thresholds (book not retrieved).

---

## 1. Quake / Source movement model

### 1.1 Formulas (verified code)

Quake 3 `PM_Accelerate` (bg_pmove.c, active `#if 1` branch) [V]:
```
currentspeed = DotProduct(velocity, wishdir);
addspeed     = wishspeed - currentspeed;
accelspeed   = accel * frametime * wishspeed;
if (accelspeed > addspeed) accelspeed = addspeed;
velocity    += accelspeed * wishdir;
```
Source `CGameMovement::Accelerate` [V, mirror link above]:
```
currentspeed = mv->m_vecVelocity.Dot(wishdir);
addspeed     = wishspeed - currentspeed;
accelspeed   = accel * gpGlobals->frametime * wishspeed * player->m_surfaceFriction;
if (accelspeed > addspeed) accelspeed = addspeed;   // clamp line present in ground version
```
Quake 3 `PM_Friction` (ground) [V]:
```
if (speed < 1) { velocity = 0; return; }            // Q3 cutoff: 1 u/s
control = speed < pm_stopspeed ? pm_stopspeed : speed;
drop   += control * pm_friction * frametime;
newspeed = max(speed - drop, 0) / speed  (scale velocity)
```
Source `CGameMovement::Friction` [V]:
```
speed = |velocity|;
if (speed < 0.1f) ...                                // Source cutoff: 0.1 u/s
friction = sv_friction * player->m_surfaceFriction;
control  = (speed < sv_stopspeed) ? sv_stopspeed : speed;
drop    += control * friction * frametime;
```
Source `CGameMovement::AirAccelerate` [V, excerpt]:
```
if (wishspd > 30) wishspd = 30;                      // cap applies to wish speed used in addspeed
currentspeed = velocity.Dot(wishdir);
addspeed     = wishspd - currentspeed;
accelspeed   = accel * wishspeed * frametime * m_surfaceFriction;   // uncapped wishspeed here
// [U] the excerpt did not show "if (accelspeed > addspeed)"; assumed present as in the ground version
velocity += accelspeed * wishdir;
```

Why air strafing works [D]: the cap compares the projection of velocity onto the wish direction (v dot w) with 30. If you hold a direction perpendicular to your velocity, v dot w is near 0, so each tick adds up to 30 u along that direction. The total speed |v| therefore grows even though the forward projection is capped. In Quake 3 there is no 30 cap, so the added amount per tick is limited only by wishspeed - v dot w (cap at 320 along the input direction).

Ground time constants (continuous approximation, W = 320 u/s; friction applies only when grounded) [D]:

| Quantity | Quake 3 (a=10, f=6, stop=100) | CS:GO-style (a=5.5, f=5.2, stop=80) |
|---|---|---|
| Top speed | 320 u/s = 8.13 m/s | 320 u/s = 6.10 m/s |
| Accel below cap (a x W) | 3200 u/s^2 = 81.3 m/s^2 | 1760 u/s^2 = 33.5 m/s^2 |
| Friction decel at top speed (f x W) | 1920 u/s^2 = 48.8 m/s^2 | 1664 u/s^2 = 31.7 m/s^2 |
| Time to 90% top speed (with friction) | 0.13 s | 0.37 s |
| Time to 90% top speed (no friction) | 0.09 s | 0.16 s |
| Time top speed to 0 (no input) | 0.36 s | 0.46 s |
| Stop-speed cutoff used | 100 u/s = 2.54 m/s | 80 u/s = 1.52 m/s |

Working for the time-to-90% (friction on): v(t) = (aW/f)(1 - e^(-f t)) until the cap; Quake 3 aW/f = 533 u/s, so v = 288 u/s at t = 0.13 s; CS-style aW/f = 338 u/s, so t = 0.37 s. Stop: Quake 3 from 320 to 100 u/s takes ln(3.2)/6 = 0.19 s, then 100/600 = 0.17 s; CS-style from 320 to 80 takes ln(4)/5.2 = 0.27 s, then 80/416 = 0.19 s.

Air gain (frame-rate effect) [D]:
- Quake 3: 1 x 320 = 320 u/s^2 = 8.1 m/s^2 on the input axis (per second, independent of tick rate).
- Source with a = sv_airaccelerate, wishspeed 320 u/s (wish speed is 320 once you hold a direction): per tick the gain is min(a x 320 x dt, 30 - v dot w). Per second on a zero-projection axis, this becomes min(a x 320, 30 x tickrate):
  - CS:GO (a = 12): min(3840, 30 x 66.7 = 2000) u/s^2 = 38.1 m/s^2 at 66.7 tick; the accel term caps at 3840 u/s^2 = 73.1 m/s^2 at 128 tick or higher [D].
  - The per-tick clamp binds below about 128 Hz for a=12, which means air strafe gain is tickrate-dependent. The TF2 ClassicMovement README says the same thing about tickrate effects on air movement [S].

### 1.2 Default values per game (cvars)

| Game | sv_accelerate | sv_airaccelerate | sv_friction | sv_stopspeed | Max ground speed | Gravity | Jump | Source |
|---|---|---|---|---|---|---|---|---|
| Quake 3 | pm_accelerate 10 [V] | pm_airaccelerate 1 [V] | pm_friction 6 [V] | pm_stopspeed 100 [V] | g_speed 320 u = 8.13 m/s [S] | DEFAULT_GRAVITY 800 u = 20.3 m/s^2 [V] | JUMP_VELOCITY 270 u/s = 6.86 m/s [S, define NOT FOUND in bg_public.h or bg_pmove.c] | bg_pmove.c [V]; g_speed: https://forums.insideqc.com/viewtopic.php?p=33424 (search snippet); https://www.giantbomb.com/wd/3015-652/ (search snippet) |
| Quake 1 / QuakeWorld (ezQuake manual) | 10 [S] | 10 [S] | 4 [S] | NOT FOUND | NOT FOUND | 800 [S] | 270 [S, Quake II FAQ and QuakeWorld forum snippets, not Quake 1] | https://www.quaddicted.com/files/webarchive/ezquake.sourceforge.net_20121129/docs/index.html%3Fvars-serverside-physics.html ; gravity: https://quakewiki.org/wiki/Sv_gravity ; jump: https://speeddemosarchive.com/quake/qdq/articles/q2dq2_FAQ.html (search snippet, Quake II) |
| Half-Life 2 (SDK) | NOT FOUND | NOT FOUND | NOT FOUND | NOT FOUND | NOT FOUND | 600 [U, from HL2 jump constant] | Jump height 21 u (code comment "approx. 21 units") [V] | gamemovement.cpp mirror (link above) |
| Counter-Strike: Source | NOT FOUND | NOT FOUND | 4 [S] | NOT FOUND | NOT FOUND | NOT FOUND | NOT FOUND | Valve wiki Sv_friction (fetch blocked, snippet only): https://developer.valvesoftware.com/wiki/Sv_friction |
| CS:GO / CS2 | 5.5 [S] (same site also says 5.6 in its intro text) | 12 [S] (allowed range 0 to 25 per one page) | 5.2 [S] | 80 [S] | 320 u = 6.10 m/s [U, memory] | 800 [S] | sv_jump_impulse 301.993 [U, not found in this pass] | Friction: https://totalcsgo.com/commands/svfriction ; Stopspeed: https://totalcsgo.com/commands/svstopspeed ; Accel: https://totalcsgo.com/commands/svaccelerate ; Air accel: https://csdb.gg/commands/sv_airaccelerate ; Gravity: https://totalcsgo.com/command/svgravity |
| Team Fortress 2 | NOT FOUND | 10 [S] (plugin README stated default) | NOT FOUND | NOT FOUND | Ground speed cap 520 u = 9.91 m/s, not changeable [S]; Spy base 320 u = 6.10 m/s [S] | NOT FOUND | NOT FOUND | https://github.com/mphe/TF2-ClassicMovement (README) |
| Titanfall 2 / Apex | Not Source-cvar based; see sections 2 and 6 | | | | | | | |

Notes on the table:
- The CS:GO sv_accelerate conflict (5.5 vs 5.6) is on the same totalcsgo.com site; both are reported, the table uses 5.5.
- A QuakeWorld analysis (search snippet, https://quakeworld.nu/wiki/QW_physics_air) says sv_airaccelerate had no effect in the code it examined [S, unverified].
- The Valve wiki's sv_stepsize page exists (https://developer.valvesoftware.com/wiki/Sv_stepsize) but could not be fetched (403); its value is 18 [U, memory].

### 1.3 Time, speed and distance rules for the table above

Metric conversions [D]: 320 u = 8.13 m/s (Q3); 320 u = 6.10 m/s (Source with 1.905 cm); 270 u/s = 6.86 m/s (Q3 jump); 301.993 u/s = 5.75 m/s (CS:GO impulse, [U]).

---

## 2. Titanfall 2 and Apex Legends pilot / legend movement

### 2.1 Apex Legends (secondary; wiki.gg current revision unless noted)

| Item | Value | Status | Source |
|---|---|---|---|
| Walk | 5.07 m/s (199.53 in/s raw) | [S] | https://apexlegends.wiki.gg/wiki/Sliding (Movement page content) |
| Sprint | 7.59 m/s (299 in/s raw); sprint/walk = 1.50 | [S] | same |
| Crouch walk | 2.34 m/s (92 in/s raw) | [S] | same |
| Weapon held | -13% movement speed; regen item -40% and blocks sprint | [S] | same |
| Holstered run | about 7.4 m/s; armed sprint 6.5 m/s (T_status, cited by GameRevolution) | [S] | https://www.gamerevolution.com/?p=494517 (search snippet) |
| Older Fandom values | walk 3.8 m/s, sprint 5.7 m/s, crouch 1.75 m/s (older revision, conflicts) | [U] | https://apexlegends.fandom.com/wiki/Movement (fetch returned 402; snippet only) |
| Slide duration on flat ground | about 1 s before deceleration (sprint slide) | [S] | apexlegends.wiki.gg Sliding page |
| Slide initial speed | depends on entry speed and slope; no number | NOT FOUND | same |
| Slide friction | "friction-based", no coefficient | NOT FOUND | same |
| Slide on slope | downhill extends slide; steep angles can turn deceleration into acceleration; no numbers | [S] | same |
| Slide-jump | conserves momentum longer than a sprint jump; no numbers | [S] | same |
| Slide boost / mantle-jump initial speed | 10.3 m/s (jump-and-crouch during mantle); source is wiki.gg | [S] | apexlegends.wiki.gg Movement page (stated as "super glide", from a wall-climb launch, not a slide) |
| Wall climb | hold jump on a vertical surface, climb about 6 m | [S, snippet] | search snippet from apex wiki page (not fetched) |
| Mantle height and duration | NOT FOUND | NOT FOUND | - |
| Air strafe / air control numbers | NOT FOUND | NOT FOUND | - |

Derived [D]: Apex sprint is 1.50 x walk, crouch is 0.46 x walk.

### 2.2 Titanfall 2 (retail numbers NOT FOUND)

- Walk, sprint, crouch speeds: NOT FOUND.
- Slide entry boost, friction ramp, duration, slope effects, slide-hop cooldown: NOT FOUND. The Titanfall wiki Slide Hop page exists (https://titanfall.fandom.com/wiki/Slide_Hop) but returned no numbers in search.
- Wall-run speed, double-jump velocity and rules, air control: NOT FOUND.
- 2016 tech-test patch notes (secondary, from a Giant Bomb forum repost, not an official Respawn page): "Increased max wall-running speed. Increased base wall-running speed and air speed. Increased acceleration while jumping off walls." Bunny-hop dampening was being scaled back. Pilot movement speed was lowered earlier to reduce erratic close-range movement. Source: https://www.giantbomb.com/forums/titanfall-2-686332/respawn-making-changes-to-titanfall-2-based-on-pla-1801347/ and https://gameinformer.com/b/news/archive/2016/08/24/respawn-is-making-changes-to-titanfall-2-based-on-player-concerns
- A student project (CSE 125) reproduces Titanfall-style constants in Quake units: double-jump upward speed 600, slide-hop jump speed 500, double-jump cooldown 0.10 s, slide-boost cooldown 2.0 s, slope influence 400. An older version of the same file lists a different jump speed (380 vs 660). These are not Respawn numbers. Source: https://cse125.ucsd.edu/2026/cse125g2/docs/TitanfallConstants_8hpp_source.html [U as Respawn values].

### 2.3 Bunny-hop and momentum

- Friction in both engines applies only while grounded (section 1), so a tick spent airborne skips the friction drop. This is the mechanism behind bunny hopping in Quake and Source. The order of jump check vs friction in bg_pmove.c was not fetched [U]. Titanfall-specific dampening factors have no numbers in this pass: NOT FOUND.
- CS-style rule: lowering tickrate changes air strafe gain and ground frame counts (TF2 README: at 100 tick there is at most 1 ground frame; at 66 tick 3 to 5 consecutive jump frames) [S]. Source: https://github.com/mphe/TF2-ClassicMovement

### 2.4 Coyote time and jump grace (Titanfall, Apex)

NOT FOUND for Titanfall or Apex.

---

## 3. Jump feel conventions with numbers

### 3.1 Celeste (verified, released Player.cs; pixels and seconds)

Source: https://raw.githubusercontent.com/NoelFB/Celeste/master/Source/Player/Player.cs (the released file; the Source folder lists only Player/ and PICO-8/; repo https://github.com/NoelFB/Celeste; 80.lv article https://80.lv/articles/celestes-movement-code-available-on-github/ gives a 5400-line Player class).

| Constant | Value | Status | Use in code |
|---|---|---|---|
| JumpGraceTime (coyote) | 0.10 s | [V] | Set to 0.1 when grounded; decrements by DeltaTime; reset to 0 on jump |
| VarJumpTime (hold window) | 0.20 s | [V] | varJumpTimer = 0.2 on jump |
| CeilingVarJumpGrace | 0.05 s | [V] | Ends hold early on ceiling hit |
| JumpSpeed (launch) | -105 px/s (up) | [V] | Speed.Y = JumpSpeed |
| JumpHBoost | 40 px/s | [V] | Speed.X += 40 x moveX on jump |
| Gravity | 900 px/s^2 | [V] | Speed.Y approaches max by Gravity x mult x dt |
| HalfGravThreshold | 40 px/s | [V] | mult = 0.5 when abs(Speed.Y) < 40 and jump is held |
| MaxFall | 160 px/s | [V] | Terminal velocity without fast-fall |
| FastMaxFall | 240 px/s, FastMaxAccel 300 px/s^2 | [V] | Used when down is held and Speed.Y >= MaxFall |
| MaxRun | 90 px/s | [V] | Ground top speed |
| RunAccel | 1000 px/s^2 | [V] | Toward max x moveX |
| RunReduce | 400 px/s^2 | [V] | Used when abs(Speed.X) > max |
| AirMult | 0.65 | [V] | Multiplies RunAccel and RunReduce in air |
| Jump buffer duration | NOT FOUND in released code | NOT FOUND | Input.Jump.ConsumeBuffer() is called but the input class is not in the release |

Derived [D] from the Celeste constants (pixels):
- Jump apex with no hold: 105^2 / (2 x 900) = 6.1 px.
- Jump apex with full 0.2 s hold (rise pinned at -105 by Speed.Y = min(Speed.Y, varJumpSpeed)): 105 x 0.2 = 21 px during hold, then 6.1 px after = about 27 px.
- Release early: height drops by 105 px/s x (time left in hold). The pinning is what makes the hold feel like a variable jump.
- Ground: 0 to MaxRun in 90/1000 = 0.09 s (RunAccel only; friction branch not shown in the excerpt).

Qualitative only: Maddy Thorson's write-up says a short window after leaving a ledge allows a jump, and a press just before landing fires on landing [S, https://maddymakesgames.com/articles/celeste_and_forgiveness/index.html]. That article gives no numbers.

### 3.2 Super Mario 64 (verified, mario.c; scale NOT FOUND)

Source: https://raw.githubusercontent.com/n64decomp/sm64/master/src/game/mario.c (set_mario_action_airborne) [V].

| Action | Launch vel[1] | Status |
|---|---|---|
| ACT_JUMP / ACT_HOLD_JUMP | 42.0 (set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f)) | [V] |
| ACT_DOUBLE_JUMP | 52.0 | [V] |
| ACT_TRIPLE_JUMP | 69.0 | [V] |
| ACT_BACKFLIP | 62.0 | [V] |
| ACT_LAVA_BOOST | 84.0 | [V] |
| Emerge from pipe (cutscene) | 52.0 | [V] |

- Units: SM64 vel is units per frame; the game runs at 30 fps (TASVideos page: https://tasvideos.org/GameResources/N64/SuperMario64 [S]). So 42 u/frame = 1260 u/s before scale.
- Gravity per frame (-4) and terminal velocity (-75): NOT FOUND in the files fetched (mario.c and mario_actions_airborne.c). [U from memory]
- Jump height in metres: a University of Leicester paper (search snippet, https://journals.le.ac.uk/index.php/pst/article/download/4860/4886/17664) gives about 5 x Mario's height and a 0.5 s fall. [S, unverified]
- Press vs release: NOT VERIFIED in this pass.

### 3.3 Quake family and Source (derived from section 1)

| Game | Launch | Apex height | Air time | Gravity | Status |
|---|---|---|---|---|---|
| Quake 3 | 270 u/s = 6.86 m/s | 1.16 m | 0.675 s | 800 u/s^2 = 20.3 m/s^2 | [D]; 270 [S, define NOT FOUND] |
| CS:GO-style | 301.993 u/s = 5.75 m/s | 1.09 m | 0.755 s | 800 u/s^2 = 15.2 m/s^2 | [D]; 301.993 [U] |
| Half-Life 2 (SDK) | 160 u/s = 3.05 m/s | 0.41 m (21 u) | 0.53 s | 600 u/s^2 = 11.4 m/s^2 [U] | [D]; 21 u and 160 [V] |

HL2 derivation: flMul = 160 is "approx. 21 units" [V code comment]. Height = 160^2 / (2 x 600) = 21.3 u.

### 3.4 Halo (HEK reference, not metres)

Source: https://nikon.bungie.org/misc/hek_tutorial/references/general/halo_player_stats.html [S, game version not stated]. Jump heights are in generic 3ds Max units: standing 80, crouched walk 76, crouch-jump 99. Forward run 2.25 world units/s; backward and strafe 2.0 wu/s. Units cannot be converted to metres from this source. Halo Infinite (unofficial, Halopedia): run 2.6 wu/s, sprint about 1.11x, jump 2.5 wu/s x 1.29 global multiplier, gravity 1.14x (https://www.halopedia.org/Master_Chief_(gameplay), search snippet [S, unofficial tools]).

### 3.5 Other games

| Game | Numbers | Status |
|---|---|---|
| Destiny 2 | jump height depends on mobility (pre-EoF); no number found | NOT FOUND |
| Titanfall 2 | NOT FOUND | NOT FOUND |
| Mirror's Edge | NOT FOUND | NOT FOUND |
| Dying Light | ledge detection over 200 raycasts/frame (GDC Q&A, speaker Bartosz Kulon): https://gdconf.com/news/speaker-qa-bartosz-kulon-explains-turning-nba-dreams-game-dev-expertise | [S] |
| Hollow Knight | NOT FOUND | NOT FOUND |
| Super Mario Odyssey | NOT FOUND | NOT FOUND |

### 3.6 Jump-feel rules asked about

| Question | Answer | Status |
|---|---|---|
| Coyote time (ms) | 100 ms (Celeste) | [V] |
| Jump buffer (ms) | NOT FOUND in Celeste release. A third-party guide gives a 0.10 to 0.15 s coyote window (not a buffer figure) [S, https://bugnet.io/blog/coyote-time-and-input-buffering-explained]. Not a game source. | NOT FOUND (primary) |
| Variable jump height | Hold pins rise speed at launch speed for up to 0.2 s (Celeste). Release ends the hold. | [V] |
| Heavier gravity when falling | No: Celeste uses one gravity (900 px/s^2) and caps fall speed (160 / 240 px/s). Half gravity applies only near the apex while jump is held. | [V] |
| Apex hang | Celeste apex half-gravity below 40 px/s (about 0.5x) while jump is held. | [V] |
| Terminal velocity | Celeste 160 px/s; fast fall 240 px/s. Other games NOT FOUND. | [V] |
| Jump on press or release | Celeste: press starts the jump (Input.Jump.Pressed). SM64 and first-person games: not verified. | [V] for Celeste |

---

## 4. Sprint

| Game | Walk | Sprint | Sprint/walk | Strafe / back | Time to sprint | Sprint FOV | Source |
|---|---|---|---|---|---|---|---|
| Apex Legends | 5.07 m/s | 7.59 m/s | 1.50 | NOT FOUND | NOT FOUND | NOT FOUND | apexlegends.wiki.gg Movement page [S] |
| Destiny 2 (pre-EoF) | 5.00 m/s (tier 0) to 7.00 (tier 10) | about 8.0 to 8.3 m/s | about 1.6 at tier 0 | strafe 4.25 (tier 0) to 5.95 (tier 10); about 85% of walk | NOT FOUND | NOT FOUND | https://mein-mmo.de/en/destiny-2-mobilitaet-steigern,180842/ ; https://www.blueberries.gg/leveling/destiny-2-mobility/ [S] |
| Destiny 2 (post-EoF) | Mobility removed from visible stats; default equals mobility 30 (Titan/Warlock) or 40 (Hunter); base movement bug acknowledged | NOT FOUND | - | - | - | - | https://thegamepost.com/bungie-destiny-2-player-movement-speed-bugged-edge-of-fate-update/ [S] |
| Halo Infinite | 2.6 wu/s (run) | about 1.11x (Halopedia, unofficial) | about 1.11 | NOT FOUND | NOT FOUND | NOT FOUND | https://www.halopedia.org/Master_Chief_(gameplay) [S] |
| Halo (HEK, CE era) | run 2.25 wu/s | NOT FOUND | - | backward and strafe 2.0 (0.89 x forward) | NOT FOUND | NOT FOUND | https://nikon.bungie.org/misc/hek_tutorial/references/general/halo_player_stats.html [S] |
| CoD MW tactical sprint, Mirror's Edge Catalyst, Dying Light, Death Stranding, Breath of the Wild, Skyrim | NOT FOUND in this pass | - | - | - | - | - | Not searched individually |

Sprint FOV: searched for Halo, Destiny, Apex and CoD; NOT FOUND. Avatar: Frontiers of Pandora has a sprint FOV toggle (base FOV 80, range 60 to 105) [S, search snippet, not fetched]. Fortnite has a sprint-only FOV slider [S, search snippet]. Minecraft widens FOV while sprinting and adds 0.5 to 1 block of reach [S, search snippet].

Toggle vs hold / auto-sprint conventions: NOT FOUND with numbers.

Quake / Source (derived for comparison, section 1): ground top speed 8.13 m/s (Q3) and 6.10 m/s (CS:GO style); there is no separate sprint in these engines.

---

## 5. Slopes and sliding as traversal

| Game | Mechanic | Numbers | Source | Status |
|---|---|---|---|---|
| Tribes (Ascend) | Friction is removed while the key is held, so speed grows on descents | Speed cap removal is a patch note in a forum archive (secondhand). Skiing numbers NOT FOUND (Tribes wiki fetch returned 402). | https://tribes.fandom.com/wiki/Skiing (not fetched); archived patch excerpt: https://www.beyondgaming.be//archive/games-general.91/tribes-ascend.800697?page=4 | [S] |
| Journey | Sand dunes are sliding terrain with physically plausible sand | NOT FOUND (GDC talk by Jenova Chen not found) | https://www.pushsquare.com/news/2012/02/interview_thatgamecompany_journey (search snippet) | [S, qualitative] |
| Apex / Titanfall slides | Downhill extends slides; steep slopes turn deceleration into acceleration | NOT FOUND | apexlegends.wiki.gg Sliding page | [S] |
| Breath of the Wild shield surfing | NOT FOUND | NOT FOUND | - | NOT FOUND |
| Exo One / Sable dunes | NOT FOUND | NOT FOUND | - | NOT FOUND |
| GDC on why slopes feel good | NOT FOUND | NOT FOUND | - | NOT FOUND |

---

## 6. Ledges and steps

### 6.1 Step smoothing (Quake 3, verified)

Source: https://raw.githubusercontent.com/ioquake/ioq3/master/code/cgame/cg_view.c and https://raw.githubusercontent.com/ioquake/ioq3/master/code/cgame/cg_local.h [V].

```
// cg_view.c, CG_StepOffset
timeDelta = cg.time - cg.stepTime;
if (timeDelta < STEP_TIME)
    cg.refdef.vieworg[2] -= cg.stepChange * (STEP_TIME - timeDelta) / STEP_TIME;
```
- STEP_TIME = 200 ms [V, cg_local.h].
- MAX_STEP_CHANGE = 32 u [V define; usage in clamp NOT FOUND in this pass].
- The offset is linear over 200 ms and is applied to the view Z only (first person; not third person) [V].
- Where cg.stepChange and cg.stepTime are set (pm/predict code) was not fetched [U].

Quake 1 view code (search snippet from InsideQC, https://forums.insideqc.com/viewtopic.php?p=48620): stair smoothing capped at 12 units with rate 80; classic Quake used 160 [S, Quake 1 not Quake 3].

### 6.2 Other engines and games

| Item | Value | Status | Source |
|---|---|---|---|
| cl_smoothstairs (Source) | NOT FOUND (no convar page located) | NOT FOUND | search only |
| Source sv_stepsize | 18 u | [U, memory; Valve wiki not fetched (403)] | https://developer.valvesoftware.com/wiki/Sv_stepsize (snippet) |
| Quake 3 STEPSIZE | 18 u | [U, memory] | - |
| Unreal CharacterMovement MaxStepHeight | 45 cm; WalkableFloorAngle about 44.8 deg | [U, memory; Epic docs fetch returned only a table of contents] | https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/UCharacterMovementComponent (not usable) |
| Apex climb | Wall climb about 6 m, then mantle | [S, snippet] | apex wiki snippet |
| Dying Light ledge detection | Over 200 raycasts per frame (performance cost); the team first placed about 50,000 invisible ledge hooks on one map | [S] | https://gdconf.com/news/speaker-qa-bartosz-kulon-explains-turning-nba-dreams-game-dev-expertise ; https://techradar.com/features/dying-light-2s-head-programmer-replaced-50000-ledges-with-a-magical-one |
| Mirror's Edge | Ledge grab prevented by carrying a weapon heavier than a pistol (Wikipedia snippet); no numbers | [S] | https://en.wikipedia.org/wiki/Mirror%27s_Edge |
| Halo Infinite clamber, Destiny mantle | NOT FOUND | NOT FOUND | - |

---

## 7. Game-feel frameworks

| Item | Finding | Status | Source |
|---|---|---|---|
| Swink, Game Feel (2008): ADSR on movement; "tight" vs "floaty" | Book not retrieved; the six-area breakdown (input, response, context, polish, metaphor, rules) is from Wikipedia | [S] for structure; ADSR numbers NOT FOUND | https://en.wikipedia.org/wiki/Game_feel |
| Swink: 100 ms correction cycle | NOT FOUND in the book; secondary sources cite 100 ms | [U] for attribution | https://gamedesignskills.com/game-design/game-feel/ (Brazie: keep action-to-result under 100 ms where possible) |
| Human reaction time | About 240 ms (Bayreuth lecture) | [S] | https://medienwissenschaft.uni-bayreuth.de/wp-content/uploads/GxD-07-Interfaces.pdf |
| Mark Brown, GMTK "Secrets of Game Feel and Juice" | Video exists; no numbers retrieved | [S] existence | https://amara.org/v/C3BGA (subtitle host page) |
| Jan Willem Nijman, "The Art of Screenshake" | Talk exists (about 40 min); venue not verified. Blog summary of Nuclear Throne pistol shot: camera kicked 6 px; screenshake +4; decays quickly. Attribution of the blog post is uncertain. | [S] | https://infovore.org/?p=5275 (candidate, not fetched) |
| "Trauma squared" shake formula | NOT FOUND | NOT FOUND | - |
| Jonasson and Purho, "Juice it or lose it" (2012) | Talk exists | [S] | search snippet |
| Seth Coster (controller feel, GDC) | NOT FOUND | NOT FOUND | - |
| Activision, "Controller to Display Latency in Call of Duty" (GDC 2019, Akimitsu Hogge) | Method: high-speed camera with LED-equipped controller, or photodiode closed loop; no ms figure seen in snippets | [S] | https://gdcvault.com/play/1025968/Controller-to-Display-Latency-in |
| Bungie / Titanfall GDC talks on movement feel | NOT FOUND | NOT FOUND | - |

---

## 8. Analog stick

| Item | Value | Status | Source |
|---|---|---|---|
| XInput left thumbstick dead zone | 7849 of 32767 = 24.0% | [V constant, S value via Wine xinput.h and search] | https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad (names constants, range -32768 to 32767) ; https://goma.googlesource.com/wine/+/refs/tags/wine-1.1.8/include/xinput.h |
| XInput right thumbstick dead zone | 8689 of 32767 = 26.5% | [V constant, S value] | same |
| XInput trigger threshold | NOT FOUND (constant exists; value not read) | NOT FOUND | - |
| Circular dead zone | Check whether stick magnitude exceeds the dead zone | [S; Microsoft getting-started guide, quoted by snippet] | https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput |
| Rescale past dead zone | Zeroing values just outside the zone causes a jump; rescale the range past the zone for smooth response | [S, forum] | https://forums.ogre3d.org/viewtopic.php?p=451339 |
| Shooter dead zone | 0.03 to 0.05 minimum (community site) | [U, low quality] | search snippet |
| CoD: WW2 built-in dead zone | About 10 to 15% (user estimate) | [U, anecdotal] | search snippet |
| Response curves (linear, exponential, dynamic) | Descriptions only (calculator site) | [S, qualitative] | https://west-games.com/analog-stick-response-curve-calculator/ |
| Walk / run thresholds for well-regarded games | NOT FOUND | NOT FOUND | - |

---

## Derived numbers for our planet (g = 10.5 m/s^2, moon g = 3.2 m/s^2)

Formulas: launch v = sqrt(2 g h); air time t = 2v/g [D].

| Target apex | v at 10.5 m/s^2 | air time at 10.5 | v at 3.2 m/s^2 | air time at 3.2 |
|---|---|---|---|---|
| 1.0 m | 4.58 m/s | 0.87 s | 2.53 m/s | 1.58 s |
| 1.2 m | 5.02 m/s | 0.96 s | 2.77 m/s | 1.73 s |
| 1.5 m | 5.61 m/s | 1.07 s | 3.10 m/s | 1.94 s |

For comparison, Quake 3 (g = 20.3 m/s^2) gives 1.16 m with 0.675 s air time, and CS:GO-style (g = 15.2 m/s^2, since 800 u at 1.905 cm) gives 1.09 m with 0.755 s [D].

---

## Not found, unverified, and failed fetches

Not found (searched, no usable number):
- Titanfall 2: walk, sprint, crouch, slide, wall-run, double-jump, mantle, air control, coyote and buffer.
- Apex: slide friction coefficient, mantle height and duration, air strafe numbers.
- Sprint FOV: Halo, Destiny, Apex, CoD.
- Sprint speeds for CoD tactical sprint, Mirror's Edge Catalyst, Dying Light, Death Stranding, BotW, Skyrim.
- Mirror's Edge, Dying Light, Halo Infinite ledge numbers.
- Tribes ski speeds (wiki returned 402); Journey / Exo One / BotW surfing numbers; GDC talks on slopes.
- Swink ADSR thresholds (book not retrieved).
- Seth Coster controller-feel talk; Bungie movement-feel talks.
- Analog response-curve numbers and walk/run thresholds.
- Source cvars for HL2, CS:S, TF2 (accel, friction, stopspeed), cl_smoothstairs.
- Quake 3 JUMP_VELOCITY define (270 from secondary sources only).
- SM64 gravity (-4) and terminal velocity (-75); SM64 scale in metres.
- Unreal CharacterMovement default values (docs fetch gave only a table of contents).

Unverified (memory or conflicting):
- CS:GO sv_jump_impulse 301.993; CS:GO sv_maxspeed 320; sv_stepsize 18.
- CS:GO sv_accelerate 5.5 vs 5.6 (same site, conflicting).
- Quake 3 MAXS_Z 32 (player box height 56 u).
- HL2 gravity 600.
- Destiny sprint 8.0 vs 8.3 m/s (two sources).
- Apex older Fandom speeds (3.8 / 5.7 / 1.75 m/s) vs wiki.gg current (5.07 / 7.59 / 2.34).

Failed or blocked fetches (this session): Valve developer wiki (403), csdb.gg sv_friction (429) and sv_stopspeed (403), Apex Fandom Movement (402), Tribes Fandom Skiing (402), Gamespot Celeste article (403), Source SDK GitHub path (404 on master and main; mirror used instead), Unreal docs (table of contents only).
