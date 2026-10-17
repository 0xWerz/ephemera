# Getting Ephemera from 25 fps to 60 fps on an RTX 3060 (12 GB) without looking worse

Date: 2026-10-14. Scope: web research, a read of the project's config and source, and a read of the local UE 5.8 engine source (`.unreal/UE_5.8/Engine/Source`). Nothing was run, no project file was changed.

Evidence tags used below:

- **[src]** read in the local UE 5.8 engine source (path given).
- **[doc]** read on a fetched Epic or NVIDIA page (link given).
- **[proj]** read in this project's files (path:line given).
- **[snip]** seen only in a search snippet; the page itself was blocked or not opened.
- **[est]** my estimate. Numbers tagged this way are not measurements. They are there to rank work, and every one must be checked with the profiling plan in section 6.

Pages that refused to load (HTTP 403 or blocked): Epic's Fortnite Lumen blog and "Battle-testing UE 5.1" post, TechPowerUp, TechSpot, Eurogamer/Digital Foundry. Where a claim leans on one of them it is tagged [snip] or left out. I found no trustworthy measured RTX 3060 numbers for Silent Hill 2 or Black Myth: Wukong (only estimator sites), so those two games are not used as evidence.

---

## 0. The short version

1. The 25 fps is partly physics and partly our own settings. Native 1440p is 3.7 million pixels. Alan Wake 2's own published spec puts the RTX 3060 at the **Medium** preset, 1440p at 30 fps with DLSS or FSR **Balanced** (about 835p internal) or 1080p at 60 fps with **Performance** (540p internal). The Last of Us Part I's published spec puts the 3060 at **1080p/60 High** with baked lighting, and wants an RTX 2080 Ti class card for 1440p/60. Neither game does what we do: native resolution, fully dynamic GI, real-geometry grass.
2. The project ships on **Epic** scalability (`Quality = 3` in `EphemeraUserSettings.h`, applied with `SetOverallScalabilityLevel` at `EphemeraUserSettings.cpp:52`). Epic's own docs call Epic the tier that costs about 8 ms for Lumen at 1080p on a console, and High (about half the cost) the 60 fps tier. Epic tier also runs TSR with a 200% history, which Epic says makes TSR's most expensive pass four times dearer.
3. Two of the project's own choices look expensive and nearly invisible: Lumen **hit lighting** for reflections (`WindowsEngine.ini:6`, which Epic says not to use with non-trivial materials) and **up to about 700,000 grass instances that are probably in the ray tracing scene** (Epic's guideline is under 100,000, and Epic names grass as a ray tracing cost; confirm with `stat SceneRendering`).
4. The two frame-time measurements already in the repo (38 fps at 1080p, 47 fps at 75% of that) say **9 to 15 ms of the 26 ms frame does not shrink with resolution**. Cutting render scale alone would need about 50% (720p inside a 1440p window) to get near 60 fps, and by the second data point would still fall short. The fixed part is geometry, shadows and ray tracing scene work. Also, with TSR, Nanite keeps its triangle density tied to *output* resolution, so lowering screen percentage does not lower Nanite's cost unless `r.Nanite.ViewMeshLODBias` is changed [src] (section 3).
5. A realistic path: trim the fixed part to about 7 ms, trim the per-pixel part by a quarter, then render at about 75% (1080p inside a 1440p window) with TSR. That lands near 16 ms [est]. Native quality is kept where it shows (output resolution, TSR reconstruction, DLAA as an opt-in).

---

## 1. What this project does today (the facts that matter)

