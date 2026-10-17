# Night sky, cloud shadows, ground stability, grass wind: causes and fixes

UE 5.8, Ephemera. Written 2026-10-14 from the owner's playtest verdicts. No engine run, no render, no ssh:
everything below is read from our code, the local engine source (`.unreal/UE_5.8/Engine`), Epic pages and papers.

Tags: **[SRC]** read in engine source or our code, **[WEB]** read on a fetched page, **[CALC]** arithmetic from
numbers in our code, **[GUESS]** reasoned but needs a render. Line numbers in `build_slice.py` and
`EphemeraSkyRig.cpp` drift (both were being edited while this was written): search by identifier.
Engine paths are relative to `.unreal/UE_5.8/Engine/Source/Runtime/Renderer/Private` unless they start with `Shaders/`.

## 0. Verdict table

| Owner's words | Most likely cause | Confidence | First fix |
|---|---|---|---|
| (a) night "dark ... pixelated ... Photoshop colour-grading layer ... sea super bright" | 1. Night clouds are about 10 stops brighter than moonlit ground (`Unlit` 0.028), so they clip to a flat tinted layer and, with a bright-half meter and a ceiling 3 EV above the floor, pull the exposure down until the ground is black. 2. The star dome holds about 82,000 stars (real: about 9,000), 76,000 of them 2 to 10x smaller than a pixel and point-sampled with no filtering, so they blink under TSR jitter; and its galaxy band is 2 to 8x brighter than the ground. 3. Global night grade (gain 0.84/0.95/1.12, saturation x0.55) tints everything, not just the dark. | 1: high if weather was not `clear`; 2: high; 3: medium. Sea: unknown (ranked hypotheses in 1.5) | N1, N2, N4 |
| (b) "shadows of clouds look weird" | The cloud shadow map is 512 x 512 over 12 km (23 m a texel; up to 135 m streaks on the ground at low sun), centred on the planet not the camera, shrinks to a 3 km radius after any flight (shadows then missing or smeared past the map edge), is traced with 16 un-jittered samples, and its density depends on camera distance. | High (all [SRC]) | C1 to C5 |
| (c) "some flickering in the floor" | No single proof. Candidates: screen-derivative relief shading, un-baked specular variance (wet sand, rock), sub-pixel blades, VSM and Lumen noise, cloud-shadow crawl (b), and the H.264 Baseline stream. | Low to medium; ranked in 3.1 | G1 to G6, then bisect |
| (d) grass "moves weirdly ... not linked to the surface" | The wind displaces by a linear push (shear), not a bend: blades stretch 10 to 15 percent and keep a constant lean; each blade flutters at its own frequency; the root colour does not match the ground. | High for the mechanics [SRC]; the root-float cause is [GUESS] | W1 to W5 |

---

## 1. Night

### 1.1 What the code does (numbers)

- Exposure white point is `2^EV100` cd/m2: `r.EyeAdaptation.LensAttenuation` is 0.78, which makes `LuminanceMax` 1.0 when the luminance range is extended, and `r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True` is set in `DefaultEngine.ini` [SRC `PostProcess/PostProcessEyeAdaptation.cpp:104-107, 376-388`]. So EV -1 is white at 0.5 cd/m2, EV 2 at 4 cd/m2, EV 15.5 at 46,000.
- Night in `UpdateSky`: floor EV -1 (`NightExposureFloorEV`), ceiling floor + 3 (`CeilingEV = Lerp(GroundExposureCeilingEV, nightev + 3, Night)`), bias -0.5, histogram metered on the 55 to 95 percent band once `Night` > 0.6 (`BrightHalf`); adaptation to a brighter scene 2.5 EV/s (`SpeedUp`), to a darker one 1.0 falling to 0.35 EV/s at night (`SpeedDown`).
- Night grade: `ColorSaturation x 0.55`, `ColorGain (0.84, 0.95, 1.12)` over the whole frame (both lerped by `NightColour`).
- Sun light 95,000 lux; Shine capped at 1.2 lux; NightFill 0.45 lux; sky light x 0.025 at night.
- Clouds: the engine lights night-side clouds with the sun at noon strength (our own finding, `CLOUD_LIT_CODE` header). The material multiplies the albedo by `lerp(Unlit, 1, lit)`, `Unlit` = **0.028**, and nothing in the rig ever sets it [SRC: `make_cloud_material`, grep of `Source/` finds no `Unlit`].

### 1.2 What the references say

