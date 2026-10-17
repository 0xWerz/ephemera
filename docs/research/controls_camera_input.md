# Controls, camera and input research for a UE 5.8 first-person game

Date of research: 2026-10-13. Scope: mouse look, UE5 input and latency, gamepad look stick, procedural first-person camera, spring maths, comfort/accessibility, low frame-rate responsiveness.

Confidence tags used below:
- [V] verified: text or code fetched from the primary page or source file during this research (URL given).
- [S] search excerpt only: the claim appeared in a search result snippet but the page could not be fetched in full (or it was a 403/404).
- [C] community or commercial source (forums, fan wikis, tools sites). Treat as a rough guide.
- [D] derived: my arithmetic or derivation from a verified formula. Not a published number.
- [U] unverified / not found: no source located in this research.

---

## Most useful findings

1. Quake's mouse path is `yaw -= m_yaw * (counts * sensitivity)` with `m_yaw = 0.022`, `sensitivity = 3` by default, so degrees per count = m_yaw x sensitivity, independent of frame time [V in_win.c, cl_main.c].
2. Source/CS convention: m_yaw default 0.022 deg per count at sensitivity 1 [S csdb.gg / totalcsgo.com; Liquipedia snippet]. At 800 DPI and sensitivity 1 that is 17.6 deg per inch and 51.9 cm per 360 [D].
3. cm/360 = 360 x 2.54 / (DPI x yaw x sensitivity), using yaw = 0.022 for Source/CS, 0.07 for Valorant (community) [D, C].
4. FOV-aware sensitivity (pixel matching): dtheta_new / dtheta_ref = tan(FOV_new/2) / tan(FOV_ref/2) [D from pinhole model; the monitor-distance-match concept is from mouse-sensitivity.com forum posts [C], not an official formula].
5. Horizontal/vertical FOV: hFOV = 2 atan( tan(vFOV/2) x W/H ). Example: 90 deg horizontal at 16:9 is 58.7 deg vertical [C mouse-sensitivity forum excerpt; formula checked by D].
6. UE5 Enhanced Input dead zones: UInputModifierDeadZone remaps LowerThreshold..UpperThreshold to 0..1 and clamps the rest; EDeadZoneType = Axial, Radial ("Smoothed Radial"), UnscaledRadial [V Epic docs, UE 5.8].
7. Scaled-radial dead zone (Sutphin, 2013): m' = (m - d) / (1 - d), direction preserved; size 0.1-0.2 typical, 0.25 in his code [V gamedeveloper.com].
8. Epic's low-latency frame syncing (30 Hz example, rhi.SyncInterval 2): default sync can reach up to 130 ms worst-case input latency; r.GTSyncType 2 with r.OneFrameThreadLag 1, r.VSync 1 and rhi.SyncSlackMS 0 gives about 66 ms best case [V Epic docs, UE 5.8].
9. NVIDIA Reflex is built into UE5 and enabled from the editor plugin section. The page names no console variable, so t.ReflexMode and r.NVIDIA.Reflex are unverified [V developer.nvidia.com/UNrealengine].
10. AMD Anti-Lag 2 UE plugin exposes r.AMD.AntiLag2 and supports RDNA1+ (RX 5000+). No latency numbers published in the sources found [S gpuopen.com].
11. Meta's late latching removes "up to one additional frame" of pose latency by sampling at the end of the render thread (VR, not mouse-look, but the same principle) [V developers.meta.com].
12. Half-life damping (Holden): x <- lerp(x, goal, 1 - 2^(-dt/halflife)). Exponential form (Driscoll): x <- lerp(x, goal, 1 - exp(-lambda dt)), lambda = ln 2 / halflife [V both].
13. Exact critically damped spring (Holden): damping d = 4 ln2 / halflife, stiffness s = d^2/4. Juckett gives closed forms for under-, critically and over-damped cases [V both].
14. Unity Mathf.SmoothDamp (from Game Programming Gems 4, ch. 1.10 per Unity source comment): omega = 2/smoothTime, x = omega dt, exp = 1/(1 + x + 0.48x^2 + 0.235x^3). Approximation is stable but is not exact for large dt [V UnityCsReference].
15. Quake 3 stair smoothing: offset = stepChange x (STEP_TIME - timeDelta)/STEP_TIME; the STEP_TIME value itself was not seen in the fetched excerpt [V formula, U value].

---

## 1. Mouse look

### 1.1 Conventions and numbers

