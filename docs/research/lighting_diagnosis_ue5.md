# Lighting diagnosis (UE 5.8): bright shade, sky "waves", odd mountains

Written 2026-10-14. Nothing was run in Unreal, no code was changed. Every engine fact below is
read from the local UE 5.8 source (`.unreal/UE_5.8/Engine/...`, cited as `[ENG file:line]`), from
Epic documentation (`[DOC url]`), or is labelled `[GENERAL]` (widely published photometry, no page
fetched) or `[UNVERIFIED]` (a guess or a secondhand claim). Project facts are cited as
`SR` = `unreal/Source/Ephemera/Private/Frame/EphemeraSkyRig.cpp`, `BS` = `unreal/Tools/build_slice.py`.

The owner's words, for reference: surfaces "light up" in shade and "light is coming after them";
a rock looks like "a blender render with bad lighting"; the sky has "shades and lighting waves
that make no sense coming from the ground up" and "inconsistent non-smooth lighting effects";
"mountains are lit up even though it doesn't make sense".

---

## 1. Verdict: ranked suspects

### A. Surfaces too bright in shade, "light comes after them"

| Rank | Suspect | Why (short) | Confidence |
|---|---|---|---|
| A1 | **Auto exposure on a very dark world** | Albedo 0.02 to 0.05 everywhere. The histogram meter averages the 35th to 90th percentile (`SR:58-59`), bias +0.3, speed up 2.5 EV/s, EV floor -2. Turn toward a rock face or the ground (little sky in frame) and the meter lifts a shaded rock about 2 to 3 stops; it "lights up" over 1 to 2 s. Local exposure (highlights 0.78) pulls sunlit areas down while shade is left alone, so the sun/shade ratio on screen shrinks. | High (mechanism certain; size unmeasured) |
| A2 | **Lumen is view-dependent and lags** | Surface-cache direct lighting updates 1/32 of texels per frame, radiosity 1/64 (`[ENG LumenSceneLighting.cpp:42-55]`), screen probes are 1 per 32 px (project override, default 16), temporal history 10 frames, screen traces on. The sun moves every frame. Epic: fast movement makes indirect light "pop in as it catches up". | High |
| A3 | **Sky light is captured from the wrong place, and its lower hemisphere is a sunlit albedo-0.4 planet** | `SkyLight` sits at the rig's origin = the reference planet's centre (`BS:4248`, `FrameSubsystem.cpp:129`); real-time capture is rendered from the sky light's own position (`[ENG ReflectionEnvironmentRealTimeCapture.cpp:557, 595]`). Sky Atmosphere paints the ground it sees with `GroundAlbedo` (engine default 0.4) times the sun; the project never sets it. Lumen reads this cube for every ray that misses (200 m max trace). | High that it is wrong; effect size needs step 4 |
| A4 | **Sky sheen on dark rock (Fresnel + unoccluded rough reflection)** | At albedo 0.046 and F0 0.028 the specular lobe is comparable to the diffuse one at grazing angles; Lumen takes reflections for roughness above 0.4 from the diffuse gather, with no sun-shadow awareness. | Medium |
| A5 | Missing small-scale AO | Short-range AO bent normal is turned off in the project's scalability tier. Material AO only reaches Lumen's final gather diffuse. | Medium |
| A6 | Cloud shadow strength 0.88 leaves 12 % of the sun (11 klux) in a cloud shadow; equal to the whole sky light | Physically defensible; it reads as "shade is not dark". | Low to medium |

### B. Sky: waves "from the ground up", non-smooth shading

