# Ground-level photoreal landscape for Dawnhollow (R = 5,000 m, UE 5.8.3)

Date: 2026-10-13. Scope: web research only. Nothing was run in Unreal, no builds, no project files changed.

**Legend.** `[V]` = Epic or source page read in this session. `[S]` = search-result snippet only, page not read. `[I]` = internal note in this repo (engine-source references). `UNVERIFIED` = not found in any source this session; treat as a starting guess to test in the editor.

---

## 0. Corrections to the brief

- **`r.Nanite.AllowTessellation` does not exist in the engine source.** The source-checked CVar is `r.Nanite.Tessellation` (`NaniteCullRaster.cpp:94-95`) [I: `docs/research/ue58_terrain_foliage_practices.md`]. The name in the brief appears in a third-party guide only.
- **Death Stranding's talk was SIGGRAPH 2017, not GDC.** It covered lighting, AA and one height-fog model, not terrain [S](https://www.metalgearinformer.com/decima-engine-rendering-techniques-shown-off-during-siggraph-2017/).
- **Electric Dreams is a PCG and Substrate demo (GDC 2023, UE 5.2), not a Nanite or Lumen showcase** [S](https://gamefromscratch.com/epic-release-gdc-procedural-generation-demo-electric-dreams/).
- **No talk on Hellblade II terrain or volumetric fog was found.** Ninja Theory's Iceland work is described as photography, photogrammetry and satellite data, and the game uses Nanite and Lumen [S](https://www.tweaktown.com/news/80052/hellblade-ii-senua-saga-isn-in-full-production-yet/index.html), [S](https://gamespot.com/articles/hellblade-2-will-use-unreal-engine-5-microsoft-con/1100-6478264/).
- **The Quixel Rebirth ground-layering tutorial was not readable.** The 80.lv article returned an empty page [V-empty](https://80.lv/articles/quixel-s-rebirth-making-a-real-time-photorealistic-cinematic). Rebirth's ground method is UNVERIFIED.

---

## 1. What makes a photoreal landscape shot in UE5

| Project | What is documented | Source |
|---|---|---|
| Valley of the Ancient (2021) | Megascans library plus Nanite, Lumen, landscape and foliage tools. No scatter or material setup is documented. | [S](https://dev.epicgames.com/documentation/en-us/unreal-engine/valley-of-the-ancient-sample-game-for-unreal-engine) |
| Electric Dreams (GDC 2023, UE 5.2) | Experimental PCG, Substrate, a handful of Quixel assets. Epic advised against live use of PCG and Substrate at the time. | [S](https://gamefromscratch.com/epic-release-gdc-procedural-generation-demo-electric-dreams/) |
| Rebirth (GDC 2019, UE 4.21) | Over 1,000 Megascans scans from a month in Iceland. Grading and post done inside UE. Camera tracked in VR. | [S](https://www.cgchannel.com/2019/03/watch-rebirth-quixels-stunning-new-photorealistic-short/) |
| Death Stranding (Decima) | SIGGRAPH 2017: single height-fog model for both aerial perspective and an artistic look. Terrain analysis: high-poly meshes with parallax occlusion on small stones (outside analysis, not a developer claim). | [S](https://www.metalgearinformer.com/decima-engine-rendering-techniques-shown-off-during-siggraph-2017/), [S](https://gamingbolt.com/death-stranding-graphical-analysis-pushing-the-ps4-to-its-max-limit) |
| Hellblade II | Iceland photogrammetry plus satellite data, Djúpalónssandur beach WIP. Nanite and Lumen. | [S](https://www.tweaktown.com/news/80052/hellblade-ii-senua-saga-isn-in-full-production-yet/index.html) |

**Ingredients the sources support, and the rest.** The 8 to 10 ingredients below are the common practice in the sources above and in typical breakdowns. Only the first three have a source that names them. The rest are UNVERIFIED as "shared by all" until someone reads a breakdown.

1. Scanned rock and boulder kit placed into the terrain (Megascans or Poly Haven). `[S]` for Valley and Rebirth.
2. Layered ground material with height maps, so the surface has relief. `[S]` for Poly Haven height maps only.
3. Fog from the sky plus local fog pockets. `[V]` for the Epic fog stack (section 5).
4. Scatter at three size classes: stones, rocks, boulders. UNVERIFIED.
5. Low sun with backlight. UNVERIFIED.
6. Wet sand and wet rock at the waterline. UNVERIFIED.
7. Shallow depth of field. `[S]` for the Epic method; values UNVERIFIED.
8. Colour grading and local exposure in post. `[S]` that Local Exposure exists; values UNVERIFIED.
9. Macro colour variation over the terrain (large-scale tint). UNVERIFIED.
10. Props that tell the story: driftwood, wreckage, paths. UNVERIFIED.

---

## 2. Nanite displacement and tessellation (UE 5.4 to 5.8)

**Enable and setup.**
- Material input: the **Displacement** pin on the pixel stage. World Displacement and Tessellation Multiplier are deprecated (`SceneTypes.h:162-163`) [I].
- Engine CVars from source [I]: `r.Nanite.Tessellation` = 1; `r.Nanite.MaxPixelsPerEdge` = 1.0 (lower means more triangles); `r.Nanite.PrimaryRaster.PixelsPerEdgeScaling` = 30 (%); `r.Nanite.ShadowRaster.PixelsPerEdgeScaling` = 100; `r.Shadow.Virtual.Nanite.AllowTessellationDirectional` = 1.
- Displacement scaling defaults: Magnitude 4.0, Center 0.5 (`EngineTypes.h:3455-3459`) [I].
- The **NaniteDisplacedMesh** plugin lives in `Plugins/Experimental` and is off by default [I].
- Epic's Nanite page describes "dynamic programmable displacement" and an "offline adaptive tessellator" for static meshes, but gives no setting names [V](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine).
- The "Enable Tessellation" material toggle, the "Displacement Fade" setting, and the mesh density needed: UNVERIFIED.

**Cost and limits [V]:**
- Translucent materials are not supported. Masked and Opaque work.
- With WPO: "You must clamp the amount of displacement" to limit how many clusters are culled.
- Morph targets are not supported with Nanite.
- Scene cap: 16 million Nanite instances.
- Ray tracing uses the fallback mesh unless `r.RayTracing.Nanite.Mode 1` is set (experimental).
- Platform: "current console and desktop platforms" with DX12 and SM6. Metal is not named.

**Status and known bugs.**
- Experimental. Epic's 5.8 notes list "Mesh Terrain (Experimental): variable tessellation, and Nanite" [S](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes, cited in [I]).
- **UE-370671 (5.7.3):** multiplying the displacement output in the material causes ghost terrain. Workaround: remove the Multiply and use Magnitude alone [S](https://issues.unrealengine.com/issue/UE-370671).
- **UE-367789:** displacement direction is wrong on landscapes with non-uniform scale [S](https://issues.unrealengine.com/issue/UE-367789).
- **Forum report (5.8):** Nanite displacement works on a landscape in 5.7.4 but not in 5.8 [S](https://forums.unrealengine.com/t/nanite-displacement-bugged-in-exact-same-landscape-in-ue-5-8-but-not-in-ue-5-7/2739815). I read the title and snippet only. It concerns a landscape. Whether it affects ordinary static meshes: UNVERIFIED.

**Mac (Metal).** Nanite on macOS is beta, on Apple Silicon M2 or later, with macOS 15 or later and SM6 in the Metal RHI. M1 is not supported [S](https://dev.epicgames.com/documentation/en-us/unreal-engine/macos-development-requirements-for-unreal-engine), [S](https://www.unrealengine.com/tech-blog/bringing-unreal-engine-on-macos-up-to-feature-parity-with-windowsprogress-report). Lumen on Apple Silicon uses software ray tracing only [S](https://dev.epicgames.com/documentation/en-us/unreal-engine/macos-development-requirements-for-unreal-engine). Whether displacement itself works on Metal: UNVERIFIED.

**Typical displacement magnitude for a rock scan.** UNVERIFIED. No source gave centimetre values. The formula (height minus Center, times Magnitude, in Unreal units) is also UNVERIFIED. Start by tuning on one scan, keeping the material output in 0 to 1, and adjusting Magnitude only.

---

## 3. Scattering rocks and ground cover on a spherical world

**Density and size classes.** The brief's values (boulders 2 to 6 m at 1 per 400 m²; rocks 0.3 to 1.5 m at 1 per 20 m²) are UNVERIFIED. No breakdown was found that gives numbers.

**PCG density.** The Surface Sampler's control is `PointsPerSquaredMeter` (`points_per_squared_meter` in Python). It sets grid cell size, with a minimum from Point Extents. It is not a point count. Looseness controls placement inside each cell, and Steepness controls the falloff [S](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/PCG/FSurfaceSamplerParams).

**Clustering at slope feet, gullies and scree fans.** A PCG tutorial covers slope-based filtering and normal-to-density control [S](https://daily.dev/posts/adding-rocks-with-hierarchical-generation---procedural-content-generation-pcg---episode-9-umndwua9q). No source gave scree or gully rules. Use a slope attribute (filter to 0 to 35 degrees, UNVERIFIED) and a distance-to-channel mask.

**Sinking, tilting, colour-matching.** No source found. Common practice: sink 10 to 20 percent of the rock, align to the surface normal, and tint albedo toward the local ground colour. UNVERIFIED.

**Instancing with Nanite.** For Nanite-only projects, Epic says use ISM, not HISM [S](https://dev.epicgames.com/documentation/unreal-engine/instanced-static-mesh-component-in-unreal-engine). Nanite has its own culling and LOD. HISM is for projects that also use fallback meshes.

**PCG at run time.** Runtime Generation works in standalone builds. Generation sources include player controllers, World Partition sources and PCG Generation Source Components. Generation Radius and Cleanup Radius Multiplier are set per grid size [V](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-pcg-generation-modes-in-unreal-engine). That is distance-based, so it does not require Z-up. Whether PCG assumes Z-up in its projection or surface sampler: UNVERIFIED.

**Planets.** One Fab listing (PlanetGen) describes a runtime spherical terrain plugin with biomes and radial gravity. Whether it uses PCG: UNVERIFIED. [S](https://www.fab.com/listings/96c36654-f244-4c75-a527-cfaaab2d5ac4?lang=es-mx)

---

## 4. Icelandic ground specifically

**Real ground [S].** The Eldhraun lava field is from the Laki eruption of 1783 to 1784. It covers about 565 km² and is almost entirely covered in wooly fringe moss. The moss is fragile and slow to recover [S](https://www.amusingplanet.com/2017/09/the-mossy-lava-fields-of-iceland.html), [S](https://www.funiceland.is/nature/geology/eldhraun-lava-field/). Recovery times differ between sources (about 100 years is the common figure).

**Free assets, checked on Poly Haven [V] unless noted.**

| Brief asset | Checked | Use for |
|---|---|---|
| `rock_moss_set_01` | CC0, 3D model, about 8 m wide, 63K tris, 1K to 8K maps (AO, ARM, diffuse, normal DX/GL, rough). No displacement. | Mossy hero rocks. Heavy: LOD or reduce. [V](https://polyhaven.com/a/rock_moss_set_01) |
| `boulder_01` | CC0, 3D model, 1.8 m wide, 124K tris, diffuse, normal, rough, AO. | Boulders. [V](https://polyhaven.com/a/boulder_01) |
| `coast_sand_rocks_02` | CC0, texture set (not a model), displacement included, 15 m wide. | Coastal rock and cliff. [V](https://polyhaven.com/a/coast_sand_rocks_02) |
| `aerial_beach_01` | CC0, texture set, diffuse, normal, displacement, rough, AO. | Beach and sand ground. [V](https://polyhaven.com/a/aerial_beach_01) |
| `rocks_ground_05` | CC0, 8K texture set, dry soil, coarse gravel, pebbles. | Ground layer for scree. [S](https://polyhaven.com/a/rocks_ground_05) |
| `volcanic_rock_tiles` | CC0, 8K, dark pitted basalt. A tiled wall, not loose ground. | Basalt tint reference only. [S](https://polyhaven.com/a/volcanic_rock_tiles) |
| `mossy_rock` | CC0, rock with moss patches. | Moss on rock. [S](https://polyhaven.com/a/mossy_rock) |
| `aerial_ground_rock` | CC0, compacted dirt with stones and some moss. | Patchy moss ground. [S](https://polyhaven.com/a/aerial_ground_rock) |
| `moss_01`, `rock_moss_set_02` | Titles only. Not opened. | Candidate moss mounds. [S](https://polyhaven.com/a/moss_01) |

**Gaps.**
- No Poly Haven black volcanic sand, basalt column, or lava cobble asset was found. A black-sand look may need a Fab or Megascans item.
- `Iceland Collections` (Epic Marketplace) has 149 assets, 4K and 8K textures, UE 5.1+. Its price and licence for this project: UNVERIFIED. [S](https://www.unrealengine.com/marketplace/en-US/product/iceland-collections)
- Basalt columns, braided streams, tephra plains and driftwood: no free asset confirmed. UNVERIFIED.

---

## 5. Atmosphere at ground level

**Sky Atmosphere (verified as sky-only).** Sky Atmosphere gives aerial perspective and curvature-aware ground-to-space transitions [S](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine). To let it affect height fog, enable **Support Sky Atmosphere Affecting Height Fog** in Project Settings (Rendering). It needs Support Sky Atmosphere enabled [S](https://dev.epicgames.com/documentation/zh-cn/unreal-engine/sky-atmosphere-component-in-unreal-engine). Then set the fog's Fog Inscattering and Directional Inscattering colours to black, and use the Height Fog Contribution control under Art Direction [S, Chinese page].

**Composition order [V]:** Sky Atmosphere aerial perspective, then Exponential Height Fog, then Local Fog Volumes, then Volumetric Fog if enabled [V](https://dev.epicgames.com/documentation/en-us/unreal-engine/local-fog-volumes-in-unreal-engine).

**Local Fog Volumes [V].** Each volume is a sphere that blends radial and height fog. Non-uniform scale is not supported. Defaults: Height Fog Falloff 1000, Height Fog Offset 0, Scattering Distribution 0.2, Fog Albedo white, Fog Emissive none. Radial and Height Fog Density values are not stated. Console variables and defaults:
- `r.LocalFogVolume.ApplyOnTranslucent` 0
- `r.LocalFogVolume.RenderIntoVolumetricFog` 1
- `r.LocalFogVolume.TilePixelSize` 128
- `r.LocalFogVolume.TileMaxInstanceCount` 32
- `r.LocalFogVolume.GlobalStartDistance` 2000
- `r.LocalFogVolume.HalfResolution` 0
- `r.LocalFogVolume.MaxDensityIntoVolumetricFog` 0.01

Cost is culled per screen tile, similar to dynamic lights. Shadows and light functions are ignored. On low-end hardware, limit overlap. Priority: a lower Fog Sort Priority draws behind higher ones.

**Volumetric Fog [V].** Enable it in the Exponential Height Fog details. It gives shadowing and lighting in volumes, at higher cost. Key properties: View Distance, Scattering Distribution, Albedo, Extinction Scale, Start Distance, Near Fade In Distance, Static Lighting Scattering Intensity. Grid resolution is `r.VolumetricFog.GridSizeZ`. Cost figures: about 1 ms on PS4 at High; about 3 ms on a GTX 970 at Epic settings. When Volumetric Fog is on, Fog Cutoff Distance, Start Distance and Fog Max Opacity have no effect.

**God rays.** Light shafts need Volumetric Fog on. Per the English page, raise Scattering Distribution toward 0.9 for stronger shafts. A Korean translation of the same page says the value should be near 0, with 0.2 as the starting value. These conflict. Test both. Volumetric Scattering Intensity on the light defaults to 1.

**Spherical planet.** Epic's fog is planar in world Z, except that a local volume is a sphere and can be placed anywhere. Epic does not document planet-local fog. A workable approach, which is my derivation and UNVERIFIED for look: place Local Fog Volumes at valley floors and along the camera path, scale them uniformly, keep Fog Albedo near white, and let Sky Atmosphere handle distance. A custom ray-marched post-process fog oriented to local up is possible but not documented here.

---

## 6. Camera, light and post (cinematic stills)

**Sourced [V or S]:**
- Epic's Depth of Field method: a low aperture (f-stop), a closer camera, a lower field of view, then set Focus Distance [S](https://dev.epicgames.com/documentation/unreal-engine/cinematic-depth-of-field-in-unreal-engine). Other camera settings (Max F-Stop, blade count, Focus Method) exist in the Chinese page only.
- Local Exposure is an extra exposure control for high-contrast scenes, meant to keep detail in bright and dark areas [S](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-effects-in-unreal-engine). Exposure metering defaults to Histogram [S](https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine).

**UNVERIFIED (common practice, not checked in a source this session):**
- Sun elevation 5 to 15 degrees for golden hour. Backlight and rim light from the sun behind the subject.
- Focal lengths: 24 to 35 mm wide, 85 to 200 mm compressed.
- Composition: foreground interest, leading lines, layers of depth, a scale figure.
- Bloom, vignette, film grain, chromatic aberration (sparingly), ACES grading values.

**Project note.** Lumen on Mac is software RT only. A rotating sun invalidates the virtual shadow map cache (from project experience). For stills, lock the sun.

---

## 7. Water at the shore

**Water plugin [V].** Water bodies (ocean, lake, river) interact with Landscape through a spline workflow. The wave model is Gerstner only, driven by a Water Waves asset with Simple or Spectrum generators, and custom wave models are possible through C++ or Blueprint [S](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/Water). Shore foam and wave interaction are not covered on the Water System page [V](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-system-in-unreal-engine). Water Zone is not mentioned on that page: its existence and behaviour are UNVERIFIED. Single Layer Water is linked as a shading model, so it exists [V](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-system-in-unreal-engine).

**Spherical planet.** The docs do not say whether water levels are flat or radial. Treat the Water plugin as flat and Z-based: UNVERIFIED for a sphere. The custom mesh ocean is the likely path.

**Recipe for shore waves and wet sand on a custom mesh ocean (UNVERIFIED technique):**
1. Keep the ocean as a sphere at radius R_sea, displaced by the sine swells. Keep its material separate from the land.
2. Shore foam: in the ocean material, use the difference between scene depth and pixel depth (`SceneDepth` minus `PixelDepth`, standard material nodes) to fade a foam mask near the shoreline. Animate with a scrolling noise.
3. Wet sand: in the ground material, darken albedo and drop roughness where the land height is below the sea radius plus a band (about 1 to 3 m, UNVERIFIED). Lower the normal-map strength there.
4. Avoid mirror-black sand at low sun: clamp roughness to a floor (0.35 to 0.5, UNVERIFIED).

---

## Recommended recipe

Ordered by visual gain per unit of work. Values are starting points. UNVERIFIED values must be tested in the editor.

1. **Displacement on the ground.** Use `aerial_beach_01` and `coast_sand_rocks_02` (both have height maps). Enable `r.Nanite.Tessellation` 1, use the Displacement pin only (no Multiply), tune Magnitude on one patch by eye. Keep the default Center 0.5. Test on Mac first (M2+ and macOS 15+). Existing assets. Note the 5.8 regression risk.
2. **Local fog pockets.** Place spheres in valleys and at the camera's feet. Set Fog Albedo near white, keep Height Fog Falloff at 1000 (default) and tune Density by eye. Keep overlap low. No new assets.
3. **Sky Atmosphere into height fog.** Enable Support Sky Atmosphere Affecting Height Fog. Set the fog inscattering colours to black. Use Height Fog Contribution. No new assets.
4. **Rock kit scatter at two sizes.** Use `boulder_01` (1.8 m) for boulders and `rock_moss_set_01` (8 m, heavy: LOD it) as hero rocks. Scatter with PCG Surface Sampler using `PointsPerSquaredMeter` as a target; add a slope filter. Density values: UNVERIFIED. Existing assets (the user already has rock scans).
5. **Gravel and scree layer.** Use `rocks_ground_05` for pebble and scree ground, blended by slope. No new assets.
6. **Sun and light for stills.** Lock the sun at low elevation (5 to 15 degrees, UNVERIFIED). Use backlight. Lumen software RT on Mac.
7. **Depth of field.** Low f-stop (f/2.8 to f/5.6, UNVERIFIED), closer camera, lower FOV, manual Focus Distance. No new assets.
8. **Shore waves and wet sand.** Use the custom recipe in section 7. Clamp sand roughness at 0.35 to 0.5 (UNVERIFIED). No new assets.
9. **Moss mounds.** Test `moss_01` and `rock_moss_set_02` (titles only, not opened) as mounds at 0.3 to 2 m. Check size and triangle count first. **Needs checking, possibly new assets.**
10. **Black sand and basalt.** No free Poly Haven asset found. Look at Megascans Iceland Collections (licence and price UNVERIFIED) or Fab. Basalt columns and braided streams: no free asset found. **Needs new assets.**

**Assets needing new downloads or checks:** black volcanic sand, basalt cobbles and columns, tephra, driftwood (all not found on Poly Haven). Megascans Iceland Collections and any Fab listing need licence and price checks.
