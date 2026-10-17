# Planet surface seen from altitude: research for Dawnhollow (R = 5,000 m)

Scope: web research only. No project files were changed. Nothing was run in Unreal.

**Legend.** `[V]` = page fetched and read in this session. `[S]` = search-result snippet only, page not read. `UNVERIFIED` = not found in a source this session; it comes from memory, a standard formula, or my own derivation. Calculations are marked "my calc".

---

## 0. Diagnosis from the numbers (my calc)

Assumptions: 1080p display, 60 degree vertical FOV, camera at h = 1.8 km.

- **Horizon.** d = sqrt(2Rh + h^2) = sqrt(2 x 5000 x 1800 + 1800^2) = 4.6 km. The planet's curvature drops the surface 1.97 km between the camera and that horizon. Most of the frame is a steep, curved ground plane.
- **Pixel footprint.** Nadir footprint is about 1.7 m per pixel. The 4 m triplanar repeat is about 2 px and the 7.7 m mesh cell about 4.5 px. Both texture detail and geometry are at or below pixel scale. The relief that reads at 1.8 km sits at 10 to 200 m (gullies, ridges, valley walls, drainage), and the current stack has none of it.
- **Flat-colour fade.** Straight down, the 500 m cutoff sits at about 74.5 degrees depression, so roughly half the frame lies beyond it. At a 45 degree pitch, essentially the whole frame lies beyond it. This is the most likely cause of the "smear".

---

## 1. Erosion for a pure height function

### (a) Derivative-damped fBm (Inigo Quilez)

`[V]` https://iquilezles.org/articles/morenoise/ (2D form; the article's loop, quoted verbatim):

```glsl
const mat2 m = mat2(0.8,-0.6,0.6,0.8);
float terrain( in vec2 p ) {
    float a = 0.0; float b = 1.0; vec2 d = vec2(0.0,0.0);
    for( int i=0; i<15; i++ ) {
        vec3 n = noised(p);          // value + 2 derivatives
        d += n.yz;
        a += b*n.x/(1.0+dot(d,d));   // damp octave by accumulated slope
        b *= 0.5; p = m*p*2.0;
    }
    return a;
}
```

- 15 octaves, amplitude halves per octave, frequency doubles per octave.
- The article's `noised` definition was not retrieved. Its hash is a placeholder, so use any gradient or value noise with analytic derivatives.
- **Sphere:** use 3D noise on the unit direction, `noised(vec3)` returning value plus 3 derivatives (the article has a 3D version, `[V]`). Replace `m` with a 3D rotation. Each octave's damping then uses the accumulated 3D gradient.
- **Cost:** about 15 noise evaluations per sample. UNVERIFIED timing; estimate roughly 1 to 2 k ALU on GPU.
- **Look:** flatter valleys and rougher ridges, and it reduces fine detail on steep slopes. It is erosion-like, not true drainage.

### (b) Gully / erosion noise (Clay John, Fewes, Runevision)