| Rank | Suspect | Why (short) | Confidence |
|---|---|---|---|
| B1 | **Cloud shadows seen through the air** | Sun has `bCastCloudShadows` on in `fronts`, `overcast`, `broken` (`SR:380-382, 403`). `CloudShadowOnAtmosphereStrength` and `CloudShadowOnSurfaceStrength` are never set, so they are the engine default 1.0 (`[ENG DirectionalLightComponent.cpp:1078-1079]`). The atmosphere's ray march and the volumetric fog both sample the cloud shadow map, so every cloud casts a dark vertical curtain of air down to the ground. With clouds 300 to 450 m up inside a 1.4 km shell these curtains are close and sharp. | High |
| B2 | **The mist volume** (volumetric fog fed by the `MIST_CODE` box) | Hash-noise "banks" (310 m and 95 m features), froxel grid 12 px, jitter + reprojection, ends at 700 m, lit by the sun's VSM + cloud shadows + Lumen's translucency volume (grid 32 px). Layered sheets. | Medium to high |
| B3 | **Sky Atmosphere LUT artefacts** | FastSkyLUT and `AerialPerspectiveLUT.FastApplyOnOpaque` are on (engine defaults); the engine's own help says they artefact with "high frequency details ... scattering lobe". The project uses Mie g = 0.7 and a 90 m deep haze layer inside a 1.4 km shell, which is exactly such a detail. Froxel LUT 64 wide, 32 slices over 40 km. | Medium |
| B4 | **Encoder, not renderer** | The owner views through Pixel Streaming (H.264 Baseline, `docs/research/pixel_streaming.md`); smooth dark gradients are the classic macroblock/banding case. | Medium (cheap to rule out; do it first) |
| B5 | Volumetric cloud reconstruction | `r.VolumetricRenderTarget.Mode=1` (half-res, no temporal reconstruction) + upsample mode 4; `DistanceToSampleMaxCount=2`, min 32 samples; cloud shadow map has no temporal filter (`NewFrameWeight` 1.0). | Medium |
| B6 | Sky light capture time slicing | Cube faces are captured 2 per frame, clouds at half resolution; "lighting discrepancy between faces" when the sun moves (`[ENG ReflectionEnvironmentRealTimeCapture.cpp:46]`). | Low to medium |
| B7 | Known engine bug UE-232061 (cloud shadow sampled as if the camera were at the planet's north pole in SkyView LUT) | Reported against 5.5, fix targeted at 5.8 (secondhand, page returned 403). The 5.8 shader shows a patched-looking line (`SkyAtmosphere.usf:716-719`). | Low `[UNVERIFIED]` |

### C. Mountains lit "even though it doesn't make sense"

| Rank | Suspect | Why (short) | Confidence |
|---|---|---|---|
| C1 | **Atmosphere is not shadowed by terrain** | `bCastShadowsOnAtmosphere` and `bCastShadowsOnClouds` default to 0 and are not set (`[ENG DirectionalLightComponent.cpp:1071-1072]`). Haze in front of a sun-shadowed slope is lit as if the sun reached it, so the range glows. | High |
| C2 | **Lumen reach: 200 m view/trace distance, far field off** | Past 200 m only screen traces + sky; no ridge shades a valley or another ridge; rays that leave the 200 m budget return the sky cube, whose lower half is the sunlit albedo-0.4 ground (A3). | High |
| C3 | Aerial perspective strength/LUT | `AerialPerspectiveViewDistanceScale` 0.38 makes far slopes look near; froxel slices at 2 km are about 550 m apart; unshadowed in-scatter. | Medium |
| C4 | VSM clipmap setup | `Clipmap.ZRangeScale=5000` is 5x the default; Epic's help says values that are too large give "depth imprecisions and shadow flickering". `FirstLevel=8`. Far sun shadows can drop out, lighting slopes that face the sun's far side. | Medium `[UNVERIFIED size]` |
| C5 | Derivative relief normal on far terrain | `PLANET_CODE` tilts the normal from `ddx/ddy` of a procedural height, eased to 0.35 beyond 700 m only. | Low to medium |

---

## 2. What the scene sets (code facts that matter)

| Item | Setting | Where | Consequence |
|---|---|---|---|
| Sun | 95,000 lux, `bAtmosphereSunLight`, VSM, source angle 4.3 deg, shadow angle factor 0.35 | `SR:102-134` | Fine. The shadow edge is 3x sharper than the disc. |
| Cloud shadows | `bCastCloudShadows` = false by default, **true in fronts/overcast/broken** (`cloudshadow=1`). Strength 0.88, resolution scale 3, ray samples x2, extent 6.12 km. `CloudShadowOnAtmosphereStrength`, `CloudShadowOnSurfaceStrength`, `bCastShadowsOnAtmosphere`, `bCastShadowsOnClouds`, `CloudScatteredLuminanceScale`: **not set** | `SR:122-131, 380-403` | Default weather ("fronts") has cloud shadows on the air and the mist. Shadow map texel = 2 x 6.12 km / 1536 = 8 m. |
| Light shafts | No `bEnableLightShaftOcclusion`/`Bloom` on the sun (grep: none) | - | Not a cause. |
| Sky light | `bRealTimeCapture`, intensity 1.2, `bLowerHemisphereIsBlack = false`, `CastShadows`, never moved, volumetric scattering default | `SR:243-250, 703-707` | Capture position = rig origin = planet centre (A3). |
| Sky Atmosphere | radius = planet - 80 m, height 1.4 km, Rayleigh/Mie repacked by 60/1.4, Mie g 0.7, haze scale height 90 m, AP view distance scale 0.38, `TraceSampleCountScale` 4, `GroundAlbedo` **not set** (0.4) | `SR:161-166, 927-978`, `UpdateSky` | Bright ground in every capture/AP/cloud base. |
| Clouds | Custom material, per-sample atmospheric transmittance on, `ground_contribution` on, `SkyLightCloudBottomOcclusion` 0.45, tracing 600/120 km, `ViewSampleCountScale` 1.5 | `SR:168-184`, `BS:2275-2550` | Cloud bases are lit by the albedo-0.4 ground. |
| Height fog | Density 2e-7, `FogMaxOpacity` 1e-4, **volumetric fog on**, distance 700 m, albedo (230,235,242), scattering g 0.3 | `SR:193-209` | Fog is only a carrier. Everything visible comes from `MistVolume` (a cube with a volume material). |
| Post | Histogram, EV -2 to 15.5, speed up 2.5 / down 1.0, metered 35 to 90 % (ground), bias +0.3, **local exposure highlight 0.78 by day, shadow 1.0** (shadow minus 0.4 x Twilight), film slope 0.92 toe 0.58 shoulder 0.26, contrast 1.08, bloom 0.3, skylight leaking 0 | `SR:39-67, 272-363, 679-694` | See A1. |
| Lumen | GI + reflections, HWRT on Windows, `HardwareRayTracing.LightingMode=0` (surface cache), distance fields capped at 64 voxels per chunk, `CachedLightingPreExposure` 8 | `Windows/WindowsEngine.ini`, `DefaultEngine.ini` | `LumenSceneViewDistance` and `LumenMaxTraceDistance` **not set** = 20,000 cm each (`[ENG Scene.cpp:592, 597]`); `r.LumenScene.FarField` = 0 (`[ENG LumenScene.cpp:86-88]`). |
| Lumen tier overrides | `ScreenProbeGather.DownsampleFactor=32` (engine default 16 `[ENG LumenScreenProbeGather.cpp:86]`), `ShortRangeAO.BentNormal=0`, radiance cache probe res 16 | `DefaultScalability.ini [GlobalIlluminationQuality@3]` | Sparser, blurrier GI; less contact AO. |
| VSM | clipmap `FirstLevel=8`, `ZRangeScale=5000` (engine default 1000 `[ENG VirtualShadowMaps/VirtualShadowMapClipmap.cpp:78-82]`), directional LOD bias -1 | `DefaultEngine.ini:38-46` | C4. |
| Atmosphere LUTs | AP LUT 64 wide, 32 slices, 40 km deep, 8 samples/slice; FastSkyLUT 512x256; `DistanceToSampleCountMax` 12 | `DefaultEngine.ini:69-92` | Slices are squared-distributed (`docs/research/look_audit_ours.md` 2.1): slice 1 ends at 39 m, 4 at 625 m, 7 near 1.9 km. |
| Clouds cvars | `RenderTarget.Mode=1`, `SampleMinCount=32`, `DistanceToSampleMaxCount=2`, `HighQualityAerialPerspective=1` | `DefaultEngine.ini:93-103` | B5. |
| Fog tier | `VolumetricFog.GridPixelSize=12`, `GridSizeZ=96` | `DefaultScalability.ini` | Coarse froxels. |
| Materials | Stone: albedo 0.046, rough 0.62 to 0.95, `Specular` = 0.35 x `lerp(0.45,1,AO)` (F0 = 0.08 x 0.35 = 0.028). Planet: `mirror` 0.18 to 0.35. Grass: two-sided foliage, normal blended 66 % toward planet up, transmission (0.38,0.42,0.12) | `BS:3667-3745, 775-990, 3267-3470` | F0 is fine; the issue is grazing Fresnel on albedo 0.02 to 0.05 (A4). |
| Emissive | None on rocks/ground/grass (grep) | - | Not a cause. `LumenDiffuseColorBoost` is the engine default 1.0 (no boost) `[ENG Scene.cpp:598]`. |

**The sky light position finding in detail.** `UpdateSky` moves `Atmosphere`, `Clouds`, `MistVolume`, `Motes` and `Stars` but never `SkyLight`, so it stays at the rig actor's spawn point, (0,0,0) (`BS:4248`). The frame system parks the reference body, i.e. the planet under the player, at the origin (`FrameSubsystem.h:33`, `.cpp:129`). The Sky Atmosphere's centre is the body's location. The real-time capture builds six cube views at `SkyLightProxy->CapturePosition` = the component's world location (`[ENG ReflectionEnvironmentRealTimeCapture.cpp:557]`; `[ENG SkyLightComponent.cpp:264, 548]`) and sets up the atmosphere for that point with `ComputeViewData` (`:595`). `ComputeViewData` snaps a camera below the surface back up onto it along the line from the planet centre (`[ENG SkyAtmosphereCommonData.cpp:271-290]`); with the camera exactly at the centre that direction is 0/0. Two consequences, one certain and one not:
- Certain: the capture is never made at the player's location, so its "up", its sun elevation and its aerial perspective belong to some other point of the planet (or are degenerate).
- Not known without running: whether the cube comes out as NaN/black (then Lumen shade would be too dark, which contradicts the owner), or as a valid sky seen from one fixed surface point. Step 4 settles it in one frame.
When the player is on another body (the moon "Hush"), the sky light is still at the origin while the atmosphere belongs to a body elsewhere.

---

## 3. Known causes in UE5, per symptom (with sources)

### 3.1 Over-bright surfaces in shade

**a. Auto exposure and local exposure.**
- Histogram metering takes the average of the bins between Low and High percent and drives it to middle grey plus compensation. Epic: Low Percent "higher values give bright spots on the screen more priority"; High Percent "a good starting range is 80 to 95"; Speed Up is the adaptation speed "from a dark environment to a bright environment" `[DOC auto-exposure-in-unreal-engine]`.
- Our world is dark: albedo 0.04 sunlit is 0.04 x 90 klux / pi = 1,150 cd/m2, EV100 13.2 (L = 2^EV100 x 12.5/100 `[GENERAL]`, `L*8 = 2^EV`); 18 % grey at the same light would be EV 15.3. With bias +0.3 the meter, if the band is mostly ground, settles the *ground* at about middle grey, 2 stops brighter than a camera at "sunny 16". The code comment says the same: "an open sky belongs two stops and more above the ground" (`SR:44-46`) and an experiment (`keyspan`) exists because "turning to a shaded cliff ... lifting it to grey" was observed (`SR:735`).
- A rock in shade at EV 10.6 (sky only) with the meter free is lifted by a further 2.6 stops to the same grey as the sunlit rock. That is the literal complaint.
- Local exposure: `CalculateLogLocalExposure` compresses log luminance of any region whose *blurred* base is above middle grey by `HighlightContrastScale`, and of regions below by `ShadowContrastScale` `[ENG PostProcessHistogramCommon.ush:279-310]`. Highlight 0.78 darkens the sunlit side; shadow 1.0 leaves shade alone. The on-screen sun-to-shade ratio therefore falls by about 0.78 in log terms (6:1 becomes about 4.5:1) before the film curve. At twilight shadow scale drops to 0.6 (`SR:682`), which lifts shade further. Epic's own Lumen forum guidance names Local Exposure as the tool that flattens Lumen-on contrast `[FORUM lumen-skylightleaking-gi]`.
- Epic on Lumen: "Accurate global illumination creates high-contrast lighting where no single exposure value works well" `[Epic tech blog, Lumen in Fortnite Chapter 4]`.

**b. Lumen surface cache, update rate and view dependence.**
- Epic: "Fast camera movement will cause Lumen Scene updating to fall behind where the camera is looking"; indirect light "pop[s] in as it catches up"; the fix is Final Gather Lighting Update Speed `[DOC lumen-technical-details-in-unreal-engine]`.
- The numbers: direct lighting update factor 32, radiosity 64 (texels per frame = surface cache / factor) `[ENG LumenSceneLighting.cpp:42-55]`; screen probe temporal history 10 frames `[ENG LumenScreenProbeGather.cpp:227]`; project probe spacing 32 px. The sun moves every frame (comment in `DefaultScalability.ini`), clouds drift, so the surface-cache lighting of terrain is always partly stale and the *indirect* (shade) part follows the *direct* part with a delay. Cloud shadows are sampled in surface-cache direct lighting (`r.LumenScene.DirectLighting.CloudTransmittance` = 1 `[ENG LumenSceneDirectLighting.cpp:110]`), so a drifting cloud shadow is repainted into the cache at 1/32 per frame.
- Screen traces sample the previous frame's lit colour: GI that depends on what is in view. Epic: large Indirect Lighting Scale "will cause view-dependent global illumination, as Screen Traces cannot support it correctly" `[DOC lumen-technical-details]`; an Epic summary of Lumen also warns that a mismatch between on-screen (shadow map) and off-screen (ray-traced) shadowing makes GI "highly view-dependent" `[search-result paraphrase, not read on page]`.
- Card coverage: "By default, Lumen only places 12 Cards on a mesh" and large meshes are limited `[DOC lumen-technical-details]`. The terrain is 650 m chunks of a 7.7 m mesh: few cards per chunk, low texel density, so the bounce on cliffs is blocky and slow to refine.
- Lumen "Overview" modes and visualisations exist exactly for this (`r.Lumen.Visualize`, see step 5).

**c. Sky light real-time capture.**
- It captures SkyAtmosphere, VolumetricCloud and sky domes `[DOC sky-lights-in-unreal-engine]`, rendered at the sky light's location `[ENG ReflectionEnvironmentRealTimeCapture.cpp:557]` (comment at `:581`: "a sky dome ... at the SkyLight position").
- Sky Atmosphere adds the planet's reflected light: `L += Light0Illuminance * Transmittance * Throughput * NdotL * GroundAlbedo / PI` for rays that hit the ground `[ENG SkyAtmosphere.usf:821-832]`; engine default "Earth Albedo is 0.4 (linear)" `[DOC sky-atmosphere-component]`. A sunlit planet of albedo 0.4 under 85 klux is 10,800 cd/m2, brighter than the sky (about 5,000). For a vertical rock face the lower half of the cube then supplies about 0.4 x 85 klux x 0.5 = 17 klux versus the sky's 7.5 klux. The actual ground is basalt at 0.03 to 0.08, which would give 1.3 to 3.4 klux. `[arithmetic on GENERAL values]`
- `bLowerHemisphereIsBlack=false` is correct for a planet (the engine option cuts at world Z `[ENG ReflectionEnvironmentShaders.usf:446]`), but it leaves that bright "ground" in place. Epic's option text: "Whether all lighting from the lower hemisphere should be set to zero" `[DOC sky-lights]`.
- Lumen does not clamp the hemisphere: `EvaluateSkyRadiance(Direction)` returns the cube in any direction `[ENG LumenTracingCommon.ush:32-48]`. A ray that does not hit within `LumenMaxTraceDistance` (200 m) returns it. On a convex 5 km world a ray leaving a mountain flank downward passes over lowland for kilometres, so it "misses" and gets the albedo-0.4 ground.
- Epic doc: "Sky occlusion is limited to the Lumen Scene's first 200 meters" `[DOC lumen-technical-details]`; "Lumen limits how far rays are traced ... missing an occluder causes light leaking" (same page).
- Sky light intensity: 1.2 x the captured radiance. The capture of a ~5,000 cd/m2 sky gives about 15 klux at 1.0 `[GENERAL]`; 1.2 is plausible and not the main culprit (it was 2.6 earlier: `look_audit_ours.md`).

**d. Specular and Fresnel on dark rough rock.**
- Specular input 0.5 means about 4 % `[DOC physically-based-materials]`; UE's F0 = 0.08 x Specular, so 0.35 gives 2.8 %. Rocks and dirt: 3 to 5 % `[GENERAL]`. So F0 is correct.
- The issue is the *ratio*: diffuse albedo 0.046, specular 0.028 at normal incidence and rising toward 1 at grazing. Schlick at 75 degrees: 0.028 + 0.972 x (1 - 0.26)^5 = 0.24 before roughness damping. A sky of 5 to 10 kcd/m2 at the horizon times even 0.05 to 0.1 effective reflectance gives 250 to 1,000 cd/m2 on a rock whose shade diffuse is 100 to 200 cd/m2 (albedo 0.046 x 15 klux / pi = 220 for an unoccluded flat patch). The sheen does not know it is in shade. This is also `look_audit_ours.md` section 1.
- Lumen traces dedicated reflection rays only up to `MaxRoughnessToTrace` 0.4 `[ENG Scene.cpp:658]`; the stone is 0.62 to 0.95, so its "reflection" is the diffuse gather's sky with only short-range occlusion. Epic: material AO on the cavity map is multiplied into base colour and into Specular `[DOC physically-based-materials]`; our stone does scale specular by `lerp(0.45,1,AO)` but the AO channel is a scan's, not a world-scale occlusion.

**e. Two-sided foliage.** Two-sided foliage adds transmission from the back; blades in shade pick up the sky light cube through their `SubsurfaceColor`. `ShortRangeAO.FoliageOcclusionStrength` is 0.7 or 1 depending on tier `[ENG LumenShortRangeAOHardwareRayTracing / BaseScalability.ini:354]`. Grass normals are bent 66 % toward planet up, so a meadow reads as one surface and takes the same sky/ground cube; bright meadow in shade follows A3. Low priority for "a rock".

**f. Skylight leaking.** The project sets `LumenSkylightLeaking = 0` (`SR:328, 359`), the right choice: Epic's forum answer is that leaking adds sky light *without occlusion* and should be used sparingly `[FORUM lumen-skylightleaking-gi]`; the shader adds `GetSkyLightReflection x SkylightLeakingColor x distance factor` `[ENG LumenTracingCommon.ush:77-88]`. Not a cause here; keep it 0.

**g. Screen traces and "Lumen too bright outdoors".** A common forum complaint class: outdoor Lumen looks flat/bright because sky occlusion stops at 200 m, because exposure range is wide, and because skylight leaking/diffuse boost are set. The mitigation Epic gives is raise Scene View Distance / Max Trace Distance and Scene Detail `[DOC lumen-technical-details]`. I did not find a single forum thread matching "rock lit in shade" exactly `[UNVERIFIED]`.

### 3.2 Sky: waves from the ground up and non-smooth shading

**a. Cloud shadows in the atmosphere.**
- Epic: enable "Cast Cloud Shadows" to cast cloud shadows onto the atmosphere and surfaces `[DOC sky-atmosphere-component]`; "Cloud Shadow Map Resolution Scale drives the resolution and performance cost of the sunlight shafts" `[DOC volumetric-cloud-component]`.
- Engine: when `bCastCloudShadows` is on and `CloudShadowOnAtmosphereStrength > 0` (default 1.0), the sky view, AP LUT and per-pixel passes sample the cloud shadow map along each ray: `PlanetShadow0 *= lerp(1, GetCloudVolumetricShadow(...), VolumetricCloudShadowStrength0)` `[ENG SkyAtmosphere.usf:739-743; SkyAtmosphereRendering.cpp:1623-1660]`. Same for volumetric fog: `bLight0CloudPerPixelTransmittance ... GetCloudShadowOnSurfaceStrength() > 0` `[ENG VolumetricFog.cpp:1668]`.
- Hillaire's SIGGRAPH 2020 deck (UE's sky) describes the cloud shadow map feeding atmospheric light shafts `[Hillaire 2020 s2020_pbs_hillaire_slides.pdf]` (found by search; not read in full).
- Shape: the map is a light-space texture, 512 x 3 = 1536 texels over 12.24 km, 8 m per texel; cloud base 300 to 450 m; with `FogMaxOpacity` aside the mist is lit by it. Cloud shadow column edges appear as blurred curtains from the ground up, and move with wind (cloud drift 0.0012 per second) and with the shadow map snapping: `SnapLength` 20 km, `SnapToPixelGrid` 1, `NewFrameWeight` 1.0 (no temporal filter; "Experimental ... disabled by default") `[ENG VolumetricCloudRendering.cpp:180-222]`. The project already saw "bands that moved with the camera" at 16 samples (`SR:125`).
- Known bug: UE-232061, "cloud shadow sampling can wrongly assume the camera sits at the planet's North Pole ... cloud shadows swing violently once the camera is inside the atmosphere but above the clouds", affects 5.5, targeted 5.8 `[issues.unrealengine.com/issue/UE-232061, 403 to my fetch; summary from search]`. Check the version's fix by step 9.

