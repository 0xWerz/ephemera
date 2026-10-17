# Hush (1,400 m radius airless moon): terrain realism research

Date: 2026-10-13. Scope: web research only. No builds, no engine runs.

## How to read this report

- **[src]** = a source URL was fetched or returned in a search summary and the value is attributed to it.
- **UNVERIFIED** = could not be checked against a primary page. Treat as a starting value to tune, not a fact.
- Several sources were reachable only through search summaries (the fetch timed out, returned 404, or was a members-only or binary page). Those are marked "search summary".

Headline finding: most of the realism gap is scale and statistics. The current build has roughly 6% relief-to-radius from smooth noise, which reads as a potato. A real airless surface is dominated by a crater population that is steep-sided at small sizes, degraded with age, and has fine detail down to the sub-metre scale.

---

## 1. Lunar crater morphology

**Simple craters.** Depth-to-diameter about 0.2 at small sizes. [src: search summary of Pike 1977, "Size-Dependence in the Shape of Fresh Impact Craters on the Moon", NASA NTRS https://ntrs.nasa.gov/citations/19780057837. Full-text fetch timed out, so this is from the abstract summary.] Simple craters are "bowl-shaped and superficially similar to the transient crater." [src: https://en.wikipedia.org/wiki/Impact_crater]

**Rim height 0.04 D.** UNVERIFIED. The user-supplied figure could not be confirmed. Pike's rim-height fits exist [src: https://www.lpod.org/wiki/July_25,_2007, search summary], but I did not retrieve the coefficients. Do not treat 0.04 D as sourced.

**Complex craters.** Flat floor, central uplift, terraced walls. [src: https://en.wikipedia.org/wiki/Impact_crater] Pike-type depth summary (search summary, secondary): complex depth d/D falls from about 0.1 to 0.01 with increasing size. Transition to complex at about 10.6 km (one analysis of Pike 1977), 15 km for mare and 21 km for highland (Pike 1981, per a 2018 study summary). [src: search summaries, URLs not confirmed]

**Simple-to-complex transition.** Real Moon: about 15 to 20 km. [src: summaries above] For Hush, gravity is about 1.3e-3 m/s² (derived from G, radius 1,400 m and an assumed Moon-like density of 3,344 kg/m³; UNVERIFIED density). A gravity-regime argument then suggests nearly every Hush crater should stay bowl-shaped at all sizes. This is my reasoning, UNVERIFIED. Practical consequence: keep craters under about 300 m simple (bowl), and give only the largest 300 to 600 m craters a flat floor and small central peak. That is an art choice, not a physical result.

**Design profile (my construction, not from a source).** With x = r / R (R = crater radius), depth d = 0.2 D for small craters:

```
cavity(x)   = d * (x^2 - 1)                     // bowl, -d at centre, 0 at rim
rim(x)      = H_r * (1 - ((x - 1) / w)^2)       // only |x-1| < w, w ~ 0.3; H_r = 0.08 R (= 0.04 D, UNVERIFIED)
ejecta(x)   = t_r * x^-3   for x > 1 + w        // exponent -3 verified below
t_r         = 0.14 * R^0.74                     // metres, R in metres
h(x)        = smin(cavity, rimpart, k)          // k ~ 0.1 R, see §4
```

**Ejecta falloff.** Thickness power-law exponent -3.0. [src: https://cratermaker.readthedocs.io/en/latest/_modules/cratermaker/components/morphology/basicmoon.html, `_EJPROFILE = -3.0`] The rim thickness formula 0.14 (D/2)^0.74 (McGetchin, Settle and Head 1973) appears only as a commented-out line in the same file [src: same URL]. Cratermaker's ejecta reference radius is in compiled code and could not be read.

**Degradation (cratermaker, Minton et al. 2019; Riedel et al. 2020).** Constants: kv1 = 0.30, neq1 = 0.0084, eta = 3.2, gamma = 2.0, beta = 2.0, psi_1 = 2.0 (mare scale), psi_2 = 1.2 (highland scale), breakpoint 500 m, burial softening 1.5 × thickness^2. [src: same cratermaker URL]. Their exact meaning in a game shader is UNVERIFIED.

**Keep at small sizes (10 m to 600 m).** Keep the bowl (cavity), a soft rim whose height scales with size, a thin ejecta blanket with x^-3 falloff, and a per-crater degradation state. Drop complex morphology below about 300 m. Give only the large ones a flat floor.

---

## 2. Crater size-frequency distribution (SFD)

Cumulative N(>D) = K · D^-b.

- Saturation slope: b = 2 (cumulative). Small-crater production slopes are steeper (about -3). Both UNVERIFIED in this session.
- A "geometric saturation" constant of K = 1.54 km^-2 at b = 2 (D in km) is a common reference (Gault 1970). UNVERIFIED (from memory, not fetched).
- Cumulative slope -1.83 was reported as the observed slope in Hartmann and Gaskell-type simulations for maria and martian plains [src: search summary of NASA NTRS results, URL not confirmed; UNVERIFIED].
- Real saturation is a few percent to tens of percent of geometric saturation, depending on the surface. The real equilibrium fraction is UNVERIFIED.
- Lunar production polynomial (Neukum and Ivanov 2001) coefficients a0 to a6 were listed in a secondary paper [src: https://arxiv.org/pdf/2604.16223]: a0 = -2.5489, a1 = -2.9794, a2 = 0.42605, a3 = 0.32288, a4 = -0.030823, a5 = -0.022295, a6 = 0.019473. The full set is 12 terms and the rest were not found. Do not use it as-is.

**Recipe.** Hush area = 4 pi (1.4 km)^2 = 24.6 km². With K = 1.54 f km^-2 (f = fraction of geometric saturation) and b = 2, the count in a size band [D_lo, D_hi] is

```
count = 37.9 * f * (D_lo^-2 - D_hi^-2)     // D in km; 37.9 = 1.54 * 24.6
```

| Class | count per unit f | highland f = 0.3 | mare f = 0.03 |
|---|---|---|---|
| 300 to 600 m | 316 | ~95 | ~10 |
| 150 to 300 m | 1,264 | ~379 | ~38 |
| 75 to 150 m | 5,058 | ~1,517 | ~152 |
| 40 to 75 m | 16,963 | ~5,089 | ~509 |
| 20 to 40 m | 71,119 | ~21,340 | ~2,134 |
| 10 to 20 m | 284,475 | ~85,340 | ~8,534 |

f values (0.3 highland, 0.03 mare, a 10x ratio) are UNVERIFIED art parameters. The 10 to 20 m class is about 85,000 craters for highlands. That is too many to evaluate per frame, so generate them per cell from a hash (§4) and bake only the visible ones into the mesh or normal map. The 4.3 m cell cannot resolve 10 to 20 m craters (2 to 5 cells across). Carry those in normals or displacement.

**Degradation per crater.** Store an age state s in [0, 1] per crater, hashed from its position. Suggested mapping, UNVERIFIED design: rim height × (1 − 0.7 s), depth × (1 − 0.5 s), profile smoothed with width ∝ s, ejecta albedo boost and rays only for s < 0.1. Fresh craters (rays, bright ejecta) should be rare, perhaps 1 to 5% of craters above about 1 km (UNVERIFIED).

---

## 3. Large-scale structure

Ranked by realism per unit of work (my ranking, UNVERIFIED):

1. **Crater population itself** (§1 and §2). Most of the realism.
2. **Maria as flat, dark, partly buried basins.** Maria cover about 16% of the Moon's surface and terra about 84% [src: https://photojournal.jpl.nasa.gov/catalog/PIA00302, Clementine caption]. Maria are the darkest regions in the Clementine 750 nm map [src: same]. Burial of old craters inside maria is a single-line rule (thickness² softening in §1). Maria albedo 0.07 and highland albedo 0.12 to 0.16 are the user's numbers and are UNVERIFIED here. The Moon-wide average is 0.136 [src: https://en.wikipedia.org/wiki/Moon, infobox, Saari citation], which does not split maria from highlands.
3. **Boulder fields and ejecta rays** around fresh craters. Cheap as instanced rocks or albedo streaks.
4. **Wrinkle ridges and rilles.** Lower value on a 1.4 km body; skip unless a feature is needed.
5. **Giant basins.** Skip. A 600 m crater is already 0.21 of Hush's radius.

---

## 4. Procedural techniques used in practice

- **Sebastian Lague, "Procedural Moons and Planets" (2020).** Exact crater expressions could not be retrieved. The public repo `github.com/SebLague/Procedural-Planets` (MIT) has no crater code; its noise folder is named `Procedural Planet Noise` [src: https://github.com/SebLague/Procedural-Planets, fetched]. The moons project files appear to be on Patreon [src: search summary, https://www.patreon.com/SebastianLague/sitemap]. The cavity, rim and floor formulas are therefore UNVERIFIED. The construction in §1 is my own; do not attribute it to Lague. A community port, `timoflesch/procedural_planets`, has shape and noise files but no crater code [src: https://github.com/timoflesch/procedural_planets, fetched].
- **Inigo Quilez smooth min.** The article URL returned 404 at both http and https, so it could not be read. The polynomial form, from memory and UNVERIFIED against the page, is:

```glsl
float smin(float a, float b, float k) {       // IQ "polynomial smooth min"
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}
```

  glsl-smooth-min ships `poly`, `pow` and `exp` variants "sourced from Iñigo Quílez's article" [src: https://github.com/glslify/glsl-smooth-min, fetched; poly.glsl body not visible in the page].
- **Worley / Voronoi crater placement.** Hash each cell's coordinates to get a feature point, a radius, a depth and an age state. Test the 3x3 neighbourhood so craters crossing cell borders are drawn. This is standard practice, UNVERIFIED as a cited source (a search for it returned only general Worley noise background [src: https://en.wikipedia.org/wiki/Worley_noise]).
- **No Man's Sky.** The GDC talk "Continuous World Generation in No Man's Sky" exists, but the transcript is members-only [src: https://www.gdcvault.com/play/1024265/Continuous-World-Generation-in-No]. Elite Dangerous and Star Citizen talks were not found.
- **Cratermaker (Python).** Real-world reference for crater populations, SFDs, degradation and ejecta [src: https://cratermaker.readthedocs.io]. Use it as a checker, not a source for shader code (see §2 and §1 for usable constants).

---

## 5. Why the moon reads as "low poly" or fake, and fixes

1. **Missing detail below the mesh.** A 4.3 m grid cannot show features under about 10 m. The Moon has detail at every scale. Fix: a crater population down to about 10 m in normals or displacement, plus a few cm-scale albedo and roughness noise. Nanite displacement or Static Displacement Mapping is the engine route [src: https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine].
2. **Too many smooth noise hills.** The Moon's relief is craters on craters, not rolling hills. Cut the 55 m octave and replace it with crater population. Keep at most one broad low-amplitude octave.
3. **Relief-to-radius ratio.** Current relief is about -35 m to +47 m, about 82 m total, or 5.9% of 1,400 m. The Moon's total topographic range is about 20 km on a 1,737 km radius, about 1.1% (UNVERIFIED recollection, not fetched). Reduce large-scale amplitudes and let big craters carry the silhouette at a reduced depth-to-diameter (see §7 recipe).
4. **Texture tiling and albedo.** Add low-frequency albedo variation (maria versus highland, ray streaks, dust), and break tiling with a second, rotated or world-space texture sample.
5. **Vertex normals.** Compute normals analytically from HeightM with a finite-difference step of about 0.5 m (about 1/8 of a cell). Coarse mesh normals make facets visible in shading.
6. **Scale cues.** Boulders and small craters at 1 to 10 m. Without them, the eye reads the surface as a scaled-up model.

---

## 6. Lunar surface photometry

**Lommel-Seeliger.** The single-scattering law for a dark, smooth, particulate surface:

```
r_LS ∝ mu0 / (mu0 + mu)        // mu0 = cos(incidence), mu = cos(emission)
```

The structure μ0/(μ0+μ) is confirmed in an arXiv lunar dust review [src: https://arxiv.org/pdf/2511.08503, search summary]. Consequence: near the limb (mu → 0) it tends to a constant, so little limb darkening, as observed on the Moon. This is derived from the formula, not from a source.

**Hapke.** Hapke's model adds single-scattering albedo w, a particle phase function P(g), an opposition term B(g) and multiple-scattering H-functions. Standard form (Hapke 2012; UNVERIFIED against the book):

```
r = (w / 4π) * mu0 / (mu0 + mu) * [ (1 + B(g)) P(g) + H(mu0) H(mu) - 1 ]
B(g) = B0 / (1 + tan(g/2) / h)
```

The opposition term has shadow-hiding and coherent-backscatter parts [src: https://arxiv.org/pdf/2511.08503, search summary]. Helfenstein and Veverka give the Hapke parameters for lunar terrain and find dark mare about 2.5 times less macroscopically rough than bright highland [src: NASA, search summary, URL not confirmed]. Lommel-Seeliger alone does not reproduce the opposition surge. A SMART-1 study found shadowing alone cannot explain the lunar opposition effect [src: search summary, URL not confirmed].

**Unreal values.** Unreal's Specular 0.5 corresponds to F0 = 0.04, the standard dielectric reflectance (from memory, UNVERIFIED). Regolith: Roughness 0.9 to 1.0, Specular 0.1 to 0.2 (F0 about 0.01 to 0.02), no clear coat. The "wet" look comes from low roughness plus strong normal-map contrast. Keep normal-map intensity moderate and roughness high everywhere, including ray-streak and crater-wall areas.

**Approximating the Moon's look in Default Lit.** Default Lit has no opposition surge. Two options (UNVERIFIED that either is free of problems with Lumen):
- Use a Custom node to multiply albedo by 1 + B0 / (1 + tan(g/2) / h) with g from the view and sun vectors, with B0 about 0.5 to 1 and h about 0.05 (design values).
- Accept Lambert with high roughness and tune the exposure curve. This is the cheap path.

---

## 7. Lighting an airless body

- **Black sky and hard shadows.** No atmosphere, so no aerial perspective. Use a near-black sky light and let shadows be fully dark.
- **Exposure.** A common photographic rule for the lunar surface is Sunny 16 (f/16 at 1/ISO), with the "Looney 11" variant (f/11, one stop more) for the Moon's darker regolith [src: https://en.wikipedia.org/wiki/Looney_11_rule]. Set the exposure so sunlit regolith sits near Looney 11, not Sunny 16. Apollo exposure settings were not confirmed.
- **Fill and bounce.** The parent-planet fill (earthshine on the Moon) is weak; use a faint directional fill at about 0.5 to 2% of sun intensity from the planet's direction (UNVERIFIED design value). Interstellar's VFX team used reflected stage light to fill the shadow side of spacecraft [src: search summary, URL not confirmed], which is a practical precedent for fill sources.
- **Films and games.** Deliver Us The Moon, Starfield and KSP visual mods were not researched. Their exposure choices are UNVERIFIED.

---

## 8. Unreal Engine 5 specifics

- **Nanite landscape and displacement.** Enable Nanite on the landscape, then Build Data. Nanite Tessellation is driven by the landscape material's displacement. The editor does not show Nanite until the data is rebuilt or the proxy is saved [src: https://dev.epicgames.com/documentation/en-us/unreal-engine/using-nanite-with-landscapes-in-unreal-engine].
- **Seams between chunks.** Nanite simplifies each mesh independently, so chunk borders can open cracks at distance. The landscape doc names a **Nanite Skirt** option (extra vertices moved down by Nanite Skirt Depth) so neighbouring meshes overlap [src: same URL]. Apply the same overlap for the cube-sphere chunks, since the doc does not give a numeric skirt depth (UNVERIFIED, set to about one cell, 4.3 m).
- **Limits.** Nanite supports Opaque and Masked blend modes only, and needs DirectX 12 with Shader Model 6. World Position Offset must be clamped. The scene-wide Nanite instance limit is 16 million [src: https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine].
- **Virtual shadow maps.** Enable with `r.Shadow.Virtual.Enable` (the default in new projects). Keep caching on. Soft-shadow rays default to 8 per ray; 4 to 8 samples per ray works well. Set `r.Shadow.Virtual.SMRT.RayCountDirectional` to 0 for hard shadows. Directional clipmap FirstLevel 6 (about 64 cm) and LastLevel 22 (about 40 km) are defaults and are generous for a 2.8 km body [src: https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine]. Set the sun's source angle near the real Sun's apparent size, about 0.5 degrees (UNVERIFIED value, from memory). Keep `r.Shadow.Virtual.NormalBias` at the 0.5 default. Watch `MaxPhysicalPages`, since exceeding it corrupts shadows.
- **Detail blending.** Triplanar detail normals fade with camera distance over roughly 20 to 200 m (design values, UNVERIFIED). Distance-based detail blending is standard practice, not sourced.
- **Known issue.** A UE 5.8 forum report says landscape Nanite displacement stopped working after 5.7.4 [src: https://forums.unrealengine.com/t/nanite-displacement-bugged-in-exact-same-landscape-in-ue-5-8-but-not-in-ue-5-7/2739815]. Check before relying on it.

---

## Recommended recipe for a 1,400 m radius moon

Ordered by expected realism gain per unit of work. All numbers UNVERIFIED unless noted.

1. **Remove the 55 m and 35 m noise octaves.** Keep at most one broad octave, 3 to 6 m amplitude. Target large-scale relief about 1 to 2% of R (14 to 28 m total).
2. **Rebuild craters from a size-frequency population** (§2): 10 to 600 m, b = 2, highland f = 0.3, mare f = 0.03. Hash per cell, evaluate the 3x3 neighbourhood.
3. **Use the bowl profile for craters below 300 m** (depth 0.2 D, rim H_r = 0.08 R, ejecta t_r = 0.14 R^0.74 with x^-3 falloff). Use the same bowl for larger craters but with depth reduced to about 0.1 D and a flat floor filled to about 0.5 of depth.
4. **Blend craters with IQ smin** (k about 0.1 R) so rims meet without seams.
5. **Per-crater age state s in [0, 1]** (rim × (1 − 0.7 s), depth × (1 − 0.5 s)); fresh craters (s < 0.1) get rays and bright ejecta, about 1 to 5% of those above 1 km.
6. **Sub-mesh craters (10 to 20 m and 20 to 40 m) in normal map or Nanite displacement** (4.3 m cells cannot resolve them).
7. **Maria as flat, dark, partly buried basins** covering about 16% of the surface. Burial softening: thickness² × 1.5 (cratermaker value).
8. **Analytic normals** from HeightM with a 0.5 m step.
9. **Regolith material**: Roughness 0.9 to 1.0, Specular 0.1 to 0.2, moderate normal intensity, albedo 0.07 to 0.12 with low-frequency variation.
10. **Exposure at Looney 11** (one stop above Sunny 16), black sky, a faint planetshine fill at 0.5 to 2% of sun intensity.
11. **Nanite on chunks with overlapping skirts** (Nanite Skirt equivalent, about one cell overlap), WPO clamped, Nanite displacement for the 10 to 20 m class.
12. **Virtual shadow maps**: keep defaults, 4 to 8 SMRT samples, sun source angle about 0.5 degrees, MaxPhysicalPages checked against r.Shadow.Virtual.ShowStats.
