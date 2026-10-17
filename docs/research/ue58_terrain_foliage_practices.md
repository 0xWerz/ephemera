# UE 5.8 terrain and foliage practices

Engine references are file:line in `.unreal/UE_5.8/Engine/Source`. Epic pages were read 2026-10-13. Third-party sources are labelled. UNVERIFIED means not confirmed in source or in an Epic page.

## 1. Nanite displacement / tessellation

- Material input: use **Displacement** (`MP_Displacement`, pixel stage; unconnected reads -1). `MaterialAttributeDefinitionMap.cpp:408` and `:88-91`. **World Displacement** and **Tessellation Multiplier** are deprecated (`SceneTypes.h:162-163`). Do not use them.
- Displacement scaling: `FDisplacementScaling` Magnitude 4.0, Center 0.5 (`EngineTypes.h:3455-3459`).
- Dicing and cost: `r.Nanite.Tessellation` = 1 (`NaniteCullRaster.cpp:94-95`). `r.Nanite.MaxPixelsPerEdge` = 1.0 (`:134-135`); lower means more triangles. `r.Nanite.PrimaryRaster.PixelsPerEdgeScaling` = 30 (%) (`:332-333`) controls how far dicing coarsens when over budget. `r.Nanite.ShadowRaster.PixelsPerEdgeScaling` = 100 (`:339-340`).
- Shadows: `r.Shadow.Virtual.Nanite.AllowTessellationDirectional` = 1 (`VirtualShadowMapArray.cpp:201-202`).
- Plugin: NaniteDisplacedMesh is in `Plugins/Experimental` and off by default (`.uplugin:12`).
- Status: experimental. Epic's 5.8 notes list "Mesh Terrain (Experimental): variable tessellation, and Nanite" [https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes]. A forum report says displacement breaks on the same landscape in 5.8 but not 5.7.4 [https://forums.unrealengine.com/t/nanite-displacement-bugged-in-exact-same-landscape-in-ue-5-8-but-not-in-ue-5-7/2739815]. Single report, not confirmed by Epic.
- Limits: collision not supported (third-party Fab listing, UNVERIFIED with Epic). WPO, Lumen, and VSM interaction: UNVERIFIED.

## 2. Nanite foliage

- Enable: Project Settings > Rendering > "Nanite Foliage (Experimental)", restart required. Cvar `r.Nanite.Foliage` = 0 (`RendererSettings.h:1549-1553`, `NaniteResources.cpp:119-120`). [https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage-in-unreal-engine?lang=en-US]
- Components: Nanite Assemblies (static and skeletal); Nanite Voxels, enabled on a plain Static Mesh via Shape Preservation = Voxelize; Nanite Skinning (skeletal only, replaces WPO). Dynamic Wind plugin (`Plugins/Experimental/DynamicWind`, off by default) needs a Dynamic Wind Skeletal Data asset per skeletal mesh. [same page]
- Status: Experimental; Epic advises caution when shipping. [same page]
- Skinning cutoff: `r.Skinning.DefaultAnimationMinScreenSize` = 0.1 per Epic page. Not found in source: UNVERIFIED.
- Grass: Epic's Nanite foliage page gives no grass recommendation (UNVERIFIED). Procedural Vegetation Editor is also Experimental [5.8 notes].
- Masked cost: Epic says alpha masking "doesn't work well with it since it introduces a lot of overdraw", that masked functions can be expensive, and that foliage stays flat cards. [same page]

## 3. TSR for foliage

