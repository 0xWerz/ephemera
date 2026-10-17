# Volumetric clouds on a small planet (Dawnhollow, R = 5 km): research notes

Date: 2026-10-13. Scope: web research only, no project files touched.

Labels used below:
- **Verified**: read from a fetched source this session (URL given).
- **Search-only**: seen only in a search snippet, not opened.
- **UNVERIFIED**: not confirmed. Treat as memory or hypothesis and test in-engine.

Sources that could not be read: Schneider 2017 Nubis, Nubis Evolved 2022 PDF (over WebFetch's 10 MB limit), Hillaire 2016 Frostbite PDF (same limit), Hillaire EGSR 2020 paper (HTTP 403), Wrenninge Oz slides (not found). Epic's cloud console-variable reference was not found.

## 0. Key findings

1. Epic's Advanced Material Output defaults are phase G = 0, G2 = 0, Phase Blend = 0, and multi-scatter contribution, occlusion and eccentricity = 0.5. Those are isotropic phase values. If phase and multi-scatter were never set, that alone explains much of the flatness. [Epic properties page]
2. Epic's own text says g < 0 is forward and g > 0 is backward. That is the reverse of the usual Henyey-Greenstein convention. Check the sign in-engine before setting values.
3. Geometry (my calculation, not a cited source): at R = 5000 m, an eye 1.8 m above ground sees the horizon at about 134 m. Cloud base (350 m) drops below the curve beyond about 2.0 km ground distance, and cloud top (670 m) beyond about 2.7 km. The layer is 6.4% of planet radius; Earth's cloud layer is roughly 0.02% of its radius.
4. Epic's docs do not say that per-sample transmittance must be on when per-pixel transmittance is on. I could not find that requirement. Per-pixel transmittance is documented as applying to opaque meshes only.
5. Epic recommends `r.VolumetricRenderTarget.Mode 0` for quality. A forum post says Mode 2 traces one pixel per 4x4 block and reprojects the rest, which can leak clouds over opaque geometry. That is a likely cause of the stair-stepped limb and edge blur.

## 1. Standard cloud model (Schneider, Guerrilla)

Verified from the SIGGRAPH 2015 "Real-time Volumetric Cloudscapes of Horizon Zero Dawn" slides. Numbers are slide numbers. [https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf]

- Two levels of detail: a low-frequency base shape and high-frequency detail with distortion (slide 48). Ray-march with a sampler (slide 22).
- Perlin-Worley: Worley noise inverted to give packed billows, used to dilate Perlin noise (slide 29).
- Texture 1: 128^3, 4 channels. Channel 1 is Perlin-Worley; channels 2-4 are Worley at rising frequency. It defines the base shape (slide 31).
- Texture 2: 32^3, 3 channels of Worley at rising frequency. It adds erosion detail (slide 32).
- 2D curl-noise texture: 128^2, 3 channels. It distorts the shapes for turbulence (slide 33).
- Height gradients: three presets (stratus, cumulus, cumulonimbus), blended per cloud type. Coverage is 0 to 1 (slide 34).
- Build order: Texture 1 times height signal, then times coverage, which thins the bottoms (slides 35-36). Then erode the edges with Texture 2 and distort with curl noise (slide 37).
- Weather map: R = coverage (about +/-0.5), G = precipitation, B = cloud type (+/-1) (slide 40). Precipitation pushes the map toward cumulonimbus at 70% coverage (slide 42).
- Clouds are drawn in a 35,000 m radius around the player. The transition to cumulus starts at 15,000 m and about 50% coverage (slide 45).
- Cloud-type altitude diagram (slide 20): low types sit below roughly 1,500 m. Read from a diagram, so approximate.

UNVERIFIED (not retrieved): the `remap()` formula, exact density-height-gradient fractions, extinction and density constants, and the 2017 and 2022 Nubis formulas. A Guerrilla web page gives only an overview and "under two milliseconds on the PlayStation 4". [https://www.guerrilla-games.com/read/nubis-evolved]

Hillaire, Frostbite 2016 (sky and clouds): the deck exists at https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf but could not be read. Its cloud formulas are UNVERIFIED.

## 2. Cloud lighting

Verified, Schneider 2015 slides:
- Beer's law: `T = e^(-t)` (slide 52).
- Per-sample energy: `Energy = e^(-d) * HG` (slide 55). Each sample is multiplied by the Henyey-Greenstein phase.
- Beer alone versus Beer times HG: HG adds forward brightening near the sun (slide 56).
- Dark edges and crevices: a point deeper in a cloud collects more in-scattered light. Bulges and edges are lighter; crevices are darker. Film does this with many samples; games need an approximation (slide 58).
- The "powdered sugar" term is named on slide 59. Its formula is on later slides I did not read: UNVERIFIED. Common form from memory: `1 - exp(-2d)` (UNVERIFIED).

UNVERIFIED, from memory, not checked against a source:
- Dual-lobe HG: forward g about 0.8, back g about -0.3, blend about 0.3-0.5. Also check the sign convention noted in Key finding 2.
- Wrenninge (SIGGRAPH 2013, "Oz: The Great and Volumetric", DOI 10.1145/2504459.2504518, [https://history.siggraph.org/learning/oz-the-great-and-volumetric-by-wrenninge-kulla-and-lundqvist]): per-octave a, b, c values of about 0.5. I could not confirm them. Epic's defaults of 0.5 for contribution, occlusion and eccentricity are Epic's parameters, not verified Wrenninge constants.
- Sky-light and ground-bounce formulas: UNVERIFIED.

Epic's cloud-side equivalents: Sky Light Cloud Bottom Occlusion, "a fast approximation that avoids ray tracing or an AO texture" (Epic properties page). Ground Contribution adds single-scattered light from the shadowed ground (Epic properties page). Epic recommends only one multi-scatter octave for games (Epic component page).

## 3. Unreal specifics

### 3.1 Volumetric Advanced Material Output node (class MaterialExpressionVolumetricAdvancedMaterialOutput)

Source: [https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-properties-in-unreal-engine] and [https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialExpressionVolumetricAdvancedMaterialOutput]

| Property (input if hooked up) | Default | Range | Epic description (paraphrased) |
|---|---|---|---|
| Phase G / Phase G2 | 0 / 0 | -1 to 1 | HG g for each lobe. Epic text: g < 0 forward, g > 0 backward (see Key finding 2). |
| Phase Blend | 0 | 0 to 1 | Lerp between G and G2. |
| Multi Scattering Contribution | 0.5 | 0 to 1 | Contribution of each successive octave. Clamp toggle exists. |
| Multi Scattering Occlusion | 0.5 | 0 to 1 | How much occlusion drops per octave. |
| Multi Scattering Eccentricity | 0.5 | 0 (anisotropic) to 1 (isotropic) | How isotropic the phase becomes per octave. |
| Multi Scattering Approximation Octave Count | not stated | max 2 | 0 = single scattering only. Epic: use one octave in games for performance. |
| Clamp Multi Scattering Contribution | not stated | bool | Clamps to [0,1]. Off means the artist must keep brightness reasonable. |
| Per Sample Phase Evaluation | not stated | bool | Phase per sample instead of once per pixel. Slower. |
| Ground Contribution | not stated | bool | Adds shadowed ground light as single scattering. Adds tracing cost. |
| Gray Scale Material | not stated | bool | Uses only the R channel of inputs. Lighting stays coloured. |
| Ray March Volume Shadow | not stated | bool | Off uses the cloud shadow map instead of secondary ray marching. |
| Conservative Density (Vector3) | not stated | n/a | X > 0 means evaluate the material. Y and Z pass extra data. |

Epic's example material: the page names `m_SimpleVolumetricClouds` and its instance `m_SimpleVolumetricCloud_Inst` (Engine/EngineSky/VolumetricClouds), not `m_SimpleVolumetricCloud`. Its parameters include Phase_Controls (Phase A, Phase B in -1..1, Phase Blend), Multiscatter_Controls (Intensity, Occlusion, Eccentricity, each 0..1), Cloud_AlbedoColor (alpha = light occlusion strength), Cloud_GlobalDensity, Layout_CloudHeightProfile, Layout_CloudGlobalPattern, Layout_GlobalCloudMask, and Noise_Texture3D with Noise_Bias and Noise_Strength. [https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-material-in-unreal-engine] Open this instance in-editor as a working reference.

UNVERIFIED: the Extinction, Subsurface and Ambient Occlusion pin mappings. They do not appear in the Epic pages I read. The "Cloud Sample Attributes" node and its outputs (Altitude, Altitude In Layer, Norm Altitude In Layer, Shadow Sample Distance) were not found. Also UNVERIFIED: "Volumetric Cloud Empty Space Skipping" as a named feature. Epic describes Conservative Density as "accelerate[s] the ray marching by skipping expensive material evaluation early on" (Epic component page).

### 3.2 VolumetricCloud component

Verified from the Epic properties page unless marked:
- Layer Bottom / Layer Height (km above ground). Our values, 0.35 and 0.67 km, are in range. Default not stated.
- Planet Radius: used only when no Sky Atmosphere is present. Ours must match the Sky Atmosphere ground radius (5 km).
- View / Reflection / Shadow View / Shadow Reflection Sample Count Scale: each is clamped by an r.VolumetricCloud CVar (listed in section 3.4). Defaults not stated.
- Tracing Max Distance Mode: "Distance from Point of View" or "Distance from Cloud Layer Entry Point". Tracing Max Distance is in km. Tracing Start Max Distance is in km.
- Shadow Tracing Distance (km).
- Stop Tracing Transmittance Threshold: tracing stops below this mean transmittance. Epic recommends it for performance.
- Sky Light Cloud Bottom Occlusion.
- Ground Albedo: only used when Ground Contribution is on.
- Use Per Sample Atmospheric Light Transmittance: "Whether to apply atmosphere transmittance per sample instead of using the light global transmittance." Default not stated.
- Aerial Perspective ray and Mie scale: UNVERIFIED (not found on the pages read).

Epic's setup note: a level with Directional Light (Atmosphere Sun Light on), Sky Atmosphere, Sky Light, and Volumetric Cloud. Clouds "adapt to... ground to outer space transitions". [https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-in-unreal-engine]

Suggested starting values (my suggestions, not from a source): thin layer 0.30-0.45 km for a stylised 5 km planet; View Sample Count Scale 2-3; Tracing Max Distance around 30 km; Stop Tracing threshold about 0.02; Sky Light Cloud Bottom Occlusion about 0.4. Tune these in-engine.

### 3.3 Directional light cloud settings

Verified (Epic properties page, names and descriptions):
- Atmosphere Sun Light; Cast Shadows on Clouds; Cast Shadows on Atmosphere; Cast Cloud Shadows.
- Cloud Scattering Luminance Scale: scales light scattered in cloud media, to offset approximate multiple scattering.
- Per Pixel Atmosphere Transmittance: "Whether to apply atmosphere transmittance per pixel on opaque meshes instead of using the light's global transmittance."
- Cloud Shadow Strength; Cloud Shadow on Atmosphere Strength (0 disables); Cloud Shadow on Surface Strength (0 disables); Cloud Shadow Depth Bias; Cloud Shadow Extent (world-space radius around the camera, km); Cloud Shadow Map Resolution Scale; Cloud Shadow Ray Sample Count Scale.

Epic's guidance: Beer shadow maps "are usually enough for clouds viewed from the ground". Ray-marched shadows give sharper, coloured shadows and suit ground-to-space transitions, at higher cost. Narrowing Cloud Shadow Extent gives sharper results. [Epic component and properties pages]

Per-sample vs per-pixel transmittance: the Sky Atmosphere page says `r.SkyAtmosphere.TransmittanceLUT.LightPerPixelTransmittance` enables per-pixel light transmittance "only for opaque objects in the deferred renderer", and "space/planetary views will be more accurate with it enabled", at higher cost. [https://dev.epicgames.com/documentation/unreal-engine/sky-atmosphere-component-properties-in-unreal-engine] I did not find the claim that per-sample must be on whenever per-pixel is on. Test both combinations.

### 3.4 Console variables

Verified:
- `r.VolumetricCloud.ViewRaySampleCountMax`, `r.VolumetricCloud.ReflectionRaySampleMaxCount`, `r.VolumetricCloud.Shadow.ViewRaySampleMaxCount`, `r.VolumetricCloud.Shadow.ReflectionRaySampleMaxCount`: caps for the sample-count scales (component properties page).
- `r.VolumetricCloud.ShadowMap.MaxResolution`, `r.VolumetricCloud.ShadowMap.RaySampleMaxCount`: caps for the cloud shadow map (component properties page).
- `r.VolumetricCloud.HighQualityAerialPerspective` = 1 for cinematic quality (component page).
- `r.VolumetricRenderTarget` = 0 to start cinematic quality. `r.VolumetricRenderTarget.Mode 0` is recommended for quality (component page).
- `r.VolumetricRenderTarget.PreferAsyncCompute`: a bug report says it makes clouds ignore volumetric fog. [https://issues.unrealengine.com/issue/UE-220895] (search-only)
- Mode 2: reprojection from a 4x4 block (forum, unofficial). [https://forums.unrealengine.com/t/volumetriccloud/688516] (search-only)
- Sky Atmosphere: `r.SkyAtmosphere.AerialPerspective.StartDepth` default 100 m; `r.SkyAtmosphere.AerialPerspectiveLUT.Depth` default 96 km; `r.SkyAtmosphere.FastSkyLUT` can show artifacts with "high-frequency details in the sky such as Earth shadow"; `r.SkyAtmosphere.MultiScatteringLUT.HighQuality` (64 samples vs 2); `r.SkyAtmosphere.SampleLightShadowmap`. (Sky Atmosphere properties page)

UNVERIFIED (not found in any source read): `r.VolumetricCloud.DistanceToSampleMaxCount`, `r.VolumetricCloud.SkyAO.*`, `r.VolumetricCloud.EmptySpaceSkipping`, `r.VolumetricRenderTarget.UpsamplingMode`, `r.VolumetricRenderTarget.ReprojectionBoxConstraint`, `r.VolumetricRenderTarget.MinimumDistanceKmToEnableReprojection`, and the meanings of Mode 1 and Mode 3. Check with `help r.VolumetricCloud` in the engine. Epic's cloud-CVar reference was not found.

### 3.5 Space, night side and planet-scale weather

- Night-side brightness: the planet's shadow reaches clouds through atmosphere transmittance. Epic says per-sample transmittance applies it "instead of using the light global transmittance". If it is off, the cloud uses the global value and may not see the planet's shadow. My hypothesis: turn per-sample on for clouds, keep per-pixel off, then check the night side. UNVERIFIED in-engine.
- FastSkyLUT: Epic names Earth shadow as an artifact source (Sky Atmosphere page). Turn it off for planetary views while testing.
- Planet-scale weather map: no Epic document on this was found. The default material places its patterns with `Layout_CloudGlobalPattern` and `Layout_CloudGlobalScale` (in km). Sampling a pattern by direction from the planet centre is the usual approach. That is UNVERIFIED as an Epic recommendation.
- Community references (search-only, none opened in full): Munich LMU thesis driving UE 4.27 clouds from meteorological data [https://discover.ub.uni-muenchen.de/catalog/22a959f9-3e52-44c5-8be2-804e0299f27f]; Digital Production art-direction tutorial, UE4/5 [https://www.digitalproduction.com/2021/07/01/unreal-engine-4-5-tutorial/]; a Fab planet kit with volumetric clouds and atmosphere, UE 5.4 [https://www.fab.com/listings/fc591129-a147-4684-acbb-38a990e30c3a]. Ultra Dynamic Sky and a "planetary clouds" video were named in searches but not checked: UNVERIFIED that they do full-planet volumetric clouds.

## 4. Clouds seen from orbit

Verified or search-only:
- Global cloud fraction: about 67% from MODIS (King et al. 2013), about 72% over ocean and 55% over land (search-only). A CERES-based study (Minnis et al. 2011, TRMM and MODIS) gives about 59% (search-only). [https://earthobservatory.nasa.gov/images/36518/ceres-global-cloud-fraction] (not read)
- Microsoft Flight Simulator (secondary, pre-release AVSIM forum summary of a podcast): clouds fully 3D, "600 km" view distance, 20 vertical layers of weather data, data up to 65,000 ft. No GDC source found. [https://www.avsim.com/forums/topic/560392-avsim-podcast-with-msfs-team-help-me-list-all-the-info/]
- Blackrack's True Volumetric Clouds (KSP): release 5 notes say the 3D noise, cloud shading, formations and layer rendering were improved. No 2D-to-volumetric transition details. [https://www.patreon.com/posts/139879553]

UNVERIFIED (general meteorology and game knowledge, no source read): the scale hierarchy (synoptic systems in thousands of km, mesoscale cells 10-100 km, cumulus fields about 1 km), comma cyclones, the ITCZ band, cloud streets, cirrus veils, limb brightening. Space Engine, Star Citizen and Starfield were not researched.

Small-planet approach (my reasoning, not a cited source): make the layer thin relative to the planet, use fine detail and self-shadowing, soft wispy bases and low opacity variation. The current 320 m layer on a 5 km planet is too thick. A 150-250 m layer would look more like a surface film. Fade high-frequency erosion with distance and pixel footprint so orbit does not show popcorn.

Orbit geometry (my calculation): at 20-30 km altitude the planet's disk spans about 16-23 degrees of view. The limb is the most important place for the layer.

## 5. Failure causes for the four faults

| Fault | Likely causes | Fix |
|---|---|---|
| 1. Flat, no shading | Phase G and G2 at 0 (Epic defaults, isotropic). Multi-scatter off or defaulted. Constant Base Color 1.0 removes variation. Bottom occlusion not set. (Epic defaults above; slides 35-36 on density rising with altitude and thin bases.) | Set phase lobes (sign check), octave count 1, albedo from altitude (darker bases), Sky Light Cloud Bottom Occlusion about 0.4. |
| 2. Night side lit | Per-sample transmittance off, so the global light value is used and the planet's shadow is missed (Epic wording). FastSkyLUT artifacts near Earth shadow. | Per-sample ON on the cloud component, per-pixel OFF, FastSkyLUT 0 for testing. Check the sun's direction at a night-side point. UNVERIFIED in-engine. |
| 3. Popcorn from orbit, blur up close, stair-stepped limb | Mode 2 reprojection (4x4 blocks). Low sample scales (default not stated). Tracing Max Distance too short for grazing rays. Fine detail (180 m repeat) too strong at orbit distances. Layer too thick for the planet. | Mode 0; raise View Sample Count Scale and Tracing Max Distance; fade detail with distance; thin the layer. |
| 4. Dirty planet, black clouds, one lit cap | Cloud Shadow on Atmosphere Strength above 0 darkens the sky and planet. Per-pixel transmittance applies to opaque meshes, including the planet. Black or one-sided lighting suggests a transform or radius mismatch between Sky Atmosphere and the cloud component. All UNVERIFIED hypotheses. | Atmosphere shadow strength 0. Surface strength about 0.5. Keep per-pixel OFF. Check that the Sky Atmosphere ground radius and Planet Radius are both 5 km, and that the component transforms agree with the planet centre. Also check the Rayleigh and Mie exponential heights against the 1.4 km atmosphere (Earth-scale defaults would be inconsistent; values not verified in this session). |

## Recommended recipe

Values marked (Epic) come from Epic's pages. Values marked (suggested) are my starting points and need tuning in-engine.

**Material**
1. Phase G = 0.8, Phase G2 = -0.3, Phase Blend = 0.3, Per Sample Phase Evaluation on while testing. Check the sign in-engine: if the lobe that should brighten toward the sun is wrong, negate both. (Values UNVERIFIED; sign convention per Epic text.)
2. Multi Scattering Approximation Octave Count = 1. Contribution, Occlusion, Eccentricity = 0.5 each (Epic defaults). Clamp on. (Epic)
3. Replace constant Base Color 1.0 with altitude-based albedo: about 0.55 at the base rising to 1.0 at the top, with +/-5% low-frequency variation. (suggested)
4. Use Conservative Density (X > 0) to skip empty space before evaluating the full material. (Epic)

**Component and light**
5. Planet Radius = Sky Atmosphere ground radius = 5 km. Layer Bottom 0.30 km, Layer Height 0.45 km (150 m layer). (Radius match: Epic; layer values suggested.)
6. View Sample Count Scale 2-3; Tracing Max Distance Mode "Distance from Point of View", Tracing Max Distance about 30 km; Stop Tracing Transmittance Threshold about 0.02. (suggested; sample-scale clamps per Epic)
7. Sky Light Cloud Bottom Occlusion about 0.4. Ground Contribution off until the frame budget allows. (suggested; Epic on optional ground contribution)
8. Directional light: Cast Cloud Shadows on; Cloud Shadow on Atmosphere Strength 0; Cloud Shadow on Surface Strength about 0.5; Cloud Shadow Extent about 5 km; Ray March Volume Shadow on for ground-to-orbit transitions. (Strength names per Epic; values suggested)
9. Use Per Sample Atmospheric Light Transmittance on for the cloud component; Per Pixel Atmosphere Transmittance off on the sun. Test the night side from the ground and from orbit before changing anything else.

**Console variables**
10. `r.VolumetricRenderTarget 0` (Epic: Mode 0 recommended for quality); `r.VolumetricCloud.HighQualityAerialPerspective 1`; `r.SkyAtmosphere.FastSkyLUT 0` while testing night side and Earth shadow; `r.SkyAtmosphere.TransmittanceLUT.LightPerPixelTransmittance 1` only for orbit tests, since it affects opaque deferred meshes only. (Epic)

## Source list
- Epic cloud component properties: https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-properties-in-unreal-engine
- Epic cloud component overview: https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-in-unreal-engine
- Epic cloud material: https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-material-in-unreal-engine
- Epic Advanced Material Output (Python API): https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialExpressionVolumetricAdvancedMaterialOutput
- Epic Sky Atmosphere properties: https://dev.epicgames.com/documentation/unreal-engine/sky-atmosphere-component-properties-in-unreal-engine
- Schneider SIGGRAPH 2015 slides: https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf
- Guerrilla Nubis Evolved overview: https://www.guerrilla-games.com/read/nubis-evolved
- Nubis Evolved 2022 PDF (not read): https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-NubisEvolved-NoVideos.pdf
- Hillaire Frostbite 2016 (not read): https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf
- Wrenninge Oz (search-only): https://history.siggraph.org/learning/oz-the-great-and-volumetric-by-wrenninge-kulla-and-lundqvist
- UE-220895 (search-only): https://issues.unrealengine.com/issue/UE-220895
- Unofficial forum thread on Mode 2 (search-only): https://forums.unrealengine.com/t/volumetriccloud/688516