**b. Volumetric fog and the mist volume.** Volumetric fog is a froxel grid (project 12 px, 96 slices) with jitter and temporal reprojection (`r.VolumetricFog.Jitter`, `.TemporalReprojection`, `.HistoryWeight` `[ENG VolumetricFog.cpp cvars]`). With Lumen it is lit by Lumen's translucency volume, "Lumen Dynamic GI + shadowed Skylight" (`VolumetricFog.usf:1006-1012`), a 32 px grid. Mist banks come from a hash value noise; a froxel grid sampling a value-noise at 95 m features with 12 px froxels shows sheets and steps. The mist ends at 700 m, so anything beyond is unfogged: a visible shell.

**c. Aerial perspective LUT / sky view LUT.**
- Engine help: `FastSkyLUT` "is faster but can result in visual artefacts if there are some high frequency details in the sky such as earth shadow or scattering lob"; `AerialPerspectiveLUT.FastApplyOnOpaque` has the same warning `[ENG SkyAtmosphereRendering.cpp cvars]`. Epic's Sky Atmosphere page lists "noise, aliasing, and some rings of different colors" as the visible low-quality symptom and Trace Sample Count Scale as the cure `[DOC sky-atmosphere-component]`.
- Our Mie g is 0.7 (tight lobe) and the haze is dense and low (scale height 90 m), so scattering varies fast with angle near the horizon, which is the case the warning describes.
- Planet scale: 5 km radius is far from the 6,360 km the LUT parameterisation was tuned for; sample counts were raised (`DefaultEngine.ini:80-92`). The squared slice distribution means a 2 km mountain sits between slices 550 m apart, so its in-scatter is interpolated linearly through a layer 90 m deep: banding on land and in sky.
- Hillaire's paper: the technique "does not require high dimensional LUTs, so it does not suffer from visual artifacts associated with them" but the AP volume remains a coarse froxel grid `[eg.org cgf14050; abstract]`.

