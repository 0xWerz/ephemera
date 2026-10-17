# Ocean and underwater for Ephemera (UE 5.8): research and a design for this game

Study date 2026-10-14. Public sources only (papers, talks, slides, docs), plus the engine source in
`.unreal/UE_5.8` and this repo. Tags like [S3] point to the source list at the end. **Verified (engine)**
means I read it in `.unreal/UE_5.8/Engine/...`. **Inference** marks my own reasoning. **Not found** marks
what I looked for and could not read. Every millisecond figure for our project is an estimate until it is
measured on the PC (RTX 3060); nothing here was run.

State of the tree while I wrote this (2026-10-14, 05:30-05:45): the sea code was being edited at the same
time. `Tools/terrain/water.py` gained `animated()` (Tessendorf-quantised moving slope maps, 128 x 128 x 128, 24 s
loop), `EphemeraCloudBuilder::ImportWaveVolume` imports them as volume textures, `OCEAN_CODE` reads them
(`ChopAnim`, `SwellAnim`), and `M_Underwater` v1 exists with a sky-rig hook. I read all of it and changed
none of it. Section 1 says what it already covers; the design in section 3 builds on it, not around it.

---

## 0. Verdict in ten lines

1. The two complaints have one cause each. "Movement is not realistic": the sea is a static 30 m-cell
   sphere (256 cells per cube face over a 5 km planet) whose material only moves normals. Nothing rises,
   falls, hides what is behind it or breaks. "Empty inside": below the surface nothing draws the surface,
   the light, the particles or the bed's light loss; only one post-process with a hard on/off switch at mean
   sea level.
2. Waves must become geometry. Technique set, ranked by what shipped games and papers converge on: FFT
   cascades (Tessendorf) with choppy displacement, Jacobian foam, a camera-following clipmap mesh, LEAN-style
   roughness from lost slope [S1][S2][S4][S5][S6][S9].
3. We can bake the FFT. Quantise the frequencies (Tessendorf eq. 34-35) and play a looping flipbook: three
   cascades as texture arrays (about 80 MB with mips for the two main ones, 112 MB with the optional third),
   no compute shader [S1]. A runtime GPU FFT is possible from the game module, but it is a late upgrade, not a
   prerequisite (section 5).
4. The planet is small: the horizon is **110 m away for a standing player** (eye 1.2 m on a 5 km sphere), 700 m
   at 50 m up, 3.3 km at 1 km up. A dense mesh is needed only inside that radius. This makes the camera-centred
   mesh cheap (about 100k vertices) and is the strongest reason not to use Unreal's Water plugin or a terrain
   scheme.
5. Unreal's Single Layer Water (SLW) works on our own static mesh, with Lumen reflections, but **not with
   Nanite** (verified), **has no camera-under-water path** (hard-coded `false`, verified) and **measures the
   sun's path to the bed along world Z** (verified; wrong everywhere except the poles of a planet-centred
   world). So: SLW for the view from above, our own material and post-process for below.
6. The shading numbers are the cheapest large win: today's scattering gives the deep sea a diffuse
   reflectance of 0.05-0.7 percent, which is black. Open-sea numbers from pure-water optics give 0.3 / 1.6 /
   4.4 percent (R/G/B) [S13]. Section 3.6.
7. The existing underwater post-process is a good v1; it lacks the waterline, the surface seen from below with
   real refraction, depth-based light loss of the bed, god rays, particles, sound. Sections 3.7 and 3.8.
8. Walking into the sea "just makes you go under" because nothing is there: no immersion state, buoyancy,
   drag. The Outer Wilds numbers already in `docs/research/ow_mechanics.md` transfer almost directly (3.9).
9. Foam persistence is free in a baked FFT flipbook if the foam is accumulated in rest-position space (it is
   then advected with the water automatically). Section 3.2.
10. Order of work by impact per effort is in 3.12. Honest risks in section 4.

---

## 1. What is in the tree today, and why it looks like it does

| Piece | Where | State |
|---|---|---|
| Sea mesh | `EphemeraPlanetBuilder.cpp:808 BuildOcean` | One static cube-sphere, 6 x 256 x 256 cells (about 30.7 m between vertices), not Nanite when it is a real sea. `AEphemeraBody` adds it as the `Ocean` component, tide only via WPO `up * tide` |
| Material | `build_slice.py` `OCEAN_CODE`, `make_ocean_material` | SLW. Normals from 3-axis projected wave maps (97, 31, 11, 3.7, 1.3 m). Foam and a "painted" far sea from a baked depth cube, blended with `Opacity = Cover` |
| Depth | `BuildDepthMap`, 2048 per face, 8-bit, `sqrt((depth+8)/72)` | About 3.8 m per texel, 0.19 m per level near zero depth. Steps show ("a hand's length") |
| Moving normals (in flight) | `water.py animated()` | Per-wave frequencies rounded to whole turns of a 24 s loop, 128 frames of 128 x 128, BGRA8 slope, slope squared, height. Played at rate proportional to 1/sqrt(size). This is exactly Tessendorf's loop trick [S1] |
| Underwater (v1) | `UNDERWATER_CODE`, `EphemeraSkyRig.cpp:576-602` | Post-process (before DoF) switched by weight 0/1 when the eye is inside the sea sphere; `sigma (0.45, 0.10, 0.08)`/m; a Snell window mask at cos 0.60-0.70 (about 48.6 degrees); caustics from the Laplacian of the chop height; the mist fog is hidden while under |
| Swimming | none | `SenseWater` only drives sound. `GroundUnderfoot` returns "water" for footsteps. The capsule walks the sea bed (sea has `NoCollision`) |

Present SLW coefficients per cm: absorb (0.0046, 0.0013, 0.0012) = (0.46, 0.13, 0.12)/m, scatter
(1.0e-5, 3.0e-5, 3.6e-5)/cm. The single-scattering albedo is `scatter / (absorb + scatter)` = (0.002, 0.023,
0.029). An isotropic scatterer lit by ambient irradiance E returns `albedo * E / (4 pi)`, i.e. `albedo / 4` of
a white Lambertian surface: **0.05 %, 0.6 %, 0.7 %**. That is a black sea with a faint green, which matches
the player's "looks like shit". Blue is also absorbed 12 times more than pure water does (0.12 vs 0.0101 /m)
[S13], so even the shallows lose their blue.

Planet facts used below: radius 5000 m, surface gravity 10.5 m/s^2 (`build_slice.py:3400`), tide +/-5 m, player
capsule radius 0.30 m, half-height 0.66 m, eye 1.20 m above the feet, walk 4.8 m/s, sprint 8 m/s, jets 17 m/s^2
lift (`EphemeraPlayer.h`). Horizon distance `sqrt(2 R h + h^2)`: 110 m (h 1.2), 130 m (1.7), 316 m (10),
709 m (50), 1428 m (200), 3317 m (1000). Drop below the tangent plane at distance r: 1.0 m at 100 m, 25 m at
500 m, 101 m at 1 km, 417 m at 2 km.

---

## 2. Findings, with sources

### 2.1 Wave models

**Tessendorf, "Simulating Ocean Water" (SIGGRAPH course notes) [S1].** What I used, checked in the text:
- Deep-water dispersion `w^2 = g k`; finite depth `w^2 = g k tanh(k D)` (eq. 32); surface tension term
  `g k (1 + k^2 L^2)` for waves under about 1 cm.
- Looping: all frequencies must be multiples of `w0 = 2 pi / T` (eq. 34); use `wbar(k) = floor(w(k)/w0) * w0`
  (eq. 35). The surface then repeats exactly after T.
- Statistical model: sum of sinusoids by inverse FFT; Phillips spectrum for fully developed wind seas
  (k^-4 tail, a `|k.w|^2` direction term, cut-off below the longest wave the wind raises).
- Choppy waves: displace horizontally by `lambda * D(x,t)`, `D = -i (k/|k|) h~(k,t)`, which sharpens crests;
  the Jacobian of `x -> x + lambda D(x)` falls below 1 where the surface folds, and "signals the presence of the
  overlap"; that is the foam criterion.
- The patch tiles seamlessly, and the tiling shows as periodicity unless the patch is large relative to the
  view.