| Item | Value | Source / tag |
|---|---|---|
| Quake m_yaw / m_pitch default | 0.022 deg per count (after sensitivity) | [V] https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/cl_main.c |
| Quake sensitivity default | 3 | [V] same file |
| Quake m_filter default | 0 (averages current and previous count if 1) | [V] https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/in_win.c |
| Source/CS m_yaw default | 0.022 | [S] https://csdb.gg/command/m-yaw/ ; https://totalcsgo.com/commands/myaw |
| Source/CS m_pitch | 0.022, sign inverted when inverted | [S] Liquipedia excerpt https://liquipedia.net/counterstrike/Mouse_Settings (page 403 on fetch) |
| m_rawinput | Ignores OS pointer acceleration; CS2 reportedly has it always on and the cvar removed | [S] Liquipedia excerpt; Steam CS2 discussion (conflicting reports, unverified) |
| Valorant yaw per count | 0.07 deg at sensitivity 1 | [C] https://www.tabnews.com.br/OVitor/como-calcular-edpi-e-cm-360-a-matematica-da-sensibilidade-entre-jogos (search excerpt; several tools sites agree) |
| Apex yaw per count | 0.022 | [C] search excerpt (tools sites) |
| Overwatch 2 yaw per count | 0.0066 | [C, low trust] search excerpt; no primary source |
| Quake default at 800 DPI (sens 3, yaw 0.022) | 0.066 deg/count; 360 deg = 17.3 cm | [D] |
| Source/CS at 800 DPI, sens 1 | 0.022 deg/count; 17.6 deg per inch; 51.9 cm per 360 | [D] |
| Valorant 800 DPI, sens 0.4 | 40.8 cm per 360; eDPI 320 | [D] formula checked against https://www.tabnews.com.br/OVitor/como-calcular-edpi-e-cm-360-a-matematica-da-sensibilidade-entre-jogos (Portuguese tutorial, [C]) |

Typical cm/360 that players use (all [C], low trust, no sample sizes):
- One source: most pros' ideal sensitivity 25-50 cm/360 [C] https://recharge.com (commercial blog, search excerpt).
- Valorant pro average about 45 cm/360, eDPI about 267 [C] (same commercial blog, 2025).
- Apex 26.8 cm and Overwatch 24.9 cm per 360 from one undated page [C] (search excerpt).
- No independent survey with sample sizes was found [U].

### 1.2 Formulas

Mouse-to-angle (Quake code, verified):
```
mouse_x = counts_x * sensitivity          // cl.viewangles unchanged until here
yaw   -= m_yaw   * mouse_x                 // m_yaw = 0.022
pitch += m_pitch * mouse_y                 // m_pitch = 0.022
sidemove += m_side * mouse_x               // m_side = 0.8
```
Source: https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/in_win.c

Degrees per count:
```
deg_per_count = m_yaw * sensitivity          (Source/CS/Quake convention)
cm_per_360    = (360 / deg_per_count) / DPI * 2.54  =  914.4 / (DPI * m_yaw * sensitivity)
edpi          = DPI * sensitivity
```
Derivation [D] from the verified Quake code plus the 2.54 cm/inch constant. The Portuguese tutorial [C] uses the same expression.

### 1.3 FOV and sensitivity

- Pixel-matching (monitor-distance-style) scaling, derived from a pinhole camera with focal length f = (W/2)/tan(hFOV/2): keeping on-screen pixels per mouse count constant requires dtheta proportional to 1/f, so
  ```
  sens_new = sens_ref * tan(FOV_new/2) / tan(FOV_ref/2)     [D]
  ```
  The concept (match the physical screen distance) is described on mouse-sensitivity.com [C] https://www.mouse-sensitivity.com/forums/topic/747-formulas-for-converting-from-different-fov-types/ ; that site says its calculators use only FOV and ignore camera distance.
- Axis conversion [C forum excerpt]: vertical-to-horizontal matching scales by aspect ratio; 100% vertical distance match equals 56.25% horizontal at 16:9.
  ```
  hFOV = 2 * atan( tan(vFOV/2) * W/H )      // verified by the example 90 h -> 58.7 v at 16:9
  vFOV = 2 * atan( tan(hFOV/2) * H/W )
  ```
- Games differ: PUBG changes cm/360 with FOV of the aiming mode; Warzone keeps hip-fire cm/360 fixed and changes tracking speed; Devil Daggers keeps cm/360 unchanged with FOV [C] (mouse-sensitivity.com forum excerpt).
- UE5 has a built-in FOV-dependent scale. See section 2.4.

### 1.4 Sub-count accumulation, frame-rate and pitch clamp

- Quake accumulates raw deltas between frames in `mx_accum` / `my_accum` (IN_Accumulate) and adds them at the next IN_MouseMove, so no sub-count movement is lost between frames [V in_win.c].
- Mouse look in Quake is per count, not per second. A given hand movement produces the same angle at 22 fps or 60 fps; only the latency of the update changes with frame rate [D from V code].
- Pitch clamp in Quake's angle adjustment: pitch max 80, min -70 deg; roll clamped to +/-50 [V cl_input.c, fetched excerpt https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/cl_input.c]. Modern shooters commonly clamp near +/-89 [U, not sourced in this run].
- Keyboard turn in Quake (per-second rates): cl_yawspeed 140, cl_pitchspeed 150, multiplied by a per-frame `speed` term [V cl_input.c; the `speed` definition is not in the excerpt, so frame-time scaling is an inference, U].

### 1.5 What raw input means in practice

- Raw input = no OS pointer acceleration, no smoothing, no per-frame averaging. Source's m_rawinput does this; the Quake code above does not use raw deltas but uses window-centre recentering.
- Mouse smoothing and acceleration in UE are controlled by UInputSettings (section 2.1). Community posts report that setting bEnableMouseSmoothing=False and bViewAccelerationEnabled=False in Input.ini removes smoothing and acceleration [C, mixed reports] https://steamcommunity.com/app/1475810/discussions/0/3177859358960451419 ; bViewAccelerationEnabled is not in the UE 5.8 UInputSettings docs [V absent].

