# Look audit: why Dawnhollow reads "glossy" and "flat"

Audit of the code as it stands on 2026-10-13. Nothing was run in Unreal; every number is traced
from source, from the scanned textures on disk (measured, 512 px downsample), or from the engine
source under `.unreal/UE_5.8`. Estimates are labelled as estimates. Paths are relative to
`unreal/`. `BS` = `Tools/build_slice.py`, `SR` = `Source/Ephemera/Private/Frame/EphemeraSkyRig.cpp`.

## 0. The short version

**Glossy** has one root cause and four amplifiers. The root: the ground's diffuse albedo is
0.016 to 0.043 (charcoal), while its specular reflectance is F0 = 0.034 (`Specular` 0.42). Seen
at the low angle a walking player sees ground, the specular lobe returns more light than the
diffuse one, so every surface shows the sky as a pale sheen. The amplifiers: all bare rock is
globally "wet" (roughness 0.59 to 0.65), moss uses a grass scan with roughness 0.69, the sky light
is multiplied by 2.6 so surfaces reflect a sky 2.6 times brighter than the one drawn, and there is
no ambient occlusion or specular occlusion beyond 25 to 120 m.

**Flat** is six things together: no direct light under cloud except a 10% sun leak; nothing small
casts a shadow (grass never, relief never, because the mesh has a point every 7.7 m and
tessellation is off in play); Lumen traces stop at 200 m so hills never shade valleys; haze is
unshadowed grey from the sky atmosphere only (no fog component, no volumetric fog); the exposure
meter reads only the brightest 45% of the frame (the sky), pushing the dark ground into the toe of
the film curve; and the grade is a global 0.86 desaturation with nothing else.

## 1. Glossy

### 1.1 What each surface actually outputs

Planet ground is `M_Planet_Dawnhollow`, built by `make_planet_material` with defaults
(`BS:2836-2841`: only `rock_start=0.34, snow_line_m=400`; so `wetness=1.0`, `far_relief=1.6`,
colours from `BS:967-971`). Outputs: base colour, normal, and `ORM` split into AO (R), roughness
(G), **specular (B)** (`BS:1014-1020`). Metallic is unconnected (0). Shading model Default Lit.

The scans, measured (mean [5th-95th percentile]):

| Scan (layer) | AO | Roughness | Mean normal xy | Scan albedo (lin RGB) |
|---|---|---|---|---|
| leafy_grass (moss, near) | 0.74 [0.59-0.88] | **0.69 [0.67-0.70]** | 0.20 | 0.317 0.233 0.104 |
| rocky_terrain_02 (field, near) | 0.96 [0.91-0.98] | 0.96 | 0.12 | 0.082 0.078 0.013 |
| damp_sand (sand, near) | 0.93 | 0.90 | 0.16 | 0.204 0.113 0.039 |
| dark_rock_02 (basalt, near) | 0.91 [0.64-0.99] | 0.82 [0.79-0.87] | 0.21 | 0.043 0.034 0.026 |
| snow_02 (snow, near) | 0.55 | 0.80 | 0.25 | 0.38 |
| aerial_grass_rock (moss, far) | 0.54 | 0.81 | 0.13 | not loaded: see below |

The scan's own colour is discarded (`Detail()` divides it by its blurred self, `BS:727-737`); the
layer colour parameter sets albedo. The far scans' ARM maps are never loaded (`far_layers` are
`{'d','n','h'}` only, `BS:983-988`).

Code that sets the numbers:

```
BS:801  [bMid]  a_ += float3(1.0, FarRough, 0.0) * bMid;      // AO forced to 1 past 25-120 m
BS:802  [bFar]  a_ += float3(1.0, FarRough, 0.0) * bFar;
BS:806-811      FarRough: moss 0.95, field 0.93, sand 0.88, rock 0.90, snow 0.62
BS:817  arm.g = lerp(arm.g, max(arm.g * 0.72, 0.46), wRock * Wetness);   // ALL rock, all distances
BS:819  arm.g = lerp(arm.g, max(arm.g * 0.35, 0.42), wet);               // sand below 3.5 m, near/mid
BS:823  ORM = float3(arm.r, arm.g, lerp(0.42, 0.20, bFar));              // specular, every layer
```

`bNear` fades out 25 to 120 m, `bFar` fades in 350 to 2200 m (`BS:789-791`).