**d. Volumetric cloud reconstruction.** `VolumetricRenderTarget.Mode` 0 traces at quarter resolution, reconstructs at half, upsamples at full; Mode 1 traces at half, upsamples `[ENG VolumetricRenderTarget.cpp:33-35]`. Epic: Mode 0 recommended for quality (clouds "fast to trace but can appear to have low resolution") `[DOC volumetric-cloud-component]`. Project uses 1 + upsampling 4 (bilateral). Cloud edges and bases shimmer with TSR on top. `r.VolumetricCloud.DistanceToSampleMaxCount=2` and `SampleMinCount=32` limit steps (comment `DefaultEngine.ini:93-98`).

**e. Encoder and TSR.** See the pattern-lock test in step 9. TSR and dithering turn into screen-locked noise; H.264 Baseline at low bitrate makes macroblock-aligned bands in dark smooth skies (already noted: `night_cloudshadows_stability.md:66`).

### 3.3 Mountains lit oddly

**a. Atmosphere not shadowed by terrain.** `bCastShadowsOnAtmosphere`, `bCastShadowsOnClouds` default 0 `[ENG DirectionalLightComponent.cpp:1071-1072]`; Epic: enable "Cast Shadows on Atmosphere" and "Cast Shadows on Clouds" for opaque objects to shade them `[DOC volumetric-cloud-component; sky-atmosphere-component]`. Without it, the haze between you and a shaded mountain is fully sunlit (and the mist with a g = 0.3 forward lobe lights near the sun). The result is a pale glowing mountain whose own lighting is correct.

**b. Lumen range.** See 3.1c. Additionally Lumen's far field "extends Lumen Global Illumination to one kilometer distance from the camera by default" but "requires HLOD1" and `r.LumenScene.FarField=1` `[DOC lumen-technical-details]`; project leaves it off, and the terrain is not HLOD'd. Past the Scene View Distance "only Screen Traces are active for global illumination" (same page, stated for software tracing).