---

## 2. Unreal Engine 5 input and latency

### 2.1 Enhanced Input and UInputSettings (UE 5.8 docs)

| Property | Type | Documented text | Default |
|---|---|---|---|
| bEnableMouseSmoothing | bool | "Mouse smoothing control." | not stated [V https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UInputSettings] |
| bEnableFOVScaling | bool | "Scale the mouse based on the player camera manager's field of view." | not stated in docs; a 2019 forum post says the game default is True [C] https://forums.unrealengine.com (UserInput.ini) |
| FOVScale | float | "The scaling value to multiply the field of view by." | not stated |
| bUseMouseForTouch | bool | "Allow mouse to be used for touch." | not stated |
| bViewAccelerationEnabled | - | not present in 5.8 UInputSettings page | [U] |
| bDisableMouseAcceleration | - | not present in 5.8 UInputSettings page | [U] |

- Enhanced Input equivalent: UInputModifierFOVScaling (FOVScale, FOVScalingType). EFOVScalingType: Standard = "FOV scaling to apply scaled movement deltas to inputs dependent upon the player's selected FOV"; UE4_BackCompat = "FOV scaling was incorrectly calculated in UE4's UPlayerInput::MassageAxisInput" and "should not be used by new projects" [V https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/EnhancedInput/EFOVScalingType ; UInputModifierFOVScaling page]. The FOV scale formula is not published [U].
- Viewport: bCaptureMouseOnLaunch, DefaultViewportMouseCaptureMode, DefaultViewportMouseLockMode [V UInputSettings page, ViewportProperties].

### 2.2 High-precision (raw) mouse in Slate

- FReply::UseHighPrecisionMouseMovement(TSharedRef<SWidget> InMouseCaptor): "Enables the use of high precision (raw input) mouse movement, for more accurate mouse movement without mouse ballistics." It "implies mouse capture and hidden mouse movement". Releasing capture deactivates it [V https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FReply/UseHighPrecisionMouseMovement, UE 5.8].
- FSlateApplication::ResetToDefaultInputSettings turns high-precision input off [S, Epic API excerpt].
- `SetUseHighPrecisionMouseMode` on FSlateApplication or UGameViewportClient: not found [U]. A raw-input plugin for UE5 was not found [U].
- Mouse-delta bugs reported in UE forums (cursor visible/hidden, zero at screen edge) [C, old UE 4.8 thread].

### 2.3 Latency cvars (Epic "Low-latency frame syncing", UE 5.8 docs)

| CVar | Value / default stated | Meaning [V] |
|---|---|---|
| r.GTSyncType | 0 default ("old behaviour") | 0 = game thread syncs with render thread; 1 = game thread syncs with RHI thread; 2 = game thread syncs with swap-chain present +/- offset (ms) |
| r.OneFrameThreadLag | on by default | game thread cannot get more than one frame ahead of render thread |
| rhi.SyncInterval | examples use 1 (60 Hz) and 2 (30 Hz) | sync interval in vblanks |
| rhi.SyncSlackMS | examples use 0 and 10 | offset applied to predicted vsync time; lower = less latency, fewer hitch cushion; docs say reduce "as much as possible whilst maintaining acceptable frame rates" |
| r.VSync (r.Vsync in example) | 1 in example | needed for predictable swap interval |
| r.GTSyncType 2 fallback | on unsupported platforms falls back to mode 1 | |

Epic's numbers [V https://dev.epicgames.com/documentation/en-us/unreal-engine/low-latency-frame-syncing-in-unreal-engine]:
- 30 Hz title, rhi.SyncInterval 2, default sync: worst case "up to 130 ms".
- Same title with r.GTSyncType 2, r.OneFrameThreadLag 1, r.VSync 1, rhi.SyncSlackMS 0: best case "~66 ms (two 30 Hz frames)".
- Same with rhi.SyncSlackMS 10: "~76 ms".
- 60 Hz titles gain less ("less noticeable") because latency already starts lower.