| Surface | Albedo (linear, luminance) | Roughness near / mid-far | Specular (F0) | AO near / mid-far | Verdict |
|---|---|---|---|---|---|
| Moss | 0.032 0.043 0.019 (0.039), x0.75-1.25 drift | **0.69** / 0.95 | 0.42 (0.034) to 0.20 | 0.74 / 1.0 | Too dark by 2x, far too smooth and too specular near |
| Lichened lava (field) | 0.040 0.043 0.030 (0.040) | 0.96 / 0.93 | 0.42 to 0.20 | 0.96 / 1.0 | Too dark; specular too high for a porous crust |
| Bare basalt | 0.019 0.019 0.022, x0.74-1.12 beds, x0.72 damp, x0.45 young (0.006-0.021) | **0.59** / **0.65** (after `BS:817`) | 0.42 to 0.20 | 0.91 / 1.0 (x0.62 in joints) | Darker than charcoal and uniformly "just rained" |
| Black sand, dry | 0.016 0.016 0.018 | 0.90 / 0.88 | 0.42 to 0.20 | 0.93 / 1.0 | 2-3x too dark |
| Black sand, swash (alt < 3.5 m) | same, **not darkened** | **0.42** | 0.42 | 0.93 | Roughness fine for a film; albedo should halve, F0 should be water's |
| Stream bed | col x0.55 + 0.01 0.03 0.036 | 0.45 (`BS:859`) | 0.42 | scan | Acceptable (under water) |
| Snow, near | 0.83 x 0.34-0.66 ash (`BS:827`) = 0.28-0.55 | 0.80 | 0.42 | 0.55 | Albedo fine for ashy firn; F0 is ice's 0.018, not 0.034 |
| Snow, far | 0.83 | **0.62** | 0.20 | 1.0 | Wrong direction: distant snow is matt (0.85+), this makes caps shine |
| Crevasse ice | IceC x 0.30 0.52 0.70 | 0.28 near | 0.42 | x0.5 | Fine |
| Scanned stones `M_Stone` (`BS:2335`) | rock 0.020 0.020 0.023; moss as above | `clamp(arm.g*0.75, 0.42, 0.8)` = **0.62**; moss 0.95 | 0.42; moss 0.25 | scan | Same faults as the terrain rock |
| Grass / heath / sedge `M_Grass` (`BS:2045-2098`) | root 0.016 0.030 0.008, tip 0.060 0.085 0.024, dry tip 0.150 0.118 0.050; sedge dry tip 0.190 0.150 0.065; x Shade 0.30-1 | 0.68 constant | 0.18 constant | Shade 0.30 root to 1.0 at 70% height | Two-Sided Foliage, **no normal input** (flat blade facets), subsurface = colour x (0.38 0.42 0.12). Values plausible; 3-6x brighter than the ground under it |
| Sea, near `M_Ocean` | Single Layer Water; absorb 0.0046 0.0013 0.0012 /cm | `(0.0004 + lost slope variance)^0.25` = 0.14 up close (`BS:1269`) | 0.255 (F0 0.02) (`BS:1412`) | - | Physically right |
| Painted far sea (opacity to 1 over 1.5-7 km) | 0.003 0.011 0.016 deep, 0.010 0.046 0.048 shelf | about 0.38 (Cox-Munk 0.021) | 0.255 | - | Right; but see 1.3, it reflects a 2.6x sky |
| Foam | 0.70 0.72 0.72 | 0.6 | 0.255 | - | Fine |
| Stream `M_Stream` | SLW, absorb 0.0040 0.0014 0.0012; white water 0.74 0.77 0.78 | same formula; 0.7 in white water | 0.255 | - | Fine |
| Clouds `M_Clouds` | albedo 0.96 (`BS:1754`); phase g 0.6 / -0.3 blend 0.3; 2 multi-scatter octaves, contribution 0.6, occlusion 0.45 | - | - | - | Fine |
| Hush regolith (`BS:1076`) | 0.105 / 0.047 | max(scan, 0.92) | 0.14 | scan | Correct, and it is the one surface the owner did not call glossy |

`make_terrain_material` / `TERRAIN_CODE` (`BS:180-350`), `foliage_parent` and `dress_dawnhollow`
(`BS:1817`, `BS:2506`) are never called from `main()`: dead code, ignore when tuning.

### 1.2 Why that is shine: diffuse against specular