**c. VSM.** Clipmap level range and ZRangeScale as in section 2. Epic's help text: values of ZRangeScale "too large cause depth imprecisions and shadow flickering" `[ENG VirtualShadowMaps/VirtualShadowMapClipmap.cpp:78-82]`.

**d. Aerial perspective.** In-scatter is a fixed function of distance and view angle, not of terrain shadow; at 0.38 distance scale a far slope is treated as nearer.

**e. Sky light.** Slopes facing away from the sun get sky light with no occlusion by ridges beyond 200 m (3.1c).

---

## 4. Systematic isolation plan

Conventions: "Dev build" means a Development or Test build, because several flags are
`SHOWFLAG_FIXED_IN_SHIPPING` (the Visualize* ones, `DirectLighting`, `DirectionalLights`) `[ENG ShowFlagsValues.inl:88-90, 358-362]`. Run
`~` console commands in play, or relaunch with project hooks that already exist and need no code:
`unreal.py shot --weather clear|fronts --time <s> --look <pitch,yaw> --sky "k=v,k=v" --cvars "a=b,c=d"`
(`unreal/Tools/unreal.py:310-366`; sky keys are read at start-up and on weather change only, so
they are relaunch switches). Sky keys used below: `sun`, `skylight`, `cloudshadow`, `clouds`, `fog`,
`fogbanks`, `mie`, `airdepth`, `localhigh`, `localshadow`, `bias`, `key`+`keyspan`.

"Measure" always means: pixel values from a lossless capture, not from the stream.
`HighResShot 1920x1080 0 0 0 0 0 1 1` writes the PNG plus **all buffer-visualisation targets as EXR**
(args: size, 4 region zeros, mask 0, dump targets 1, HDR 1) `[ENG UnrealClient.cpp:2470-2494]`. Use the
same fixed camera, time and weather every run. Pre-tonemap, shade/sun *ratios* in the EXR are
independent of exposure, which is what lets step 2 separate light from camera.

**Stations** (fix them once): S1 sun-lit rock; S2 the same rock's shaded side (same albedo); S3 same rock
inside a cloud shadow; S4 horizon sky and S5 zenith sky; S6 mountain 2 km away; S7 mountain 5 km away;
S8 shaded rock filling the frame (the "lighting up" case).

### Step 0 Codec vs renderer (do first, 5 minutes)
- Command: capture S4/S5 with `HighResShot 1920x1080` locally (not Pixel Streaming); compare to the stream.
- Measure: do the waves/bands exist in the PNG?
- Reading: absent in PNG, present in stream = encoder (raise bitrate, H.264 High/Main, `-PixelStreamingEncoderH264Profile=HIGH`). Present in both = renderer; continue.
- Then `r.AntiAliasingMethod 1` (FXAA) vs 4 (TSR): pattern vanishes with FXAA = TSR/history; unchanged = not temporal.

### Step 1 Pattern-lock rule (applies to every sky/mountain test)
Take two captures with the camera rotated 30 degrees, and two 2 s apart with the camera still.
- Stays on the same cloud/world feature, drifts with wind = **cloud shadow / cloud / mist banks** (world-locked).
- Stays at the same angle from the camera or same distance = **LUT / froxel / fog-grid** (view-locked).
- Stays at the same screen pixels = **TSR, dither, codec** (screen-locked).
- Flicks on a time scale of 0.5 to 2 s with no motion = sky-light time slicing / Lumen update (step 5).

### Step 2 Exposure and local exposure (symptom A, first cause to exclude)
1. Relaunch with `--sky "keyspan=0.05,key=13.5"` (holds EV 13.5 by day, `SR:742-748`) and compare S8 (shaded rock fills frame) against the default run. If S8 no longer "lights up" when you turn to it, A1 is confirmed.
2. `ShowFlag.LocalExposure 0` (and/or `--sky "localhigh=1,localshadow=1"`): the sun/shade ratio in the **PNG** should rise from about 4.5:1 toward about 6:1.
3. `ShowFlag.VisualizeHDR 1` with `r.EyeAdaptation.VisualizeDebugType 1` (histogram) at S8 versus S1: where the band sits, and the EV the meter chose `[DOC auto-exposure-in-unreal-engine]`. `ShowFlag.VisualizeLocalExposure 1` + `r.LocalExposure.VisualizationMode 1` (local exposure map), `3` (base luminance) `[ENG PostProcessVisualizeLocalExposure.cpp:12-24]`.
4. Measure in the EXR (`PreTonemapHDRColor` or `SceneColor`): R = L(S1)/L(S2) on the same rock patch.
- R in the EXR >= 4 (physical, section 5) but the PNG's R is far smaller, or S8 changes with the meter: **exposure/local exposure/tonemap** is the problem (A1). 
- R in the EXR already < 3 under a high sun: the **lighting itself** is flat; go to steps 3 to 6.

### Step 3 Decompose the light (which term fills the shade)
Use the EXR value of the shaded patch S2 and compute E = pi x L / rho (rho from `vis BaseColor`, about 0.046) as an equivalent irradiance in lux (valid if specular is small; step 6 checks that).
| Command | S2 should... | Meaning |
|---|---|---|
| `--sky "sun=0"` (relaunch) | stay ~unchanged if the shade is sky + GI | direct light is not the shade's source |
| `--sky "skylight=0"` | fall to near 0 (a few % from bounce) | if it keeps > 30 %: the extra light is **Lumen bounce/radiosity or specular**, not the sky light cube |
| `ShowFlag.SkyLighting 0` | fall by the sky's share | confirms the cube's weight (run-time switch of the same) |
| `ShowFlag.GlobalIllumination 0` / `ShowFlag.LumenGlobalIllumination 0` | fall to AO-less ambient | the share Lumen's diffuse gather gives |
| `ShowFlag.LumenSecondaryBounces 0` | fall by the bounce share | surface-cache bounce on dark ground is tiny (3 % per bounce); a big drop = surface-cache albedo too bright (check step 5d) |
| `ShowFlag.Specular 0` and/or `ShowFlag.LumenReflections 0` | fall at grazing view | specular sheen share (A4) |
| `ShowFlag.AmbientOcclusion 0` / `ShowFlag.LumenShortRangeAmbientOcclusion 0` | rise in crevices | how much AO is already working |
| `ShowFlag.DirectionalLights 0` (Dev build) | everything but sun | confirms the above |
Expected physical shade (section 5): E_shade about 5 to 15 klux on an open, flat patch; much less on a vertical face in a hollow. If E from the EXR is above ~20 klux, or S2 still has > 50 % of S1's luminance on a surface facing away from the sun with sky under 90 degrees: the fill is too strong.

### Step 4 Validate the sky light cube (A3, C2: the single most decisive step)
- Command: `ShowFlag.VisualizeSkyLightIlluminance 1` (Dev build). It integrates the **sky light cube** and prints lux on screen (`IlluminanceMeter.cpp:200-231`; it also prints the capture world position).
- Measure: (a) lux at noon, clear: expect about 10 to 25 klux `[GENERAL]` scaled by intensity 1.2; (b) change time with `-EphemeraTime=` (or wait) at one spot, and walk 1 km: the number must follow the **player's** sun elevation and horizon; (c) read the printed capture position.
- Reading:
 - lux = 0 or NaN/garbage, or capture position (0,0,0) = planet centre: the sky light is broken for a planet. (If lux is 0 the shade should be dark; the owner sees it bright, so look at step 5 for another bright fill.)
 - lux is sensible but **does not change with where you stand** or has the wrong sun elevation: the cube is a fixed far-point sky. Everything that reads it (Lumen misses, reflections, mist ambient) is lit for a different place.
 - lux far above 25 klux at noon: the albedo-0.4 lower hemisphere is adding energy. Compare `--sky "skylight=1.0"` and look at vertical faces (step 3).