Derived for this project [D, extrapolated, not measured]: at 40 fps the frame is 25 ms; at 22 fps it is 45.5 ms; at 60 fps 16.7 ms. If the best-case latency scales as two frames (Epic's 30 Hz example), that is about 50 ms at 40 fps and about 91 ms at 22 fps. Test before relying on it.

Not found [U]: documented defaults for rhi.SyncSlackMS and rhi.SyncInterval; the meaning of r.FinishCurrentFrame and r.RHICmdBypass (search returned nothing); t.MaxFPS semantics (one forum post says an internal cap near 120 fps remained with t.MaxFPS 0 and vsync off, [C] https://forums.unrealengine.com/t/120-fps-cap/2674250).

### 2.4 NVIDIA Reflex (UE5)

- Built into UE5: "Reflex is available natively through Unreal Engine 5" and "can be enabled through the plugin section of the Unreal Engine Editor". No version list and no cvar given [V https://developer.nvidia.com/UNrealengine].
- Reflex also ships inside the DLSS Frame Generation plugin packages (UE 5.2 bundle) [S].
- Reported errors on UE 5.5 with an older DLSS 3.7.20 build [C, forum].
- Cvars t.ReflexMode and r.NVIDIA.Reflex: not found in any source [U].
- Reflex "Low Latency Mode aligns game engine work to complete just-in-time for rendering" [S, NVIDIA developer blog https://developer.nvidia.com/blog/?p=17938].
- Savings: Rust, up to 38% system latency (vendor headline, one game) [S https://www.nvidia.com/en-us/geforce/news/rust-reflex-out-now/]. TechSpot found about 10-12 ms at 1080p and 3 ms in another case, largest gains when GPU-bound below 60 fps [S https://www.techspot.com/article/2123-nvidia-reflex-rtx/]. A Blur Busters post claims about 50% in supported cases [C, unofficial, https://forums.blurbusters.com/viewtopic.php?p=126652].

### 2.5 AMD Anti-Lag 2 (UE plugin)

- Plugin toggled in Project Settings or via console variable r.AMD.AntiLag2 [S, gpuopen.com/anti-lag-2 excerpt]. v2.0.4 adds UE 5.7 and drops UE 5.1 [S]. AMD plugin for UE 5.5 released with FSR 3.1.3 (Feb 2025) [S].
- Supports RDNA 1 and newer (RX 5000+). Works with FSR Frame Generation on D3D12 [S].
- Mechanism: delay inserted just before inputs are sampled, claimed to beat Anti-Lag 1 [S, qualitative]. No ms figures found [U].

### 2.6 Apple / Metal

- No official Apple latency numbers found [U]. Apple Developer Forums (third-party reports): displaySyncEnabled = NO reduced a one-second nextDrawable stall from over 90% of cases to about 50% [C] https://www.developer.apple.com/forums/thread/773229 ; with maximumDrawableCount 3 one developer measured CPU-to-display latency 16-50 ms depending on queued frames [C] https://developer.apple.com/forums/thread/711033.
- Lower drawable count shortens the queue but at 2 the CPU can block on getNextDrawable [C].

### 2.7 Latency savings summary

| Technique | Documented saving | Tag |
|---|---|---|
| r.GTSyncType 2 + small rhi.SyncSlackMS (30 Hz example) | worst 130 ms -> best 66 ms; 76 ms with 10 ms slack | [V] Epic |
| NVIDIA Reflex, GPU-bound | ~10-12 ms in one test, up to 38% in one vendor headline | [S] |
| AMD Anti-Lag 2 | not quantified | [S] |
| Late latching (VR) | up to one frame; example 4 ms and 10 ms | [V] Meta |
| Metal drawable count / displaySync | not quantified by Apple | [C] |

---

## 3. Gamepad look stick

### 3.1 Dead zone shape and size

| Method | Formula | Source |
|---|---|---|
| Axial (per-axis) | if |x| < d: x = 0; same for y. Causes snapping to cardinal directions. | [V] Sutphin, https://www.gamedeveloper.com/disciplines/doing-thumbstick-dead-zones-right |
| Radial | if |v| < d: v = 0 | [V] same |
| Scaled radial | v' = normalize(v) * (|v| - d) / (1 - d) | [V] same |

- Sizes: "often somewhere between 0.1 to 0.2" [V]; code example d = 0.25; OUYA needed "as high as 0.25"; new Xbox 360 pad fine at 0.1 [V].
- UE5 Enhanced Input: UInputModifierDeadZone maps LowerThreshold..UpperThreshold to 0..1, clamps above [V https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/EnhancedInput/UInputModifierDeadZone?lang=en-US, UE 5.8]. EDeadZoneType: Axial ("Apply dead zone to axes individually"), Radial (display name "Smoothed Radial", "Apply dead zone logic to all axes simultaneously"), UnscaledRadial ("...without any smooth input which the normal Radial deadzone applies") [V https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/EnhancedInput/EDeadZoneType].
- Halo Infinite community values: Center Deadzone 3.5 to 8 [C] https://www.gfinityesports.com/halo-infinite/controller-settings/ ; Halo Infinite has separate dead zones per stick [C].

### 3.2 Response curves

| Game | Curve options and maths | Tag |
|---|---|---|
| Call of Duty | Standard (general audience, compensates heavy stick), Linear (no initial speed delay at full deflection), Dynamic (reverse S-curve, more precision near centre); "slope scale" tilts the curve. No exponents published. | [C] GameRevolution, Dot Esports, Sportskeeda excerpts; sources disagree on Standard's shape |
| Apex Legends (ALC) | Sensitivity and response curve bar (0-10 in community guides); lower value = more sensitive. Advice: lowest dead zone you can hold steady. | [C] community guides only |
| Cyberpunk 2077 | "First-Person Camera (Controller)" advanced options: Horizontal Turning Bonus, Turning Ramp-up Time. Setting both to 0 removed acceleration (user report). | [C] https://steamcommunity.com/app/1091500/discussions/0/4140563392619838981 |
| Halo Infinite | "comprehensive acceleration table" (values not verified); Look Acceleration 4-5, Look Sensitivity 5.5-6 in guides | [C] |
| Destiny 2 | Traction (controller-only) removed; replaced by sprint-turn speed scalar 0.3-0.8; Bungie said 10 sensitivity cap too low on controller | [S] https://destructoid.com/?p=279493 |
| UE5 Enhanced Input | UInputModifierResponseCurveExponential exists in the plugin (per-axis exponent) but the page could not be read | [U] |
| Quake (reference) | linear, no curve | [V] |

Exponent values for Call of Duty, Apex, Halo and Destiny curves: not found [U].

### 3.3 Turn acceleration / turn boost

- Cyberpunk 2077 exposes "Horizontal Turning Bonus" and "Turning Ramp-up Time" [C]. Numeric defaults: not found [U].
- Halo Infinite: Look Acceleration (community guides give values 4-5 on the in-game scale; scale not documented) [C]. Not in degrees per second.
- Destiny 2 sprint-turn scalar 0.3-0.8 [S].
- Max yaw/pitch in deg/s for Halo, Destiny, Apex, Call of Duty, Titanfall 2 at default sensitivity: not found [U]. A search for measured turn rates returned only drone and vehicle material.
- Pitch-to-yaw ratio: only one Apex community guide gives "pitch a few ticks below yaw", a preference, not data [C].
- Stick smoothing/aim easing in and out (ms): not found [U] beyond the Cyberpunk ramp-up option name.

### 3.4 Aim assist reference

- GDC Vault "Techniques for Building Aim Assist" (GDC 2013; Insomniac, Resistance 3 per listing): covers camera acceleration, deadzoning, magnetism, centering and friction. Slides are members-only. The listing was reached via a search excerpt [S] https://www.gdcvault.com/play/1017942/Techniques-for-Building-Aim-Assist (login error on fetch).

---

## 4. Procedural first-person camera motion

### 4.1 Quake (verified code, id Software GPL source)

Roll (strafe tilt):
```
side  = dot(velocity, right)
sign  = side < 0 ? -1 : 1
side  = |side|
if (side < cl_rollspeed) side = side * cl_rollangle / cl_rollspeed
else                     side = cl_rollangle
roll  = side * sign
// cl_rollangle = 2.0 deg, cl_rollspeed = 200 units/s
```
Source: https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/view.c

Head bob:
```
cycle = (cl.time mod cl_bobcycle) / cl_bobcycle            // cl_bobcycle = 0.6 s
if (cycle < cl_bobup) cycle = pi * cycle / cl_bobup         // cl_bobup = 0.5
else                  cycle = pi + pi * (cycle - cl_bobup) / (1 - cl_bobup)
bob = sqrt(vx^2 + vy^2) * cl_bob                             // cl_bob = 0.02
bob = bob * 0.3 + bob * 0.7 * sin(cycle)
clamp bob to [-7, 4]                                         // Quake units
vieworg.z += cl.viewheight + bob
```
Source: same view.c. Derived: bob period 0.6 s, so 1.67 cycles/s, independent of speed; amplitude scales with horizontal speed and hits the +4 clamp near 200 units/s [D]. Conversion of Quake units to cm: not verified [U].

Stair step smoothing in V_CalcRefdef: oldz advances by steptime x 80, offset capped at 12 units [V view.c excerpt].

### 4.2 Quake 3 (verified formulas, constants partly unseen)

Stair step (cg_view.c, verified):
```
timeDelta = cg.time - cg.stepTime
if (timeDelta < STEP_TIME)
    vieworg.z -= cg.stepChange * (STEP_TIME - timeDelta) / STEP_TIME
```
Landing (cg_view.c, verified):
```
delta = cg.time - cg.landTime
if (delta < LAND_DEFLECT_TIME)
    vieworg.z += cg.landChange * (delta / LAND_DEFLECT_TIME)
else if (delta < LAND_DEFLECT_TIME + LAND_RETURN_TIME)
    f = 1 - (delta - LAND_DEFLECT_TIME) / LAND_RETURN_TIME
    vieworg.z += cg.landChange * f
```
Source: https://raw.githubusercontent.com/id-Software/Quake-III-Arena/master/code/cgame/cg_view.c
- STEP_TIME = 200 ms is widely cited but the #define was not in the fetched excerpt [U].
- LAND_DEFLECT_TIME, LAND_RETURN_TIME values and the landChange magnitude: not in the excerpt [U]. Landing dip in cm vs fall speed: not found [U].

### 4.3 Source engine

- Punch-angle spring (PUNCH_DAMPING 9, PUNCH_SPRING_CONSTANT 65): the values could not be confirmed. The raw GitHub path for player.cpp returned 404 and no search hit contained the identifiers [U].
- Source cl_bob, cl_bobcycle, cl_bobup defaults: Valve wiki returned 403 [U]. CS2's cl_bobcycle default is 0.98 [C] https://csdb.gg/command/cl-bobcycle/ (not Source/HL2).
- cl_smoothstairs: no evidence it exists [U].

### 4.4 FOV change with speed

- Sprint FOV widening is common practice [C, general]. Titanfall 2 sprint FOV boost: not found [U]. Titanfall 2 vertical FOV slider 70-110 [S] https://www.wsgf.org/node/29698.
- Mirror's Edge: camera tilts with speed (GDC 2008 report) [S] https://www.engadget.com/2008-02-29-gdc08-first-screenshots-of-mirrors-edge.html ; a centre dot reduced sickness [S] https://www.kpbs.org/news/arts-culture/2008/07/27/mirrors-edge-a-little-too-close-to-reality . Numbers not found [U].
- Borderlands 4 PC FOV range about 70-110, default about 90 [S].

### 4.5 Turn lag and body-aware cameras

- Star Citizen (Kotaku): head-mounted camera looked like a bobble head; the team added eye stabilisation (counter-rotation about a distant focus point) which removed about 80% of the bounce, but caused trouble in confined spaces [S] https://kotaku.com/star-citizens-first-person-view-is-based-on-birds-1787021141.
- Cyberpunk 2077: "additive camera motions" toggle reportedly removes head bob but not all shake [C] (Steam thread).
- Dying Light: a HeadbobFactor in default_levels.xml; anecdotal effect [C].
- Camera lag behind fast turns: no quantitative source found [U].

---

## 5. Spring and damping maths

### 5.1 Half-life damper (frame-rate independent, exact for a fixed target) [V Holden]

Source: https://theorangeduck.com/page/spring-roll-call
```
x <- lerp(x, goal, 1 - 2^(-dt / halflife))
// code form:
x = lerp(x, g, 1.0f - expf(-(0.69314718056f * dt) / (halflife + eps)));   // eps = 1e-5
```
Half-life = time for the gap to halve. Holden gives no numeric defaults; "small values work well for filtering jitter" [V].

Exponential form with rate lambda [V Driscoll https://www.rorydriscoll.com/2016/03/07/frame-rate-independent-damping-using-lerp/]:
```
x <- lerp(x, goal, 1 - exp(-lambda * dt))
lambda = -ln(r)     where r = fraction of gap left after 1 s
halflife = ln 2 / lambda
```
Driscoll's Unity form: `Mathf.Lerp(source, target, 1 - Mathf.Pow(smoothing, dt))`; Driscoll states "Don't use plain Lerp for damping inside Update or FixedUpdate" and that smoothing values must be retuned if the update rate changes.

### 5.2 Exact critically damped spring (Holden) [V]

```
d   = 4 * ln2 / halflife              // damping
s   = d^2 / 4                          // stiffness (critical: omega = d/2)
y   = d / 2                            // half-damping
c   = g + (d * q) / (d*d / 4)          // q = goal velocity
j0  = x - c
j1  = v + j0 * y
eydt = exp(-y * dt)
x'  = eydt * (j0 + j1 * dt) + c
v'  = eydt * (v - j1 * y * dt)
```
Holden: "Now it works even for very high damping values"; the naive spring is unstable for large dt and the exact one is stable for large dt or stiffness. Derived: omega = d/2 = 2 ln2 / halflife = 1.386 / halflife [D].

### 5.3 Ryan Juckett closed forms [V https://www.ryanjuckett.com/damped-springs/, published 2012-07-20, updated 2020-11-29]

```
omega = sqrt(k/m)               // angular frequency
zeta  = beta / (2 sqrt(m k))    // damping ratio
alpha = omega sqrt(1 - zeta^2)  // damped frequency (underdamped)

Underdamped (0 <= zeta < 1):
x(t) = e^(-omega zeta t) [ x0 cos(alpha t) + ((v0 + omega zeta x0)/alpha) sin(alpha t) ]
v(t) = -e^(-omega zeta t) [ (x0 omega zeta - ((v0 + omega zeta x0)/alpha) alpha) cos(alpha t)
                           + (x0 alpha + ((v0 + omega zeta x0)/alpha) omega zeta) sin(alpha t) ]

Critically damped (zeta = 1):
x(t) = ((v0 + x0 omega) t + x0) e^(-omega t)
v(t) = (v0 - (v0 + x0 omega) omega t) e^(-omega t)

Overdamped (zeta > 1), z = -omega zeta +/- omega sqrt(zeta^2 - 1):
c1 = (v0 - x0 z2)/(z1 - z2),  c2 = x0 - c1
x(t) = c1 e^(z1 t) + c2 e^(z2 t)
v(t) = c1 z1 e^(z1 t) + c2 z2 e^(z2 t)
```
Juckett's code exposes CalcDampedSpringMotionParams(deltaTime, angularFrequency, dampingRatio) and the update newPos = posPosCoef*oldPos + posVelCoef*oldVel. Recommended camera parameters: not given [U].

### 5.4 Unity Mathf.SmoothDamp [V https://raw.githubusercontent.com/Unity-Technologies/UnityCsReference/master/Runtime/Export/Math/Mathf.cs]

Comment in source: "Based on Game Programming Gems 4 Chapter 1.10".
```
smoothTime = max(0.0001, smoothTime)
omega  = 2 / smoothTime
x      = omega * dt
exp    = 1 / (1 + x + 0.48 x^2 + 0.235 x^3)
change = current - target
temp   = (currentVelocity + omega * change) * dt
output = target + (change + temp) * exp
// velocity update and overshoot clamp follow in source (velocity line not in the fetched excerpt, U)
```
Unity docs (search excerpt, not fetched in full): "smoothTime: approximately the time it will take to reach the target" and the smoothing "will never overshoot" [S https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/mathf/smoothdamp].

### 5.5 Parameter meanings and typical values

| Parameter | Meaning | Typical camera value |
|---|---|---|
| halflife | time for the gap to halve (Holden) | not sourced; tune |
| lambda | exponential rate, lambda = ln2/halflife | not sourced |
| frequency f (Hz) | omega = 2 pi f | not sourced |
| damping ratio zeta | 1 = critical, <1 oscillates, >1 sluggish | 1.0 for no overshoot (Holden/Juckett definition) |
| smoothTime | approx. time to reach target (Unity) | not sourced |

Derived sanity values [D]: at 40 fps (dt = 0.025 s) and halflife 0.05 s, the per-frame blend is 1 - 2^(-0.5) = 0.293. At 22 fps (dt = 0.0455 s) the same halflife gives 0.466. A fixed lerp of 0.293 would give different convergence at each rate.

---

## 6. Comfort and accessibility

### 6.1 Xbox Accessibility Guideline 117 "Visual distractions and motion settings" [V https://devdocs.xbox.com/gaming/accessibility/xbox-accessibility-guidelines/117]
- Goal: "offer field-of-view, camera-shake, motion-blur, and sensitivity settings to reduce motion sickness."
- Cause: motion sickness when visual walk-bob, weapon-sway or head-bob conflicts with a still body.
- "Avoid or allow disabling" camera shake, camera bobbing, motion blur. Halo Infinite has 0-100% sliders (cited).
- "Avoid repetitive side-to-side or up-and-down" motion outside core gameplay.
- "Provide adjustable field-of-view settings" that let players choose an angle "least likely to make them sick based on sitting distance". Examples: Halo Infinite FOV slider, Minecraft FOV up to 110.
- "Provide horizontal and vertical camera sensitivity settings" (Halo Infinite example).
- "Provide the ability to disable automatic camera movement" (Sea of Thieves auto-centre example).
- "Allow players to choose between 1st and 3rd person camera views."

### 6.2 Game Accessibility Guidelines [V titles only, https://gameaccessibilityguidelines.com/full-list/]
- "Avoid VR simulation sickness triggers" (Vision, Basic).
- "If the game uses field of view (3D engine only), set an appropriate default for the expected viewing environment" (Vision, Basic).
- "If the game uses field of view (3D engine only), allow a means for it to be adjusted" (Vision, Intermediate).
- "Avoid (or provide option to disable) any difference between controller movement and camera movement" (Vision, Intermediate).
- "Avoid any sudden unexpected movement or events" (Cognitive, Advanced).
- "Provide an option to turn off / hide background movement" (Cognitive, Intermediate).
- "Include an option to adjust the sensitivity of controls" (Motor, Basic).
- "Avoid / provide alternatives to requiring buttons to be held down" (Motor, Intermediate).
- "Allow controls to be remapped / reconfigured" (Motor, Basic).
- Not on the full-list page: motion blur, head bob, horizon stability, separate X/Y sensitivity, invert axis, dead zone. Guideline body text was not fetched.

### 6.3 FOV values

| Claim | Value | Tag |
|---|---|---|
| Console FOV default | about 60-80 (sources disagree; one says 60) | [S] EPFL concept page; Technipages; forum |
| PC FOV default | about 90-100; a Unity forum suggests PC about 30 deg higher than console | [S/C] |
| Titanfall 2 | vertical 70-110 slider | [S] wsgf |
| Minecraft | up to 110 | [V XAG 117 example] |
| Rainbow Six Siege | console capped at 60; PC up to 90 before a patch | [S] |
| Horizontal vs vertical | Calculator sites say PC usually horizontal, consoles and UE often vertical; check each game | [C] |

Motion-sickness research on FOV: no study found; a calculator site says a wider FOV at close viewing distance reduces risk [C, uncited] [U].

### 6.4 Standard options checklist (PC good practice)

Sourced by [V] XAG/GAG items above: separate horizontal/vertical sensitivity, invert axis (not sourced in GAG full list, [U]), toggle vs hold, remapping, FOV slider, camera shake/bob/motion blur toggles, auto-centre toggle, 1st/3rd person choice, reticle colour/design options. Controller dead-zone sliders: standard in Halo and Apex per community guides [C].

---

## 7. Low frame rate responsiveness

- Fixed timestep with accumulator and interpolation (Gaffer On Games) [V https://www.gafferongames.com/post/fix_your_timestep/]:
  ```
  accumulator += frameTime (clamped: if frameTime > 0.25, frameTime = 0.25)
  while (accumulator >= dt) { previous = current; integrate(current, dt); accumulator -= dt }
  alpha = accumulator / dt
  render = current * alpha + previous * (1 - alpha)
  ```
  Article uses dt = 1/60 in the first example and 0.01 in the interpolation example; it gives no single recommended dt. The 0.25 s clamp is the spiral-of-death limit.
- Decouple camera from simulation: apply look every render frame from accumulated mouse counts (Quake's per-count model in section 1.4), smooth orientation with the half-life damper (section 5.1) rather than fixed lerp.
- Frame-rate-dependent smoothing: the fixed per-frame lerp `a = lerp(a, b, r)` is retuned by frame rate; use `1 - exp(-lambda dt)` or `1 - 2^(-dt/halflife)` [V Driscoll, Holden].
- Late latching: Meta says "up to one additional frame of latency" removed by sampling poses at the last moment, e.g. at the end of the render thread [V https://developers.meta.com/horizon/documentation/unreal/unreal-late-latching/]. A mouse-cursor late-latch hobby project exists [S https://github.com/nlguillemot/LateLatching]. No UE5 mouse late-latch feature found [U].
- Engine sync: Epic's r.OneFrameThreadLag and r.GTSyncType (section 2.3) [V].
- Input sampling per frame in UE5 (sample timing relative to render): not found [U]. A dev.to post says UE input is tied to frame rate [C, unofficial].

---

## Not found, or unverified (explicit list)

- Max yaw and pitch rates (deg/s) for Halo, Destiny, Apex, Call of Duty, Titanfall 2: not found.
- Stick response exponents for Apex, Halo, Destiny, Call of Duty: not found.
- Turn-boost amount, ramp-up time and delay for any shooter: not found (Cyberpunk option names only).
- Pitch-to-yaw ratio for any shooter: not found.
- Gamepad stick smoothing ramp times (ms): not found.
- Source punch spring constants 9 and 65: not verified.
- Source cl_bob defaults for Half-Life 2: not found (Valve wiki 403).
- Quake 3 STEP_TIME value and LAND_* timing: not in fetched excerpt.
- Landing dip in cm per fall speed; head-bob amplitude in cm and steps per second for walk and sprint: not found.
- Titanfall 2 sprint FOV and wall-run roll: not found.
- Star Citizen head-stabilisation option names and values: not found beyond the 80% claim.
- Dying Light, Cyberpunk camera numbers: not found.
- UE: SetUseHighPrecisionMouseMode, UGameViewportClient mouse mode, a raw-input plugin, EFOVScalingType formula, UInputModifierResponseCurveExponential parameters, bViewAccelerationEnabled, default bEnableMouseSmoothing: not found.
- UE cvars t.ReflexMode, r.NVIDIA.Reflex, r.FinishCurrentFrame, r.RHICmdBypass, t.MaxFPS semantics: not verified.
- Reflex and Anti-Lag 2 millisecond savings in UE5 at 40-60 fps: not found in primary sources.
- Apple Metal official latency guidance: not found.
- Pro cm/360 survey with sample sizes: not found.
- Motion sickness research on FOV or camera bob with quantities: not found.
- GDC aim-assist slides: members-only.

---

## Sources (primary first)

Primary, fetched:
- https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/in_win.c
- https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/cl_main.c
- https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/cl_input.c
- https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/view.c
- https://raw.githubusercontent.com/id-Software/Quake-III-Arena/master/code/cgame/cg_view.c
- https://raw.githubusercontent.com/Unity-Technologies/UnityCsReference/master/Runtime/Export/Math/Mathf.cs
- https://www.ryanjuckett.com/damped-springs/
- https://theorangeduck.com/page/spring-roll-call
- https://www.rorydriscoll.com/2016/03/07/frame-rate-independent-damping-using-lerp/
- https://www.gafferongames.com/post/fix_your_timestep/
- https://www.gamedeveloper.com/disciplines/doing-thumbstick-dead-zones-right
- https://devdocs.xbox.com/gaming/accessibility/xbox-accessibility-guidelines/117
- https://gameaccessibilityguidelines.com/full-list/
- https://dev.epicgames.com/documentation/en-us/unreal-engine/low-latency-frame-syncing-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UInputSettings
- https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/EnhancedInput/UInputModifierDeadZone?lang=en-US
- https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/EnhancedInput/EDeadZoneType
- https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/EnhancedInput/EFOVScalingType
- https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/EnhancedInput/UInputModifierFOVScaling?lang=en-US
- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FReply/UseHighPrecisionMouseMovement
- https://developer.nvidia.com/UNrealengine
- https://developers.meta.com/horizon/documentation/unreal/unreal-late-latching/
Secondary, search excerpts or low-trust:
- https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/mathf/smoothdamp
- https://www.mouse-sensitivity.com/forums/topic/747-formulas-for-converting-from-different-fov-types/
- https://www.tabnews.com.br/OVitor/como-calcular-edpi-e-cm-360-a-matematica-da-sensibilidade-entre-jogos
- https://csdb.gg/command/m-yaw/ ; https://totalcsgo.com/commands/myaw
- https://liquipedia.net/counterstrike/Mouse_Settings (403 on fetch)
- https://www.gfinityesports.com/halo-infinite/controller-settings/
- https://steamcommunity.com/app/1091500/discussions/0/4140563392619838981
- https://steamcommunity.com/app/1475810/discussions/0/3177859358960451419
- https://destructoid.com/?p=279493
- https://www.wsgf.org/node/29698
- https://kotaku.com/star-citizens-first-person-view-is-based-on-birds-1787021141
- https://www.engadget.com/2008-02-29-gdc08-first-screenshots-of-mirrors-edge.html
- https://www.kpbs.org/news/arts-culture/2008/07/27/mirrors-edge-a-little-too-close-to-reality
- https://www.gdcvault.com/play/1017942/Techniques-for-Building-Aim-Assist
- https://gpuopen.com/anti-lag-2/
- https://www.nvidia.com/en-us/geforce/news/rust-reflex-out-now/
- https://www.techspot.com/article/2123-nvidia-reflex-rtx/
- https://forums.blurbusters.com/viewtopic.php?p=126652
- https://developer.apple.com/forums/thread/711033 ; https://www.developer.apple.com/forums/thread/773229
- https://forums.unrealengine.com/t/120-fps-cap/2674250
- https://csdb.gg/command/cl-bobcycle/
- https://github.com/nlguillemot/LateLatching