Physical:
- Full moon at zenith: **0.26 lux** for a 0.568 degree disc (Epic's Sky Atmosphere page); "about 0.05 to 0.1 lux" typical, up to 0.32 for a supermoon (Wikipedia). Irradiance table in Jensen et al.: full moon 2.1e-3 W/m2, integrated starlight 3.0e-8, airglow 5.1e-8, zodiacal light 1.2e-7, diffuse galactic 9.1e-9. Airglow is the main light on a moonless night.
- Luminance: moon surface 2.5 kcd/m2, night sky about 1 mcd/m2 (darkest 0.4), a grey card under the full moon 1.4 mcd/m2, scotopic/mesopic threshold 5 mcd/m2, mesopic/photopic 5 cd/m2 (Wikipedia, orders of magnitude). By `0.26 lux x 0.18 / pi` a grey card is nearer 15 mcd/m2; the two Wikipedia figures bracket it.
- Our moonlit ground: albedo 0.1 at 1.2 lux = 0.038 cd/m2 = 38 mcd/m2 [CALC]. That is the design's compression (4.6x the real zenith moon, roughly 2.5x the real ground), and it is fine; the point is where everything else sits relative to it.

Perceptual (how to make night read as night):
- Kirk and O'Brien 2011: rods feed the short-cone signal; mixing weights reach kappa1 = 0.25 and kappa2 = 0.4 when fully scotopic; blending "a linear amount of a single blue" (Durand and Dorsey, Khan and Pattanaik) "is inconsistent with psychophysical data". Their range reduction keeps scotopic pixels at most `x = max(1 - w^beta (1 - gamma), gamma) * nu` of display white with gamma 0.25 to 0.5 (the cinema neutral-density, day-for-night trick): **a scotopic image never reaches full white.** Their RGB approximation notes the model does not cover the noise and acuity loss, which other work (iCAM06, Pattanaik) adds. [WEB: EECS-2011-91 text, section 5.1 and 2.3]
- Jensen et al. 2001: tone-map with Ward's histogram adjustment (cone/rod switch plus acuity loss) and a blue shift; stars drawn with the Spencer et al. glare filter, not as pixels; Milky Way from a 14400 x 7200 photographic mosaic. [WEB]
- Ghost of Tsushima (Patry, SIGGRAPH 2021 Advances): the two stated challenges are "Make night feel like night, and not just darker day" and increasing visibility in dark areas; "Night vision is not black & white". Purkinje shift modelled as a delta in an opponent space with rod input k_L = k_M = 0.2, k_S = 0.29, K = 45, surround 0.6; slide comparison at an average of **0.05 lux**; moon used like the sun for bounce and for the atmosphere LUTs; exposure driven mostly by illuminance, not albedo. [WEB]
- Alan Wake 2, from our own frame statistics (`look_gap_vs_aw2.md`): its night has 50 percent of pixels below 16/255 **but p5 = 3.5** and 0.27 percent glint pixels ("smooth and never quite black"); ours has 47 percent below 16, **p5 = 0** and 5.6 percent glint ("black with wiry grass in it"). Those three numbers are the acceptance test for night.
- RDR2, Horizon (Nubis covers a day/night cycle but not night lighting), Uncharted 4 (the 2016 talk covers HDR LUTs, moss and wetness, nothing on night): no primary source on night found; not relied on.

### 1.3 Diagnosis

**Clouds (the "solid layer", the dark ground, the bright sea).** [CALC]

| Quantity | cd/m2 | Against white at EV -1 (0.5) |
|---|---|---|
| Moonlit ground (0.1 albedo, 1.2 lux) | 0.038 | 0.076, x0.71 for the -0.5 bias = 0.054, i.e. 1.7 stops under mid grey |
| Sunlit thick cloud (0.96 x 95,000 / pi) | about 29,000 | |
| Night cloud now (x 0.028) | about 800 (within 2x) | 1,600x: **10.6 stops over white** |
| Night cloud at `Unlit` = (1.2 + 0.45) / 95,000 = 1.7e-5 | about 0.5 | about 1 |

With 800 cd/m2 clouds in the frame the 55 to 95 percent histogram band is cloud; the meter closes toward the ceiling (EV 2, white = 4 cd/m2) and the ground (0.038) lands at 0.01, black. The clouds themselves clip, go through the film curve and the night gain (0.84, 0.95, 1.12), and come out as a flat blue-grey sheet with no structure: the "swipe from Photoshop". The sea is brightest at grazing angles because Fresnel climbs to 1 there, and the thing it reflects (sky capture, clouds) is 3 orders of magnitude over the land. "Waited a little bit and now it's dark" fits the eye following the fading scene down from EV 14 at 0.35 EV/s and stopping at that ceiling (EV 2) instead of at the floor (EV -1): about 35 s. The edges of such a cloud (10^5:1 contrast to the sky) are also the worst case for the half-resolution cloud trace and the bilateral upsample (`r.VolumetricRenderTarget.Mode=1`, `UpsamplingMode` 4 [SRC `VolumetricRenderTarget.cpp:33-40`]), which reads as blockiness.

Caveat: if the night in question was `clear`, this cause is absent and the sky was only stars (below).

**Stars and galaxy ("pixelated").** [CALC from `STARS_CODE`]
- Three layers on a 3D grid at 70, 150 and 310 cells per radian, **one star in every cube**. A pixel samples a sphere through that grid, so a star shows only where the sphere passes within its radius: a shell `2r` thick, which leaves about **5,000, 17,000 and 59,000 stars** on the whole sphere in the three layers, 82,000 in all. The sky has about 9,000 stars brighter than magnitude 6.5 (about 1,000 in a 1080p, 90 degree frame).
- Core radius is 0.035 to 0.09 cells, divided by `1 + 0.35 layer`, over the scale: layer 0 is 0.03 to 0.07 degrees, layer 1 **0.010 to 0.026**, layer 2 **0.004 to 0.010**. One pixel at 1080p and 90 degrees horizontal is **0.06 degrees** at the centre. So the 76,000 stars of layers 1 and 2 are 2 to 15 times smaller than a pixel and are point-sampled at the pixel centre with a cubic falloff. Chance a pixel lands in a star is `(4/3) pi r^3` per layer: about 1,300 lit pixels in a 1080p frame, a different subset on each of TSR's jitter phases, each of random brightness. Every one of them twinkles. That is a "pixelated" sky.
- The galaxy: peak `0.32 x 0.49 x 2.6 = 0.41` display-linear at the core and about 0.12 along the band, against 0.054 for the moonlit ground. Smooth, low-frequency (4 octaves of value noise, finest about 1 degree), no granularity: a soft blue gradient. Brighter than the thing the night is lit for.
- Hash `frac(sin(dot(cell, ...)) * 43758)` with `cell` up to 310 puts 40,000 into `sin`: GPU-dependent result (the Mac and the PC will lay out different stars). Not the cause, worth fixing.
- 8-bit banding is not the problem: the tonemapper dithers the back buffer with triangular noise [SRC `Shaders/Private/PostProcessTonemap.usf:579-600`, `r.BackbufferQuantizationDitheringOverride`]. The owner watches through Pixel Streaming, H.264 **Baseline**, 15 Mbps start (`docs/research/pixel_streaming.md`): a dark, noisy, low-contrast sky is the classic macroblock case. Judge the sky on a lossless local capture before blaming the renderer.

### 1.4 Fixes

**N1. Cloud night brightness.** Confident.
- `make_cloud_material`: default `Unlit` 0.028 to **2e-5**; bump the stamp (`clouds-v22` to `clouds-v23`).
- `UpdateNight`: keep the lux values (`ShineLux`, `FillLux`) in members; in `UpdateSky`, where `Drift` is set on `CloudInstance`:
  `CloudInstance->SetScalarParameterValue("Unlit", FMath::Max((ShineLuxNow + FillLuxNow) / SunLux, 1e-6f) * Tuned("cloudnight", 1.f));`
  A per-frame material parameter, so no render-state recreation (changing a light's properties would invalidate the VSM cache).
- `CLOUD_LIT_CODE`: with `Unlit` this small the dusk edge becomes a 10 stop cliff across 600 m of clearance. Blend in log space and widen it: `float lit = along >= 0.0 ? 1.0 : smoothstep(-40000.0, 150000.0, clear); return exp2(lerp(log2(max(Unlit, 1e-7)), 0.0, lit));` [GUESS on the numbers; needs a dusk render].
- Later, optional: make Hush an atmosphere light (below) so clouds get a silver lining.

**N2. Exposure at night.** Confident that it removes the runaway; the number is taste.
- `UpdateSky`: `CeilingEV = Lerp(GroundExposureCeilingEV, nightev + 1.0, Night)` (was `+ 3.f`). A lamp pool then clips a little rather than turning the world black. If the lamp case demands more, add it from the lamp's own state, not from the sky.
- Per Kirk and O'Brien, cap the night display range: set `FilmWhiteClip`-side headroom or lerp `ColorGain` highlights so the brightest scotopic content tops out near 0.5 to 0.8 of white at `Night` = 1 [GUESS].

**N3. Night grade only where it is dark.** Medium.
- `UpdateSky` (`NightColour` block): leave global `ColorSaturation` and `ColorGain` at their day values. Drive `ColorSaturationShadows` day 0.85 to night **0.50**, `ColorGainShadows` to (0.90, 0.97, 1.10), `ColorGammaShadows` as now, and raise `ColorCorrectionShadowsMax` from 0.09 to about 0.25 at night (it is in post-exposure scene units: moonlit ground is 0.054, stars 0.1 to 0.4, lamps and the moon above). The warm lamp, fire and Hush then stay warm against cool shade ("night vision is not black and white"; Purkinje belongs to the dark pixels). Verify: lamp-lit boulder against moonlit moss. [GUESS]
- Night grain: `FilmGrainIntensity` 0.14 to `0.14 * (1 - 0.7 * Night)`. It is multiplicative (`LinearSceneColor *= lerp(1, grain, intensity)`, `Scene.h:2179`) and on a dark, encoded frame it is what the encoder turns into blocks. [GUESS]

**N4. Stars: density, filtering, brightness.** Confident on the diagnosis; numbers need an eye test.
In `STARS_CODE`:
1. Cut density. Keep layer 0 (about 5,000 stars on the sphere), thin layer 1 with a spawn test (`if (h2.z > 0.17) continue;`, about 3,000), delete layer 2 (59,000 stars at 0.004 to 0.010 degrees, all sub-pixel). About 8,000 stars, the real count. With the footprint filter below every star in view becomes visible, so the density cut matters more after the filter than before it.
2. Pixel-footprint filtering. At the top: `float3 d = normalize(Dir); float px = max(length(ddx(d)), length(ddy(d)));` (radians a pixel). Per layer: `float foot = 0.8 * px * scale; float rEff = min(max(radius, foot), 0.14); float flux = max((radius*radius)/(rEff*rEff), 0.15); float core = saturate(1.0 - dist / rEff); core = core*core*core;` then multiply by `flux`. The 0.14 cap stops the glow being cut by the cell edge (a star sits up to 0.35 cells off centre); the 0.15 floor keeps faint stars visible, as every game does. A star is then at least about one pixel wide at every resolution and does not shimmer under TSR jitter.
3. Split the sum: `pts` (stars) times `Brightness` (0.32), and `gal` times `Brightness * 0.15` (about 0.02 to 0.06 display-linear, level with the moonlit ground). Today the band multiplier `1 + band * 1.6` also scales the whole sum, so apply it to the `pts` term only.
4. Replace the `sin` hash with an integer one (PCG or Hoskins' "hash without sine").
5. Better, as a second step: Milky Way from NASA SVS Deep Star Maps 2020 (equirectangular EXR, 4K to 64K, positions and colour of 1.7 billion stars from Hipparcos-2, Tycho-2, Gaia DR2; check the credit terms on the page) baked to a BC6H cube of 2048 per face with mips (about 32 MB), plus an analytic Gaussian pass for the ~3,000 brightest catalogue stars (Yale BSC or HYG). That gives real constellations and a granular band. BC6H on its own rings around bright points, so keep point stars analytic. [WEB: NASA SVS 4851; Eurographics 2025 "Smaller than Pixels" does flux-preserving sub-pixel star splatting]

**N5. A sky floor.** Night must not be black (owner's earlier direction) and AW2's p5 is 3.5. In the star material add a faint blue gradient brighter toward the horizon, 0.004 to 0.010 display-linear at the horizon and half that at the zenith (airglow with a van Rhijn-style horizon boost), using a `LocalUp` vector parameter set by the rig. [GUESS]

**N6. Moonlight in the atmosphere and clouds (polish).** Epic: two atmosphere lights, "0 for the Sun and 1 for the Moon"; the second light has no multiple scattering; `bCastShadowsOnClouds` is disabled on index 1 [SRC `DirectionalLightComponent.h:170, 188`]. A separate directional light (no surface effect: wrong lighting channel, no shadows, no volumetric scattering) with `bAtmosphereSunLight = true`, `AtmosphereSunLightIndex = 1`, `AtmosphereSunDiskColorScale = 0` gives a glow round Hush and moonlit cloud edges. It must not be `Shine`: the cloud albedo hack in N1 would dim it as well. [GUESS: brightness is untested, the atmosphere is packed 43x]

**N7. Moon glint.** `Shine` has `ForwardShadingPriority` 0 and the Sun has 1, and the engine picks **one** forward directional light by priority (`LightGridInjection.cpp:1500-1514`). Single-layer water, translucency and fog are forward-shaded, so they take the Sun and not Shine; the sea's brightness is therefore probably not Shine's glint (that the water pass reads that forward set is from Epic's design, not re-read in source).

### 1.5 Bisect for the sea and the sky (use the existing `-EphemeraSky=` keys on the PC, deep night)

Render at `Night` > 0.9 (the rig logs `Ephemera night:` every 15 s) and compare frame statistics with the lookgap tool:
1. `-EphemeraWeather=clear`: if the "solid layer" and the black ground vanish, N1 is the cause.
2. `-EphemeraSky=stars=0` on its own: sky pixelation gone, or not.
3. `-EphemeraSky=shine=0`, then `nightfill=0`, then `nightsky=0`: which one moves the sea.
4. `-EphemeraSky=nightev=-1,nightbias=-0.5` against `-1.0 +1`: is the meter running away?
5. Sea hypotheses, ranked: (H1) it reflects the cloud capture (N1 fixes it); (H2) surf and foam, albedo 0.70 against 0.1 ground, lit by Shine and fill (cap foam at night); (H3) Lumen reflection of Hush: `MOON_CODE` caps its glow with `EyeAdaptationInverse`, which may not hold in Lumen's card capture (untested); (H4) the sky capture holds sunlit Hush (the 2026-10-14 finding) at intensity 0.03.

---

## 2. Cloud shadows

### 2.1 How the engine makes them [SRC, all in `VolumetricCloudRendering.cpp` and `Shaders/Private/VolumetricCloud*.us*`]

- A **Beer shadow map**: per texel three values, front depth, mean extinction, max optical depth (`VolumetricCloud.usf:2215`). A surface gets `exp(-min(MaxOD, MeanExt x (depth - front)))` (`VolumetricCloudCommon.ush:54-68`), mixed by `CloudShadowStrength` (`DeferredLightPixelShaders.usf:202`) and applied to the **sun light only**, not to the sky light.
- Size: `min(512 x CloudShadowMapResolutionScale, r.VolumetricCloud.ShadowMap.MaxResolution)`; defaults 1.0 and **2048** (`:425-431`, `:205`). Orthographic square of side `2 x CloudShadowExtent`. Engine default extent is 150 km (`DirectionalLightComponent.cpp:1074`).
- Ray count: `16 x CloudShadowRaySampleCountScale`, doubled toward the horizon (full effect at sun height <= 11.5 degrees: `HorizonFactor = clamp(0.2 / sin(elevation), 0, 1)`), clamped by `r.VolumetricCloud.ShadowMap.RaySampleMaxCount` = 128 (`:1890-1895`). Samples are linear and **un-jittered** across the whole slanted layer (`VolumetricCloud.usf:2182-2206`).
- **Centre**: the planet-surface point under the camera, then **snapped per world axis to `r.VolumetricCloud.ShadowMap.SnapLength` = 20 km** (`:1828-1845`). `SnapToPixelGrid` = 1 removes shimmer. For a planet resting at the origin (the reference body always does: `ApplyPoses`) every surface point has |x|, |y|, |z| < 10 km, so the map is centred on **the planet's centre**, not the camera. Our comment "a map centred on the camera" is wrong for this world.
- Filter: bilinear sampler with **clamp** and no bounds test (`:3189, 3211`; `GetCloudVolumetricShadow`): outside the square the edge texel is smeared outward. One `SpatialFiltering` iteration of dilation by default; `TemporalFiltering.NewFrameWeight` 1 = off.
- The cloud material runs in this pass with the **main view**, so `CameraPositionWS` is the real camera (`VolumetricCloud.usf:2069`, `ResolveView`). `CloudSampleAttribute` exposes `ShadowSampleDistance`: 0 for view samples, > 0 for secondary rays and for this map [SRC `MaterialExpressions.cpp:21670`].

### 2.2 What our setup does to them

| Item | Now | Effect |
|---|---|---|
| Resolution scale | never set: 1.0, 512 x 512 | 12 km / 512 = **23.4 m a texel** (11.7 m at 3 km, 47 m at 12 km, 375 m at 96 km) |
| Ground stretch | ortho map along the light | x 1/sin(sun elevation): 2x at 30 degrees, 3.9x at 15, 5.8x at 10: **140 m smears** at low sun |
| Extent | ctor 6 km; `UpdateSky` rule: `Step < Extent x 0.3` shrinks | after any flight that took it to 12+ km, back on the ground it becomes `Step` = **3 km**. Cloud shell rim is 5.9 km. Map covers only points within 3 km of the sun axis through the centre = sun higher than 53 degrees; elsewhere clamp-smear of the edge texel or no shadow. [CALC] |
| Samples | 16 (32 under 11.5 degrees) over 520 to 1740 m of slanted layer | 33 to 110 m steps against cloud detail of 180 m (`Wisp`) and 52 m (`fine`): banding that is fixed in the map and **moves when the camera moves** |
| Camera dependence | `CLOUD_CODE`: `near` thins density within 520 m of the camera (`NearThin` 0.16; at a 300 m base directly overhead the density is x 0.84, on a 300 m peak under the base x 0.16), `far` morphs billows into a veil from 2.5 km to 9 km and thins x 0.06 (`Far` preset) | the shadow of a given cloud changes shape and strength with the player's position: it swims and has a hole round you when you are near the layer |
| Strength | 1.0, optical depth 3 to 15 | direct sun to about zero in the core, sky light unchanged: a black-cored, blurry-edged blob. Real cumulus shadow keeps 10 to 25 percent of the direct (forward scattering) |

### 2.3 Fixes

**C1. Resolution.** In the ctor: `Sun->CloudShadowMapResolutionScale = 4.f;` (2048 x 2048, 5.9 m a texel at 6.1 km extent, a 4x improvement; keep `MaxResolution` at 2048). Cost is 16x the trace. Measure the cloud shadow pass with `ProfileGPU` before and after; fall back to 3 (1536, 7.9 m) or 2 (1024, 11.9 m). Confident that it helps; the cost is unmeasured.

**C2. Ray count.** `Sun->CloudShadowRaySampleCountScale = 3.f;` (48 samples, 96 at low sun). Steps drop to 11 to 36 m. Confident.

**C3. Do not shrink the extent below the cloud shell.** `UpdateSky` shadow block:
```cpp
const float ShellKm = static_cast<float>((Body->Definition->RadiusM + 1000.0) / 1000.0) * 1.02f;   // R + max cloud top, 6.12 km
const float Needed = FMath::Max(ShellKm, FMath::Clamp(AltitudeKm * 1.2f + 3.f, 3.f, 96.f));
const float Step = ShellKm * FMath::Pow(2.f, FMath::CeilToFloat(FMath::Log2(Needed / ShellKm)));
```
Steps become 6.1, 12.2, 24.5, ... Confident. Fix the comment that says "centred on the camera".

**C4. Two ways to spend the texels (pick after C1 is timed).**
- A (default, most stable): leave `SnapLength` 20, planet at the origin: map centred on the planet, no seam, nothing moves with the player, only with the sun.
- B (if C1 is too slow): `r.VolumetricCloud.ShadowMap.SnapLength=0.5` so the map follows the player, extent 2.5 to 3 km, scale 3 (3.3 to 3.9 m). Costs: a visible end of cloud shadow at that distance across the sun axis, and A's guarantee is lost if the reference body is ever not the home world. [GUESS]

**C5. Take the camera out of the shadow.** `make_cloud_material`: expose `ShadowSampleDistance` from the existing `CloudSampleAttribute` node, pass it to `CLOUD_CODE` as `ShDist`, and set `near = near * (ShDist > 0.5 ? 0.0 : 1.0)` (keep `far`, so the shadow matches the far veil). Side effect: in-cloud secondary rays near the camera see full-thickness cloud, so the interior near you is a touch darker; judge on a render. Confident that it removes the "hole" and the swimming from `near`; `far` still depends on the camera but changes slowly.

**C6. Shadow depth.** `Sun->CloudShadowStrength` 1.0 to **0.88** (default of the `cloudshadowstrength` tuning key) to keep forward-scattered light in the core. [GUESS]

**C7. Filter.** `r.VolumetricCloud.ShadowMap.SpatialFiltering=2` in `[SystemSettings]` (dilation iterations, max 4). With C1 and C2 one may suffice. [GUESS]

Diagnostic: `r.VolumetricCloud.ShadowMap.Debug 1` ("print information to debug the cloud shadow map", `:176`); run it on the ground before and after a flight, and log `Sun->CloudShadowExtent` from the rig, to confirm the 3 km collapse.

---

## 3. Ground temporal stability

### 3.1 Candidates, most likely first

1. **Derivative relief shading** [SRC, `PLANET_CODE`]: with `r.Nanite.Tessellation=0` in play the relief is only shading, from `ddx(shape)` and `ddy(shape)` of a procedural height. Screen derivatives are per 2 x 2 quad and change with every TSR jitter; a height built from `Cells.Blocks`, `ledge`, `crack` steps and mip-sampled scans is not smooth. **Test first:** run with the material parameter `ReliefShade` = 0. If the flicker goes, replace it (3.2 G2). [GUESS, strongest lead in our code]
2. **Specular variance not filtered.** The terrain samples normal and ARM maps separately with `Texture2DSampleGrad`; the normal is renormalised, so mip-averaged variance never reaches roughness (Toksvig: "the shortening of averaged normals as a measure of normal variation"). Worst on wet sand (`film`: roughness 0.09) and wet rock (0.38): the shoreline and seeps glitter. `bNormalCurvatureToRoughness` does **not** help: it uses `ddx/ddy` of the **vertex** normal only (`MaterialTemplate.ush:4272-4300`), blind to our custom `Nrm`. (Our earlier doc, `look_audit_ours.md` item 20, left this "unsure"; it is now answered.)
3. **Sub-pixel blades.** Blades are 0.75 to 1.0 cm wide. At 1080p and 90 degrees horizontal a pixel is 0.06 degrees at the centre, so 1 cm is a pixel at about 10 m and a third of one at 30 m. TSR flickers on this (Epic: "particularly hard"). `r.TSR.ThinGeometryDetection` is 0 by default and only auto-on with `r.Nanite.Foliage` [SRC `TemporalSuperResolution.cpp:332`].
4. **Cloud shadow crawl** (section 2): the map is re-traced every frame from camera-dependent density.
5. **Lumen** screen-probe noise: defaults at our `sg` level 3 (Epic) are `DownsampleFactor` 16, `TracingOctahedronResolution` 8, `Temporal.MaxFramesAccumulated` 10, `FullResolutionJitterWidth` 1, `MaxRayIntensity` 10 [SRC `LumenScreenProbeGather.cpp:70, 86, 94, 227`, `LumenScreenProbeFiltering.cpp:68`; `BaseScalability.ini` GlobalIlluminationQuality@3]. The Cinematic group uses 8 / 16 / jitter 0.5 and `TwoSidedFoliageBackfaceDiffuse=1`. A forum thread on UE 5.7 Nanite foliage shimmer reports it fixed by the Epic preset, 100 percent screen, TSR and two-sided foliage (anecdotal).
6. **VSM**: moving WPO invalidates cached pages each frame (Epic's VSM page); the clipmap tuning (`FirstLevel=8`, `ZRangeScale=5000`) is large. Check for page-pool overflow in the log.
7. **The stream**: H.264 Baseline at start bitrate 15 Mbps; "random colours when standing still" (owner, 2026-10-14) was already put down to coding plus grain.

### 3.2 Fixes

**G1. TSR** (`DefaultEngine.ini [SystemSettings]`). `sg` level 3 already gives `History.ScreenPercentage` 200 and `Flickering` 1 [SRC `BaseScalability.ini` AntiAliasingQuality@3]. Add: `r.TSR.ShadingRejection.Flickering.Period=3` (Epic's flicker example uses 3; source default 2.0), `r.TSR.ThinGeometryDetection=1` (leave `Coverage.ShadingRange` at 3). Epic: lower `r.TSR.ShadingRejection.SampleCount` (2.0) gives more shimmer on newly accumulated detail, so try 3. [GUESS on benefit; the settings exist and are safe]

**G2. Relief shading.** If the test in 3.1 (1) implicates it, replace `ddx/ddy(shape)` with an analytic gradient: re-evaluate `shape` at `pb + e1` and `pb + e2` (tangent offsets of half the scan's texel) using the same `TAPH` taps, and fade `grad` with the pixel footprint (`length(dbx)`): to 0 where a pixel is wider than about 4 cm. Cost: 2 extra height evaluations on the near tier only (`bNear`). [GUESS]

**G3. Bake specular variance** (confident the property exists: `UTexture::CompositeTexture`, `CompositeTextureMode`, `CTM_NormalRoughnessToGreen` in `Engine/Classes/Engine/TextureDefines.h:324`, `CompositePower`). In `import_surface` (`build_slice.py`), for the `arm` texture: `composite_texture = <the n texture>`, `composite_texture_mode = unreal.CompositeTextureMode.CTM_NORMAL_ROUGHNESS_TO_GREEN`, `composite_power = 1.0`. Roughness is the G channel of `*_arm_*`. The normal map must be the same size or larger. This is Toksvig baked into the mip chain (Call of Duty: WWII does the same; AW2's tool "can request normal variance baking to roughness map", `aw2_rendering.md`), which is the only place the variance exists: BC5 stores x and y and rebuilds z, so the shader cannot recover it. Our earlier `aw2_rendering.md` recipe item 4 already asks for it.

**G4. Wet sand.** `arm.g = lerp(arm.g, 0.09, film)` to `lerp(arm.g, lerp(0.09, 0.28, smoothstep(20.0, 120.0, dM)), film)`: a mirror film has no detail left to show beyond 20 m. [GUESS]

**G5. Lumen** (`[SystemSettings]`): `r.Lumen.ScreenProbeGather.FullResolutionJitterWidth=0.5`, `r.Lumen.ScreenProbeGather.Temporal.MaxFramesAccumulated=16`. Leave the octahedron at 8 unless G1 to G4 fail; 16 is the next step (cost unmeasured). Note the project's earlier lesson: a Lumen change that "went out untested in motion" produced "lighting flickering and roughly switching". Test in motion. [GUESS]

**G6. Shadows from grass.** Rules with `bCastShadow` on small tufts: confirm only the near tier casts. If VSM is thrashing, turn grass shadows off beyond 20 m and rely on contact shadows (`ContactShadowLength` 0.06 is set). [GUESS]

**G7. Judge it right.** Compare lossless captures (the `lookgap` metrics: glint pixels, energy above half Nyquist, 9 px blade-scale busyness; ours were 9.5 percent, 20.6 percent and 8.9 against 1.1, 5.8 and 4.6 for the references) rather than the stream. If the stream must be judged: `-PixelStreamingEncoderH264Profile=HIGH` and a higher minimum bitrate than 2 Mbps for a dark scene.

---

## 4. Grass wind and contact with the ground

### 4.1 Reading `GRASS_WIND_CODE` [SRC]

- It returns `(W * 7 * lean - up * 1.6 * lean^2) * t^2` as a world offset. A **translation that grows with height is a shear, not a bend**: at `lean` 2.1 the tip moves 14.7 cm across and 7.1 cm down; a 15 cm turf blade becomes 17 cm long (13 percent), a 36 cm meadow blade stretches less. Real blades rotate about the root and keep their length. The eye reads stretching as "not attached".
- `lean = (0.30 + gust * (1 + flutter)) * give * Wind`: the 0.30 is a constant downwind lean on every blade, so the meadow is combed, and so the bend beyond 36 m (zero) differs from the bend inside.
- `flutter` uses `Time * (3.1 + blade * 2.9 + Rand * 0.8)`: **a different frequency for every blade of a tuft**, so neighbours drift in and out of phase: beating, "boiling", not wind.
- The fade `1 - smoothstep(2200, 3600, Far)` plus `SetWorldPositionOffsetDisableDistance(4000)` is correct: it reaches 0 four metres before the engine stops evaluating. Keep.
- Velocity: WPO with `Time` only is fine for TSR (Epic: "Materials that depend only on time already work"); `r.Velocity.EnableVertexDeformation` is 2 (auto: on unless `r.VelocityOutputPass=2`) [SRC `PrimitiveSceneProxy.cpp:63-70, 233-238`]. Set it to 1 explicitly to make that independent of any future change.
- Colour: `Root` (0.026, 0.042, 0.014) is a constant, the ground under it is `MossC` x drift x patch x lichen. The root is where the eye compares them.
- Placement: grass roots come from bilinear interpolation of a 9 x 9 analytic lattice per cell (`Ground[J][I]`, `EphemeraGroundCover.cpp`), the visible ground is a triangulated mesh on a different grid (`EphemeraGroundPatch`, 2 triangles a quad, spacing a few metres). Bilinear patch and triangles differ by roughly `spacing^2 / 8 x curvature`, a few centimetres on rolling ground: the root floats or sinks, most visible from a low eye with a 15 cm turf. [GUESS; check by lying a camera in the grass]
- Ghost of Tsushima (Wohllaib, GDC 2021): blades generated per-blade on the GPU with per-blade animation; wind as two gust layers (a large scrolling noise plus a smaller scrolling texture), player and horse displace the grass with a damped wave so it does not snap back (PlayStation Blog). Bezier blades and level-of-detail blending are in third-party reconstructions only. I could not retrieve a first-party statement about matching root colour to the terrain; I recommend it on its merits.

### 4.2 Fixes

**W1. Bend, do not push** (confident). Replace the return of `GRASS_WIND_CODE`:
```hlsl
// L: blade length in cm (turf 15, meadow 36, tall 64) times the instance's scale; Bend: tip angle a unit of lean gives, radians.
float a = max(lean * Bend, 1e-3);                  // tip angle; Bend = 0.5 gives 1.05 rad (60 degrees) at lean 2.1
float x = (1.0 - cos(a * t)) / a;                  // across, in blade lengths
float y = sin(a * t) / a - t;                      // down, in blade lengths (<= 0)
return (W * x + up * y) * L;
```
The curve `(x, t + y)` has arc length `t`, so the blade keeps its length and its root stays put. Needs `L`: add a scalar `BladeLength` per habit on the material instance (15, 36, 64) and the instance scale as `NumCustomDataFloats = 1` in `UEphemeraGroundCoverComponent` (`Scale` is already known where `Instances.Add` is called). Keep `max_world_position_offset_displacement` at 24 (the largest `x` L is 0.5 L = 32 cm for `tall` at scale 1: raise to 40).

**W2. Coherent flutter.** `flutter = sin(Time * (3.1 + Rand * 0.8) + blade * 0.9 + Rand * 17.0) * 0.20`: one frequency per clump, a small phase lag per blade, so a tuft moves as a body with ripples through it.

**W3. No constant lean.** `0.30` to `0.10` in `lean`, and stiffen the first quarter: multiply `a` by `smoothstep(0.0, 0.3, t)` before the integral is evaluated (stems are stiff; the root then reads as planted).

**W4. Root colour and contact darkening.** In `GRASS_CODE`, replace `Root` by the ground's own tone: `float patch = VNOISE(P*0.01/55.0 + 63.0)` (the ground uses the same noise for its patches) and `Root = MossC * lerp(0.70, 1.24, patch)`; blend `col = lerp(Root, col, smoothstep(0.0, 0.35, t))`. In `PLANET_CODE`: `arm.r *= lerp(1.0, 0.75, wMoss * (1.0 - bFar))` so the ground under grass is darker, the cheap form of the shade thin blades cannot cast into VSM or Lumen. [GUESS on the numbers]

**W5. Root height.** If lying in the grass shows floating or buried roots, raise `Rule.SinkCm` for `turf` and `meadow` by 2 cm, or build `Ground[][]` on the mesh's own triangulation (`GroundPatch` winding, 2 triangles a quad with the diagonal it uses) instead of bilinear. [GUESS]

**W6. Min width** for flicker (G-list item 3): add to the WPO an expansion of the ribbon along its width axis of `max(0, 0.5 * pixelWorld - halfWidth)` where `pixelWorld = distance * 2 * tan(fov / 2) / viewHeight`. Needs a width sign in vertex colour B written by `build_grass_clump` and the blade's width direction `normalize(cross(VertexNormalWS, up))`. Used by GoT-style systems (third-party description: blades facing the camera are stretched horizontally in view space). [GUESS]

---

## 5. Order of work, and what is certain

Do first (all small, all [SRC]/[CALC]): N1, N2, C1 to C3, W1 to W3, G3, the `Unlit`/extent rig lines. Then render the night with the bisect in 1.5 and the cloud shadow map debug in 2.3, then N4 (stars), then N3, N5, C4, C5, G1 to G2.

**Confident**: night clouds are about 10 stops over white at the current `Unlit`; the star dome has one star in every cube of three grids (82,000 stars, 76,000 of them smaller than a pixel and unfiltered) and a galaxy 2 to 8x brighter than the ground; the cloud shadow map is 512 x 512, centred on the planet by `SnapLength`, shrinks to 3 km after flight, clamps outside, takes 16 un-jittered samples and depends on camera distance; `bNormalCurvatureToRoughness` cannot see our custom normal; the grass wind shears and flutters at per-blade frequencies; the UE tonemapper already dithers (banding is not the sky's problem).

**Guesses that need a render**: that clouds were present in the night the owner saw; the cause of the bright sea (1.5); that derivative relief shading is the remaining floor flicker; the right numbers for N3, N5, C6, C7, G1 to G6, W4 to W6; the cost of C1 and C2 on the RTX 3060; whether Lumen reflects Hush uncapped; the dusk look after N1's log-space blend; whether grass roots float.

## 6. Sources

Engine source (local, UE 5.8): `Renderer/Private/VolumetricCloudRendering.cpp`, `VolumetricRenderTarget.cpp`, `SceneRendering.cpp` (477-492), `PostProcess/PostProcessEyeAdaptation.cpp`, `PostProcess/TemporalSuperResolution.cpp`, `Lumen/LumenScreenProbeGather.cpp`, `Lumen/LumenScreenProbeFiltering.cpp`; `Engine/Private/Components/DirectionalLightComponent.cpp:1074-1081`; `Engine/Classes/Components/DirectionalLightComponent.h`; `Engine/Private/PrimitiveSceneProxy.cpp:63-70`; `Engine/Classes/Engine/Scene.h:2179`; `Engine/Classes/Engine/TextureDefines.h`; `Renderer/Private/LightGridInjection.cpp:1500-1514`; `Engine/Config/BaseScalability.ini`; `Shaders/Private/VolumetricCloud.usf`, `VolumetricCloudCommon.ush`, `DeferredLightPixelShaders.usf`, `MaterialTemplate.ush`, `PostProcessTonemap.usf`.

Epic:
- Sky Atmosphere (second light, 0.26 lux moon, per-pixel transmittance): https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine
- Volumetric Cloud properties (shadow, scattering scale, ray march shadow): https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-properties-in-unreal-engine
- Temporal Super Resolution (flicker, WPO velocity, thin geometry): https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine
- Lumen technical details: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- Composite Texture: https://docs.unrealengine.com/4.26/en-US/RenderingAndGraphics/Textures/Composite
- DirectionalLightComponent properties: https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/DirectionalLightComponent
- Forum: UE 5.7.4 Nanite + Lumen foliage shimmer https://forums.unrealengine.com/t/ue-5-7-4-flickering-shimmering-lighting-on-foliage-trees-nanite-lumen/2742280 ; ARK cloud shadow filter flicker (anecdotal) https://steamcommunity.com/app/2399830/discussions/0/3881599433118360744 ; UE-198490 https://issues.unrealengine.com/issue/UE-198490

Night:
- Patry, Real-Time Samurai Cinema (Ghost of Tsushima), SIGGRAPH 2021 Advances: https://advances.realtimerendering.com/s2021/jpatry_advances2021/index.html
- Jensen, Durand, Dorsey, Stark, Shirley, Premoze, A Physically-Based Night Sky Model, SIGGRAPH 2001: https://graphics.ucsd.edu/~henrik/papers/nightsky/ ; Night Rendering: https://graphics.ucsd.edu/~henrik/papers/night/
- Kirk and O'Brien, Perceptually Based Tone Mapping for Low-Light Conditions, ACM TOG 30(4), 2011: https://www2.eecs.berkeley.edu/Pubs/TechRpts/2011/EECS-2011-91.html
- Moonlight: https://en.wikipedia.org/wiki/Moonlight ; luminance table: https://en.wikipedia.org/wiki/Orders_of_magnitude_(luminance)
- NASA SVS Deep Star Maps 2020: https://svs.gsfc.nasa.gov/4851 ; Smaller than Pixels, Eurographics 2025: https://diglib.eg.org/handle/10.2312/egs20251029
- Nubis (Guerrilla): https://www.guerrilla-games.com/read/nubis-authoring-real-time-volumetric-cloudscapes-with-the-decima-engine ; The Technical Art of Uncharted 4: https://advances.realtimerendering.com/other/2016/naughty_dog/index.html ; INSIDE (dithering): https://gdcvault.com/play/1023783/Low-Complexity-High-Fidelity-INSIDE

Ground and grass:
- Toksvig, Mipmapping Normal Maps: https://developer.download.nvidia.com/whitepapers/2006/Mipmapping_Normal_Maps.pdf ; LEAN mapping (Olano and Baker): https://userpages.cs.umbc.edu/olano/papers/lean/ ; Call of Duty: WWII materials: https://www.advances.realtimerendering.com/s2018/MaterialAdvancesInWWII.pdf
- Wohllaib, Procedural Grass in Ghost of Tsushima, GDC 2021: https://gdcvault.com/play/1027214/Advanced-Graphics-Summit-Procedural-Grass ; wind and interaction: https://blog.playstation.com/2021/01/12/how-stunning-visual-effects-bring-ghost-of-tsushima-to-life/ ; third-party: https://github.com/2Retr0/GodotGrass , https://tigerabrodi.blog/what-we-can-learn-from-grass-in-ghost-of-tsushima-renders

Ours: `docs/research/look_gap_vs_aw2.md`, `aw2_rendering.md`, `look_audit_ours.md`, `ue58_terrain_foliage_practices.md`, `pixel_streaming.md`, `volumetric_clouds_small_planet.md`.