| # | Fact | Where | Why it matters |
|---|---|---|---|
| 1 | Default `Quality = 3`, menu label "Highest", `ScreenPercentage = 100`. No `DefaultScalability.ini` exists, so the engine's `BaseScalability.ini` Epic values apply unchanged. | `Source/Ephemera/Public/Player/EphemeraUserSettings.h`; `EphemeraUserSettings.cpp:52,58-60`; `Private/UI/SEphemeraHud.cpp:300-307` [proj] | The owner is on the most expensive tier by default. |
| 2 | Epic tier (group 3) sets: TSR history 200%, VSM resolution bias -1.5 (directional, still and moving), SMRT 8 rays x 4 samples, 4096 physical pages, volumetric fog grid 8 px x 128 slices, Lumen reflection downsample 1, foliage reflection roughness 0.4, GI downsample 16, short-range AO bent normals, mesh SDF tracing allowed, hit lighting allowed. High (group 2) sets: TSR history 100, VSM bias 0, fog 16 px x 64, reflection downsample 2, foliage roughness 0.2, GI downsample 32, no bent normals, hit lighting not allowed. | `.unreal/UE_5.8/Engine/Config/BaseScalability.ini` sections `[AntiAliasingQuality@3]` (line 85), `[ShadowQuality@3]` (240), `[GlobalIlluminationQuality@3]` (416), `[ReflectionQuality@3]` (536) and the `@2` sections [src] | These are the biggest single block of savings and the cheapest to take. |
| 3 | Hardware ray tracing on, Lumen HWRT on, **`r.Lumen.HardwareRayTracing.LightingMode=2`** (hit lighting for reflections). | `Config/Windows/WindowsEngine.ini:3,5,6` [proj]; meaning of mode 2: `Renderer/Private/Lumen/LumenHardwareRayTracingCommon.cpp:32-41` [src] | Mode 2 "calculates lighting at a ray hit point for reflections ... full material needs to be evaluated and shadow rays traced". The grass material is a Custom HLSL node with WPO and a two-sided foliage shading model. |
| 4 | The source says of Lumen HWRT: "significant scene update costs for scenes with more than 100k instances". | `LumenHardwareRayTracingCommon.cpp:17-27` [src] | See row 5. |
| 5 | Grass: three clump habits of 58 / 50 / 32 blades, 9 triangles per blade (5 rings), densities of 13, 4.5, 3.2 and 4.0 clumps per square metre (before patch thinning), within a **110 m** ring, as movable instanced static mesh components of 16 m cells. My count at full cover: about 19.5 clumps and about 1,070 blades per m2, so **about 740,000 instances, 41 million blades, 370 million source triangles** in the ring (upper bound; bare rock, ice and steep ground lower it). Instanced meshes are in the ray tracing scene by default (`r.RayTracing.Geometry.InstancedStaticMeshes=1`, cull radius 100 m). | `Tools/build_slice.py` (`grass_clumps`, grass rules, `ground_cover_radius_m`, 110.0); `EphemeraPlanetBuilder.cpp` (`BuildGrassClump`, `Rings = 5`); `EphemeraGroundCover.cpp:253-273`; `Engine/Private/InstancedStaticMesh.cpp:112-136` [proj][src] | Seven times Epic's ray tracing instance guideline, plus a very large Nanite load. |
| 6 | At 1440p and 70 degrees vertical FOV one pixel is about 1 cm at 10 m. A 0.75-1.3 cm blade is narrower than a pixel beyond roughly 10-15 m. Most of the ring (70% of its area lies beyond 60 m) is sub-pixel geometry. | calculation from `build_slice.py` blade widths and `EphemeraPlayer.cpp` FOV handling [est] | The far grass costs full geometry price and gives TSR flicker, not detail. |
| 7 | Grass casts no shadow, has `bAffectDistanceFieldLighting = false`, WPO switched off beyond 40 m, WPO displacement bound 24 cm. | `EphemeraGroundCover.cpp:256,269,272`; `build_slice.py` (`max_world_position_offset_displacement`, 24.0) [proj] | Good choices. Keep them. (A previous research note proposed turning grass shadows on: do not. Epic says WPO geometry "always invalidates cached pages every frame" [doc VSM page].) |
| 8 | Sun rotation is set from `UpdateSky()` every frame; `Clipmap.FirstLevel=8`, `ZRangeScale=5000`. | `EphemeraSkyRig.cpp` (`UpdateSky`, `Sun->SetWorldRotation`); `DefaultEngine.ini:42-43` [proj] | Epic: "Any light movement or rotation will invalidate all cached pages for that light" [doc VSM]. A rotation every frame means the shadow cache never survives. |
| 9 | Hand lamp: spot light, 140 m attenuation, 40 degree outer cone, volumetric scattering x3, casts volumetric shadow, moves with the camera. Night adds a second dynamic light on top of everything else. | `Private/Player/EphemeraPlayer.cpp:85-103` [proj] | A moving VSM light with a 140 m reach. It likely explains much of the 5 ms night penalty (32 fps vs 38 fps) [est]. |
| 10 | Clouds: `r.VolumetricRenderTarget.Mode=1`, `SampleMinCount=32`, `DistanceToSampleMaxCount=2`, `HighQualityAerialPerspective=1`. Engine defaults are Mode 0, 2, 15, 0 and `ViewRaySampleMaxCount=768`. Mode 1 traces at half resolution per axis; Mode 0 at quarter per axis. | `DefaultEngine.ini:88-95` [proj]; `Renderer/Private/VolumetricRenderTarget.cpp:121-153`, `VolumetricCloudRendering.cpp:53,58,68` [src] | Mode 1 traces **four times as many cloud rays** as the default. `DistanceToSampleMaxCount=2` reaches the 768-sample cap 7.5 times sooner than the default 15 km. |
| 11 | Sky atmosphere: `TraceSampleCountScale = 4`, fast sky LUT 512x256, sample counts 64-128. | `EphemeraSkyRig.cpp`; `DefaultEngine.ini:69-84` [proj] | Mostly resolution-independent; probably 0.2-0.6 ms [est]. Low priority. |
| 12 | Volumetric fog: exponential height fog with `bEnableVolumetricFog`, plus a camera-following volume material. Epic grid is 8 px / 128 slices. | `EphemeraSkyRig.cpp` (`Fog`) [proj] | At 1440p, Epic's grid is about 7.4 million froxels against about 0.9 million at High. |
| 13 | Ocean uses the Single Layer Water shading model. Bloom is the standard method (`BM_SOG`). Auto exposure is histogram. | `build_slice.py` (`MSM_SINGLE_LAYER_WATER`); `EphemeraSkyRig.cpp` [proj] | Right choices. Post processing is not where the time goes. |
| 14 | `r.OneFrameThreadLag=0`, `r.GTSyncType=1`: the game does not run a frame ahead. | `DefaultEngine.ini:63-64` [proj] | Costs nothing while GPU-bound, but removes CPU/GPU overlap. With thousands of instanced components and a large ray tracing scene the render thread may matter; check `stat unit`. |
| 15 | Sky light has `bRealTimeCapture = true`. | `EphemeraSkyRig.cpp` [proj] | Time-sliced by default (`r.SkyLight.RealTimeReflectionCapture.TimeSlice=1`, 2 cube faces per frame) [src]; small. |

---

## 2. Why Alan Wake 2 and The Last of Us Part I run faster on this card

### 2.1 What the studios themselves publish for an RTX 3060