- Finite depth and shoreline are not an FFT strength ("odd looking underwater sea mound ... the FFT method
  again is a challenge"); Tessendorf points to iWave-type height-field solvers for that.

**Spectra beyond Phillips.** JONSWAP (fetch-limited) and TMA (JONSWAP made depth-aware), with directional
spreading and a "swell" parameter that elongates waves into parallel trains, are the empirical set used for
graphics in Horvath, "Empirical directional wave spectra for computer graphics" (DigiPro 2015): TMA for the
non-directional part, Donelan-Banner-type directional spreading, a normalised swell parameter [S7]. I read
the abstract and a 2025 paper's summary of it; the formulas in 3.2 are standard oceanography written out by me
(JONSWAP: `S(w) = alpha g^2 w^-5 exp(-1.25 (wp/w)^4) gamma^r`, `sigma = 0.07 / 0.09` below / above the peak),
not copied from Horvath.

**Cascades and repetition.** Dupuy and Bruneton: a single FFT patch repeats; instead compute n patches that
each sample a different part of the spectrum with different sizes, so the combined field repeats at the least
common multiple of the periods; "in practice, n = 4 is enough to avoid artifacts" [S5]. Arc Blanc (2025) uses
3 cascades by default, patch lengths 256 m, 16 m, 4 m, 256 x 256 each, on an RTX 2070 Super; its height
synthesis (3 cascades, height only) is 0.376 ms GPU, plus 1.175 ms for the velocity field it also computes [S8].

**Gerstner (trochoidal) waves.** A sum of a few Gerstner waves is the older vertex-shader approach; Tessendorf
notes it is the same dispersion physics and that oceanographers prefer statistical models [S1]. Uncharted 3
(GDC 2012) used 4 Gerstner waves plus 4 grids of "wave particles" per vertex, with vector (not height) displacement so
sharp crests need no very fine mesh [S4]. Crest keeps ShapeGerstner for shoreline and special waves, and says
realistic shoreline waves are "a challenging open problem" [S9].

**Foam.** Tessendorf: foam where the Jacobian drops (folding) [S1]. Dupuy and Bruneton turn that into
whitecap coverage: `b = H(eps - j)`, with `j` the Jacobian determinant of the summed patches, then since `j` is
roughly Gaussian over a pixel footprint they get coverage `W` in closed form from the mean and variance of `j`,
both linearly pre-filterable (mip-mapped) so distant foam anti-aliases; foam is a smooth Lambertian surface
with reflectance 0.4 [S5]. Sea of Thieves: foam at wave peaks "using the method described in the reference
paper", plus foam around objects from depth comparisons in a camera-centred window, progressively blurred with
feedback so it disperses, blended with artist textures, with calm/normal/stormy variants [S2]. Production note:
Horizon Forbidden West put foam in vertex colour and says it could not make features smaller than a triangle,
so foam popped; they wanted one vertex per pixel [S3]. So: foam in the pixel shader.

### 2.2 What shipped games did

| Title | What is documented | Source / gap |
|---|---|---|
| Sea of Thieves (Rare, UE4) | FFT per Tessendorf; colour = blend of deep and sub-surface colour by view angle, sun direction and a **wave peak mask from the choppiness offsets**; foam as above; area specular from the low sun using Karis' closest-point-on-sphere; **Snell's window applied when looking up from below**; shallow-water details from a GPU surface simulation (Mei et al. 2007) for deck splashes, streams | 2-page talk [S2]. Cascade sizes and mesh not stated |
| Uncharted 3 (Naughty Dog, GDC 2012) | Wave particles (Yuksel 2007) evaluated analytically: stateless, so rewindable; vector displacement; **screen-projected grid rejected (aliasing), quasi-projected rejected (large displacements); irregular geometry clipmaps (Losasso and Hoppe 2004) used**: rings of 16 patches follow the camera, "any point in a ring will always be sampled from the same place", fixer patches, no T-joints, vertex blending between levels; flow shader = two UV phases half a cycle apart | GDC 2012 slides [S4]; the talk is paywalled. Shoreline: not covered |
| Horizon Forbidden West (Guerrilla) | Breaking waves from **one baked animated cross-section** (Houdini), played along artist-drawn wavefront curves; river deformation as baked volume textures (128 layers); "ideally one vertex per pixel" | SIGGRAPH 2022 slides [S3]. **The deck explicitly leaves out underwater effects.** Nothing public that I could open on HFW's underwater rendering |
| Assassin's Creed IV | Ocean by Ubisoft Singapore; adaptive tessellation named as the next-gen difference | Secondary interviews only [S19b]. **No primary talk found**; GDC 2023 "Making Waves for Skull and Bones" covers the lineage but is paywalled [S19] |
| Atlas (Studio Wildcard) | GDC 2019 "Wakes, Explosions and Lighting: Interactive Water Simulation": ocean surface, interactive effects, server sync | Session page only [S18]; slides not open. Not used for numbers |
| Subnautica | **Not found.** No technical breakdown I could open. Do not cite it for a technique | |
| Killzone 3 / COD WWII (via HFW) | Streamed mesh waves; a looping Houdini sim | Mentioned in [S3] only |

### 2.3 Mesh and level of detail

- **Projected grid** (Hinsinger 2002): a screen-space grid re-projected on the water plane; adaptive and
  constant memory; used by Dupuy-Bruneton [S5]. Naughty Dog tried it and dropped it for aliasing [S4].
- **Clipmap rings** (Losasso-Hoppe, as adapted by Naughty Dog [S4]; same idea in Crest, where "each added LOD
  doubles the horizontal range", a "down sample factor" of 2 gives one vertex per 2 x 2 LOD-data texels, and ocean data
  (displacement, foam, depth) lives in cascaded textures centred on the viewer [S9]). A point in a ring is
  always sampled at the same world place, which is what stops vertices swimming.
- **Inference:** a polar grid centred on the camera swims when the camera turns or moves (every ring rotates
  against the world lattice); a clipmap snapped to its own lattice does not.

### 2.4 Shading

- LEAN mapping (Olano and Baker 2010) is already used in the repo; Bruneton-Neyret-Holzschuch 2010 is the
  ocean-specific form: geometry, normals and BRDF are one hierarchy, and what the mesh and the normal map no
  longer resolve is moved into the BRDF [S6]. The repo's `Rough = pow(0.0004 + lost slope, 0.25)` is that idea.
- Cox and Munk 1954 mean-square slope `0.003 + 0.00512 U` (U in m/s) is already the amplitude anchor.
- Sea of Thieves' colour model (above) is the cheapest convincing sub-surface term: crests are thin, so they
  glow in backlight [S2].

### 2.5 Unreal Engine 5.8 Single Layer Water (verified in source)

| Question | Answer | Where |
|---|---|---|
| Any mesh? | Yes: shading model on an Opaque or Masked material; the water pass runs after the base pass and deferred lighting, before translucency [S10] | `MaterialShared.cpp:6432-6447` ("must be opaque or masked", cannot combine with other shading models) |
| Nanite? | **No.** `IsSupportedShadingModel` returns `!HasShadingModel(MSM_SingleLayerWater)`; the mesh is rendered as an ordinary static mesh. The repo already builds the real sea as non-Nanite | `Engine/Private/Rendering/NaniteResources.cpp:3407-3410` |
| Lumen? | Yes: a dedicated Lumen reflection pass for SLW (`ELumenReflectionPass::SingleLayerWater`), cost depends on `r.Water.SingleLayer.Reflection.DownsampleFactor` and `.Denoising`. Epic's Lumen page says SLW reflections are "forced to mirror" [S10b]; the 5.8 shaders read the material roughness in the SLW composite, so treat the mirror claim as unverified | `SingleLayerWaterRendering.cpp:1696-1730`, `SingleLayerWaterComposite.usf:103-165` |
| Ambient and sun | Sun = the first directional light's colour times pi; ambient = the Lumen diffuse GI at the water pixel times pi | `BasePassPixelShader.usf:1699-1700` |
| Units | Coefficients are used as they are against depths in **centimetres**, so they are per cm (Epic's own example red 0.0033 equals Pope-Fry pure water at 650 nm, 0.0034 per cm) | `SingleLayerWaterShading.ush:160,173-175` |
| Camera under water? | **Hard-coded `false`** in the base pass and in the composite. SLW never shades a surface from below. A mesh seen from behind with default single-sided culling is simply not drawn | `BasePassPixelShader.usf:1698`, `SingleLayerWaterComposite.usf:68` |
| `View.UnderwaterDepth` | Public field, but only the Water plugin's `UWaterSubsystem::ComputeUnderwaterPostProcess` sets it (via `UWorld::OnBeginPostProcessSettings`, a public event). When it is set the renderer draws sky, fog and clouds **before** SLW and translucency, and skips some cloud composition | `SceneView.h:1793,2045`, `WaterSubsystem.cpp:816,999`, `World.h:1913`, `DeferredShadingRenderer.cpp:3721,3868-3881`, `SingleLayerWaterRendering.cpp:1875-1885` |
| Sun path to the bed | `DistanceFromScenePixelToWaterTop = max(0, waterPos.z - scenePos.z)`: **world Z**, a flat-water assumption. On our planet-centred world "up" is +Z only at the poles | `SingleLayerWaterShading.ush:205` |
| Scene texture in the SLW material | Only CustomDepth and CustomStencil. No scene depth, no scene colour | `HLSLMaterialTranslator.cpp:8163-8168` |
| Depth pre-pass | On by default and read-only; needed for virtual shadow maps | `r.Water.SingleLayer.DepthPrepass` |
| Velocity | WPO motion is written in the SLW depth pre-pass by default (`r.Water.SingleLayer.VelocityOutputPass=1`) | `SingleLayerWaterRendering.cpp:164` |
| Epic's own Water plugin | `Plugins/Experimental/Water`; flat bodies, Z-based immersion, landscape based; underwater is a post-process volume it injects | not usable on a sphere (matches `ground_level_landscape.md`) |

### 2.6 Underwater optics and rendering

- **Pure-water absorption**, Pope and Fry 1997, per metre (file is per cm; I multiplied by 100): 465 nm 0.0101;
  550 nm 0.0565; 610 nm 0.264; 650 nm 0.340; 700 nm 0.624 [S13]. Representative R / G / B (610 / 550 / 465 nm):
  **(0.264, 0.0565, 0.0101) /m**. Red is down to 1/e in 3.8 m, green in 18 m, blue in 99 m.
- **Real water adds** dissolved matter (CDOM), phytoplankton and sediment. `Kd(490)` of 0.1 /m is the NOAA
  example of reduced clarity (one e-fold per 10 m) [S14b]; `Kd(PAR)` is about `1.7 / Secchi depth` (Poole and
  Atkins; 1.4 in turbid estuaries) [S14]. I did not find a citable table of Kd by colour for Jerlov types; the
  presets in 3.8 are my design values built from the pure-water floor and these two anchors, not measurements.
- **Snell's window:** critical angle `asin(1/1.333)` = 48.6 degrees, so a cone of about 97 degrees shows the
  whole sky squeezed; outside it the surface is a mirror (total internal reflection) of the water; the rim is
  dark; rough surface shrinks it [S15]. Sea of Thieves applies it from below [S2].
- **Caustics, cheap and good:** GPU Gems ch. 2 traces rays up from the bed, refracts with n = 1.33 at the wave
  normal, and uses the angle to the vertical to read a sun-disc light map; water "transparency of 77-80 percent
  per metre" makes near-vertical paths dominate [S12]. Crest uses a caustics texture with focal-depth and
  depth-of-field controls [S9]. (Evan Wallace's WebGL Water lists caustics; the area-ratio method in 3.8 is
  the one I know from his article, which I did not open.) A practical Unreal recipe: light-function material
  of two panned layers on spotlights, split into slightly offset R, G, B lights, a bluer wide scatter light, fog
  with black in-scatter [S16].
- **Crest underwater (Unity):** a full-screen pass between transparents and post-processing using the opaque
  depth, a "meniscus" line where the lens meets the water, an ocean material with Cull Off so the underside is
  shaded, depth-fog density factor per channel, and "UnderwaterEnvironmentalLighting" reducing environment
  light with depth. Limits: transparent objects need the fog applied in their own shader. Legacy: a curtain
  mesh in front of the camera. Caustics, shadows and Snell's window are not documented there [S9].
- **God rays in Unreal:** community answers are mostly meshes or spot lights; the one thread I could open has
  no working answer for a translucent water plane [S17]. Engine fact: volumetric fog samples the main
  directional light's **light function**, so a caustic light function also paints the shafts (verified,
  `VolumetricFog.cpp:629,914`), and Lumen's direct lighting supports light functions
  (`LumenSceneDirectLighting.cpp:245`).
- **Sound:** air-water impedance gap (413 vs 1.48e6 rayl) passes `4 Za Zw / (Za + Zw)^2` = 1.1e-3 of the
  intensity, -29.5 dB (my arithmetic). Outer Wilds uses about an 800 Hz low-pass under water
  (`ow_gap_analysis.md` row 15).

### 2.7 Swimming (our own docs and the engine)

- Outer Wilds (`ow_mechanics.md` 1.6, `ow_gap_analysis.md` 1.11): quadratic drag `0.5 * rho * v^2 * 0.00392`
  with rho 30 for water (5.9 m/s^2 at 10 m/s); buoyancy = density ratio x **local** gravity x submerged
  fraction, full strength at about one body height; walk speed x0.5 with the camera under water; the jetpack
  burns no fuel in water unless boosting, and boost needs the camera out of the water; splash at 15 m/s or
  more.
- `UCharacterMovementComponent` swimming exists (`PhysSwimming`, `MOVE_Swimming`) but depth comes from
  `ImmersionDepth()`, which line-traces the **brush of an `APhysicsVolume`** (`CharacterMovementComponent.cpp:
  3807-3833`). We have no volume on a sphere. The player already integrates its own velocity in air
  (`ApplyFlight`), so a water branch there is the natural route.

---

## 3. Design for Ephemera

### 3.1 Decisions

1. **Keep SLW for the surface seen from above**, on a new camera-following clipmap, because it gives Lumen
   reflections, Fresnel, refraction, volume colour for free. Plan for its Z-up flaw (3.6, test T2).
2. **Own everything below the surface**: an underside material on the same mesh, and the post-process.
   Do not set `View.UnderwaterDepth` at first (it is optional, and changes pass order; see risk R3).
3. **Bake FFT cascades as texture-array flipbooks**, three cascades, with a Lagrangian foam channel.
4. **Planet-fixed wave field**: a cube-sphere chart (equi-angular), blended at face seams with
   variance-preserving weights. Waves must be fixed to the planet or they slide when the player walks.
5. **Mesh lattice is planet-fixed too** (same chart), snapped per ring, with morphing between rings.
6. **Keep the far sphere** as is, with a masked hole where the clipmap draws.
7. **Shore physics from the depth cube**, upgraded to 16-bit; a second cube for the shore wave phase later.
8. **Swimming is our own state in `AEphemeraPlayer`**, sampling a small CPU copy of the same waves.

### 3.2 Data to bake (new `Tools/terrain/ocean_fft.py`, numpy only)

**Sea state to bake (physics of this planet, g = 10.5).** Phase speed `c = sqrt(g L / 2 pi)`: 2.6 m/s at 4 m,
4.3 at 11 m, 7.3 at 32 m, 12.9 at 100 m (the repo's `water.py` uses g = 9.81, 3 percent slow; change it).
A fully developed 10 m/s sea (Pierson-Moskowitz) has Hs 2.0 m, Tp 6.8 s, wavelength 78 m, but a 5 km planet
cannot raise that locally. JONSWAP fetch-limited, F = 8 km: U = 5 m/s gives Hs 0.22 m, Tp 2.0 s (7 m);
U = 10 gives Hs 0.44 m, Tp 2.6 s (11 m); U = 15 gives Hs 0.66 m, Tp 2.9 s (14.5 m). So the visible big motion
must be **swell** arriving from outside the planet's own weather (a fiction we are free to choose; Iceland
has it). Bake two components with one seed:
- swell: JONSWAP-shaped, `Tp = 7.7 s` (wavelength 100 m), `gamma = 6`, narrow spreading `cos^(2s)`, s = 30 around
  the swell heading, Hs 1.2 m;
- wind sea: JONSWAP fetch F = 8 km, U = 10 m/s, Donelan-Banner-like spreading (about +/-35 degrees), Hs 0.44 m.
Runtime scales each cascade by a gain for calm / moderate / rough; for visibly different peak periods bake
2-3 states from the **same random numbers** and blend them (correlated fields blend as an interpolated
spectrum; independent fields would lose variance).

**Cascades.** Texture arrays with one slice per time frame, so mips filter in space only (a volume texture
would average frames in its mips; harmless for the slope maps in flight because the slope-squared term
restores the variance, but wrong for displacement).

| | L (m) | N | texel | wavelengths | w_max | loop T | frames | size, 16 B/texel | notes |
|---|---|---|---|---|---|---|---|---|---|
| C0 swell + long sea | 512 | 128 | 4.0 m | 31-512 m | 1.45 rad/s | 64 s | 128 | 32 MB | `k` 0.012-0.20 |
| C1 wind sea | 96 | 128 | 0.75 m | 5.2-31 m | 3.55 | 24 s | 112 | 28 MB | `k` 0.20-1.2 |
| C2 ripples (optional) | 24 | 128 | 0.19 m | 0.9-5.2 m | 8.57 | 8 s | 96 | 24 MB | `k` 1.2-7; otherwise keep the in-flight slope maps for this band |

Totals with mips (x4/3): 43 + 37 (+ 32) MB; GPU memory is no concern on 12 GB. Periods 512, 96, 24 m and loops
64, 24, 8 s are mutually incommensurate: the combined pattern repeats at 192 s and 2 km+. Time frames: the
highest harmonic in a cascade is `w_max T / 2 pi` (14.8, 13.6, 10.9); eight frames per period of that
harmonic is 119, 109, 88 frames, which the 128 / 112 / 96 above meet. Linear interpolation then costs about
2 percent of amplitude at the swell peak (harmonic 8, 15 frames per period) and up to 8 percent at each
cascade's top harmonic, which carries little energy. Loop quantisation moves a wave's speed by at most `w0 / w`:
12 percent at the swell peak (w 0.81), 18 percent at `k = 0.2` (C1 floor); on average half of that. Invisible.

**Channels per frame per cascade (8 half floats = 2 x RGBA16F):**
- A = (h, Dx, Dz, sx): height, the two horizontal choppy displacements (already multiplied by their lambda;
  lambda 0.35 C0, 0.9 C1, 0.6 C2), slope along x;
- B = (sz, tr, det, foam): slope along z, `tr = lambda (dDx/dx + dDz/dz)`, `det = lambda^2 (dDx/dx dDz/dz -
  (dDx/dz)^2)`, foam accumulated.

The Jacobian of the surface map is `J = 1 + g tr + g^2 det` for a gain `g` scaling the cascade (exact, so the
sea state can change at runtime), and the whole sea's `J` is the first-order sum over cascades
(`J = 1 + sum_c (g_c tr_c + g_c^2 det_c)`; cross-cascade determinant terms dropped).

**Foam is baked in rest-position space.** In the choppy model a texel is a water parcel's rest position `x0`
and the surface point is `x0 + D`. A foam value stored per `x0` therefore moves with the water. Offline, per
frame: `foam(t) = max(source(t), foam(t - dt) * exp(-dt / tau))`, with `source = saturate((eps - J) * k)`
and `tau` 2.5-5 s; iterate the loop until frame 0 matches frame F (3 passes). A looping, advected, decaying
foam, no feedback buffer at runtime. (Sea of Thieves needs a feedback blur for the same effect [S2].)
Baked at nominal gain; at runtime modulate by the gain.

**Spectrum (per cascade, then per bin).**
`S(w) = alpha g^2 w^-5 exp(-1.25 (wp/w)^4) gamma^r`, `r = exp(-(w - wp)^2 / (2 sigma^2 wp^2))`.
Fetch-limited constants: `alpha = 0.076 (U^2 / (F g))^0.22`, `wp = 22 (g^2 / (U F))^(1/3)`.
2D density: `E(k, theta) = S(w) (dw/dk) / k * D(theta)`, `dw/dk = g / (2 w)`. Per bin amplitude
`|h0| = sqrt(2 E dk^2)`, with complex Gaussian `xi`; dispersion `w = sqrt(g k)` (add `tanh(k D)` only for a
depth-specific bake; the shelf is handled in the shader). Quantise `wbar = floor(w / w0) w0` (never 0).
Time evolution `h~(k, t) = h~0(k) e^{i wbar t} + conj(h~0(-k)) e^{-i wbar t}`. Outputs by inverse FFT:
`h`, `Dx = -i kx/|k| h~`, `Dz = -i kz/|k| h~`, `sx = i kx h~`, `sz = i kz h~`,
`dDx/dx = kx^2/|k| h~`, `dDz/dz = kz^2/|k| h~`, `dDx/dz = kx kz/|k| h~` (signs follow the FFT convention
used; check one frame by finite differences). Pack two real fields per complex FFT. Bake time: seconds.
Checks to print in the script (as `water.py` does): rms slope per cascade against Cox-Munk, loop-seam step vs
mean step, `min J` per cascade, foam coverage fraction (target 2-6 percent at U 10).

**Importer.** Copy `ImportWaveVolume` into an `ImportWaveArray` that builds a `UTexture2DArray`
(`Source.Init(N, N, Frames, 1, TSF_RGBA16F, bytes)`; the planet builder already does the same for a cube).
`Python` cannot call `UpdateSourceFromSourceTextures` (not a UFUNCTION), so this stays C++. Set `NeverStream`,
`SRGB=false`, `CompressionSettings = TC_HDR` or uncompressed, mips on, `Filter = Trilinear`, `Address = Wrap`.
Sample time as `float3(uv, slice)` twice and lerp; to wrap, use `(slice + 1) % F`.

**CPU copy for gameplay.** `Source` bulk data is editor-only, so also write a small `UEphemeraWaveTable`
data asset: C0 at 64 x 64 x 64 frames, C1 at 64 x 64 x 56, `h, Dx, Dz` as fp16 (1.5 MB + 1.3 MB). C++ evaluates
the same chart and the same time to give heights, horizontal displacement and orbital velocity at a point.

**Depth cube upgrade.** Replace the 8-bit sqrt cube by 16-bit (R16 UNORM) over -16 to +16 m range for the
swash band plus a coarse range for deep water (or two cubes). 6 x 2048^2 x 2 B = 50 MB. Shore damping and
breaking need depth steps well below the 0.19 m the byte gives.

### 3.3 Mesh: the clipmap, and how it joins the far sea

**Layout (a static mesh asset built by a new `BuildOceanClipmap` in `EphemeraPlanetBuilder`, like `BuildOcean`).**
One ring mesh: a 128 x 128 cell square with the central 64 x 64 cells removed (12.3k vertices, 24k triangles),
flat in the XY plane, unit spacing. Eight instances in one `UInstancedStaticMeshComponent` (one draw call),
ring k scaled by `s_k = s0 * 2^k` with `s0 = 0.5 m`; the hole of ring k is exactly the footprint of ring k-1,
and ring 0 gets a 64 x 64 cell filler at 0.5 m so the centre is solid (a second small mesh, 4k vertices).

| ring | spacing | half extent | needed for |
|---|---|---|---|
| 0 | 0.5 m | 32 m | walking player, everything within wading range |
| 1 | 1 m | 64 m | |
| 2 | 2 m | 128 m | the horizon of a standing player (110-130 m) |
| 3 | 4 m | 256 m | |
| 4 | 8 m | 512 m | 50 m up (horizon 709 m) |
| 5 | 16 m | 1024 m | |
| 6 | 32 m | 2048 m | |
| 7 | 64 m | 4096 m | jet-flight altitudes up to about 1 km |

About 100k vertices, 200k triangles, any frame. Rings beyond the horizon at the current height are culled
on the CPU (hide ring k if its inner edge is past `horizon(h) * 1.2`). Displacement is gated per cascade
by ring so no wave is sampled coarser than 6 vertices per wavelength: C2 in rings 0-1, C1 in rings 0-3,
C0 in rings 0-5 (rings 6-7 show a flat sea with normals only).

**Planet-fixed lattice (no swimming).** Use the same equi-angular cube chart as the waves. For the face
nearest the camera (hysteresis of about 7 degrees past the cube edge) the lattice vertex is the direction
`normalize(axis + tan(sa / R) e1 + tan(sb / R) e2)`, with `(sa, sb)` multiples of the ring spacing in metres of
arc along the face's two great-circle axes. The component carries the snapped chart offset; each ring snaps to
**twice** its spacing so a fine vertex always lands on a coarse vertex. The vertex shader turns the flat
mesh position into the sphere (equidistant mapping, so cell size is true out to 4 km: angle = r / R). A
moving camera only hops the lattice by whole cells, and the wave field is a function of the vertex's
planet position, so nothing slides. When the camera crosses a cube edge the lattice changes face; the pop
is a sub-cell vertex shift under waves that are at least 6 cells long, i.e. invisible. (Naughty Dog's rule,
"any point in a ring is always sampled from the same place" [S4], is the same invariant.)

**Morphing.** Over the outer 25 percent of each ring, the vertex blends toward the nearest coarser lattice
position (`m = smoothstep(0.75, 1.0, chebyshev / halfExtent)`; snap `x` to `2 s` and lerp), and cascade
amplitudes that the next ring cannot carry fade by the same `m`. Rings meet without cracks and without a
step in height.

**Bounds.** The mesh is flat and the shader bends it onto the sphere, so set a large negative Z bounds
extension on the asset (`UStaticMesh` Positive/Negative bounds extension, verified present) and
`bNeverDistanceCull`; keep `SetCastShadow(false)`, `bAffectDistanceFieldLighting=false`, and set
`bVisibleInRayTracing=false` (the ray-traced scene would hold the undisplaced flat mesh).

**Joining the far sphere.** The existing sphere stays and keeps its painted far look. Give it a Masked blend
and an opacity mask that is 0 inside the clipmap's square (the same Chebyshev test the clipmap uses, from a
material parameter collection holding the lattice centre and half extent) and 1 outside. The clipmap's
outermost ring has displacement and tide-consistent height faded to the sphere's own level over its last
ring, so the two surfaces coincide at the seam; the clipmap extends 2 percent past the mask edge so the seam
has no gap. Where both draw, they are the same material at the same height, so any z-fighting is between
identical colours. Alternative if Masked costs too much: drop the far sphere 3 m under the clipmap and lift
it back over the last ring through WPO; coarser, more fiddly, not recommended. Both use the same tide term
(`1.5 cos^2 - 0.5`) and the same depth lookup so colour and height agree across the seam.