- Extra: `r.SkyLight.RealTimeReflectionCapture.TimeSlice 0` (full capture each frame) and `r.SkyLight.RealTimeReflectionCapture.TimeSlice.SkyCloudCubeFacePerFrame 6`: if the "light coming after" flicker on shade disappears, time slicing (B6) contributed.
- The engine property GroundAlbedo and a sky light move cannot be set from the console; the test that closes this is an editor-side property change (Sky Atmosphere Ground Albedo 0.05; SkyLight to the player's feet) in a scratch copy. Listed in section 6; not done here.

### Step 5 Lumen reach, lag, coverage (A2, C2)
1. Coverage: `r.Lumen.Visualize 3` (Lumen Scene) and `5` (Surface Cache Coverage; pink = none) at S6, S7 and S1; `r.Lumen.Visualize 8` (Albedo), `9` (Normals), `10` (Emissive), `12` direct, `13` indirect `[ENG LumenVisualize.cpp:27-60]`; `r.Lumen.Visualize.CardPlacement 1` `[DOC lumen-technical-details]`.
 - Albedo in the Lumen Scene (8) visibly lighter than the real albedo (`vis BaseColor`): the card capture of the custom ground/rock material picks a different branch (`Dist`-dependent blends, `ddx` in an ortho card view) and bounce is overstated.
 - Large pink/black areas on the terrain: coverage gaps; mountains get no bounce and only sky.
2. Reach: `r.Lumen.TraceDistanceScale 10` (max trace 200 m to 2 km, `[ENG LumenDiffuseIndirect.cpp:88-231]`) and `r.LumenScene.FarField 1` (HWRT; test `ShowFlag.LumenFarFieldTraces 0/1`). Measure S6/S7 and valley floors, with exposure pinned.
 - Mountains and valley shade darken, "glow" reduced: **C2 confirmed** (200 m trace/miss to sky).
 - No change: it is not Lumen reach; go to step 10.
 - A/B with `r.Lumen.HardwareRayTracing 0` (software tracing against distance fields; different failure modes).
3. Lag and view dependence, with exposure pinned (step 2.1): stand still looking away, snap to S8, capture at 0.1, 0.5, 2, 6 s; mean luminance of a 100 x 100 crop in the EXR.
 - Rises > 25 % over the first seconds: Lumen lag. Then test, one at a time: `r.LumenScene.Lighting.ForceLightingUpdate 1`; `r.LumenScene.DirectLighting.UpdateFactor 4`; `r.LumenScene.Radiosity.UpdateFactor 8`; `r.Lumen.ScreenProbeGather.Temporal.MaxFramesAccumulated 4`; `r.Lumen.ScreenProbeGather.DownsampleFactor 8`. The one that removes the rise names the term. Visual aids: `r.Lumen.Visualize 16/17` (direct/indirect lighting updates), `23` (frames accumulated), `22` (fast-update amount) `[ENG LumenVisualize.cpp:27-60]`.
 - `r.Lumen.ScreenProbeGather.ScreenTraces 0` (also `ShowFlag.LumenScreenTraces 0`) and `r.Lumen.Reflections.ScreenTraces 0`: view-dependent GI disappears = screen traces.
 - Ground truth for stills: `r.Lumen.ScreenProbeGather.ReferenceMode 1` (1,024 rays per probe, no filtering; expensive, standing still) `[ENG LumenScreenProbeGather.cpp:62-68]`. If the reference is much darker in shade than the default: the production gather over-brightens (sparse probes, interpolation, temporal, leaking).
4. Probe grid: `r.Lumen.Visualize 22/23` plus `r.Lumen.ScreenProbeGather.DownsampleFactor 16` vs 32: blotches in smooth shade that shrink with 16 = the project's override.

### Step 6 Specular sheen on dark rock (A4)
- Commands: `vis Roughness`, `vis Specular`, `vis MaterialAO`, `vis BaseColor` (buffer visualisation; `viewmode visualizebuffer` in the editor) `[ENG BaseEngine.ini:3140-3162]`; then `ShowFlag.Specular 0`; `ShowFlag.LumenReflections 0`; `ShowFlag.ReflectionEnvironment 0`.
- Measure: S2 at a grazing view (camera 5 to 10 degrees above the rock's lit/shaded face), EXR luminance with and without specular.
- Reading: a drop of > 30 % in shade = sheen is the extra brightness. Check by `r.Lumen.Reflections.MaxRoughnessToTrace 0.7` (traces rays and gets real occlusion for rougher stone; the shade side should darken; `[ENG LumenReflections.cpp:108]`) and `r.Lumen.Reflections.SmoothBias 0` baseline. Also `r.Lumen.Reflections.DownsampleFactor 1` is default.

### Step 7 Cloud shadows: lighting, sky and mist (B1, A6)
- `--weather clear` (no clouds, no cloud shadows, mist 0.10) vs `--weather fronts` vs `--sky "cloudshadow=0"` on `fronts` (clouds visible, no shadows): waves/curtains gone with `cloudshadow=0` = B1 (cloud shadow on air/mist).
- Run-time: `r.VolumetricCloud.ShadowMap 0` (turns the map off for everything; the same effect without relaunch `[ENG VolumetricCloudRendering.cpp cvar list]`); `r.LumenScene.DirectLighting.CloudTransmittance 0` (only Lumen's cache).
- Debug: `r.VolumetricCloud.ShadowMap.Debug 1`, `r.VolumetricCloud.SkyAO 0` (cloud ambient occlusion on sky light).
- Smoothing tests (all cvars): `r.VolumetricCloud.ShadowMap.TemporalFiltering.NewFrameWeight 0.2`, `r.VolumetricCloud.ShadowMap.SpatialFiltering 3`, `r.VolumetricCloud.ShadowMap.RaySampleMaxCount 256`. If the curtains smooth but remain: the structure is real (B1 as designed), the cure is a lower `CloudShadowOnAtmosphereStrength` (property) rather than filtering.

### Step 8 Mist volume and froxels (B2)
- `ShowFlag.VolumetricFog 0` (all mist off): waves gone = B2. `--sky "fogbanks=0"` (uniform mist) and `--sky "fog=0"` (zero density; the fog cells stay on): banks vs grid.
- Grid: `r.VolumetricFog.GridPixelSize 6`, `r.VolumetricFog.GridSizeZ 128`; `r.VolumetricFog.TemporalReprojection 0` and `r.VolumetricFog.Jitter 0` to see the raw grid (if bands appear raw: froxels; if noise: reprojection is hiding it).
- Lumen's volume: `r.Lumen.TranslucencyVolume.Enable 0` (mist then uses the sky light SH if `SkyLightVolumetricScatteringIntensity` > 0): mist shade changes = it is lit by Lumen's coarse grid (GridPixelSize 32 `[ENG LumenTranslucencyVolumeLighting.cpp:45-50]`).

### Step 9 Atmosphere LUTs and clouds (B3, B5, B7)
- Pass A: `ShowFlag.Cloud 0` (clouds off), then `ShowFlag.Atmosphere 0` (sky and aerial perspective off): which layer holds the waves.
- Pass B (artefact toggles, each alone): `r.SkyAtmosphere.FastSkyLUT 0`; `r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque 0`; `r.SkyAtmosphere.SampleLightShadowmap 0`; `r.SkyAtmosphere.AerialPerspectiveLUT.DepthResolution 64` / `.Width 128` / `.SampleCountMaxPerSlice 16`; `r.SkyAtmosphere.TransmittanceLUT.UseSmallFormat 0` `[ENG SkyAtmosphereRendering.cpp cvars]`.
 - FastSkyLUT 0 removes sky waves = LUT resolution/lobe; FastApplyOnOpaque 0 fixes land bands = AP froxels.
 - `--sky "mie=0"` / `"mie=0.3"`: waves follow the Mie amount = the tight forward lobe.
- Pass C (clouds): `r.VolumetricRenderTarget.Mode 0` and `3`; `r.VolumetricCloud.SampleMinCount 64`; `r.VolumetricRenderTarget.UpsamplingMode 3`; `r.VolumetricRenderTarget.ReprojectionBoxConstraint 1`. Compare PNGs; shimmer gone with Mode 0 = B5.
- UE-232061 test: stand above the cloud base, rotate, and watch whether the *shadow on the air* swings with your viewing direction rather than the sun's; a view-direction-dependent shadow is the bug, a sun-locked one is not. `[UNVERIFIED]`.

### Step 10 Mountains (C1 to C5)
- Pin exposure (step 2.1), clear weather first. At S6/S7: `ShowFlag.Atmosphere 0` (removes AP/haze; the mountain's own lighting remains). If the mountain is dark and correct without it: **C1/C3** (the glow is the atmosphere, unshadowed in-scatter).
- `--sky "airdepth=0.1"` and `"airdepth=1.0"`: how much the 0.38 scale matters; `--sky "mie=0"`.
- Own lighting: `ShowFlag.SkyLighting 0`, `ShowFlag.GlobalIllumination 0` at S6: mountain flank brightness vs sun. A flank facing away from the sun that stays bright with both off = sun shadow/normal problem (below); one that goes dark = sky/GI fill (steps 4, 5).
- VSM: `r.Shadow.Virtual.Enable 0` (cascaded fallback; compare far self-shadowing), `r.Shadow.Virtual.Clipmap.ZRangeScale 1000` (default; Epic warns 5,000 is "too large"); `ShowFlag.VisualizeVirtualShadowMap 1` (Dev). Mountain flank that was lit and becomes dark = C4.
- Normals: `vis WorldNormal` at S6; per-pixel speckle where the mesh normal is smooth = derivative relief normal (C5). `ShowFlag.Specular 0` removes its sheen.
- Lumen reach again (step 5.2): lowland shade on valley floors beneath the mountain should darken with `TraceDistanceScale 10`.

### Decision table

| Observation | Cause |
|---|---|
| Waves absent in local PNG | Encoder (B4) |
| R(EXR) physical, PNG flat; S8 stable under `keyspan` | Auto/local exposure (A1) |
| R(EXR) < 3 | Lighting too flat: steps 3 to 6 |
| `skylight=0` kills shade, `VisualizeSkyLightIlluminance` > 25 klux or location-independent | Sky light cube (A3) |
| Brightness creeps up over seconds with exposure pinned | Lumen update lag/temporal (A2) |
| Disappears with `ScreenTraces 0` | Screen-trace view dependence |
| Drops with `ShowFlag.Specular 0` at grazing | Sheen (A4) |
| `cloudshadow=0` or `r.VolumetricCloud.ShadowMap 0` removes curtains | B1 |
| `ShowFlag.VolumetricFog 0` removes sheets | B2 |
| `FastSkyLUT 0` / `FastApplyOnOpaque 0` fix it | B3 |
| `ShowFlag.Atmosphere 0` makes mountain dark/correct | C1/C3 |
| `TraceDistanceScale 10` changes valleys and mountains | C2 |
| `Clipmap.ZRangeScale 1000` or VSM off changes far lit faces | C4 |

---

## 5. Physical reference values

All `[GENERAL]` unless a source is named; use as order-of-magnitude checks.

**Light (clear day, sun 60 degrees up).**
- Direct sun on a surface facing it: about 100 to 120 klux at 90 degrees `[GENERAL]`. The project uses 95 klux (direct normal), so a horizontal surface at 60 degrees gets about 82 klux.
- Sky on a horizontal surface, clear: about 10 to 25 klux (15 klux typical). Share of total illuminance: 12 to 20 %.
- Sunlit : open-shade ratio on the same horizontal surface: (82 + 15) / 15 = 6.5 : 1, i.e. 2.7 stops. Photographic rule of thumb: sunny day shadow detail about 2 to 3 stops under sun. Vertical faces away from the sun: sky half-dome plus ground bounce: 4 to 8 klux.
- Overcast: 5 to 30 klux total on a horizontal surface, no direct; shadows are soft, ratio between open and under-canopy 3 to 10 : 1.
- Under a cumulus shadow: direct is mostly gone (5 to 25 % forward-scattered), sky is brighter near clouds: total 25 to 40 % of sunlit. The project's 0.88 strength gives (11.4 + 15) / 97 = 27 %, in range.
- Moonlight (for night checks): 0.1 to 0.3 lux full moon; the project's 1.2 lux cap is deliberately brighter (`SR:80-92`).
- EV100 = log2(L x 100 / 12.5) = log2(8 L) with L in cd/m2. Sunny-16 (EV 15) is for 18 % grey at about 90 klux.

**Surface albedo (linear).**
- Epic's table: charcoal 0.02, fresh asphalt 0.02, worn asphalt 0.08, bare soil 0.13, green grass 0.21, desert sand 0.36, concrete 0.51, fresh snow 0.81 `[DOC physically-based-materials-in-unreal-engine]`.
- Basalt/volcanic rock: fresh dark basalt about 0.03 to 0.07; weathered, dusty or lichened 0.08 to 0.15 `[GENERAL; no page fetched]`. The project's 0.046 for stone, 0.016 to 0.07 for ground (`look_audit_ours.md` patch plan) is the dark end but legitimate. Luminance of 0.046 in 82 klux sun: 1,200 cd/m2 (EV 13.2); in 15 klux open shade: 220 cd/m2 (EV 10.8).
- Display values at a physical "sunny-16" exposure: rock 0.046 / 0.18 = 25 % of mid grey, about sRGB 60 to 70 in sun, about sRGB 20 to 25 in shade. If the shaded rock reads above ~sRGB 45 at the same exposure that makes the sunlit rock ~65, the shade is lit about 2x too much or the exposure/local exposure has moved.
- Metering reality: an auto-exposing camera pointed at an all-basalt scene will display it near mid grey (the "grey blender look"); fixed exposure or a narrow EV range for the day is how photographers keep dark ground dark.

**Specular.**
- Dielectric F0 for rock, dirt, moss: 0.03 to 0.05; Epic's 0.5 default = 4 % `[DOC physically-based-materials]`. UE: F0 = 0.08 x Specular, so Specular 0.375 to 0.625.
- Project: stone 0.35 -> 0.028, ground "mirror" 0.18 to 0.35 -> 0.014 to 0.028: at or below the real range. Do not lower further; Epic: "For very diffuse materials ... Resist [setting Specular to zero]. ... use a high Roughness" `[DOC physically-based-materials]`. Dryness comes from roughness 0.8 to 0.95 and cavity AO on specular.
- Fresnel at grazing is physical (0.24 at 75 degrees for F0 0.028 before roughness); what is missing in a real scene is occlusion of the sky from the hollows and the roughness damping, not lower F0.

**Sky light share.**
- Sky light intensity 1.0 reproduces the captured sky; 1.0 to 1.2 is fine. The share to check: sky irradiance on an open horizontal patch about 12 to 20 % of (sun + sky) at noon, 30 to 60 % at 15 degrees sun elevation.
- Atmosphere ground albedo for **bounce** should match the terrain (about 0.03 to 0.10 here), not Earth's 0.4 `[DOC sky-atmosphere-component: "Earth Albedo is 0.4"]`.
- Cloud shadow depth: 70 to 90 % reduction of direct under a thick cumulus; 0.88 is physically fine.

**Aerial perspective for a 5 km world** (from `planet_surface_from_altitude.md`, which sets the distance table): a mountain 2 km away on a clear day should lose a modest 10 to 25 % of contrast and shift toward blue; if the glow in the range is brighter than the sky behind it, the haze is over-lit or unshadowed.

---

## 6. Fix directions (not applied; ordered by expected payoff)

1. **Sky light placement**: move `SkyLight` to the camera/eye each frame (only on the reference atmosphere body) so the capture is made at the player's place; verify with step 4.
2. **Sky Atmosphere ground albedo** to the terrain's mean (0.05 to 0.08): lowers the lower-hemisphere cube, cloud ground contribution and multi-scatter.
3. **Lumen range**: PPV `LumenMaxTraceDistance` 20,000 -> 100,000 to 150,000 cm and `LumenSceneViewDistance` 20,000 -> 60,000 (cost 0.3 to 1 ms, `look_audit_ours.md` item 15); test far field.
4. **Exposure**: hold the day's EV in a narrow band (the `keyspan` experiment) and keep local exposure shadow 1.0 / highlight 1.0 by day; or tie EV to the sun's illuminance (physical sunny-16), not to a histogram.
5. **Sun shadows on air**: set `bCastShadowsOnAtmosphere = true` (and clouds if wanted); lower `CloudShadowOnAtmosphereStrength` (try 0.15 to 0.3) while keeping `CloudShadowOnSurfaceStrength` at 1.0; turn `NewFrameWeight` to about 0.2.
6. **Lumen lag**: `LumenSceneLightingUpdateSpeed` / `LumenFinalGatherLightingUpdateSpeed` 2 to 3, `DownsampleFactor` back to 16, `ShortRangeAO.BentNormal` 1, `Temporal.MaxFramesAccumulated` 6 to 8.
7. **Atmosphere LUT**: `FastApplyOnOpaque 0` and/or larger AP LUT if step 9 points there; `VolumetricRenderTarget.Mode 0`.
8. **VSM**: `Clipmap.ZRangeScale` back toward 1,000 if C4 is confirmed.
9. **Stream**: H.264 High or Main profile and higher bitrate for any judgement of sky smoothness.

---

## 7. Sources

Engine source (local, UE 5.8, read directly) under `.unreal/UE_5.8/Engine/`:
- `Source/Runtime/Renderer/Private/ReflectionEnvironmentRealTimeCapture.cpp` (capture position, time slicing cvars, lower hemisphere pass)
- `Source/Runtime/Engine/Private/Components/SkyLightComponent.cpp:264, 548-557`; `Source/Runtime/Engine/Public/Rendering/SkyAtmosphereCommonData.cpp:271-290`
- `Shaders/Private/SkyAtmosphere.usf:739-743, 821-832` (cloud shadow on air, ground bounce); `Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp` (cvars, `CloudShadowOnAtmosphereStrength` use)
- `Source/Runtime/Engine/Private/Components/DirectionalLightComponent.cpp:1071-1079` (defaults), `Classes/Components/DirectionalLightComponent.h:160-241`
- `Source/Runtime/Renderer/Private/VolumetricCloudRendering.cpp`, `VolumetricRenderTarget.cpp`, `VolumetricFog.cpp`, `Shaders/Private/VolumetricFog.usf:995-1045`
- `Shaders/Private/Lumen/LumenTracingCommon.ush:32-103` (sky on miss, leaking); `LumenHardwareRayTracingCommon.ush`; `Source/Runtime/Renderer/Private/Lumen/LumenSceneLighting.cpp:42-55`, `LumenScreenProbeGather.cpp:62-229`, `LumenVisualize.cpp:27-60`, `LumenDiffuseIndirect.cpp:88-231`, `LumenScene.cpp:86-116`, `LumenReflections.cpp:108-192`
- `Source/Runtime/Engine/Private/Scene.cpp:586-658` (PPV Lumen defaults); `Source/Runtime/Engine/Classes/Engine/Scene.h:1808-1815`
- `Shaders/Private/PostProcessHistogramCommon.ush:279-310` (local exposure); `Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp:60-100, 810-820`; `PostProcessVisualizeLocalExposure.cpp:12-24`
- `Source/Runtime/Renderer/Private/IlluminanceMeter.cpp:200-231` (sky light lux readout); `Source/Runtime/Engine/Public/ShowFlagsValues.inl`; `Source/Runtime/Engine/Private/UnrealClient.cpp:2433-2500` (HighResShot arguments); `Config/BaseEngine.ini:3140-3162` (buffer visualisation names)

Epic documentation (fetched 2026-10-14):
- Lumen technical details: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- Lumen GI and reflections: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine
- Sky lights: https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine
- Sky Atmosphere component: https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine
- Volumetric Cloud component: https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine
- Auto exposure (incl. local exposure): https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine
- Physically based materials: https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials-in-unreal-engine
- Physical lighting units (units only, no lux table): https://dev.epicgames.com/documentation/en-us/unreal-engine/using-physical-lighting-units-in-unreal-engine

Forum, bug tracker, talks:
- Lumen SkylightLeaking GI (Epic forum): https://forums.unrealengine.com/t/lumen-skylightleaking-gi/2695452
- UE-232061, cloud shadow / SkyView LUT north-pole assumption: https://issues.unrealengine.com/issue/UE-232061 (403 on fetch; claim from a search summary, `[UNVERIFIED]`)
- Lumen in Fortnite Chapter 4 (Skylight Leaking, Diffuse Color Boost, exposure): https://www.unrealengine.com/tech-blog/lumen-brings-real-time-global-illumination-to-fortnite-battle-royale-chapter-4 (search summary)
- SIGGRAPH 2022, Lumen (Wright et al.): https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf (listed, not read in full)
- Hillaire, SIGGRAPH 2020 / CGF paper, sky and atmosphere: https://blog.selfshadow.com/publications/s2020-shading-course/hillaire/s2020_pbs_hillaire_slides.pdf ; https://diglib.eg.org/handle/10.1111/cgf14050 (abstract only)
- Frostbite "Moving to PBR" (units): https://blog.selfshadow.com/publications/s2014-shading-course/frostbite/s2014_pbs_frostbite_slides.pdf (not read; named for EV100/lux conventions)
- I did **not** find or watch an Unreal Fest / Unreal Engine YouTube talk specific to these symptoms; no timestamps are claimed.

Project documents used: `docs/research/look_audit_ours.md`, `night_cloudshadows_stability.md`, `pixel_streaming.md`, `planet_surface_from_altitude.md`, `volumetric_clouds_small_planet.md`.