Lineage: Clay John's "Eroded Terrain Noise" (Shadertoy, 2018) and Fewes' 2023 Shadertoy version `[S]` (via https://blog.runevision.com/2026/03/). Runevision's "Fast and Gorgeous Erosion Filter" (March 2026) is the most detailed account `[V]`: https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html. The Shadertoy code (wXcfWn) returned HTTP 403, so the GLSL was not read.

Algorithm as described `[V]`:
1. Gullies are stripes aligned to the downhill direction (negative gradient).
2. Domain is a jittered square grid. Each cell has a random pivot and rotates its stripe pattern about it (Worley-like). Neighbouring cells are blended; blending two unaligned sines gives a smaller sine, which keeps the result continuous.
3. Each stripe is an extruded wave: cosine for height offset, sine for slope.
4. Octave loop: each octave's raw gullies fade toward the previous octave's output using a combined mask. Mask update:
   ```glsl
   combiMask = pow_inv(combiMask, detail) * newMask;   // 0 = fade target, 1 = gully
   ```
   `pow_inv(t,p) = 1 - pow(1 - saturate(t), p)`. Examples use `detail` = 0.7, 1.5, 3.0.
5. Lacunarity is "typically half the size" (about 0.5 per octave).
6. Normalisation: the cos/sin pair is treated as a 2D vector, scaled by k = 2 and clamped to length 1. Discontinuities are accepted.
7. Fade target from altitude:
   ```glsl
   fadeTarget = inverse_lerp(valleyAlt, peakAlt, h) * 2.0 - 1.0;
   ```
8. Gully weight 0.5 with strength x 2.0 as an example, to keep peaks pointy.

Not given by the article: cell scale, stripe frequency, octave count, rounding values, pretend slope. These are UNVERIFIED and must be tuned.

Known limitation: drainage lines can stop partway down slopes (author's own remark).

- **Cost:** a few cell evaluations per octave, several octaves. UNVERIFIED per-sample cost.
- **Look:** convincing branching gullies and ridges. This is the closest analytic match to the target look.
- **Sphere:** it is a 2D filter on a height. For a cube-sphere, run it per face on the face coordinates, or in 3D with direction-based cells. Seam handling is UNVERIFIED.

### (c) Ridged multifractal and "swiss" turbulence

Standard form (Musgrave, *Texturing and Modeling*; book not fetched, so UNVERIFIED):
```glsl
float sum = 0.0, w = 1.0, amp = 1.0;
for (int i = 0; i < N; i++) {
    float s = offset - abs(noise(p));  // ridges where noise crosses zero
    s *= s;
    sum += s * w * amp;
    w = clamp(s * gain, 0.0, 1.0);     // ridges feed the next octave
    p *= lacunarity; amp *= gain;
}
```
Typical UNVERIFIED values: offset 1.0, gain 2.0, lacunarity 2.0. Swiss turbulence is `abs()` with derivative-based damping of this kind; exact formula UNVERIFIED. De Carpentier's "Scape" basics article could not be located (only gamedev.net pointers) `[S]`.

- **Cost:** about the same as fBm, N noise calls.
- **Look:** sharp crests, good for volcanic and alpine ridges. Without damping it looks like rubber sheets, which is why the damped variant is preferred.
- **Sphere:** works in 3D.

### (d) Domain warping

The IQ warp article returned HTTP 404 for this session, so details are from memory, UNVERIFIED:
```glsl
q = vec2(fbm(p), fbm(p + vec2(5.2, 1.3)));
r = vec2(fbm(p + 4.0*q + vec2(1.7, 9.2)), fbm(p + 4.0*q + vec2(8.3, 2.8)));
f = fbm(p + 4.0*r);
```
- **Cost:** about 3 to 4 fBm evaluations.
- **Look:** breaks the isotropic "blob" look and gives coherent, swirling structure. Alone it does not produce gullies. Use it on the macro layers only.
- **Sphere:** works in 3D.

### Summary table

| Technique | Cost per sample (estimate) | Look | Sphere |
|---|---|---|---|
| IQ damped fBm `[V]` | ~15 noise calls | Erosion-like, smooth | Yes (3D noise) |
| Runevision gully filter `[V]` | several cells x octaves | Best match: branching gullies | Per face or 3D (UNVERIFIED seams) |
| Ridged multifractal `UNVERIFIED` | N noise calls | Sharp crests, can look like sheets | Yes |
| Domain warp `UNVERIFIED` | 3 to 4 x fBm | Coherent swirl, no gullies | Yes |

---

## 2. How AAA games texture terrain across distance

Most of the GDC and SIGGRAPH material was not reachable. GDC Vault pages are members-only, and the Horizon and RDR2 searches returned no terrain talks.

- **Far Cry 5** (Jeremy Moore): "Terrain Rendering in Far Cry 5" covers GPU compute LOD, culling and stitching, and rendering "at all distances" `[S]` https://gdcvault.com/play/1025261/Terrain-Rendering-in-Far-Cry. Procedural World Generation (Etienne Carrier): biome-based texturing and procedural cliffs `[S]` https://gdcvault.com/play/1025557/Procedural-World-Generation-of-Far. Slides not read.
- **Ghost Recon Wildlands** (GDC 2017, Ubisoft): terrain made with World Machine plus Houdini, and a custom terrain editor `[S]`. Curvature, AO and blend-mask maps from Substance `[S]` https://80.lv/articles/landscape-and-material-pipeline-of-ghost-recon-wildlands/?amp=1. Decals were spread procedurally. Slide content on distance blending: UNVERIFIED.
- **Microsoft Flight Simulator 2020** (Asobo): a streaming tile system requests tiles by screen-space size and available bandwidth, and falls back to a parent tile when finer data isn't ready `[S]` (Asobo terrain-system PDF). No ocean detail found. Users reported visible repeating grid tiling in the whitecaps `[S]`, which is user speculation.
- **Horizon Zero Dawn, Red Dead Redemption 2 (SIGGRAPH 2019), Death Stranding/Decima, Star Citizen, Elite Dangerous, No Man's Sky**: no technical content retrieved. UNVERIFIED.

**Common practice, not sourced (UNVERIFIED):** detail tiling at 2 to 8 m, macro variation at 50 to 500 m, distance blends over tens to hundreds of metres, and baked colour or satellite-style maps beyond roughly 1 to 2 km. The O3DE docs confirm the pattern of macro colour as the only colour source beyond the detail render distance, blended with detail inside it `[S]` https://docs.o3de.org/docs/user-guide/components/reference/terrain/terrain-detail-material/.

---

## 3. Unreal-specific methods

- **Runtime Virtual Texturing for landscape** `[V]` https://dev.epicgames.com/documentation/en-us/unreal-engine/runtime-virtual-texturing-in-unreal-engine. RVT is a GPU-filled shading cache for landscapes and splines. `r.VT.RVT.TileCountBias` scales resolution: -1 halves, +1 doubles. Upload throttle `r.VT.MaxUploadsPerFrame` defaults to 64 in the Editor and 8 in game. Pools come from `BaseEngine.ini`. Nanite compatibility not stated, so UNVERIFIED. Tile and mip defaults not given.
- **Nanite tessellation/displacement, landscape macro variation:** not verified in this session.
- **Whole-planet height/normal cube map.** Sampled by direction, this gives relief shading beyond mesh resolution. Texel-size sizing (my calc, arc per face = (pi/2) x 5000 = 7,854 m):

| Target texel | Face texels needed | Choose | Actual texel | Memory per cube (R16F / BC4, my calc) |
|---|---|---|---|---|
| 2 m | 3,927 | 4096 | 1.9 m | 201 MB / 50 MB |
| 5 m | 1,571 | 2048 | 3.8 m | 50 MB / 12.6 MB |
| 10 m | 785 | 1024 | 7.7 m | 12.6 MB / 3 MB |

Corner texels in gnomonic cube mapping are smaller than face-centre texels (about 0.6x; UNVERIFIED citation). Equirectangular at 2 m would be about 15.7k x 7.9k with pole distortion, so a cube is preferable.

Use the 7.7 m (1024/face) cube for orbit, 2 m (4096/face) only for the descent band if needed. Store height in R16 (range -360 to 540 m gives about 8 mm steps) and derive normals from neighbouring samples.

---

## 4. What real terrain looks like from 2 to 30 km (Iceland)

What reads as real from altitude `[S]` (Smithsonian aerial feature https://www.smithsonianmag.com/travel/andre-ermolaevs-aerial-view-icelands-volcanic-rivers-180949684/ and stock-footage captions, treated as leads only):
- **Braided meltwater on sandur outwash plains** of dark basaltic material. Channel braiding is a strong read.
- **Dendritic drainage** on valley walls. Not verified from a fetched source (NASA Earth Observatory search failed).
- **Valley shadows and talus** at the base of slopes; sharp ridge crests.
- **Albedo contrast.** Values from the brief, UNVERIFIED: fresh basalt and black sand 0.03 to 0.06; moss 0.05 to 0.10, green-dominant; snow 0.8 to 0.9; glacial meltwater milky turquoise.
- **Coastlines:** fractal, with skerries, fjords and a surf line.

**Why smooth noise looks like plasticine (my analysis):** isotropic noise has no slope-correlated structure, so ridges and valleys are not in the right places. There is no drainage, so no branching. Shading is uniform because roughness and albedo are flat. Shadows and AO have no features to act on at the 10 to 200 m scale where the eye reads relief. The fixes are: an erosion filter (section 1b), a gully/valley layer, talus and colour varied with slope, coast with fractal skerries, and matte materials.

---

## 5. Ocean from altitude

### (a) Sun glint (Cox-Munk)

Mean-square slope (total, upwind + crosswind): sigma^2 = 0.003 + 5.12e-3 x U, U = wind at 12.5 m `[V]` https://oceanopticsbook.info/view/surfaces/cox-munk-sea-surface-slope-statistics (Mobley). Original: Cox & Munk 1954, https://data-ww3.ifremer.fr/BIB/Cox_Munk_JOSA1954.pdf `[S]`.

Conversion to GGX (my derivation; citation for Beckmann-GGX matching, e.g. Walter et al. 2007, UNVERIFIED):
- The total mss is the sum of two axes, so per axis the variance is mss/2.
- Beckmann m satisfies m^2 = 2 x per-axis variance = mss, so m = sqrt(mss).
- Set GGX alpha = sqrt(mss). Unreal uses alpha = roughness^2, so **roughness = mss^(1/4)**.
- Using sqrt(2 mss) would be wrong for this definition.

| U (m/s) | mss | alpha | UE roughness |
|---|---|---|---|
| 3 | 0.0184 | 0.136 | 0.37 |
| 5 | 0.0286 | 0.169 | 0.41 |
| 8 | 0.0440 | 0.210 | 0.46 |
| 12 | 0.0644 | 0.254 | 0.50 |

Use roughness as the floor for distant water, and add a variance-based increase (Toksvig, below). A glint roughness near 0.05 gives the hard blob. Sea-state glint should be a broad patch.

### (b) Water colour

- Pope & Fry (1997): absorption minimum 0.0044 +/- 0.0006 m^-1 at 418 nm `[V]` https://omlc.org/spectra/water/abs/pope97.html. Red and green values were not retrieved: UNVERIFIED.
- Epic's example coefficients in Single Layer Water: R 0.0033, G 0.0016, B 0.0011 (reciprocal metres) `[V]`. These are art-tuned, not physical; with them red's e-fold depth is 1/0.0033 = 303 m.
- Mechanism: shallow water over bright sand looks turquoise because red is absorbed first and blue/green return from the bed. Deep water keeps only blue. **Dawnhollow's black volcanic sand seabed will look dark, not turquoise, in shallows.**
- Shader form: `colour = bedAlbedo * exp(-2 k d) * E + scatter * (1 - exp(-k_s d))`.
- Depth at which the seabed vanishes: UNVERIFIED. Tune in engine.

### (c) Single Layer Water (Unreal)

`[V]` https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine. Inputs: Absorption and Scattering (per RGB), Phase G (-0.75 and 0.5 examples; 0 isotropic), Color Scale Behind Water ("simply a visual effect", not real caustics or shadows), Opacity (0, 0.5, 1). Refraction samples scene colour and depth.
- Requires Blend Mode Opaque or Masked.
- Ocean default values, foam, water-depth computation, Nanite and large-sphere behaviour: not in the page. UNVERIFIED.

### (d) Shoreline foam from depth

Foam = saturate(1 - depth / w), w about 1 to 3 m, plus noise to break it up. Standard technique, values UNVERIFIED.

### (e) Long-distance ocean (Sea of Thieves, AC, MSFS, Unreal Water plugin)

Not verified. The Toksvig paper (Mipmapping Normal Maps, 2005) relates shortened averaged normals to a roughness increase `[S]` https://developer.download.nvidia.cn/whitepapers/2006/Mipmapping_Normal_Maps.pdf. Its exact formula was not read, so it is UNVERIFIED here.

---

## 6. Atmospheric perspective from above

Koschmieder contrast (standard formula, UNVERIFIED citation): C(d) = exp(-beta d), beta = 3.912 / V.

| Visibility V | Haze at 4.6 km (horizon) | Haze at 20 km |
|---|---|---|
| 40 km | 36% | 86% |
| 80 km | 20% | 62% |
| 120 km | 14% | 48% |

Thin, clean polar air is about 80 to 120 km. Under 40 km reads as smog.

Unreal `[V]` https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine:
- Aerial Perspective View Distance Scale, Mie Scattering Scale, Rayleigh scale: numeric defaults not stated.
- Mie Anisotropy example 0.8. Multiscatter 1, Earth albedo 0.4.
- Example ground radius 6,360 km (Earth). Dawnhollow is 5 km. Whether the model stays correct at that scale is UNVERIFIED; check the horizon colour and aerial contrast against the table above.
- Height fog: enable "Support Sky Atmosphere Affecting Height Fog"; set Fog Inscattering and Directional Inscattering to black to let the atmosphere drive fog; scale with Height Fog Contribution.

---

## Recommended recipe (ordered; targets the 2 km view)

**Height function**
1. Add the gully erosion filter (section 1b) to land heights. Start with about 6 to 8 octaves from a cell of 400 to 800 m down to 6 m, amplitude about 15% of local relief. Tune in engine.
2. Add a ridged volcanic layer on all land, not only inland: 50 to 300 m wavelength, amplitude 10 to 40 m, derivative-damped. Add 6 octaves of damped fBm from 30 m to 2 m wavelength, amplitude about 0.3 m at the finest.
3. Bake a height cube: 1024 per face (7.7 m) for orbit and 4096 per face (2 m) for descent, R16, with normals derived in the shader for distance LOD. Memory: 12.6 MB at 1024, 201 MB at 4096 per cube (50 MB if BC4).

**Ground material**
4. Replace the 500 m flat cutoff with a continuous ladder: 4 m detail (albedo and normal) full to 150 m and faded out to 600 m; macro variation at 40 m and 120 m fading in from 60 m; the baked colour cube beyond about 1.5 km.
5. Bake the five-layer blend into the colour cube (1024 per face). Keep normal detail from the height cube at distance; do not drop to the plain vertex normal.
6. Matte the surfaces: basalt about 0.8, black sand about 0.85 (also under water, to remove the plastic shore), moss about 0.7, snow about 0.5. These are UNVERIFIED starting points.

**Ocean**
7. Set roughness to mss^(1/4) with U = 5 to 8 m/s (0.41 to 0.46) as a floor at distance. Remove the mirror sheet; keep the glint a broad patch.
8. Replace the flat teal with depth-based absorption and scattering (Epic's coefficients as a starting point), with a black-sand bed tint. Add shoreline foam over depth 0 to 2 m.

**Atmosphere**
9. Set haze to a visibility of 80 to 120 km (14 to 20% haze at the 4.6 km horizon).
10. Verify Sky Atmosphere at 5 km ground radius: tune Aerial Perspective View Distance Scale and Mie scattering scale to hit the table in section 6, and keep fog density near zero above the surface.

---

## Not verified in this session
- Shadertoy sources (403), Clay John and Fewes code details, Scape article, Horizon/RDR2/Death Stranding/Star Citizen/Elite/NMS talks, Sea of Thieves, AC and MSFS ocean methods.
- Pope & Fry red and green coefficients. Epic Single Layer Water ocean defaults.
- Sky Atmosphere defaults and behaviour at 5 km radius.
- NASA Earth Observatory Iceland imagery and dendritic drainage evidence.