**C++ ownership.** A small `UEphemeraOceanComponent` on `AEphemeraBody` next to the existing `Ocean` mesh: it
owns the ISM, finds the camera, snaps, writes `GridCentre/GridFace/HalfExtent` to a material parameter
collection, and the wave table. Update after the player has moved (the player's `Tick`), one frame late is
harmless (rings overlap).

### 3.4 Shader math

Two `Custom` nodes, like today's. Inputs: rest position `P` (planet-local), `Time`, the two arrays, the depth
cube, per-cascade gains `g0 g1 g2`, ring index (`PerInstanceCustomData`), tide terms.

**Vertex (WPO).** Metres, planet-local.
```
up   = normalize(P);  R = |P| * 0.01
d    = max(depthCube(up) + tide(up), 0)                         // m of water
for each of up to 3 charts (axis i, sign) with weight w (variance preserving: w_i = a_i / sqrt(sum a^2),
                                                         a_i = pow(|up_i|, 6), skip if w < 0.02):
    ab = atan(up[a] / up[i], up[b] / up[i]) * R                 // metres of arc on the chart
    for each cascade c enabled for this ring:
        uv = rot(f) * ab / L_c + off(f, c)                     // per-face rotation aligns the wind
        s  = frac(Time / T_c) * F_c;  i0 = floor(s);  a = s - i0
        A  = lerp(tex2dArray(A_c, (uv, i0)), tex2dArray(A_c, (uv, (i0+1) % F_c)), a)   // mip 0
        k  = g_c * shore_c(d) * morph_c
        disp += w * k * (A.h * up + A.Dx * e1_f + A.Dz * e2_f)     // e1,e2: chart axes projected onto the tangent plane
return disp  (metres)  +  up * tide(up)
```
The wind direction on a sphere cannot be uniform; each face gets a fixed rotation that points its +x at the
prevailing wind's tangent at the face centre; seams blend two wind directions, which reads as a confused sea
near the seam only. Chart stretch is at most about 1.3 at face corners with the equi-angular mapping
(the in-flight 3-axis projection has up to 1.41; both figures are my **Inference** from the projection
geometry, not measured).

**Shore damping `shore_c(d)` (Inference, constants to tune).** Waves are felt by the bed when `d < lambda/2`
(Crest's rule [S9]) and a wave breaks when `H > 0.78 d`. Real waves grow before they break (Green's law,
`~ d^-1/4`) but a deep-water FFT cannot shoal, so: `shore_c = min(1.35, (lambda_c / (2 d))^0.25)` for
`d > d_b`, and a smooth ramp to 0 below `d_b = H_c / 0.78` and to 0.15 m of depth, with `H_c` the cascade's
rms height x 4 (about 1.2 m for C0 at Hs 1.2). Also clamp the trough: `eta >= -0.9 d`, so the surface never
digs into the bed.

**Pixel (normal, foam, roughness).** The rest position comes from the interpolated chart coordinates (not the
displaced position). Sample `A, B` of each cascade at that coordinate with analytic gradients (mips in space
only); then
```
slope = sum w * g_c * shore_c * (A.sx * e1 + B.sz * e2)          // tangent vector
N     = normalize(up - slope)                                    // the old return value
J     = 1 + sum g_c * tr_c + g_c^2 * det_c                       // from B
white = saturate((Jthr - J) * 6)                                 // instant whitecap, Jthr ~ 0.3
lag   = sum w * B.foam * g_c                                      // baked, advected, decaying
foam  = max(white, lag) * lace(noise)                            // lace: existing Chop-based eaten-lace
lost  = sum MSS_c * smoothstep(0.5 * lamMin_c, 2.0 * lamMin_c, footprint)   // slope variance this pixel can no longer resolve
Rough = pow(0.0004 + lost + legacyChopLost, 0.25)                 // legacy chop maps keep supplying < 5 m
```
The in-flight `Waves()` calls (11, 3.7, 1.3 m chop and 97, 31 m swell) become the **C2 and capillary layer**,
keeping their LEAN variance term; drop the 97 and 31 m swell calls once C0 and C1 exist. `MSS_c` is each
cascade's mean-square slope (printed by the bake script) and `lamMin_c` its shortest wavelength. Wave-crest mask
for the sub-surface glow (Sea of Thieves [S2]): `crest = saturate(-0.5 * (sum g tr))` plus height; feed it into
`PhaseG` and `ScatteringCoefficients` (3.6), not into emissive (SLW attenuates emissive by opacity).

**Mip filtering of foam and roughness.** Foam `lag` is linear data and mips fine. `J`'s coverage is not
linear; Dupuy-Bruneton's mean-and-variance trick [S5] is the correct fix but needs mipped `tr`, `det`
statistics; start with `white` from the nearest mip only when the footprint is below 2 texels and fade it by
distance (the repo already fades far surf).

**TSR.** Wire the previous-frame WPO through a `PreviousFrameSwitch` (time minus the frame time) so
`r.Water.SingleLayer.VelocityOutputPass=1` gets true motion; otherwise crests ghost under TSR (risk R6).

### 3.5 Shoreline

1. **Now (depth-driven, cheap):** the shore damping above; the existing swash/surf bands keep working off
   the upgraded depth cube; add the tide into `d` (already done in `OCEAN_CODE`). Foam from `lag` and the
   depth-band foam together.
2. **Rocks and sea stacks:** an SLW material may only read CustomDepth/CustomStencil (verified). Enable
   `Render CustomDepth` on the stacks and rocks that stand in water (a few dozen instances) and make foam
   rings where `CustomDepth - PixelDepth` is small (Sea of Thieves' depth-comparison foam [S2], with the
   Lagrangian foam above instead of a feedback blur).
3. **Later (item 10 in 3.12), shore-parallel breaking waves:** solve the eikonal equation `|grad tau| = 1/sqrt(g d)`
   on the cube-sphere from the waterline (fast marching, numpy/scipy, offline). `tau` is the time a wave
   needs to reach the shore. Add a wave train `eta = A(d) f(w (t + tau(x)))`, with `f` a sawtooth-skewed
   profile and a forward horizontal shift that grows as depth falls, a foam burst when `A / d > 0.78`, and
   a swash line that runs up and down a few metres with the wave phase. Wavefronts then bend round headlands
   by refraction, like Horizon's hand-authored wavefront curves [S3], but computed from the bathymetry
   (**Inference**; Crest calls shoreline waves an open problem [S9]). Bake `tau` as an R16F cube (50 MB or
   1024 faces for 12 MB).

### 3.6 Surface colour and light (SLW parameters)

All per cm for the material (per m x 0.01). Isotropic equivalent of backscatter `~ 2 b_b`.

| Preset | absorb (R,G,B) /m | scatter (R,G,B) /m | albedo | deep reflectance (R,G,B, white = 1) | Use |
|---|---|---|---|---|---|
| Open sea | 0.30, 0.075, 0.028 | 0.004, 0.005, 0.006 | 0.013, 0.062, 0.176 | 0.3 %, 1.6 %, 4.4 % | beyond the shelf, depth over 20 m |
| Shelf (coastal green) | 0.42, 0.16, 0.11 | 0.05, 0.07, 0.06 | 0.11, 0.30, 0.35 | 2.7 %, 7.6 %, 8.8 % | near the black-sand coast |
| Glacial milky | 0.55, 0.30, 0.25 | 0.09, 0.11, 0.12 | 0.14, 0.27, 0.32 | 3.5 %, 6.7 %, 8.1 % | river mouths (mask from the stream flow map) |

(Open absorb = pure water (0.264, 0.0565, 0.0101) [S13] plus about (0.04, 0.02, 0.018) of dissolved and
particulate matter. Everything else is design; tune by A/B screenshots against the player's idea of a cold
sea.) Blend preset by baked depth with the existing `ShelfDepth` curve. Make the painted far colour from the
same albedo (`albedo / 4 * ambient`) so there is no colour step at `FarStart`.

- `PhaseG`: 0.25 base, up to 0.6 on crests (`crest` mask). A positive `g` favours light coming through toward
  the eye, i.e. low sun behind a wave: green-lit crests, which Iceland's low sun will show constantly.
  Raise `ScatteringCoefficients` by `1 + 2 crest` on crests; do not raise absorption.
- Specular 0.255 (F0 0.02) and the sun's area specular as now. SLW's own `ShoreOpacity` fades the surface over
  the last 50 cm at the waterline (`saturate(DeltaDepth * 0.02)`, composite shader), which is the soft contact
  we want.
- Remove the "Cover = painted" fade for the near sea; use Cover only for foam and the far sphere.
- **Sun path to the bed (verified flaw).** `BehindWaterSceneLuminance *= exp(-ext * max(0, waterZ - sceneZ))`
  uses world Z. At the equator the seabed under a sloping view can be tinted or darkened by an arbitrary
  amount; at the poles it is right. We cannot patch the engine. Mitigation order: (a) test (T2); (b) if it
  shows, set `ColorScaleBehindWater` to counter-scale by the *expected* factor from the baked depth and
  `up . Z` (it is evaluated at the water pixel, so this can only be approximate); (c) fall back to a custom
  translucent sea material using the baked depth for its own bed colour. Do not count on (c) being needed.
- Reflections: `r.Water.SingleLayer.Reflection=1` with Lumen HWRT is the high setting. Expected to be the
  biggest part of the sea's cost (estimate 1-2 ms at 1440p with TSR 67 percent). Lower settings: `=2` (captures
  and sky only), or `.DownsampleFactor=2` with `.Denoising=1`.