- Thin geometry: `r.TSR.ThinGeometryDetection` = 0 by default, auto-enabled when `r.Nanite.Foliage` is on (`TemporalSuperResolution.cpp:332-333`, `:499-501`). Leave it auto.
- Shading range: `r.TSR.ThinGeometryDetection.Coverage.ShadingRange` = 3 (`:343-344`). Values 0 (two-sided foliage) and 1 (foliage and hair) are cheaper. [https://dev.epicgames.com/documentation/en-us/unreal-engine/thin-geometry-detection-with-temporal-super-resolution]
- Tuning: `Coverage.MaxRelaxationWeight` 0.037 (`:355`), `Coverage.MinKeepLineContrast` 0.30 (`:360`), `ErrorMultiplier` 200 (`:385`), `AntiFlickering` 1 (`:391`).
- Flicker, not ghosting: Epic says thin Nanite foliage flickers because history rejection is too aggressive on sub-pixel geometry. [thin-geometry page]
- History: `r.TSR.History.ScreenPercentage` = 100 (`TemporalSuperResolution.cpp:54-55`). Source help text cites 200 (`:70`). Its cost is UNVERIFIED. `r.TSR.History.SampleCount` = 16 (`:45-46`), range 8-32 (`:46-48`).
- Shading rejection: `r.TSR.ShadingRejection.Mode` = 1 (`:123-124`), `.SampleCount` = 2.0 (`:130-131`), `.Flickering` = 1 (`:138-139`), `.Flickering.Period` = 2.0 (`:189-190`).
- Render scale: `r.ScreenPercentage` = 0.0 (`LegacyScreenPercentageDriver.cpp:38-39`). Its help text says 70 is a good aliasing and performance value (`:40`).
- Presets, 3060 at 1440p:
  - High (screenshot): TSR history 100, SampleCount 16, ThinGeometryDetection auto, ShadingRange 3, r.ScreenPercentage 100.
  - Performance: ShadingRange 1, r.ScreenPercentage 70. Frame-time gains UNVERIFIED.
- Masked leaves: `bDoScaleMipsForAlphaCoverage` = false (`Texture.h:1371`). `AlphaCoverageThresholds` = (0,0,0,0) (`Texture.h:1375`) and only applies when the scale option is on (`:1369`). Opacity mask clip default 0.3333 (`Engine/Private/Materials/Material.cpp:1170`). Dithered masks: UNVERIFIED.

## 4. Grass shading in shipped titles

- Ghost of Tsushima, "Procedural Grass in Ghost of Tsushima" (GDC 2021 AGS, Eric Wohllaib): blades generated on the GPU with per-blade variation [https://gdcvault.com/play/1027214/Advanced-Graphics-Summit-Procedural-Grass]. Normals, colour variation, distance blending, and fade: UNVERIFIED (talk is login-gated, not retrieved).
- Community reimplementation (GodotGrass, not Sucker Punch's method): base-to-tip colour, horizontal normal bend [https://github.com/2Retr0/GodotGrass].
- Valley of the Ancient: Epic's page points to "Collaboratively Constructing a Large World", not retrieved (UNVERIFIED).
- Electric Dreams and Witcher 4 breakdowns: not searched.

## 5. Terrain realism

Only the Unreal side is verified:

1. Height-based layer blending: Landscape Layer Blend node, Blend Type Height. Epic warns of black spots when several layers use Height blend [https://dev.epicgames.com/documentation/unreal-engine/landscape-material-expressions-in-unreal-engine].
2. Macro colour variation: community guidance only (UE Answers), not Epic.
3. Runtime Virtual Texture blending, rock-terrain intersection (PDO or distance field), scree and pebble rules, wetness: UNVERIFIED. No Epic page retrieved.
4. Placement: PCG GPU runtime scatter is "within the same performance range as the landscape GPU grass system" [5.8 notes].
- Death Stranding: SIGGRAPH 2017 Decima talk covered lighting and fog, not terrain [https://www.metalgearinformer.com/decima-engine-rendering-techniques-shown-off-during-siggraph-2017/]. A 2026 Automaton interview says the opening terrain uses three data types with distance-switched LOD, about 25M polygons [https://automaton-media.com/en/interviews/we-ask-kojima-productions-why-theyve-stuck-to-decima-for-nearly-ten-years-chief-technology-officer-on-what-makes-death-stranding/]. No talk on rock scatter or wetness found.

## 6. Lumen and Virtual Shadow Maps

- Rotating sun: "Moving or rotating any light invalidates all of its cached pages"; WPO or PDO materials and skeletal deformation invalidate pages every frame [https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine]. A continuous day/night sun therefore rebuilds its whole cache each frame. Moving lights get their own bias, `r.Shadow.Virtual.ResolutionLodBiasDirectionalMoving` (`VirtualShadowMapClipmap.cpp:50`), default UNVERIFIED. Step the sun in increments instead of rotating smoothly (judgment, not measured).
- Epic's text gives SMRT ray count 8, but source says 7. Source wins.
- Lumen Lite is Beta in 5.8 [5.8 notes].

## Verified in local engine source

- `r.Nanite.Tessellation` = 1: NaniteCullRaster.cpp:94-95
- `r.Nanite.MaxPixelsPerEdge` = 1.0: NaniteCullRaster.cpp:134-135
- `r.Nanite.PrimaryRaster.PixelsPerEdgeScaling` = 30: NaniteCullRaster.cpp:332-333
- `r.Nanite.ShadowRaster.PixelsPerEdgeScaling` = 100: NaniteCullRaster.cpp:339-340
- `r.Nanite.Foliage` = 0: NaniteResources.cpp:119-120
- `r.Shadow.Virtual.Nanite.AllowTessellationDirectional` = 1: VirtualShadowMapArray.cpp:201-202
- `r.TSR.History.ScreenPercentage` = 100: TemporalSuperResolution.cpp:54-55
- `r.TSR.History.SampleCount` = 16: TemporalSuperResolution.cpp:45-46
- `r.TSR.ShadingRejection.Mode` = 1: TemporalSuperResolution.cpp:123-124
- `r.TSR.ShadingRejection.SampleCount` = 2.0: TemporalSuperResolution.cpp:130-131
- `r.TSR.ShadingRejection.Flickering` = 1: TemporalSuperResolution.cpp:138-139
- `r.TSR.ShadingRejection.Flickering.Period` = 2.0: TemporalSuperResolution.cpp:189-190
- `r.TSR.ThinGeometryDetection` = 0: TemporalSuperResolution.cpp:332-333
- `r.TSR.ThinGeometryDetection.Coverage.ShadingRange` = 3: TemporalSuperResolution.cpp:343-344
- `r.TSR.ThinGeometryDetection.Coverage.MaxRelaxationWeight` = 0.037: TemporalSuperResolution.cpp:355-356
- `r.TSR.ThinGeometryDetection.Coverage.MinKeepLineContrast` = 0.30: TemporalSuperResolution.cpp:360-361
- `r.TSR.ThinGeometryDetection.ErrorMultiplier` = 200: TemporalSuperResolution.cpp:385-386
- `r.TSR.ThinGeometryDetection.AntiFlickering` = 1: TemporalSuperResolution.cpp:391-392
- `r.TSR.Velocity.WeightClampingSampleCount` = 4.0: TemporalSuperResolution.cpp:310-311
- `r.ScreenPercentage` = 0.0: Engine/Private/LegacyScreenPercentageDriver.cpp:38-39
- `r.Shadow.Virtual.Enable` = 0: VirtualShadowMapArray.cpp:161-162
- `r.Shadow.Virtual.Cache` = 1: VirtualShadowMapCacheManager.cpp:98-99
- `r.Shadow.Virtual.SMRT.RayCountLocal` = 7: VirtualShadowMapArray.cpp:682-683
- `r.Shadow.Virtual.SMRT.RayCountDirectional` = 7: VirtualShadowMapArray.cpp:729-730
- `r.Shadow.Virtual.SMRT.SamplesPerRayDirectional` = 8: VirtualShadowMapArray.cpp:736-737
- `r.Shadow.Virtual.ResolutionLodBiasDirectional` = 0.0: VirtualShadowMapClipmap.cpp:43-44
- `r.Shadow.Virtual.Clipmap.FirstLevel` = 6: VirtualShadowMapClipmap.cpp:58-59
- `r.Shadow.Virtual.Clipmap.LastLevel` = 22: VirtualShadowMapClipmap.cpp:64-65
- `r.Shadow.Virtual.NormalBias` = 0.5: VirtualShadowMapArray.cpp:667-668
- `r.Shadow.Virtual.MaxPhysicalPages` = 2048: VirtualShadowMapArray.cpp:174-175
- `r.Lumen.ScreenProbeGather.DownsampleFactor` = 16: LumenScreenProbeGather.cpp:86-88
- `r.Lumen.ScreenProbeGather.NumAdaptiveProbes` = 8: LumenScreenProbeGather.cpp:48-49
- `r.Lumen.ScreenProbeGather.AdaptiveProbeAllocationFraction` = 0.5: LumenScreenProbeGather.cpp:54-56
- `r.Lumen.ScreenProbeGather.ExtraAmbientOcclusion` = 0: LumenScreenProbeGather.cpp:273-275
- `r.Lumen.ScreenProbeGather.ShortRangeAO` = 1: LumenScreenProbeGather.cpp:299-301
- Material `MP_Displacement`: MaterialAttributeDefinitionMap.cpp:408
- Deprecated `MP_WorldDisplacement_DEPRECATED`: SceneTypes.h:162
- `FDisplacementScaling` Magnitude 4.0, Center 0.5: EngineTypes.h:3455-3459
- `bDoScaleMipsForAlphaCoverage` = false: Texture.h:1371
- Opacity mask clip default 0.3333: Engine/Private/Materials/Material.cpp:1170
- NaniteDisplacedMesh plugin `EnabledByDefault` false: Plugins/Experimental/NaniteDisplacedMesh/NaniteDisplacedMesh.uplugin:12

## Could not verify

- `r.Nanite.AllowTessellation`: named in an Adobe guide, does not exist in source (grep empty). Contradicted.
- `r.ContactShadows` exists (LightRendering.cpp:669); default not found.
- `r.Shadow.Virtual.ResolutionLodBiasDirectionalMoving` default.
- `r.Skinning.DefaultAnimationMinScreenSize`: named on Epic's page, not in source.
- `r.TSR.Visualize` and `r.TSR.ShadingRejection.Flickering` defaults beyond the above.
- Nanite displacement with WPO, Lumen, VSM, collision.
- Grass shading (Ghost of Tsushima, Valley of the Ancient, Electric Dreams, Witcher 4).
- Death Stranding terrain blending, rock scatter, wetness; RVT blending; PDO or distance-field blending.
- All performance numbers (frame-time gains, ms costs on RTX 3060).
- Dithered opacity mask settings.