| Game | Tier the publisher assigns to an RTX 3060 | Internal resolution implied | Source |
|---|---|---|---|
| Alan Wake 2 | "Recommended": Medium preset, **1440p at 30 fps or 1080p at 60 fps**, DLSS/FSR 2 **Balanced to Performance**, 8 GB VRAM. Even the minimum tier (RTX 2060) assumes DLSS Quality. | Balanced at 1440p = 1485x835 (1.24 MP). Performance at 1080p = 960x540 (0.52 MP). | [GGRecon requirements table](https://www.ggrecon.com/guides/alan-wake-2-pc-requirements-specs/), relaying Remedy's spec; scale factors from the [NVIDIA DLSS programming guide](https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf) (Balanced 1.724x, Performance 2.0x) |
| The Last of Us Part I | "Recommended": **1080p, 60 fps average, High**, RTX 3060 (8 GB). 1440p/60 High needs an RX 6750 XT or **RTX 2080 Ti**. 4K/60 Ultra needs RTX 4080. No upscaler is stated for any tier. | Native 1080p (2.07 MP) | [PlayStation PC page](https://www.playstation.com/en-us/games/the-last-of-us-part-i/pc/) |
| Us (today) | Epic tier, native 1440p, TSR history 200% | 3.69 MP rendered, history accumulated at 2x output | [proj] |

Pixel arithmetic against our native 1440p (3.69 MP): Alan Wake 2 at 30 fps renders **3.0 times fewer pixels** (Balanced) and at 60 fps **7 times fewer** (Performance, 1080p output). TLOU at 60 fps renders **1.8 times fewer**, with lighting that costs almost nothing at run time.

Launch-era third-party data agrees that these games are not light: DSOGaming found that at 1440p Ultra "the only GPUs that were able to provide a constant 60fps ... were the RX 7900XTX and the RTX 4090" in TLOU Part I ([article](https://www.dsogaming.com/pc-performance-analyses/the-last-of-us-part-i-pc-performance-analysis/)), and that Stalker 2 (UE5, Lumen) at 1080p Epic needs "a GPU equivalent to the AMD Radeon RX 6900XT" for over 60 fps ([article](https://www.dsogaming.com/pc-performance-analyses/stalker-2-benchmarks-pc-performance-analysis/)). An RX 6900 XT is roughly twice an RTX 3060 [est], so a UE5 Lumen game at Epic and native resolution on a 3060 is a 30 fps experience at 1080p. Our 38 fps at 1080p is in line with that, not behind it.

### 2.2 Lighting: baked or optional-ray-traced versus fully dynamic Lumen

- TLOU Part I: DSOGaming's verdict is that "the best thing about The Last of Us Part I is its baked lighting and global illumination." Baked GI costs about nothing per frame. Naughty Dog's 2013 SIGGRAPH talk on the original chose precomputed lightmaps (found via search; the 2022 remake's method is not documented in anything I could open) [snip].
- Alan Wake 2: Remedy's own article describes a "brand-new GPU-driven rendering pipeline" (mesh shaders and meshlet culling, "occlusion culling ... down to a single-pixel precision"), fully ray-traced direct lighting and path-traced indirect as the high-end PC features, and vegetation animated by a GPU skinning system ("almost 300,000 bones in Cauldron Lake") ([Remedy](https://remedygames.com/article/how-northlight-makes-alan-wake-2-shine)). The same article says nothing about what the non-ray-traced path uses for GI, so I do not claim a method for it. What the spec table shows is that on a 3060 the game is played without ray tracing and with an upscaler.
- Us: Lumen GI and Lumen reflections at Epic quality, hardware ray traced, sun that moves every frame. Epic's numbers for that: **Epic produces "around 8 milliseconds ... on next-generation consoles for global illumination and reflections" at "1080p internal resolution"**; High targets 60 fps; each level down costs "approximately half of the level above it" ([Lumen technical details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine), [Lumen performance guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine)). Fortnite's 60 fps modes used Lumen **software** ray tracing, not hardware ([Epic tech blog](https://www.unrealengine.com/tech-blog/lumen-brings-real-time-global-illumination-to-fortnite-battle-royale-chapter-4), page blocked, known through a search summary) [snip].

### 2.3 Geometry and vegetation

- Alan Wake 2 animates its vegetation with GPU skinning (Remedy's article) and neither game is documented to draw each blade as geometry. We draw up to about 41 million nine-triangle blades in a 110 m ring.
- Their worlds are authored scenes with occlusion, fixed time of day and, for TLOU, baked shadows. Ours is a 5 km sphere with a continuously moving sun and a 12.8 km shadow range.
- Epic's own guidance for Nanite foliage is to stop using WPO because it "adds per vertex calculations and forces sub-optimal cluster bounds ... that contribute to high overdraw cost" ([Nanite foliage page](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage)), and the 5.7 notes repeat that "WPO is generally not suitable for Nanite" ([Tom Looman's 5.7 summary](https://tomlooman.com/unreal-engine-5-7-performance-highlights/)). Our grass uses WPO with a 24 cm bound on blades 15-64 cm tall.

### 2.4 The honest verdict for the owner

About half the gap is not fixable without a visible trade: both games reach their frame rates on a 3060 by rendering at 540-835p internal (Alan Wake 2) or 1080p with baked light (TLOU), and the UE5 Lumen game I could check (Stalker 2) is no lighter. The other half is ours: the Epic tier, hit lighting, grass in the ray tracing scene, cloud tracing settings, a shadow cache that is invalidated every frame, and a render-scale slider that does not reduce Nanite work. Those can be removed without a change anyone would notice in a side-by-side.

---

## 3. What our own numbers say about where the frame goes

Data points already in the repo (hidden 1080p runs on the 3060): 38 fps by day (26.3 ms), 32 fps at night with the lamp (31.3 ms), and 47 fps with the stream instance drawing 75% of the pixels (21.3 ms; `docs/unreal/DIRECTION.md` "Playing it in a browser"). The owner sees about 25 fps (about 40 ms) at an assumed 1440p.

Model: `t = F + P x (pixels / 2.07 MP)` with F the resolution-independent part.

- From 100% vs 75% at 1080p: 26.3 - 21.3 = 5.0 ms for 0.4375 of the pixels, so P = 11.4 ms and **F = 14.9 ms**.
- From 1080p vs the owner's 40 ms at 1440p: P = 17.6 ms and **F = 8.7 ms**.

The second point is soft (resolution and fps are the owner's estimate), the first is a measurement with a different sky. Treat the truth as **F between about 9 and 15 ms**, i.e. 35-55% of today's frame does not shrink when pixels do. At 50% render scale on 1440p (720p internal, the look of DLSS Performance) the two fits predict 16.5 ms and 20 ms: borderline at best. That is why settings, not just scale, have to change.

Why F is large, from the engine source:

- **Nanite follows output resolution.** With TSR, the Nanite triangle edge target is multiplied by the screen-percentage ratio, so 75% scale still selects the triangle density of the full output (`Renderer/Private/DeferredShadingRenderer.cpp:381-392, 1636-1645`; `r.Nanite.ViewMeshLODBias.Enable=1`, `Offset=0`, `Min=-2`) [src]. Raster and culling work on 41 million blades barely drops with scale.
- **Ray tracing scene work is per instance**, not per pixel: "The TLAS is rebuilt every frame ... costs are mostly proportional to how many mesh instances go into the acceleration structure" ([Ray tracing performance guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/ray-tracing-performance-guide-in-unreal-engine)).
- **Shadow depth** work follows page count and geometry, and the sun invalidates the cache every frame.
- Cloud shadow map, sky LUTs, Lumen scene update, and CPU/render-thread time are also resolution-independent.

First diagnostic, two runs (section 6): measure GPU ms at `r.ScreenPercentage 100` and `50` from the same camera, then fit `P = (t100 - t50) / 0.75` and `F = t100 - P`.

---

## 4. Ranked GPU costs and the levers for each

Ranking is by *likely size of the cost in this scene* at 1440p on a 3060, then lever by lever. All ms figures are [est] unless a source is cited; the sources give orders of magnitude, not this scene's numbers.

### 4.1 Nanite grass: raster, base pass, instance culling (likely 4-8 ms, mostly fixed) [est]

Why: 740k instances, 41M blades, sub-pixel ribbons, a two-sided foliage material with a Custom HLSL node, WPO within 40 m.

| Lever | Setting | Saving | Visual cost |
|---|---|---|---|
| Let Nanite LOD follow render scale | `r.Nanite.ViewMeshLODBias.Enable=0` (or `Offset=+0.5`) [src DeferredShadingRenderer.cpp] | At 75% scale the triangle target moves from 0.75 px to 1.0 px of the rendered image, roughly 44% fewer triangles at the same LOD cut [est]. | Slightly coarser silhouettes before TSR; check grass and rock edges. |
| Coverage-preserving thinning of far grass | In `GrowCell`: beyond ~35-40 m use clumps with 12-16 wider blades at about a third the clump density (blade width x2.5), or shorten the grass ring from 110 m to about 70 m and let the ground material carry the far colour (it already uses scanned moss/aerial-grass textures). Ring area 110 m -> 70 m cuts instances by 60%. | -2 to -6 ms [est] | Low if coverage (blade count x width) is held; the far field is sub-pixel anyway. A/B with fixed screenshots. This is standard practice in shipped grass (blade count falls with distance, width rises). |
| Nanite dynamic budgets | `r.Nanite.PrimaryRaster.TimeBudgetMs` (for example 3.0), `r.Nanite.ShadowRaster.TimeBudgetMs` [src NaniteCullRaster.cpp:344-354]; uses `PixelsPerEdgeScaling` (default 30% floor) | A safety net that coarsens Nanite when over budget. | Not verified that it acts on non-tessellated clusters in 5.8. Test. |
| Global edge length | `r.Nanite.MaxPixelsPerEdge` 1.0 -> 1.5 [src] | Large fraction of Nanite raster [est] | Visible faceting on rocks at 2 px; try 1.25-1.5. |
| Tighter WPO bounds | Keep `max_world_position_offset_displacement` as low as the wind needs (24 cm today). Epic: Nanite splits WPO meshes into smaller clusters whose bounds grow with the displacement; "You must clamp the amount of displacement" [doc Nanite page] | Small to medium | None if the wind amplitude is unchanged. |
| Cheaper far shading | In the Custom node, branch on camera distance to skip the translucency/subsurface and wind terms beyond ~30 m | Base pass pixel cost [est] | None at that distance. |

Leave the following as they are: grass casts no shadow, no distance-field contribution, WPO off past 40 m.

### 4.2 Lumen (likely 8-12 ms at Epic with hit lighting and HWRT on a 3060) [est]

Anchor: Epic quotes about 8 ms (Epic) and about 4 ms (High) for GI plus reflections at 1080p internal on consoles; the 3060 is at best a little below a PS5 [est], and we run Epic with extras.

| Lever | Setting | Saving | Visual cost |
|---|---|---|---|
| **Take grass out of the ray tracing scene** | `SetVisibleInRayTracing(false)` on the grass ISM components in `GrowCell` (rocks stay). Epic: "Another is grass - it has large bounds and high geometry complexity ... disable **Visible in Ray Tracing**"; the 60 fps console preset disables `r.RayTracing.Geometry.InstancedStaticMeshes`; "fewer than 100,000 instances in the Ray Tracing Scene after culling" [doc RT guide, Lumen guide]. Check `stat SceneRendering`: "Ray tracing active instances". | -2 to -6 ms, mostly fixed cost [est] | Nearly none: grass stops appearing in Lumen reflections/GI traces. Lumen already treats foliage specially. |
| Surface-cache lighting for ray hits | `r.Lumen.HardwareRayTracing.LightingMode=0` (project setting "Ray Lighting Mode"); scalability flag `r.Lumen.HardwareRayTracing.HitLighting.Allowed=0` [src]. Epic: hit lighting for reflections "not recommended ... unless materials are trivial" [doc Lumen guide]. | -0.5 to -2 ms (a third-party Lumen write-up gives 0.5-1 ms) [est][snip] | Water reflecting cliffs uses cached lighting instead of exact; hard to see. |
| Foliage reflection rays | `r.Lumen.Reflections.MaxRoughnessToTraceForFoliage=0` (Epic tier 0.4, High 0.2). Epic: setting it to 0 "can give some significant performance wins" with foliage reflections "often hard to see" [doc Lumen guide] | -0.3 to -1 ms [est] | None visible. |
| Reflection ray density | `r.Lumen.Reflections.DownsampleFactor=2` (High). Third-party: "often saves 1-2ms" [snip: strayspark]; Epic: reflections "more blurry and less stable" [doc] | -1 to -2 ms [est] | Softer water/wet-rock reflections; TSR hides some noise. |
| GI probe density | Epic -> High values: `ScreenProbeGather.DownsampleFactor` 16 -> 32, `NumAdaptiveProbes` 8 -> 16, `ShortRangeAO.BentNormal` 1 -> 0, `ShortRangeAO.DownsampleFactor` 2, `RadianceCache.ProbeResolution` 32 -> 16, `StochasticInterpolation` 0 -> 1 or 2 [src BaseScalability.ini]. Epic's 5.7 notes: stochastic interpolation 2 "saves ~0.5ms on console at 1080p" [Looman]. | -1 to -2.5 ms [est] | Slightly softer small-scale indirect light. |
| Scene lighting update rate | `r.LumenScene.DirectLighting.UpdateFactor` 32 -> 64, `r.LumenScene.Radiosity.UpdateFactor` 64 -> 128 (High values) | -0.3 to -0.8 ms [est] | Light response lags a little when the sun moves. |
| Translucency volume | High values: `GridPixelSize` 64, `TraceFromVolume` 0 | -0.3 to -0.5 ms [est] | Lumen GI on translucent surfaces (the ocean is Single Layer Water, so little). |
| Software Lumen A/B | `r.Lumen.HardwareRayTracing=0` | Unknown here; Epic: software tracing "is the only performant option in scenes with many overlapping instances" and was Fortnite's 60 fps path; hardware has "the highest setup cost in large scenes" [doc]. Distance fields are already built (`r.DistanceFields.MaxPerMeshResolution=64`). | Lower quality for thin geometry and off-screen reflections. Measure after the grass is out of the RT scene; keep HWRT if the gap is small. |
| Lumen Lite | `r.Lumen.FinalGatherMethod=0` (Irradiance Field Gather: "Faster but lower quality GI. Targeted at mid range PC" [src Lumen.cpp:55-61]). UE 5.8 release notes: Lumen Lite (Beta) is "twice as fast as Lumen high quality" [doc 5.8 notes]. | -2 to -3 ms on top of High [est] | Visibly lower GI quality. Offer only as a "Performance" tier. |

### 4.3 Virtual shadow maps (likely 3-6 ms by day, 6-10 ms at night with the lamp) [est]

Epic: shadow cost has two independent parts, depth rendering (pages x geometry) and projection (rays x samples per pixel); "Non-Nanite geometry is much more expensive to render into VSMs than Nanite geometry"; light movement invalidates all of a light's cached pages [doc VSM page].

| Lever | Setting | Saving | Visual cost |
|---|---|---|---|
| Resolution bias | `r.Shadow.Virtual.ResolutionLodBiasDirectional` and `...Moving` from -1.5 (Epic) to 0 (High) [src BaseScalability.ini]. "Lowering by -1 doubles resolution (costs performance)" [doc]. Bias -1.5 is 2.8x linear, up to 8x the texels. | -1 to -3 ms when pages are uncached [est] | Contact-shadow edges a touch less crisp; at 0 VSM is already about one texel per pixel. |
| Keep the cache alive | Update `Sun->SetWorldRotation` only when the direction has moved more than about 0.1 degrees from the last applied one. Walking at 6 m/s on a 5 km planet turns the local sky by 0.069 degrees per second, so that is a step every ~1.5 s while walking and none while standing still (apart from the day cycle). | 0 to -3 ms [est]; **verify first** | A shadow edge moves at most 0.17 cm per metre of caster distance at each step. Verify the cache is the problem: `r.Shadow.Virtual.ShowStats 1` with `r.ShaderPrintEnable 1`; compare to `r.Shadow.Virtual.Cache.ForceInvalidateDirectional 1` (forces the same behaviour) [src]. |
| Shadow range by time of day | `Clipmap.ZRangeScale` 5000 only while the sun is below the local horizon (the far side of the planet is the occluder at night); 1000 (default) by day. `FirstLevel=8` stays. | 0 to -1 ms [est] | None by day. |
| Penumbra rays | `SMRT.RayCountDirectional` 8 (Epic tier; engine default 7) -> 4-6, `SamplesPerRayDirectional` 4; Epic: "Using values between 4 and 8 samples works well" [doc]. Reduce the source angle before reducing rays [doc]. | -0.3 to -0.8 ms [est] | A little penumbra noise (TSR smooths). |
| Lamp | `AttenuationRadius` 14000 cm -> ~6000 cm; `r.Shadow.Virtual.SMRT.RayCountLocal` 8 -> 4; `ResolutionLodBiasLocal` stays 0 with `...Moving` +1 (Epic tier already). Test `Lamp->SetCastShadows(false)` once to learn the true cost. Page count scales with the light's extent [doc]. | -1 to -3 ms at night [est] | Beam reaches 60 m instead of 140 m with VSM shadows; Lumen and fog still light farther. |
| Safety net | `r.Shadow.Virtual.DynamicRes.TimeBudgetMs` (for example 2.0; experimental) raises the LOD bias only when over budget [src VirtualShadowMapArray.cpp:786-792] | Caps the worst frames | Only when over budget. |

### 4.4 Volumetric clouds (likely 2-4 ms) [est]

| Lever | Setting | Saving | Visual cost |
|---|---|---|---|
| Trace density | `r.VolumetricRenderTarget.Mode` 1 -> 0 [src VolumetricRenderTarget.cpp:121-153]. Epic: Mode 0 "recommended for quality in fast-paced gameplay"; Mode 1 "is more expensive but looks higher quality" [doc Volumetric Cloud page]. Switch to Mode 1 only when far above the ground (the limb view that motivated it). | 4x fewer rays: -1 to -2.5 ms [est] | Softer cloud edges up close; TSR and the bilateral upsample (`UpsamplingMode=4`, default) cover most of it. |
| Samples | `r.VolumetricCloud.ViewRaySampleMaxCount` 768 (default, not set by the project) -> 192-256; `SampleMinCount` 32 -> 12-16; keep `DistanceToSampleMaxCount=2` | -0.5 to -1.5 ms [est] | Banding in thick cloud if too low; thin layer (260 m) tolerates it. |
| Aerial perspective | `r.VolumetricCloud.HighQualityAerialPerspective` 1 -> 0 on the ground (the engine default is 0; it is "a second pass to trace the aerial perspective per pixel on clouds" [src VolumetricCloudRendering.cpp:83]) | -0.2 to -0.5 ms [est] | Cloud haze slightly less exact. |
| Cloud shadow map | `r.VolumetricCloud.ShadowMap.RaySampleMaxCount` 128 -> 64, `Shadow.ViewRaySampleMaxCount` 80 -> 32. Epic: ray-marched cloud shadows are "more expensive", Beer shadow maps cheaper [doc]. | -0.2 to -0.6 ms [est] | Cloud shadows slightly smoother. |

### 4.5 Temporal super resolution (likely 2.5-3.5 ms at 1440p on Epic) [est]

Epic: the history update is "the most expensive pass in TSR's total GPU cost because the history is rendered at display resolution"; at 200% it is "4x more expensive"; "Epic and Cinematic use 200"; "If you're concerned about the project's frame budget being too costly, you can use a High (or lower) engine scalability group since the Epic anti-aliasing scalability carries a higher GPU cost" ([TSR page](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine)).

| Lever | Setting | Saving | Visual cost |
|---|---|---|---|
| History resolution | `r.TSR.History.ScreenPercentage` 200 -> 100 (or `sg.AntiAliasingQuality=2`) | -1 to -2.5 ms at 1440p [est] | At 100% render scale the 200% history is supersampling; the loss is a little sub-pixel stability on grass. Compare grass crops. |
| Thin geometry | Leave `r.TSR.ThinGeometryDetection` at its default (auto only with `r.Nanite.Foliage`); `Coverage.ShadingRange=1` is cheaper than 3 [doc thin-geometry page via repo note] | Small | Only if grass flickers at lower render scale. |

### 4.6 Volumetric fog (likely 1-2 ms when on) [est]

Epic's 4.27 page: about 1 ms on PS4 at High and about 3 ms on a GTX 970 at Epic; the grid is set by the Shadow group ([4.27 page](https://docs.unrealengine.com/4.27/BuildingWorlds/FogEffects/VolumetricFog/), via search snippet). 

| Lever | Setting | Saving | Visual cost |
|---|---|---|---|
| Grid | `r.VolumetricFog.GridPixelSize` 8 -> 16, `GridSizeZ` 128 -> 64, `HistoryMissSupersampleCount` 4 -> 2 [src BaseScalability.ini] | About 8x fewer froxels: -0.5 to -1.5 ms [est]. The camera-following volume material runs once per froxel, so it shrinks too. | Slightly coarser mist; temporal reprojection hides most. Banding at distance is fixed by `GridSizeZ`, per an Epic forum reply. |

### 4.7 Post processing, sky atmosphere, translucency (about 2-3 ms together; do not spend visual quality here) [est]

- Bloom (`BM_SOG`, `r.Bloom.ScreenPercentage 50`), histogram auto exposure, local exposure (the repo's own look audit estimates about 0.2 ms), film grain: together about 1 ms. Leave.
- Sky atmosphere LUTs are resolution-independent. If the profile shows them above 0.5 ms, lower `Atmosphere->TraceSampleCountScale` from 4 to 2 and the fast-sky sample counts from 64-128 to 32-64.
- Ocean on Single Layer Water is the cheap way to do it. Leave.

### 4.8 Frame structure (check, not a known cost)

- `stat unit`: if "Draw" (render thread) is above about 10 ms, the thousands of movable instanced components and the ray tracing scene gather are costing CPU time. `r.OneFrameThreadLag=0` removes the overlap that would hide it.
- VRAM: Streaming pool 4,000 MB, VSM pages (4096 at Epic), TSR history at 200%, ray tracing acceleration structures, and DLSS buffers (about 200 MB at 1440p for the transformer presets, per NVIDIA's table) share 12 GB. Check `stat RHI` and `memreport`.

---

## 5. Upscaling, "native" and the options menu

### 5.1 What each option costs and looks like

| Option | Render fraction at 1440p output | Internal pixels | Cost / quality notes |
|---|---|---|---|
| Today: TSR, 100%, history 200% | 100% | 3.69 MP (history accumulated at 5120x2880) | Highest quality, highest cost; Epic tier. |
| TSR 100%, history 100 ("true native") | 100% | 3.69 MP | The sensible native. |
| TSR 77% | 1971x1109 | 2.19 MP | 41% fewer pixels. Epic's screen-percentage page calls 71-100% ideal for normal-DPI desktop rendering ([page](https://dev.epicgames.com/documentation/en-us/unreal-engine/screen-percentage-with-temporal-upscale-in-unreal-engine)); the TSR page says "Rendered frames approach the quality of native 4K with input resolutions as low as 1080p" ([TSR page](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine)). A 1080p input to a 1440p output is the easier case. |
| TSR 75% | 1920x1080 | 2.07 MP | Exactly the hidden-run resolution: 38 fps by day today before any other change. |
| TSR 67% | 1715x965 | 1.65 MP | Still within Epic's quoted comfort range. |
| DLAA | 100% | 3.69 MP | NVIDIA's native-resolution AA. 3060 cost at 1440p output: **0.98 ms (presets E/F, deprecated), 2.39 ms (presets J/K, default K), 6.53 ms (L)** (NVIDIA table below). Quality gain over TSR at 100% is probably small and the cost is not [est]. |
| DLSS Quality | 66.7% (1.5x) = 1707x960 | 1.64 MP | Preset K default; about 2.4 ms on a 3060. |
| DLSS Balanced | 58% (1.724x) = 1485x835 | 1.24 MP | Preset K default; about 2.4 ms. |
| DLSS Performance | 50% (2.0x) = 1280x720 | 0.92 MP | Preset **M** is the default and costs **5.05 ms** on a 3060 at 1440p; presets J/K cost 2.39 ms. |
| FSR | any | any | Not researched for 5.8. TSR already plays that role on every GPU; on a game aimed at an RTX card FSR adds little. |

DLSS cost on the RTX 3060, NVIDIA's own table (library only, Performance-mode input, no 3D renderer; the guide warns real integrations vary), from the [DLSS programming guide, 31 March 2026](https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf):

| Preset | 1920x1080 out | 2560x1440 out | 3840x2160 out |
|---|---|---|---|
| E, F (older CNN, deprecated) | 0.60 ms | 0.98 ms | 2.08 ms |
| J, K (transformer) | 1.45 ms | 2.39 ms | 5.05 ms |
| L (transformer, newest) | 3.96 ms | 6.53 ms | 13.81 ms |
| M (transformer) | 2.81 ms | 5.05 ms | 11.10 ms |

The guide says K is "the default preset for DLAA/Balanced/Quality", that M is the default for Performance and L for Ultra Performance, and that L and M are "peak performant on RTX 40 series GPUs and above." On a 3060, then, DLSS Performance with its default preset costs more than twice DLSS Quality, and the newest presets eat a third of a 60 fps frame. If DLSS is offered, **pin preset K for every mode on 30-series cards.** NVIDIA's plugin page lists downloads for UE 5.5 to 5.8 (package 8.8.0, DLSS 4.5) with Super Resolution, DLAA, Ray Reconstruction, Frame Generation and Reflex ([NVIDIA](https://developer.nvidia.com/rtx/dlss/get-started)). Frame generation is not recommended: it adds input latency, which this project works to remove (`EphemeraFramePacer`, `r.OneFrameThreadLag=0`).

TSR's own cost at 1440p on this card is not published. Epic's page gives only examples on unnamed hardware (about 0.79 ms at 100% against 0.43 ms at 50% in Valley of the Ancient; Fortnite's TSR "effective cost" 1.5 ms with async compute). Plan on 1.0-1.6 ms at History 100 [est] and measure `TemporalSuperResolution`.

The owner earlier withdrew DLSS to "keep things native". The honest framing: **at 100% output resolution with History 100, TSR already is native.** DLSS adds nothing there. Its only job is to let the player trade resolution for frames at a quality higher than TSR gives at the same input, and on a 3060 that costs 2.4 ms before saving anything.

### 5.2 Recommended menu

Replace the raw "Render resolution" slider (40-100% in 5% steps, `SEphemeraHud.cpp:306`) and the four-way "Graphics quality" with:

1. **Frame rate target**: "Smooth (60 fps)", "Balanced (45)", "Quality (30)", "Custom". On first launch run `UGameUserSettings::RunHardwareBenchmark` and pick the default. "Smooth" selects the settings in section 4 plus Auto resolution.
2. **Resolution**: "Native (100%)", "Quality (77%)", "Balanced (67%)", "Performance (58%)", and **Auto (hold target)**. Show the rendered size live: "Rendering 1920x1080, shown at 2560x1440". Auto uses the engine's dynamic resolution: `r.DynamicRes.OperationMode=2`, `r.DynamicRes.FrameTimeBudget` = 1000/target, `MinScreenPercentage` 58, `MaxScreenPercentage` 100 (it sizes the preallocated render targets) [src DynamicResolution.cpp, UnrealEngine.cpp:539]. Epic says temporal upsampling lets "primary screen percentage changes ... be hidden by dynamic resolution" ([page](https://dev.epicgames.com/documentation/en-us/unreal-engine/screen-percentage-with-temporal-upscale-in-unreal-engine)).
3. **Upscaler**: "Unreal TSR" (default, all GPUs), "NVIDIA DLSS" (shown on RTX only, preset K pinned), "NVIDIA DLAA" (native, for users with headroom).
4. **Advanced** (hidden behind a toggle): the individual levers, named plainly: Reflections, Global lighting, Shadows, Mist, Clouds, Grass distance. Keep "Rock detail as geometry (very slow)" where it is.

Name the top tier "Cinematic" and say it is meant for faster cards. Today's default, "Highest" (Epic tier at 100%), is the configuration measured at 38 fps (1080p) and reported at about 25 fps (the owner's screen).

---

## 6. Profiling plan

Use the hidden test harness (`tools/hidden_test/run.sh`, per the project's rule never to open a visible window). Three fixed camera positions: meadow at noon looking at the horizon, a rock field, and the night lamp in mist. 300 frames each, after a 5 s warm-up, same weather preset, the sun frozen for the cache tests and moving for the real ones.

### 6.1 Steps

1. `stat unit`: Frame, Game, Draw, GPU. If GPU is not the largest, stop and look at the render thread.
2. `r.ScreenPercentage 100` then `50`, same view: `P = (t100 - t50) / 0.75`, `F = t100 - P` (section 3).
3. `stat gpu` and `ProfileGPU`. For stable pass times set `r.RDG.AsyncCompute 0` and `r.Lumen.AsyncCompute 0` while profiling (both are recommended by Epic's docs for profiling only); the sum will read higher than the real frame.
4. Counters: `stat SceneRendering` ("Ray tracing total instances" and "Ray tracing active instances", target under 100,000), `r.Nanite.ShowStats 1`, `stat NaniteRayTracing`, `r.Shadow.Virtual.ShowStats 1` with `r.ShaderPrintEnable 1` (watch invalidated pages: near 0 is healthy [doc VSM]), `stat RHI` and `memreport` for VRAM.
5. One-switch A/B runs, each against the same baseline: `r.Lumen.HardwareRayTracing.LightingMode 0`; grass invisible in ray tracing; `r.Lumen.HardwareRayTracing 0`; `sg.AntiAliasingQuality 2`; `sg.ShadowQuality 2`; `r.VolumetricRenderTarget.Mode 0`; `r.VolumetricFog.GridPixelSize 16` with `GridSizeZ 64`; lamp shadows off; sun update quantised; `r.Nanite.ViewMeshLODBias.Enable 0` at 75%. Log GPU ms per switch.
6. Order of work: whichever of Nanite, ray tracing scene and VSM is largest in the profile goes first, not the order in this note.

### 6.2 Pass names and what a healthy frame looks like

`stat gpu` names are from the engine source (`DECLARE_GPU_STAT` entries in `Renderer/Private`). The "target" column is **my allocation** for a 16.6 ms frame at 1440p output with about 1080p internal on a 3060: it follows Epic's published Lumen budget (about 4 ms for GI plus reflections at 1080p) and a third-party example split for 60 fps (Lumen GI 3-4, reflections 1-2, scene update 1, Nanite raster 2-3, shadows 2, post 1-2 ms; [strayspark](https://www.strayspark.studio/blog/ue5-lumen-optimization-60fps), a blog, not Epic). It is a design target, not a measured norm.

| Pass (stat name) | What it is | Target ms | Look harder if above |
|---|---|---|---|
| `NaniteVisBuffer` ("Nanite VisBuffer") | Nanite culling and raster | 2.0 | 4 |
| `NaniteBasePass` ("Nanite BasePass") | Material evaluation for Nanite pixels (grass shading) | 1.8 | 3.5 |
| `HZB`, depth, velocity, misc | Support passes | 0.3 | 0.8 |
| Shadow depths (VSM, Nanite and non-Nanite) | Page rendering | 1.3 | 3 (and invalidated pages should be near 0) |
| `ShadowProjection` | SMRT rays per pixel | 0.7 | 1.5 |
| `LumenSceneUpdate` + `LumenSceneLighting` | Surface cache capture and lighting | 1.2 | 2.5 |
| `LumenScreenProbeGather` | GI | 2.0 | 3.5 |
| `LumenReflections` | Reflections (Epic's example shows a 2.36 ms pass with 0.67 ms of it ray tracing, config unknown) | 1.0 | 2.5 |
| Lumen translucency volume | Volume GI | 0.3 | 0.8 |
| `RayTracingScene`, `RayTracingGeometry`, `NaniteRayTracingRebuildBLAS`, `NaniteRayTracingUpdateStreaming` | TLAS/BLAS build | 0.5 | 1.5 |
| `SkyAtmosphere`, `SkyAtmosphereLUTs`, `CaptureConvolveSkyEnvMap` | Sky | 0.4 | 1.0 |
| `VolumetricFog` | Froxel fog | 0.7 | 1.5 |
| `VolumetricCloud`, `VolCloudReconstruction`, `VolCloudComposeOverScene`, `VolumetricCloudShadow` | Clouds | 1.3 | 3 |
| `TranslucentLighting` + translucency | Ocean, particles | 0.5 | 1.2 |
| `TemporalSuperResolution` | Upscaler | 1.2 | 2.5 |
| `Postprocessing` | Bloom, exposure, tonemap, grain | 1.0 | 2 |
| Other | UI, resolves | 0.4 | 1 |
| **Total** | | **16.6** | **Aim for 15.5 to leave headroom** |

Epic's own time reference for the Lumen guide: "Lumen targets 30 and 60 fps on consoles with 8ms and 4ms frame budgets at 1080p." If `LumenScreenProbeGather + LumenReflections + LumenScene*` exceeds 5-6 ms, reduce quality or simplify the scene (third-party rule of thumb, [strayspark](https://www.strayspark.studio/blog/ue5-lumen-optimization-60fps)).

---

## 7. Suggested order of work

Stage 0 (measure, no code): sections 6.1 steps 1-4 on the three views. Decide the F/P split.

Stage 1 (no visible change, expected -4 to -10 ms [est]): grass invisible in ray tracing; Lumen `LightingMode 0` and foliage roughness 0; `r.TSR.History.ScreenPercentage 100`.

Stage 2 (negligible visible change, -3 to -8 ms [est]): VSM bias 0 and sun update step; cloud Mode 0 with an altitude switch and lower sample counts; fog 16/64; lamp reach and ray count.

Stage 3 (small visible change, -6 to -11 ms [est]): Lumen High values (-2 to -4); render scale 75% with `r.Nanite.ViewMeshLODBias.Enable 0` (about -7 once the per-pixel part has been trimmed to ~9.5 ms at 1080p: 0.4375 x 1.78 x 9.5); keep the menu's Native option at TSR 100%/History 100.

Stage 4 (authoring, -2 to -6 ms [est]): coverage-preserving grass thinning past 35-40 m and a shorter grass ring; cheaper far grass shading.

Forecast at the owner's 1440p, with an error of at least plus or minus 30%: 40 ms now, about 33 ms after stage 1, about 27 ms after stage 2, about 18 ms after stage 3 (55 fps), about 14 ms after stage 4. If stage 3 leaves the fixed part F above 8 ms, the stage 0 profile will already have said where it is.

---

## 8. What could not be verified

- Every ms saving in sections 4 and 7 is an estimate. The sources give Epic's budgets (Lumen 8 ms/4 ms at 1080p on consoles; each level about half the one above), NVIDIA's DLSS times, and a few unnamed-hardware examples. None measure this scene.
- Whether the sun's rotation really changes every frame in practice (it is set every frame; I did not confirm that the engine treats an unchanged rotation as a no-op, nor how fast the frame subsystem moves the sun).
- Whether `r.Nanite.PrimaryRaster.TimeBudgetMs` acts on non-tessellated Nanite clusters in 5.8.
- How the shipped 5.8 build handles Nanite Ray Tracing for many small ISM instances (the engine source has a full streamed-BLAS path: `Nanite/NaniteRayTracing.cpp`). The recommendation to remove grass from the ray tracing scene does not depend on it.
- Epic's own Fortnite Lumen blog and the "Battle-testing 5.1" post could not be read; Fortnite's use of software Lumen for 60 fps comes from a search summary.
- Alan Wake 2's non-ray-traced GI method, The Last of Us Part I's remake lighting method: not found in any source I could open. The TLOU "baked lighting" statement is a reviewer's description.
- RTX 3060 numbers for Silent Hill 2, Black Myth: Wukong, and a Stalker 2 3060 chart: unavailable. An estimator site (CheckFPS, not measured) lists a desktop 3060 at about 34 fps at 1440p and 45 fps at 1080p for Wukong "Ultra" without ray tracing, and 48 fps at 1440p for Silent Hill 2 "Ultra"; treat as rough context only.
- Epic's doc inconsistencies: the ray tracing guide gives `r.RayTracing.Culling.Radius` default 10000 (100 m); the source default is 30000 (`RayTracingInstanceCulling.cpp`). The engine source is what the project runs.
- The thin-geometry TSR settings cost is from the repo's earlier note, not re-checked.

---

## 9. Sources

Epic documentation (read):

- Lumen technical details: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- Lumen performance guide: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine
- Ray tracing performance guide: https://dev.epicgames.com/documentation/en-us/unreal-engine/ray-tracing-performance-guide-in-unreal-engine
- Virtual shadow maps: https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine
- Temporal Super Resolution: https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine
- Screen percentage with temporal upscale: https://dev.epicgames.com/documentation/en-us/unreal-engine/screen-percentage-with-temporal-upscale-in-unreal-engine
- Nanite: https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine
- Nanite foliage: https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage
- Volumetric cloud component: https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine
- Volumetric fog (4.27 figures, via search snippet): https://docs.unrealengine.com/4.27/BuildingWorlds/FogEffects/VolumetricFog/ and https://dev.epicgames.com/documentation/unreal-engine/volumetric-fog-in-unreal-engine
- UE 5.8 release notes (Lumen Lite, MegaLights): https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes
- Fortnite Chapter 4 Lumen blog (blocked; search summary only): https://www.unrealengine.com/tech-blog/lumen-brings-real-time-global-illumination-to-fortnite-battle-royale-chapter-4

NVIDIA:

- DLSS programming guide (execution-time tables, presets, scale ratios): https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf
- DLSS Unreal Engine plugin: https://developer.nvidia.com/rtx/dlss/get-started

Games and press:

- Alan Wake 2 requirements: https://www.ggrecon.com/guides/alan-wake-2-pc-requirements-specs/
- Remedy, Northlight: https://remedygames.com/article/how-northlight-makes-alan-wake-2-shine (Remedy's 2024 engine deck, too large to open: https://enginearchitecture.org/downloads/REAC_2024_Remedy.pdf)
- The Last of Us Part I PC requirements: https://www.playstation.com/en-us/games/the-last-of-us-part-i/pc/
- DSOGaming, TLOU Part I: https://www.dsogaming.com/pc-performance-analyses/the-last-of-us-part-i-pc-performance-analysis/
- DSOGaming, Stalker 2: https://www.dsogaming.com/pc-performance-analyses/stalker-2-benchmarks-pc-performance-analysis/
- Tom Looman, UE 5.7 performance highlights: https://tomlooman.com/unreal-engine-5-7-performance-highlights/
- Stray Spark, Lumen at 60 fps (blog, not Epic): https://www.strayspark.studio/blog/ue5-lumen-optimization-60fps
- Black Myth: Wukong PS5 resolution (search summary only): https://playfront.de/en/?p=1118689
- Matrix Awakens console resolution (search summary only): https://twistedvoxel.com/the-matrix-awakens-ps5-vs-xbox-series-x-comparison/

Local engine source read (all under `.unreal/UE_5.8/Engine/Source/Runtime/Renderer/Private/` unless noted):

- `Lumen/LumenHardwareRayTracingCommon.cpp` (lines 17-27, 32-41, 241-276), `Lumen/Lumen.cpp` (55-61), `Lumen/LumenReflections.cpp` (122-126)
- `VolumetricRenderTarget.cpp` (121-153), `VolumetricCloudRendering.cpp` (53, 58, 68, 83)
- `DeferredShadingRenderer.cpp` (381-392, 1636-1645)
- `Nanite/NaniteCullRaster.cpp` (344-354), `Nanite/NaniteRayTracing.cpp`
- `VirtualShadowMaps/VirtualShadowMapArray.cpp` (786-792), `VirtualShadowMaps/VirtualShadowMapClipmap.cpp`, `VirtualShadowMaps/VirtualShadowMapCacheManager.cpp`
- `RayTracing/RayTracingInstanceCulling.cpp` (22-30, 36-40)
- `../../Engine/Private/InstancedStaticMesh.cpp` (112-136), `../../Engine/Private/DynamicResolution.cpp`, `../../Engine/Private/UnrealEngine.cpp` (539)
- `.unreal/UE_5.8/Engine/Config/BaseScalability.ini`

Project files read: `unreal/Config/DefaultEngine.ini`, `unreal/Config/Windows/WindowsEngine.ini`, `unreal/Source/Ephemera/Private/Player/EphemeraUserSettings.cpp` and `.h`, `unreal/Source/Ephemera/Private/Frame/EphemeraGroundCover.cpp`, `EphemeraSkyRig.cpp`, `Private/Player/EphemeraPlayer.cpp`, `Private/UI/SEphemeraHud.cpp`, `unreal/Tools/build_slice.py`, `unreal/Source/EphemeraEditor/Private/EphemeraPlanetBuilder.cpp`, `docs/unreal/DIRECTION.md`, `docs/research/look_audit_ours.md`, `docs/research/ue58_terrain_foliage_practices.md`.