### 3.7 Underwater: the model and the post-process (replaces `UNDERWATER_CODE`)

**Switch and waterline.** `z_cam` = signed depth of the eye below the displaced surface under it:
`R_sea + tide + eta(cam) - |cam - centre|`, with `eta` from the CPU wave table (C0 + C1). The current hook uses
only `R_sea + tide`, so a 1 m swell is ignored. Weight = 1 when `z_cam > 0.05 m`, 0 above `-0.05 m`.
For `|z_cam| < 1.5 m` the post-process computes a **per-pixel waterline mask** (this is the "camera half under
the line"): take the near-plane point of each pixel's ray (15 cm out), sample C0, C1 height at its chart
coordinate, and mark the pixel "in water" if its radial height is below the surface. Horizontal choppy
displacement is ignored (error of a few cm). Add a 2-3 px bright meniscus band where the difference is within
1-2 cm, and for 1.5-2.5 s after surfacing a screen-space droplet layer on the lens (a tiled normal-map
overlay, fading). Outside that 3 m band the mask branch is skipped.

**What is drawn from below.** The sea mesh is closed; from inside, its back faces cover the entire upper
view. SLW draws nothing there (its single-sided material is culled). So:
- the same clipmap ISM and the far sphere get a **second component each** with `bReverseCulling = true` and
  an Opaque Unlit material `M_OceanUnder`, visible only while the eye is below water (and the SLW components
  hidden then, to save the pass). Same WPO, so the underside is the true displaced surface;
- `M_OceanUnder` shading: the view ray `v` meets the wave normal `n` from `N` (the same pixel normal as above);
  `cos_i = dot(-v, n_down)`, `sin_t = 1.333 sin_i`. If `sin_t < 1`: transmitted into air, direction `t`;
  `sky = SkyAtmosphereViewLuminance(t)` (accepts any world direction, verified `MaterialTemplate.ush:2369`)
  plus the sun disc where `dot(t, sunDir)` is within 0.27 degrees (`SkyAtmosphereLightDiskLuminance`), times
  `(1 - F)` with Schlick `F0 = 0.02` from the water side; this squeezes the sky into the 97 degree window
  without distortion tricks. If `sin_t >= 1`: total internal reflection, colour = the medium's own colour
  along the reflected direction, `veil(c, long path) * 0.9` plus a faint copy of the near bed;
- clouds are not in the sky-atmosphere luminance. Sample the real sky-light cube (`SkyLightEnvMapSample`,
  verified) at `t` at roughness 0.3 for the window's average, blended in at 60 percent, so the window carries
  cloud brightness; accept that it is not sharp;
- a thin dark band at the rim (Snell's window rim is dark [S15]) comes out of the `F -> 1` ramp.

**Medium (post-process, "before DoF", pre-exposed like v1).** Per pixel with `s` the distance to the scene
point along the ray (scene depth / ray z-component) and `z_p` the depth of that point below the surface:
```
c     = a + b                                  // beam attenuation per channel, preset below
T     = exp(-c * s)
E(z)  = E(0) * exp(-Kd * z / mu)               // downwelling at depth z; mu = 0.8, about the cosine of the refracted light
Lobj  = Scene * exp(-Kd * z_p / mu)            // the scene was lit as if in air: take the missing water above it away
Lveil = (E_amb(z_mid) * Iso + E_sun(z_mid) * Phase(g, cos_theta) * V) * sigma_s / ext
colour = Lobj * T + Lveil * (1 - T)
```
`Lveil` is SLW's own in-scatter term, so the surface does not change colour as you cross it:
`ext = sigma_a + sigma_s` of the SLW preset, `E_amb` from the sky-light cube (roughness 1) or Lumen, `E_sun`
from `SkyAtmosphereLightIlluminance`, `V` is 1 (no rock shadow; the god rays and light function add it later),
`z_mid` the depth halfway along the ray (clamped to >= 0), `Iso = 1/(4 pi)`. Sun and sky are both dimmed by `Kd`
in `Lobj`, which is wrong in detail (the sun path is longer, `z / cos(theta_w)`; the sky's is not) but right in
mood.
Visibility range (what the eye resolves) is set by `c`, not `ext`: beam attenuation includes the forward
scattering that blurs, but does not brighten, the image. A floor of 0.5 percent on the ambient so that night
and depth never go pure black (the user's "night not black").

| Preset | a (R,G,B) /m | b (R,G,B) /m | c = a + b | 4.6 / c (view range, m) | Kd (R,G,B) /m |
|---|---|---|---|---|---|
| Open sea | 0.30, 0.075, 0.028 | 0.03, 0.035, 0.04 | 0.33, 0.11, 0.07 | 14, 42, 66 | 0.32, 0.09, 0.04 |
| Shelf (default) | 0.42, 0.16, 0.11 | 0.18, 0.22, 0.24 | 0.60, 0.38, 0.35 | 7.7, 12, 13 | 0.46, 0.19, 0.14 |
| Glacial milky | 0.55, 0.30, 0.25 | 0.90, 1.10, 1.20 | 1.45, 1.40, 1.45 | 3.2 | 0.80, 0.60, 0.55 |

Light left at depth with the shelf Kd (R, G, B): 1 m 0.63, 0.83, 0.87; 3 m 0.25, 0.57, 0.66; 10 m 0.01, 0.15,
0.25; 25 m 0, 0.01, 0.03. With open-sea Kd: 10 m 0.04, 0.41, 0.67; 25 m 0, 0.11, 0.37. These give the
Icelandic look: red gone by 5 m, blue-green fading to dark teal by 15-20 m. Compare v1: `sigma (0.45, 0.10, 0.08)`
is close to the shelf row for absorption but has no separate veil colour and no depth-dependent light.
Use the same preset index everywhere (one MPC, driven by baked depth and a river mask) so the SLW surface and
the underwater medium agree.

Other terms in the pass (all cheap):
- Lens: optionally narrow the FOV about 8 percent while the eye is below water (a flat port magnifies by
  1.33; the helmet visor is flat); and a slow breathing vignette. Optional.
- The lamp: set its attenuation radius to about 14 m and tint it (0.6, 0.9, 1.0) while under water; its light
  falls off with `exp(-c d)` as well.
- Fog: the sky rig already hides the mist box and the fog component while `bUnderNow` (`bMist` is false then,
  `EphemeraSkyRig.cpp:604,617`), so the air does not fog the underwater scene. Keep that, and feed it the
  wave-aware `eyeUnder` rather than the mean sea radius.
- Aerial perspective from the sky atmosphere is on the scene colour and is negligible within 100 m; the
  medium's `T` attenuates it. Verify (T4).

### 3.8 Caustics, god rays, particles, seabed

**Caustics (two stages).**
1. *Post-process (item 8 in 3.12):* bake a looping caustic map from the same C1 height field: for each texel, cast a
   vertical-sun ray through the surface, refract with n = 1.333 using the slope, and accumulate onto a plane
   at 4 depths (1, 3, 8, 20 m) as the ratio of source to refracted cell area (the area-ratio method I know from
   Evan Wallace's WebGL Water article, not opened here; GPU Gems ch. 2 gives the cheaper variant [S12]).
   128 x 128 x 64 frames x 4 depths x 1 channel at 8 bit = 4 MB.
   In the pass: position from depth, sun direction refracted (`sin_t = sin_i / 1.333`), project the pixel onto
   the surface along it, read the map in the chart's coordinates, `colour *= 1 + g * (C - 1) * sunShare * Nl`.
   Without sun visibility, caustics appear on shadowed sides of rocks: limit by `Nl` and fade `exp(-z / 12)`.
   Chromatic fringes: sample R, G, B at depth +/- 6 percent (the three-lights trick of [S16] in one pass).
2. *Light function on the sun (item 11 in 3.12; true shadows, god rays for free):* a Light Function material on the
   existing directional light: `step(0, seaR - |AbsoluteWorldPosition|)` makes it act only below the sea;
   output = `caustic(...) * exp(-Kd z / mu)`. The sun's shadow maps shadow it (rocks, stacks), Lumen's direct
   lighting honours it, and volumetric fog multiplies by it, so shafts appear wherever there is fog density.
   Cost: one texture read per sun-lit pixel; no second light. Risk: it also touches the atmosphere sun's disc
   or the cloud lighting (R7) and needs fog density below water, which our mist box does not provide.

**God rays (v1 in the post-process, no volumetric fog).** 12-16 jittered samples along each view ray:
`shafts = sum_i caustic(surfacePoint_i) * exp(-c * (s_i + z_i / cos_t)) * ds * phase`; with temporal jitter
and TSR, it holds up at 12 samples. Estimate 0.7-1.0 ms at 1440p on the 3060; run at half resolution and
upsample if it matters. Shafts end at scene depth (no rock shadows in them).

**Marine snow.** Reuse the motes system that `EphemeraSkyRig` already drives (a camera-following quad cloud,
`SM_Motes`, 1800 quads, material-driven motion): different parameters under water: 1500-2500 particles in a
14 m box, size 2-6 mm, sink 1-3 mm/s plus the local orbital velocity, brightness by shaft strength and
`exp(-c d)`. Larger plankton motes (1 in 20) pulse slowly. No Niagara needed.

**Seabed detail.** Kelp and wrack are ground cover with a depth mask: oarweed 2-15 m on rock, wrack on
rock in the tidal band; sway uses the same orbital velocity as the water so the whole bed moves with the
swell: `x += A_orb * cos(w t - k x) * exp(-k z) * (height along blade)^2` per blade. The existing ground-cover
system (`EphemeraGroundCover`) scatters from the height field; add a "wet band" rule. Assets are a content
job (hand-made kelp ribbons with two-sided leaf material); it is the last item in the list.

### 3.9 Swimming and entering the sea (`AEphemeraPlayer`)

Submersion `f = clamp((surface - feet) / 1.32 m, 0, 1)` where `surface` is the CPU wave height under the player
(radial, tide included), and a flag `eyeUnder`. States:

| State | Entered when | Movement | Notes |
|---|---|---|---|
| Dry | `f = 0` | as now | |
| Wading | `f` in (0, 0.55) and grounded | `ApplyWalk` with speed x `(1 - 0.5 f)` and ground braking x `(1 + 2 f)`; ripples at the feet; step sounds from `GroundUnderfoot` as now | foam trail from the swash noise |
| Swimming | `f >= 0.55`, or `f > 0.3` and not grounded | `MOVE_Falling` with `GravityScale = 0`; own integration like `ApplyFlight`; jump = swim up, `JetInput` down = swim down | |
| Submerged | `eyeUnder` | as swimming, plus post-process and sound | |

**Numbers (`g` = 10.5, body 1.32 m).**
- Buoyancy: net upward acceleration `a_b = g (k_b f - 1)` with `k_b = 1.18`: equilibrium at `f = 0.85` (head
  and shoulders out), +1.9 m/s^2 when fully under (a gentle rise, not a pop). Vertical damping `c_v = 2.1 /s`
  (damping ratio 0.35 for the natural frequency `sqrt(k_b g / H)` = 3.06 rad/s, period 2.05 s): one or two
  bobs when you surface.
- The surface moves: use the instantaneous `surface(t)` and add the orbital velocity of the swell
  (A 0.6 m, wavelength 100 m: 0.49 m/s horizontal circle at the surface, factor `exp(-2 pi z / 100)`, 0.83 at 3 m)
  to the water velocity in the drag relative velocity `v_rel = v - v_water`. Swimmers then ride the swell.
- Drag: `a = -(c_lin + k_q |v_rel|) v_rel`, `c_lin = 0.4 /s`, `k_q = 0.6 /m` (same quadratic form as Outer
  Wilds' `0.5 rho v^2`, different constant because their body is not ours). Swim thrust 2.9 m/s^2 gives a
  terminal 2.2 m/s forward (about half the 4.8 m/s walk, as in Outer Wilds' x0.5); vertical swim 1.8 m/s;
  strafe 1.6 m/s. Releasing the key coasts about 1.2 m and stops in about a second.
- **Jets under water:** keep them (Outer Wilds does): thrust 0.6 x `JetLift` = 10 m/s^2 gives a terminal
  4.1 m/s; **no fuel drain unless boosting; boost disabled while the eye is under**; a bubble plume, flame
  quenched, and a distinct under-water loop. A jet launch from the surface (`f` falling through 0.6 with
  upward speed over 6 m/s) clears the water and splashes on the way up.
- Entering: impact speed `> 3 m/s` splash (small ring + droplets), `> 8 m/s` large splash and an air-bubble trail
  1.5 s long; the first 0.6 s have the quadratic drag at x0.4 so a dive reaches 2-3 m instead of stopping at
  once. Outer Wilds' splash threshold is 15 m/s; ours is lower because walking pace is lower.
- Exit: wading up the beach is automatic; from deep water, a jump at the surface (`f < 0.7`) gives a 4.5 m/s
  hop; the lens droplet layer runs 2 s.
- Gravity alignment: keep `AlignToGravity`; the camera pitch is free; while swimming the body "up" may relax
  toward the swell's surface normal with a 0.5 s time constant for a slight roll with the wave.
- Camera: reduce the stride bob, add a slow 0.12 Hz heave and roll of 1-2 degrees, and subtract nothing from
  FOV unless the optional mask effect is on.

**Why not `MOVE_Swimming`:** `ImmersionDepth` needs a physics volume brush (verified); we have none on a
sphere. Do not add one.

**Sound.** `UEphemeraPlayerAudio::PlayAt/SetLoop` already take a low-pass frequency per sound. Add
`FEphemeraSoundScene::Submerged (0..1)`. When 1: world sounds low-passed to 700 Hz and -6 dB (Outer Wilds
uses about 800 Hz [S20]); wind and shore loops -24 dB; a sea-bed bed loop (low rumble) in; suit, jets and
heartbeat stay dry. Ramp 0.12 s; at `f` between 0.8 and 1 with the eye at the line, crossfade by the waterline
fraction. Splashes and surfacing breaths are one-shots. These must be recorded or designed sounds, not
synthesised noise (user direction). A rhythmic surge tied to the swell phase at the camera (the pressure of
passing waves, 0.12 Hz) gives presence cheaply.

### 3.10 Budgets (estimates, 3060 at 1440p, TSR 67 percent; to be measured)

| Item | Cost | Basis |
|---|---|---|
| Clipmap vertex work | under 0.1 ms | 100k vertices x 3 cascades x 2 array slices x 2 textures |
| SLW base pass, near sea fills the view | 1.0-1.8 ms | estimate; scales with covered pixels |
| SLW Lumen reflections (HWRT hit lighting is configured) | 1.0-2.0 ms | estimate; biggest lever is downsample/denoise |
| SLW depth pre-pass, copies | 0.3 ms | |
| Flipbook memory | 43 + 37 (+32) MB | |
| Underwater post-process, medium + caustics | 0.5-0.8 ms | only while under |
| God rays, 12 samples | 0.7-1.0 ms | only while under |
| Runtime GPU FFT if ever added | 0.4-1.2 ms | anchor: Arc Blanc's height-only 3 x 256^2 on a 2070 Super is 0.376 ms [S8]; 8 channels is about 4x |

The above-water frame is where the sea is most of the screen; the underwater frame has no SLW pass and no
Lumen water reflection, which pays for the god rays.

### 3.11 Tests (few, on the PC, hidden windows as per the project's rule)

- T1 (look): the beach at 1.7 m eye height, noon and golden hour, tide high and low; the open sea from 50 m up.
- T2 (the Z flaw): the same shallow bed stood at the north pole, the equator, and 45 degrees; compare the bed colour.
- T3 (join): fly across the clipmap edge and the far-sphere mask; look for cracks, colour steps, z-fight sparkle.
- T4 (waterline): walk in, swim to the surface, tread water in 1 m swell; look for flicker at the line,
  sky-through-the-surface errors, fog on the underside.
- T5 (CPU/GPU parity): print the C++ `surface()` and the displaced height at 5 points read back from a debug
  material that writes the height to a render target; agree within 5 cm.

### 3.12 Order of work (impact per effort)

| # | Work | Effort | Impact | Depends on |
|---|---|---|---|---|
| 1 | Retune SLW coefficients and painted colour (3.6, albedo table) | 0.5 d | Large: the sea stops being black | none |
| 2 | Underwater post-process v2: depth-light, medium presets, `eta`-aware switch, per-pixel waterline, sound low-pass, lamp, fog off | 3 d | Large: answers "empty inside" | nothing but CPU wave height for `eta`; do v2 first with `eta = 0` |
| 3 | Swim, wade, buoyancy, splash, jets rule (3.9) | 3 d | Large: answers "just makes me go under" | 2 for the camera; CPU waves later |
| 4 | Surface from below (`M_OceanUnder`, reverse-culled twin) | 2 d | Large | 2 |
| 5 | FFT bake (C0, C1) + array importer + CPU table | 2 d | enabler | none |
| 6 | Clipmap mesh, component, vertex displacement, morph, far-sphere mask | 5 d | The biggest visible change: the sea moves | 5 |
| 7 | Pixel normals/foam/roughness from cascades, Lagrangian foam, shore damping, 16-bit depth | 3 d | Large | 6 |
| 8 | Caustic bake + post-process caustics + god rays + marine snow | 3 d | Medium-large (underwater beauty) | 2, 5 |
| 9 | Foam rings round stacks (CustomDepth) | 1 d | Medium | 7 |
| 10 | Eikonal shore waves, swash | 4 d | Medium-large (the coast reads as a coast) | 7 |
| 11 | Sun light-function caustics (true shadows, shafts) | 2 d + risk | Medium | 8 |
| 12 | Kelp, seabed detail (content) | 4 d+ | Medium | 2 |
| 13 | Player wake / ripples, shallow-water sim (render-target ping-pong) | 3 d | Medium | 7 |
| 14 | Runtime GPU FFT | 7 d | Only if the flipbook repeats visibly | section 5 |

Items 1-4 can ship without touching the mesh and fix most of what the player said. Items 5-7 are the
"sea moves" fix.

---

## 4. Risks in UE 5.8, honestly

| # | Risk | Basis | What I would do |
|---|---|---|---|
| R1 | SLW sun path to the bed uses world Z; wrong off the poles | verified `SingleLayerWaterShading.ush:205` | T2 first; mitigations in 3.6; custom translucent sea as fallback |
| R2 | SLW cannot shade a surface from below | verified `BasePassPixelShader.usf:1698` | the underside twin material (3.7); do not hunt for a switch |
| R3 | Setting `FSceneView::UnderwaterDepth` from game code changes pass order (fog, sky, clouds before SLW) and cloud composition. The hook exists (`OnBeginPostProcessSettings`) and is public, but I did not run it | verified plumbing, not behaviour | start without it; if cloud or fog ordering is wrong under water, add it behind a console variable and test with clouds on |
| R4 | The sea is not Nanite (SLW) so no virtual geometry for 200k triangles | verified | the count is small; accept |
| R5 | Large WPO on a flat mesh: bounds and culling | engine behaviour | bounds extension, never cull, ray tracing off for the sea |
| R6 | TSR ghosting on displaced crests | inference | previous-frame WPO via `PreviousFrameSwitch`; test; fall back to TSR history rejection settings only if needed |
| R7 | A light function on the main directional light may alter the atmosphere sun or cloud lighting | not verified | prototype on a copy of the light in a test map first; keep item 11 optional |
| R8 | Texture-array sampling in a vertex shader is allowed on D3D12 SM6 and Metal SM6, but the per-material sampler and texture count is easy to exceed in a Custom node | known pattern | share one sampler, budget 3 arrays + the depth cube + the legacy chop maps |
| R9 | The chart blend at cube seams produces a visible line of confused waves at 1-2 m | inference | variance-preserving weights; per-face rotation chosen so the seam waves share direction; accept |
| R10 | Wave table on the CPU and shader drift apart | likely | one shared file, T5 |
| R11 | A 64 s loop of the swell might show as repetition to a watching player | inference | 3 incommensurate cascades (192 s together); add the in-flight maps as detail; if it shows, bake 2 seeds and blend per region |
| R12 | Lumen SW fallback is not in use (HWRT is on); on a machine without DXR the sea reflection path differs | project config | `r.Water.SingleLayer.Reflection=2` fallback in the low preset |
| R13 | The user's bake and import path runs only in the editor on the PC; no Unreal on the Mac | project rule | all new C++ editor functions compile on both; run on the PC with `winsync.py` |
| R14 | Epic's Lumen page says SLW reflections are mirror-forced; our roughness-based glitter depends on roughness being honoured | doc vs shader mismatch | look at the sun glint on moving water in T1 |

---

## 5. If we want a runtime GPU FFT (global shader / RDG from the game module)

It is the better route only if (a) the baked flipbook repeats visibly, (b) we want live wind or sea-state
changes with true spectra, or (c) we want interaction forces (wakes added to the spectrum). It is a bigger
project than everything above that is not the clipmap.

What it takes (**Inference** from the engine layout; I have not built one here):
1. A shader file in the project (`unreal/Shaders/Ephemera/OceanFFT.usf`) mapped with
   `AddShaderSourceDirectoryMapping("/Ephemera", ...)` in the game module's `StartupModule`, which must run
   before shader compilation starts: the module needs `"LoadingPhase": "PostConfigInit"` in `Ephemera.uproject`
   (today `Default`). Add `Renderer`/`Projects` to `Ephemera.Build.cs` (`RenderCore`, `RHI` are already there).
2. `IMPLEMENT_GLOBAL_SHADER` compute shaders: spectrum update (h~ at time t), 1D butterfly passes (rows,
   columns), pack (h, D, slopes, Jacobian terms). 3 cascades x 256^2 x 8 channels.
3. A `FSceneViewExtensionBase` whose `PreRenderViewFamily_RenderThread(FRDGBuilder&, ...)` records the passes
   and writes into persistent render targets (`UTextureRenderTarget2DArray` created on the game thread,
   registered through `RegisterExternalTexture`, converted back with `ConvertToExternalTexture`). The material
   samples that array exactly like the baked one, so the vertex and pixel code does not change.
4. Cooked builds: global shaders in a game module cook and stage, but both Metal SM6 (Mac editor) and D3D12
   SM6 must compile cleanly; the Mac cannot be used to run it, only to compile.
5. One frame of latency for CPU wave queries unless the same FFT also runs on the CPU for the table (at
   64 x 64 it is trivial). Foam persistence becomes a ping-pong buffer.

Do (1)-(3) only after items 1-9 of the order in 3.12 are in and measured.

---

## 6. Things I looked for and did not find

- A primary talk for Assassin's Creed IV's ocean; Atlas's wake slides; any technical write-up of Subnautica's
  underwater rendering; Horizon Forbidden West's underwater rendering (the SIGGRAPH deck says it is not covered).
- A citable per-colour Kd table for Jerlov types. The presets in 3.7 are labelled as design values.
- The text of Sea of Thieves' cascade sizes and mesh; only the two-page abstract is public.
- Whether `UTexture2DArray::Source.Init` with `TSF_RGBA16F` and 128 slices imports without the 2D-array factory;
  the cloud builder already does the same for a volume (`TSF_BGRA8`), the cube does for 6 faces. Expect it to
  work; try it first in the bake step.

---

## 7. Sources

- [S1] Tessendorf, "Simulating Ocean Water", SIGGRAPH course notes (2001, 2004 reprint): https://people.computing.clemson.edu/~jtessen/reports/papers_files/coursenotes2004.pdf (read: eq. 31-35, Phillips, choppy waves, Jacobian)
- [S2] Ang, Catling, Cifariello Ciardi, Kozin, "The Technical Art of Sea of Thieves", SIGGRAPH 2018 Talks: https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf
- [S3] Malan, "Rendering Water in Horizon Forbidden West", SIGGRAPH 2022 Advances: https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Water-Malan.pdf
- [S4] Gonzalez-Ochoa, "Water Technology of Uncharted", GDC 2012: https://gdcvault.com/play/1015309/Water-Technology-of (session page) and slides https://www.slideshare.net/slideshow/water-technologyofunchartedgdc2012/12977478 (read: wave particles, clipmaps, flow)
- [S5] Dupuy and Bruneton, "Real-time Animation and Rendering of Ocean Whitecaps", SIGGRAPH Asia 2012 Technical Briefs: https://liris.cnrs.fr/Documents/Liris-5812.pdf
- [S6] Bruneton, Neyret, Holzschuch, "Real-time Realistic Ocean Lighting using Seamless Transitions from Geometry to BRDF", CGF 29(2), 2010: https://artis.inrialpes.fr/Publications/2010/BNH10/index.php
- [S7] Horvath, "Empirical directional wave spectra for computer graphics", DigiPro 2015, doi 10.1145/2791261.2791267: https://unpaywall.org/10.1145%2F2791261.2791267 (abstract read)
- [S8] "Arc Blanc: a real time ocean simulation framework", arXiv 2503.03326: https://arxiv.org/abs/2503.03326 (cascade sizes, GPU times read)
- [S9] Crest (Wave Harmonic) documentation: https://crest.readthedocs.io/en/stable/user/water-appearance.html , https://crest.readthedocs.io/en/stable/user/underwater.html , https://crest.readthedocs.io/en/stable/user/waves.html , https://crest.readthedocs.io/en/stable/user/shallows-and-shorelines.html
- [S10] Epic, Single Layer Water Shading Model: https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine ; [S10b] Epic, Lumen GI and Reflections: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine
- [S11] Engine source read, `.unreal/UE_5.8/Engine`: `Shaders/Private/SingleLayerWaterShading.ush`, `SingleLayerWaterComposite.usf`, `BasePassPixelShader.usf`, `MaterialTemplate.ush`; `Source/Runtime/Renderer/Private/SingleLayerWaterRendering.cpp`, `DeferredShadingRenderer.cpp`, `VolumetricFog.cpp`, `Lumen/*`; `Source/Runtime/Engine/Private/Rendering/NaniteResources.cpp`, `Materials/HLSLMaterialTranslator.cpp`, `Materials/MaterialShared.cpp`, `Components/CharacterMovementComponent.cpp`; `Engine/Public/SceneView.h`; `Classes/Engine/World.h`; `Plugins/Experimental/Water/Source/Runtime/Private/WaterSubsystem.cpp`
- [S12] Guardado and Sanchez-Crespo, "Rendering Water Caustics", GPU Gems ch. 2: https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics
- [S13] Pope and Fry 1997, pure-water absorption (OMLC): https://omlc.org/spectra/water/abs/pope97.html , data https://omlc.org/spectra/water/data/pope97.txt
- [S14] Poole-Atkins Secchi relation: https://www.frontiersin.org/journals/marine-science/articles/10.3389/fmars.2024.1265382/full ; [S14b] NOAA Kd490 note: https://data.commerce.gov/light-attenuation-kd490
- [S15] Snell's window, University of Arizona optics outreach: https://wp.optics.arizona.edu/oscoutreach/snells-window
- [S16] "Working with underwater caustics in real time", 80.lv: https://80.lv/articles/working-with-underwater-caustics-in-real-time
- [S17] Unreal forum, "Ocean Material / Underwater God Rays": https://forums.unrealengine.com/t/ocean-material-underwater-god-rays/416217
- [S18] Mihelich and Tcheblokov, GDC 2019, Atlas wakes (session page only): https://gdcvault.com/play/1025819/Advanced-Graphics-Techniques-Tutorial-Wakes
- [S19] Kirkpatrick, "Making Waves for Skull and Bones", GDC 2023 (session page only): https://gdcvault.com/play/1029230/Making-Waves-for-Skull-and ; [S19b] DSOGaming interview on AC4: https://www.dsogaming.com/interviews/ubisoft-talks-ac4-tech-anvilnext-engine-features-global-illumination-dx11-2-amds-mantle/
- [S20] This repo: `docs/research/ow_mechanics.md` (1.6, 8.x), `docs/research/ow_gap_analysis.md` (rows 1.10, 1.11, 15), `docs/research/look_audit_ours.md` (sea rows), `docs/research/ground_level_landscape.md` (Water plugin on a sphere)
- Known but not opened: Finch, "Effective Water Simulation from Physical Models", GPU Gems ch. 1 (Gerstner in the vertex shader); Yuksel et al., "Wave Particles", SIGGRAPH 2007 (read about through [S4]); Olano and Baker, "LEAN Mapping" 2010 (already cited in `water.py`).