A first-person camera at 1.7 m sees ground 5 m away at 71 degrees from the normal and ground 20 m
away at 85 degrees. Under a sky (uniform light from above), the reflected light is roughly
`albedo` for diffuse and the environment BRDF for specular. Using the engine's own analytic
environment BRDF (Karis' approximation, `EnvBRDFApprox`) at N.V = 0.1:

| Surface as shipped | Albedo | Rough | F0 | Specular return | Specular / diffuse |
|---|---|---|---|---|---|
| Moss, near | 0.039 | 0.69 | 0.034 | 0.050 | **1.3** |
| Moss, mid | 0.039 | 0.95 | 0.034 | 0.014 | 0.36 |
| Basalt, near (wet) | 0.019 | 0.59 | 0.034 | 0.092 | **4.9** |
| Basalt, mid (wet) | 0.019 | 0.65 | 0.034 | 0.065 | **3.4** |
| Dry sand, near | 0.016 | 0.90 | 0.034 | 0.016 | 1.0 |
| Swash sand | 0.016 | 0.42 | 0.034 | 0.22 | 14 |
| **Proposed moss** | 0.076 | 0.92 | 0.016 | 0.007 | 0.09 |
| **Proposed dry basalt** | 0.050 | 0.82 | 0.028 | 0.020 | 0.40 |

The approximation is coarse, but the ordering is not in doubt: on rock, and on near moss, more
than half of what reaches the eye is a reflection of the sky. That is what "glossy" is. It also
explains the milky, clay-like lumps in `.scratch/ue/world/show/sheet.jpg` (moss_heath,
green_hills, tables_plain): the lumps are shading-only relief (1.4) catching a sky highlight.

Real materials do not behave this way because (a) they are lighter, (b) porous surfaces lose
specular to their own cavities, and (c) a water film has F0 0.02, lower than the dry mineral's
0.04, so "wet" should lower `Specular` to 0.25, not hold it at 0.42.

### 1.3 Amplifiers

1. **Sky light x2.6** (`SR:265`: `SkyLight->SetIntensity(Tuned(TEXT("skylight"), 2.6f))`, in every
   weather). The real-time capture is multiplied for both diffuse and reflections, but the sky
   on screen is not. Every reflection of sky is 2.6x brighter than the sky beside it. It also
   fills all shade (section 2).
2. **Global wetness** (`BS:817`, comment "It has always just rained here"). Uniform roughness is the
   tell of a cheap wet look. Real wet ground is patchy: films in hollows and seeps, matt between.
   The shader already computes `seep` (`BS:837`) and `damp` (`BS:853`) and uses them only for colour.
3. **No occlusion where the sheen is.** AO is 1.0 beyond the near band (`BS:801-802`). Lumen does
   apply material AO to diffuse (`LumenScreenProbeGather.usf:1352`, `ApplyMaterialAO`); I found no
   use of it in the reflection shaders I searched, so treat specular occlusion from material AO as
   absent (unverified). Nothing multiplies `Specular` by AO. Roughness above 0.4 takes its
   reflection from the diffuse gather (`LumenMaxRoughnessToTraceReflections` 0.4, engine default,
   not overridden), so rough-ground "reflections" are sky irradiance with only screen-space
   short-range occlusion.
4. **Reflection aims at the horizon.** At grazing view the mirror direction is just above the
   horizon, where a sky with 0.77 /km of Mie mist (`BS:2692` `mist_per_km 0.75`) is brightest.
5. **Derivative bump mapping with no filtering.** The relief normal is `ddx/ddy(shape)`
   (`BS:768-771`), computed per 2x2 pixel quad from a function full of steps (ledges, joints).
   It never fades with distance. On a far cliff it becomes per-pixel random normals under a
   roughness-0.65 lobe: the rows of white dots on the mountain in `pc/game/drive_day.jpg` and
   `menu.jpg`. `bNormalCurvatureToRoughness` is off (engine default).
6. **Grass blades are flat facets.** No normal is connected (`BS:2085-2098`), so each two-sided
   blade shades as a tiny plane; at roughness 0.68 a field of them glints pale against ground five
   times darker. `r.Lumen.ScreenProbeGather.ShortRangeAO.FoliageOcclusionStrength` is 0.7 at the
   default quality 3, so blades also get less contact occlusion than opaque surfaces.

### 1.4 Micro relief is shading only

`r.Nanite.Tessellation=0` in play (`Config/DefaultEngine.ini:23`; measured 28 ms to 52 ms with it
on). Terrain is `face_resolution` 1024 on a 7,854 m cube face (`BS:2658`): a vertex every **7.7 m**
(DIRECTION.md says "4 m grid"; it is not). Everything smaller, including 1.7 m ledges, 2.4 m lava
hummocks and all "micro relief", is a tilted normal scaled by `ReliefShade` 0.55
(`EphemeraBody.cpp:148`). It tilts highlights but casts no shadow, has no silhouette and no
occlusion. A bump that catches light and never shades itself is the classic plastic look.

### 1.5 Reference values and corrections

Reference ranges are the commonly published ones (the albedo chart in Unreal's "Physically Based
Materials" page, the standard surface-albedo tables, Lagarde's wet-surface notes). They are quoted
from memory and were not re-fetched; treat as +/-30%.

| Material | Published albedo | Roughness | Specular (UE) | Now | Set to |
|---|---|---|---|---|---|
| Moss, damp green | 0.06-0.10 | 0.9-1.0 | 0.15-0.25 (cavities) | 0.039 / 0.69 / 0.42 | `MossColour` 0.060 0.085 0.030; roughness `max(scan, 0.90)`; specular 0.20 |
| Moss, dry (Racomitrium, grey-green) | 0.12-0.20 | 1.0 | 0.15 | drift x1.25 | dry patches lerp toward 0.14 0.14 0.09 |
| Lichened old lava | 0.06-0.10 | 0.9 | 0.3 | 0.040 | `FieldColour` 0.070 0.075 0.058; specular 0.30 |
| Basalt, dry | 0.04-0.08 | 0.75-0.9 | 0.35-0.4 | 0.019 / 0.59 | `RockColour` 0.048 0.048 0.054; roughness = scan (0.82); specular 0.35 |
| Basalt, wet film | half of dry | 0.3-0.5 in film only | 0.25 | everywhere | only where `seep`, `damp`, or shore: albedo x0.55, roughness 0.45, specular 0.25 |
| Volcanic black sand, dry | 0.03-0.06 | 0.85-0.95 | 0.35 | 0.016 | `SandColour` 0.035 0.035 0.038; specular 0.33 |
| Black sand, wet | 0.015-0.03 | 0.3-0.45 | 0.25 | 0.016 / 0.42 / 0.42 | albedo x0.55 by `wet`; roughness as now; specular 0.25 |
| Dry grass / straw | 0.20-0.30 | 0.6-0.8 | 0.25-0.35 | 0.15-0.19 | keep; fix normals and root occlusion instead |
| Green grass (visible band) | 0.08-0.12 | 0.6-0.7 | 0.3 | 0.06-0.085 | keep |
| Snow: fresh / old / ash-dirty | 0.8-0.9 / 0.5-0.65 / 0.25-0.4 | 0.75-0.9 | 0.22 (ice F0 0.018) | far 0.62 rough | far roughness 0.85; specular 0.22 |
| Sea water | F0 0.020 | from wave slope | 0.255 | as is | no change |

## 2. Flat

### 2.1 The lighting rig as built

| Item | Value | Where |
|---|---|---|
| Sun | 95,000 lux, colour 1.0 0.93 0.82, disc 4.3 deg, constant at all times; no time-of-day curve (atmosphere transmittance is the engine's) | `EphemeraSkyRig.h:30-33`, `SR:262` |
| Sun per-pixel transmittance | off | `SR:255` |
| Cloud shadows on the sun | on in `fronts`, `overcast`, `broken`; strength **0.9**, so the sun never falls below about 10% (9,500 lux) under any cloud. (My reading of `CloudShadowStrength`; confirm.) | `SR:80-82, 230-233, 254` |
| Sky light | real-time capture, lower hemisphere not black, casts shadows, **intensity 2.6** in every weather | `SR:136-140, 265` |
| Night | Shine up to 2.0 lux, NightFill 0.45 lux, no shadow from fill | `SR:53-55` |
| Height fog / volumetric fog | **none.** No `ExponentialHeightFog` exists in `Source/`. Volumetric fog is therefore off; no light scatters in air; the 8,000 lm lamp has no beam | grep |
| Haze | Sky-atmosphere Mie only: 0.0236 + 0.75 = 0.77 /km at the ground (190x the engine's Earth default 0.004), scale height 150 m, g = 0.7. Unshadowed by terrain or cloud | `BS:2692`, `SR:593-604` |
| Aerial perspective LUT | 40 km over 32 slices; slices are squared-distributed (`SkyAtmosphereCommon.ush:71`), so slice 1 ends at 39 m, slice 4 at 625 m: adequate. (The ini comment assumes linear slices.) | `DefaultEngine.ini:69-71` |
| Lumen | GI + reflections; Windows: hardware ray tracing, `LightingMode=2` (hit lighting for reflections only); quality 3 by default (`EphemeraUserSettings.h:38`) | `DefaultEngine.ini:11-12`, `WindowsEngine.ini:3-6` |
| Lumen distances | **not set: engine defaults `LumenSceneViewDistance` 200 m, `LumenMaxTraceDistance` 200 m**, far field off (`r.LumenScene.FarField` 0) | `Scene.cpp:592, 597` |
| Short-range AO | on (engine default), bent normal at quality 3 | `BaseScalability.ini:444-449` |
| Shadows | Virtual shadow maps; clipmap first level 8, Z range 5000; SMRT 8 rays x 4 samples at quality 3; ray-traced shadows off | `DefaultEngine.ini:35-43` |
| Contact shadows | length 0.06 on the sun | `SR:76` |
| Casts shadows | terrain chunks (7.7 m mesh), stones and boulders (default `bCastShadow = true`) | `EphemeraBody.cpp:143` |
| Casts none | **all grass, heath, sedge** (`cast_shadow=False`, `BS:2482-2490`), sea, streams, all relief under 8 m | `EphemeraGroundCover.cpp:269` |
| Distance fields | terrain capped at 64 voxels per 650 m chunk = 10 m voxels; ground cover excluded (`bAffectDistanceFieldLighting = false`); grass meshes have none | `DefaultEngine.ini:100`, `EphemeraGroundCover.cpp:256` |
| Exposure | histogram, EV -2 to 15.5, **meters the 55th-95th percentile**, bias +0.5, speed up 2.5 / down 1.0 | `SR:164-199` |
| Tone curve | default filmic curve, slope 0.92, toe 0.57, shoulder 0.30 (defaults 0.88 / 0.55 / 0.26) | `SR:200-205` |
| Grade | contrast 1.06, **saturation 0.86 global**; nothing per shadows / midtones / highlights, no white balance, no LUT | `SR:206-213` |
| Local exposure | 1.0 / 1.0 by day (no effect); 0.6 / 0.7 at night | `SR:208-211, 413-414` |
| Bloom | 0.28, standard; lens flare 0; grain 0.12; vignette 0.3; motion blur 0 | `SR:175-194` |
| Sharpening | none (`Sharpen` 0, no `r.Tonemapper.Sharpen`) with TSR | - |

### 2.2 Sun to sky ratios (estimates)

Dawnhollow's air has about a quarter of Earth's Rayleigh optical depth (blue 0.066 vertical
against 0.27: `rayleigh_scale 0.0091 x 42.9`, scale height 187 m) plus 0.12 of Mie mist. Sky
illuminance cannot be read from code; the figures below are order-of-magnitude and should be
measured (two captures with `-EphemeraSky=sun=0` and `-EphemeraSky=skylight=0` at a pinned
exposure).

| Case | Direct sun on ground | Sky, natural | Sky after x2.6 | Sun : sky | Real-world reference |
|---|---|---|---|---|---|
| Clear, noon | about 85 klux | 5-8 klux | 13-21 klux | 4-6 : 1 | 5-6 : 1 (100 : 18 klux) |
| `fronts`, in a cloud's shadow (the state of `drive_day.jpg`: weather fronts, time 217 s, sun about 59 deg up) | 9.5 klux floor | 8-15 klux | 20-40 klux | **1 : 2 to 1 : 4** | broken cloud shade 1 : 3, but with sunlit ground in view |
| `overcast` | 9.5 klux floor | 8-15 klux | 20-40 klux | **1 : 2 to 1 : 4** | 0 : 1, 5-20 klux total |

Clear noon is plausible after the lift. Under cloud the image is 70-80% shadowless sky light, and
the remaining sun is too weak for its shadows to register, and has nothing small to cast them.

### 2.3 Why there is no large-scale light and dark

1. **Sky occlusion stops at 200 m.** With default Lumen distances a valley floor is never shaded
   by its walls, a cliff base is as bright as the plateau, and a hill does not darken the hollow
   behind it. Under cloud, sky occlusion is the *only* source of form, and it is cut off at the
   scale of a football pitch. This is the main structural cause.
2. **Bounce light is negligible** at albedo 0.02-0.04 (3% per bounce), so there is no warm fill
   from sunlit ground into shade, no colour exchange. Raising albedo (section 1) fixes two things.
3. **No cast shadows below 8 m and none from vegetation.** Grass neither shadows the ground nor
   itself; it floats. The only root darkening is a painted gradient (`Shade` 0.30, `BS:2028`), and
   the ground under grass is not darkened at all.
4. **Sky fill x2.6** lifts every shadow that does exist by 1.4 stops.
5. **Cloud shadows do not reach the distance.** Clouds thin to 6% density between 2.5 and 9 km
   from the camera (`SR:228`, `FarThin=0.06`), so far hills seen from a ridge or from the air carry
   no cloud-shadow dapple: the cheapest large-scale light pattern in a landscape is thrown away.
6. **Haze separates nothing.** Mist at 0.77 /km puts hills at 1-2 km under 50-80% in-scatter, and
   that in-scatter is one unshadowed grey (aerial perspective is not shadowed by terrain or
   cloud). In `drive_day.jpg` the mountain is a flat grey card against the meadow: a hard
   line, not layers. No near haze, no shafts, no glow toward the sun.
7. **Exposure meters the sky.** Sunlit ground at albedo 0.035 is about 950 nits (EV100 12.9);
   cloud and hazy sky are 5,000-20,000 nits (EV 15.3-17.3). The meter averages only the top 45% of
   the histogram (`SR:185-187`), which on the ground is all sky, so ground lands 2-4.5 stops under
   mid grey, inside the toe (0.57), where a stop of scene contrast becomes a few display levels.
   `tables_plain_low` and `bay` in `sheet.jpg` show it.
8. **Grade.** Saturation 0.86 on colours that are already near-neutral (moss 0.032 0.043 0.019)
   leaves a grey image; `drive_day.jpg` is close to monochrome apart from the straw grass. A film
   look separates shadows and highlights in hue; nothing here does.

Against a film-like frame the image lacks: dense shadows with a cool tint, occlusion at contact
(grass roots, stone bases, cliff foot), three or more depth planes separated by lit haze, a
key-to-fill ratio above 3 : 1 somewhere in frame, and a shaped tone curve with protected highlights.

## 3. Other things that cheapen the image

- **Monochrome ground per layer.** One colour per layer times a detail term clamped to 0.45-2.0
  (`BS:803`); hue varies only in moss (`BS:805`). Add hue variation to field and rock (ochre,
  rust, grey lichen) driven by the existing `driftA/D/E`.
- **Grass against ground contrast.** Sedge tips 0.19 over ground 0.04: reads as wire. Fixed by
  raising ground albedo and adding thatch tint under cover.
- **Uniform habit heights.** Three clump meshes per habit, scale 0.9-1.7, no clumping by height;
  turf at 13 /m2 has a hard start at `cover_min` and ends at the 110 m ring.
- **Hard horizon and hard meadow edge.** Horizon at eye height is 130 m away on a 5 km world; the
  grass ring ends at 110 m and far tier at 650 m with no colour hand-off in the ground shader
  (the planet material has no "far vegetation tint"; only the unused `TERRAIN_CODE` had one).
- **Water's edge.** Sand does not darken where wet (`BS:818-819` change roughness only); foam band
  is depth < 0.7 m. No damp line above the swash.
- **Far normals exaggerated, not flattened.** `FarRelief 1.6` (`BS:802`) scales the aerial scan's
  normal up at 420 m tiling; no fade to the mesh normal at any distance. With specular 0.20 it
  sparkles from the air.
- **Snow speckle** on the mountain: snow weight uses per-pixel noise and far roughness 0.62.
- **Sky.** Zenith reads near-black grey on the ground frames: thin air, absorption x2.2, then
  desaturation. `SkyAtmosphere.GroundAlbedo` is left at the engine's 0.4 while the real ground
  is 0.03-0.08 (minor, slightly over-bright multiple scattering).
- **Silhouette.** 7.7 m mesh spacing gives smooth, low-polygon ridge lines.

## 4. Patch plan

GPU costs are estimates for an RTX 3060 at 1080p; "0" means no measurable change.

### 4.1 Safe, do now (material and rig numbers)

| # | Change | File / lines | Old | New | Cost | Confirm by |
|---|---|---|---|---|---|---|
| 1 | Raise ground albedo | `BS:967-970`, `BS:2372` (bump `planet-v14`, `stone-v5`) | Moss 0.032 0.043 0.019; Field 0.040 0.043 0.030; Sand 0.016 0.016 0.018; Rock 0.019 0.019 0.022; stone rock 0.020 0.020 0.023 | Moss 0.060 0.085 0.030; Field 0.070 0.075 0.058; Sand 0.035 0.035 0.038; Rock 0.048 0.048 0.054; stone rock same | 0 | Ground and grass within one stop of each other; sheen gone on `moss_heath` framing |
| 2 | Per-layer specular, occluded | `BS:823` | `lerp(0.42, 0.20, bFar)` | `(wMoss*0.20 + wField*0.30 + wSand*0.33 + wRock*0.35 + wSnow*0.22) * lerp(1.0, 0.6, bFar) * lerp(0.5, 1.0, arm.r)`, divided by the weight sum | 0 | Grazing view of moss: no pale film |
| 3 | Moss roughness floor | `BS:806` (after the LAYER call, or in the macro) | scan 0.69 | `max(arm.g, 0.90)` weighted by `wMoss` | 0 | same |
| 4 | Wetness only where water is | `BS:817` | `wRock * Wetness` | `wRock * Wetness * saturate(seep * steepLand + damp)`; in the same place `col *= lerp(1.0, 0.55, that)` and specular toward 0.25 | 0 | Cliffs: dark glossy streaks on matt rock |
| 5 | Wet sand darkens, water F0 | `BS:819` | roughness only | add `col *= lerp(1.0, 0.55, wet)`; specular `lerp(spec, 0.25, wet)` | 0 | Shore: dark band at the swash |
| 6 | Snow far roughness | `BS:811` | 0.62 | 0.85 | 0 | No white speckle on the far mountain |
| 7 | Stones | `BS:2335` | `clamp(arm.g*0.75, 0.42, 0.8)`, spec 0.42 / 0.25 | `clamp(arm.g, 0.6, 0.95)`, spec `lerp(0.35, 0.20, mossy) * lerp(0.5, 1, arm.r)` | 0 | Stones stop glinting |
| 8 | Sky light back to physical | `SR:265` and presets `SR:230-233` | 2.6 always | default 1.4; add `skylight=1.0` to `fronts`, `overcast`, `broken` | 0 | Shade 1.4 stops deeper; sea no brighter than the sky above it |
| 9 | AO beyond 25 m | `BS:801-802` | `float3(1.0, FarRough, 0.0)` | AO from the heights already sampled: pass `hf`-style term, e.g. `lerp(0.55, 1.0, smoothstep(0.25, 0.65, TAPH(<layer>FH, MidTiling)))`; and multiply `ORM.r` by `lerp(0.65, 1.0, smoothstep(0.15, 0.6, m1*0.5 + m2*0.5))` where `open` | about 0.1 ms | Hollows darker than crests at 50-300 m |
| 10 | Ground under plants | planet shader, after `BS:824` | none | `float grown = smoothstep(0.05, 0.3, cover) * (1.0 - wRock - wSand - wSnow) * bNear; col *= lerp(1.0, 0.6, grown); ORM.r *= lerp(1.0, 0.6, grown);` | 0 | Dark soil between blades, grass looks rooted |
| 11 | Relief normal fades and filters with distance | `BS:775` | `grad *= ReliefShade` | `grad *= ReliefShade * (1.0 - smoothstep(150.0, 700.0, Dist*0.01))`; and `FarRelief` 1.6 to 1.0 (`BS:884`) | 0 | Dotted rows on far cliffs gone |
| 12 | Overcast is all sky | `SR:81` | `CloudShadowStrength = 0.9` | 1.0 | 0 | No faint hard shadows under solid cloud |
| 13 | Meter the whole frame on the ground | `SR:185-187`, set per frame beside `SR:417-418` | 55 / 95 | `Lerp(15, 55, Fixed)` / `Lerp(92, 95, Fixed)`; bias `Lerp(0.0, 0.5, Fixed)` (ground albedo is now higher) | 0 | Ground histogram centred near mid grey; sky allowed to go bright |
| 14 | Grade and curve (4.3) | `SR:200-213` | see 2.1 | see 4.3 | local exposure about 0.2 ms | Side by side with `drive_day.jpg` |

### 4.2 Needs a look test

| # | Change | Where | Old to new | Cost | Risk / check |
|---|---|---|---|---|---|
| 15 | Long Lumen traces | `SR` constructor, new overrides | `LumenMaxTraceDistance` 20000 to 150000; `LumenSceneViewDistance` 20000 to 60000 (set `bOverride_` flags) | 0.3-1.0 ms, more surface-cache memory | Biggest "flat" fix under cloud. Check valley floors darken, no light leaking, no card pop-in. Behaviour of far hits outside the Lumen scene in 5.8 with hardware tracing is unverified |
| 16 | Tall grass casts shadows | `BS:2483-2490` | `cast_shadow=False` to `True` for meadow, tall, sedge, heath; turf stays off | 1-2.5 ms in VSM (estimate; measure) | If too dear: raise `ContactShadowLength` 0.06 to 0.10 (`SR:76`) and keep #10 |
| 17 | Grass normals and specular | `make_grass_material` `BS:2085-2098` (bump `grass-v3`) | no normal; rough 0.68; spec 0.18 | Normal = `normalize(lerp(blade normal, planet up, 0.65))` (world space, `tangent_space_normal=False`); roughness `lerp(0.85, 0.65, t)`; specular `0.25 * Shade` | 0 | Meadow shades as a soft surface, no per-blade glint. Check two-sided flip still correct |
| 18 | Volumetric fog that follows the planet | new component in `AEphemeraSkyRig` | none | `UExponentialHeightFogComponent` with `bEnableVolumetricFog`, base density near 0, `VolumetricFogDistance` 150000, scattering g 0.6, albedo 0.9; density supplied by a Volume-domain material on a camera-centred sphere: `Mist * exp(-(length(WP - PlanetCentre) - R) / MistHeight)`. Cut `mist_per_km` 0.75 to 0.30 (`BS:2692`) so haze is not doubled | 1.0-2.0 ms at `GridPixelSize 8, GridSizeZ 128`; 0.5-1.0 ms at 16 / 64 | Height fog is aligned to world Z and the player can stand anywhere on the globe, so the stock height falloff is unusable; mesh-voxelised volume materials (or a `LocalFogVolume`, `r.LocalFogVolume.RenderIntoVolumetricFog=1` is the default) are the planet-safe route. Unverified in this project. Gains: shadowed haze, shafts through `fronts`, a lamp beam |
| 19 | Keep cloud shadows at distance | `SR:228` | `FarThin=0.06` | 0.35 on the ground (lerp by `InSpace`) | 0 | Dappled far hills; check orbit view separately |
| 20 | Specular antialiasing | `make_planet_material` | off | `material.set_editor_property("normal_curvature_to_roughness", True)` | about 0.05 ms | Unsure how it behaves with Nanite and custom-node normals in 5.8 |
| 21 | Finer terrain mesh | `BS:2658` | `grid(1024, 12)` | `grid(2048, 12)` (3.8 m) | Nanite: small; 4x build time and disk | Real silhouettes and 4 m shadows without tessellation |
| 22 | Reflections lighting mode | `WindowsEngine.ini:6` | 2 | 0 | saves 0.5-1 ms | Only water is below roughness 0.4 after these fixes; check sea reflections of cliffs |

### 4.3 Proposed post-process and exposure set

| Setting | Now | Proposed (ground) |
|---|---|---|
| Method / range | Histogram, EV -2 to 15.5 | same |
| Low / high percent | 55 / 95 | 15 / 92 (55 / 95 in space, lerp by `Fixed`) |
| Bias | +0.5 | 0.0 clear, +0.3 under cloud |
| Speed up / down | 2.5 / 1.0 | 2.0 / 1.0 |
| Local exposure highlight / shadow contrast | 1.0 / 1.0 day | 0.75 / 0.90 day; detail strength 1.2; night as now |
| Film slope / toe / shoulder | 0.92 / 0.57 / 0.30 | 0.90 / 0.55 / 0.26, black clip 0, white clip 0.04 |
| Contrast | 1.06 global | 1.0 global; `ColorContrastMidtones` 1.10 |
| Saturation | 0.86 global | 1.0 global; `ColorSaturationShadows` 0.88; `ColorSaturationHighlights` 0.95 |
| Split tone | none | `ColorGainShadows` 0.96 1.00 1.05; `ColorGainHighlights` 1.04 1.00 0.96 |
| Bloom | 0.28 | 0.35 once fog exists; else keep |
| Sharpen | 0 | `Sharpen` 0.5 (or `r.Tonemapper.Sharpen=0.5`) |
| Grain / vignette | 0.12 / 0.3 | keep |

### 4.4 Proposed lighting set-ups

| | Clear midday | Fronts | Overcast |
|---|---|---|---|
| Sun | 95 klux | 95 klux, cloud shadow strength 1.0 | same; effectively 0 on the ground |
| Sky light | 1.4 | 1.0 | 1.0 |
| Target sun : sky on ground | 6-8 : 1 | 6 : 1 in sun, 0 : 1 in shade, both in frame | 0 : 1 |
| Where form comes from | sun shadows (grass, stones), bounce | moving cloud-shadow edges, shafts in fog | long-range sky occlusion (#15), contact AO (#9, #10), haze planes |
| Sky-atmosphere mist | 0.15 /km | 0.30 /km | 0.30 /km |
| Volumetric fog (if #18 passes) | on, extinction 0.15 /km | on, 0.4 /km | on, 0.6 /km, fog albedo 0.9 |
| Exposure bias | 0.0 | 0.0 | +0.3 |
| Volumetric fog cost | 1-2 ms at grid 8 / 128; start at 16 / 64 (0.5-1 ms) and raise if banding shows | | |

### 4.5 Order of work

Items 1-8 are one content rebuild and one C++ rebuild; shoot `drive_day` and the `sheet.jpg` set
again in all three weathers before touching anything else, because they change what every later
item looks like. Then 9-14, then #15 alone (it is the largest lighting change), then fog.
