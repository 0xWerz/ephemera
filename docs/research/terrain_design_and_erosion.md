# Terrain design and erosion research for Dawnhollow

Date: 2026-10-13. Scope: web research only. No builds, no Unreal, no other files touched.

## How to read this report

- **[src]** = the claim comes from a page I actually fetched and read.
- **[snip]** = seen only in a search summary; verify before relying on it.
- **UNVERIFIED** = I could not confirm it from a source. Treat it as a starting guess.
- **SPECULATION** = fiction or physics extrapolation, not a claim about real geology.

Sources that were blocked or unreadable: the Beyer 2015 thesis (not found in search), the Mei 2007 full text (only a summary page found), the Stava 2008 PDF (returned as undecodable binary), the Cordonnier 2016 PDF (too large to fetch), and the Benes 2006 PDF (404). Equations and constants that depend on those papers are marked UNVERIFIED.

---

## 1. How professional terrains are authored

**Gaea (QuadSpinner).** The Erosion node outputs three data maps: **Wear** (where material is removed), **Deposits** (where removed material comes to rest) and **Flow** (the path sediment takes from origin to rest) [src: https://docs.quadspinner.com/Reference/Erosion/Erosion.html]. Parameters documented there include Duration (default 0.04, "values around 0.02 or lower add detail while keeping main landforms"), Rock Softness, Strength (can exceed 1.0), Downcutting (deeper gullies), Inhibition (causes alluvial fans next to deep channels), Base Level, Feature Scale (default 2000 m), Volume (wider valley floors without downcutting), Sediment Removal (can be masked), and Selective Processing (Area input, or automatic bias by Slope or Altitude) [src, same page]. The same page says the algorithm keeps features consistent across resolutions, so a 512 px preview should match an 8K build. It also warns that over-using Flow for texturing looks "CG" [src, same page]. Internal algorithm: UNVERIFIED (not stated in the reference page).

**World Machine.** The Erosion (Legacy) page lists three masks: **Flow** (where water flowed), **Wear** (where bedrock was eroded) and **Deposition** (where sediment landed) [src: https://help.world-machine.com/topic/device-erosion/]. A 2013 World Machine forum post (not reopened; found via search) says hydraulic erosion has four outputs: eroded heightmap, water flow, soil deposition and wear [snip]. Thermal Erosion is described as "decompose cliff faces and accumulate talus slopes", and Snow Accumulation as "model snowfall and melt" [snip: https://www.world-machine.com/features.php].

**World Creator, Instant Terra, Houdini heightfields.** Not researched in depth. UNVERIFIED for their specific stage lists.

**Shipped games (what I could confirm).**

| Game | What is confirmed | Source |
|---|---|---|
| Ghost Recon Wildlands | Terrain Procedural Sets (TPS) assign materials by rules on **slope, height, flow map, density map, deposition map, curvature**. Houdini spreads decals and scatters rocks from terrain data. ZBrush sculpts terrain textures by hand. Substance Designer makes curvature, AO and blend masks [src: https://80.lv/articles/landscape-and-material-pipeline-of-ghost-recon-wildlands/]. World Machine was used for the terrains [snip: https://forum.world-machine.com/t/wm-used-in-ghost-recon-wildlands/4435]. A GDC 2017 talk "Terrain Tools and Technology" by Benoit Martinez is listed [snip]. | as listed |
| Horizon Zero Dawn | GDC 2017 talk on GPU run-time procedural placement, driven by a graph editor, by Jaap van Muijden [snip: https://gdcvault.com/play/1024120]. 80.lv write-up says artists could reshape mountains and turn a desert into a swamp in the graph [snip: https://80.lv/articles/the-procedural-nature-of-the-horizon-zero-dawn]. Base terrain erosion: UNVERIFIED. | as listed |
| Microsoft Flight Simulator | Asobo slide deck "Designing the Terrain System": quadtree, Mercator projection, inputs from Bing Maps, OpenStreetMap, Azure streaming, and TIN and DEM data from photogrammetry and add-ons [snip: https://WWW.ASOBOSTUDIO.COM/files/inline-images/Designing_Terrain_System_Fuentes_Lionel.pdf]. | as listed |
| Hellblade II | Real Icelandic locations rebuilt from satellite data and photogrammetry; 370+ photogrammetry pieces versus one in the original [snip: https://news.xbox.com/en-us/2024/05/20/hellblade-2-environmental-design-inspired-by-iceland/]. Nanite and Lumen were announced, not confirmed for shipping [snip: https://www.tweaktown.com/news/80052/hellblade-ii-senuas-saga-isnt-in-full-production-yet/index.html]. | as listed |
| Unity Terrain Tools (reference) | Hydraulic, thermal and wind erosion tools. "For highly detailed erosion features, use a Terrain heightmap resolution of 1025 or greater" [snip: https://docs.unity3d.com/Packages/com.unity.terrain-tools@5.1/manual/erosion.html]. | as listed |

**Not found:** Red Dead Redemption 2, God of War, Death Stranding, Far Cry 5. Searches returned only unrelated GDC talks. Their terrain pipelines remain UNVERIFIED and need a direct GDC Vault search.

**Takeaway for us.** Professional pipelines make erosion a mask generator and then drive materials and scatter from masks: flow, deposition, wear, slope, curvature, height. Curvature and AO come from a separate texturing tool, not from the erosion itself. [src: Ghost Recon and Gaea pages above]

---

## 2. Erosion algorithms we could implement

### 2a. Particle (droplet) hydraulic erosion, Lague-style

Verified defaults, from Sebastian Lague's `Erosion.cs` [src: https://raw.githubusercontent.com/SebLague/Hydraulic-Erosion/master/Assets/Scripts/Erosion.cs; project README: https://github.com/SebLague/Hydraulic-Erosion]:

| Parameter | Default | Note |
|---|---|---|
| inertia | 0.05 | 0 to 1 |
| sedimentCapacityFactor | 4 | |
| minSedimentCapacity | 0.01 | |
| erodeSpeed | 0.3 | 0 to 1 |
| depositSpeed | 0.3 | 0 to 1 |
| evaporateSpeed | 0.01 | 0 to 1 |
| gravity | 4 | |
| maxDropletLifetime | 30 | |
| erosionRadius | 3 | 2 to 8 |
| initialWaterVolume | 1 | |
| initialSpeed | 1 | |

The README "After" image uses 70k droplets [src: github README]. Your "70,000 on 255x255" figure is consistent with that, which is about 1.1 droplets per pixel. Map size for the README image is UNVERIFIED.

Per-droplet loop, as quoted from the source [src, Erosion.cs]:

```
for i in 1..numDroplets:
  pos = random point in map (float)
  dir = (0,0); speed = initialSpeed; water = initialWaterVolume; sediment = 0
  for step in 0..maxDropletLifetime:
    (h, gx, gy) = bilinear height and gradient at pos (from the 4 corner cells)
    dir = dir*inertia - (gx, gy)*(1 - inertia);  normalize(dir)   # downhill
    newPos = pos + dir
    if newPos outside map: break
    dh = height(newPos) - h                                        # negative = downhill
    capacity = max(-dh * speed * water * sedimentCapacityFactor, minSedimentCapacity)
    if sediment > capacity or dh > 0:                              # deposit
      amt = (dh > 0) ? min(dh, sediment) : (sediment - capacity) * depositSpeed
      sediment -= amt; add amt at pos (bilinear splat)
    else:                                                          # erode
      amt = min((capacity - sediment) * erodeSpeed, -dh)
      subtract amt over disc of radius erosionRadius (weights sum to 1); sediment += amt
    speed = sqrt(max(0, speed^2 - dh * gravity))                   # my sign fix, see note
    water *= (1 - evaporateSpeed)
    pos = newPos
```

**Sign caution.** The fetched summary quotes `speed = sqrt(speed*speed + deltaHeight*gravity)` with `deltaHeight = newHeight - height`. Because `deltaHeight` is negative downhill, that form appears to slow droplets on descent. The version above uses the physical sign. Check `Erosion.cs` directly before copying either form.

Beyer 2015 thesis: not found. The parameter set above is taken from the Lague code, not from Beyer. Job Talle's article describes a different "snowball" method with 35k to 100k snowballs that runs in about half a second on one CPU thread, but it has no sediment capacity or evaporation [src: https://jobtalle.com/simulating_hydraulic_erosion.html]. Its formulas are a useful alternative but not the droplet method you asked about.

### 2b. Grid "virtual pipes" shallow-water erosion (Mei 2007, Stava 2008)

Mei, Decaudin and Hu, PG 2007, pp. 47–56. The paper simulates erosion at interactive rates using velocity fields from a shallow-water model, and runs entirely on the GPU [snip: https://hgpu.org/?p=2256]. Full text not read. Stava et al. 2008 (Purdue PDF) was not decodable.

Structure, as usually described (UNVERIFIED in detail, because I could not read the papers):

1. Rain adds water height `d` to every cell.
2. Virtual pipe outflow flux `f` to each of 4 neighbours, from the height difference `(h_i + d_i) - (h_j + d_j)`, with a pipe cross-section and length (UNVERIFIED equation).
3. Water velocity from net flux (UNVERIFIED equation).
4. Sediment capacity `C = Kc * sin(alpha) * |v|` (form given in the brief; constants UNVERIFIED).
5. If `s < C` dissolve, else deposit, at rates Ks and Kd (UNVERIFIED).
6. Advect sediment with the velocity field (semi-Lagrangian) (UNVERIFIED).
7. Evaporate water by a factor Ke (UNVERIFIED).

Typical constants: UNVERIFIED. Start by tuning on a 256 px grid. The usual failure is over-deep channels when `Kc` is large relative to the time step.

### 2c. Thermal erosion (talus)

Musgrave, Kolb and Mace (1989), "The synthesis and rendering of eroded fractal terrains", proposed hydraulic and thermal erosion to create stream networks and talus slopes [src: https://history.siggraph.org/?p=119354].

Rule [snip: Beneš and Forsbach 2001, https://www.cs.purdue.edu/cgvlab/www/resources/papers/Benes-2001-Parallel_Implementation_of_Terrain_Erosion_Applied_to_the_Surfac.pdf]: material is moved from a cell to its lower neighbours only when the slope exceeds a talus threshold. Threshold: `tan(talus angle)`. Typical angle: 30 to 35 degrees [snip, unnamed developer blog]. Olsen 2004: UNVERIFIED (paper not found; cited as having speed optimisations at acceptable quality).

```
for iter in 1..N:
  for each cell i:
    r = lowest neighbour (8-connected); drop = (h_i - h_r) / dist
    if drop > tan(35 deg) = 0.70:
      move t = 0.5 * (drop - 0.70) * dist * 0.5 ... from i to r   # UNVERIFIED fraction
```

Use a Jacobi (double-buffered) update to avoid order bias. A few hundred iterations is typical (UNVERIFIED for our grid).

### 2d. Stream power (fluvial landscape evolution)

Equation: `dz/dt = U - K * A^m * S^n`, where `z` is elevation, `U` uplift, `A` drainage area, `S` slope, `K` erodibility, and `m` and `n` exponents. Typical `m = 0.5`, `n = 1` (UNVERIFIED for our use).

Cordonnier et al. 2016 (CGF 35(2)) present "the first method in computer graphics that combines uplift and hydraulic erosion" [snip: https://diglib.eg.org/handle/10.1111/cgf12820; HAL https://hal.archives-ouvertes.fr/hal-01262376]. A user paints uplift, the method builds a stream graph, and landform kernels turn it into a DEM. Constants and timing: UNVERIFIED (PDF not read). Schott et al. 2024 (see 2e) is the follow-up to this line of work.

Braun and Willett 2013 ("A very efficient O(n), implicit and parallel method to solve the stream power equation", Geomorphology 180–181, pp. 170–179, DOI 10.1016/j.geomorph.2012.10.008) give the fast implicit solver [snip: Landlab docs, https://landlab.readthedocs.io/en/latest/reference/components/stream_power.html]. Landlab says the method is stable at larger time steps than explicit schemes, but transient results depend on the time step.

**Implementation sketch (buildable in numba or C++):**

```
# Inputs: z (float32, N), U (float32, N), receiver (int32, N), sea mask
z = priority_flood_fill(z)                       # Barnes et al. 2014, DOI 10.1016/j.cageo.2013.04.024
receiver = d8_steepest_descent(z)                # -1 at sea and edges (fixed base level)
order = argsort(-z)                              # highest first (topological)
for it in 1..n_iter:
  A[:] = cell_area
  for i in order: if receiver[i] >= 0: A[receiver[i]] += A[i]   # drainage area
  for i in reversed(order):                      # lowest first: receiver updated before donor
    r = receiver[i]; if r < 0: continue
    L = dist(i, r); F = K * dt * A[i]**m / L
    z[i] = (z[i] + dt*U[i] + F*z[r]) / (1 + F)   # implicit, n = 1
  if it % k == 0: receiver = d8_steepest_descent(priority_flood_fill(z))
```

Priority-Flood: O(n) for integer data and O(n log n) for floating point [src: https://ar5iv.labs.arxiv.org/html/1511.04463]. Reference code is in https://github.com/r-barnes/Barnes2013-Depressions (under 100 lines of C++ [snip]). Practical at our size: 2048² per face is 4.2M cells, and the six-face globe is about 25M cells. Expect O(seconds) per iteration in numba. This is an ESTIMATE, not a benchmark.

Why it makes realistic rivers. Drainage area grows downstream, so `A^m S^n` concentrates incision along the trunk streams, which gives dendritic networks and sharp ridges. The result is large-scale structure that noise alone does not give (Cordonnier 2016 is the reference for this claim, [snip]).

### 2e. Recent approaches

- **Schott et al., "Large-scale Terrain Authoring Through Interactive Erosion Simulation" / "Terrain Amplification using Multi-Scale Erosion"** (ACM TOG 43(4), SIGGRAPH 2024, DOI 10.1145/3658200). Amplifies a low-resolution terrain into a higher-resolution one that is hydrologically consistent, using fast approximations of thermal, stream-power and hillslope erosion at several scales [src: https://research.adobe.com/publication/terrain-amplification-using-multi-scale-erosion]. Code: https://github.com/H-Schott/StreamPowerErosion (MIT, CMake, C/C++ inferred; uplift painted with ctrl+click) [src]. This is the best fit if we want to upsample a coarse fluvial result to the 2 m level. Cost and tile behaviour: UNVERIFIED.
- **Runevision (Rune Skovbo Johansen), "Fast and Gorgeous Erosion Filter"** (blog post and video, 30 March 2026) [snip: https://blog.runevision.com/2026/03/]. It produces branching gullies and ridges without simulating droplets, is per-point (GPU- and chunk-friendly), and can be layered over any height function [snip]. The original is a Shadertoy shader. A commercial add-on uses an adapted version under MPL 2.0 [snip], so check the licence before porting. Godot forum port: https://forum.godotengine.org/t/fast-and-gorgeous-erosion-filter/136436. Its artefacts are the usual filter look (uniform gully spacing) and no true drainage: UNVERIFIED.
- **Nick McDonald, meandering rivers** [src: https://nickmcd.me/2023/12/12/meandering-rivers-in-particle-based-hydraulic-erosion-simulations/]. Per-cell momentum map from droplet velocity (`momentumx_track += volume*speed.x`, exponentially averaged). Suspension scales with discharge: `c_eq = (1 + entrainment*discharge) * hdiff`. Outer banks speed up: `speed += momentumTransfer * dot(norm(fspeed), norm(speed)) / (volume + discharge) * fspeed`. Discharge is normalised with `erf(0.4 * discharge)`. No numeric inertia, capacity or rate values are given. Tested on a 960×960 map for about 2000 steps. Artefacts listed: jagged ridges, noisy holes from constant time steps, and costly fragile flood-fill lakes. The author plans to move lakes to an equilibrium model and handle shores. The "SimpleHydrology" and "soillib" projects were not found in search. UNVERIFIED.

**Which is the best value?** My judgement, not a sourced finding: **stream power with D8 and Priority-Flood for the large structure** (ridges, trunk rivers, dendritic networks), **plus a droplet pass for detail** (valley floors, gullies). This is about 300 lines of numba. The Fewes/Runevision filter is cheaper to code and looks good, but it does not give hydrological consistency for a whole planet.

### 2f. Cube-sphere and seams

- **Stream power and Priority-Flood are global.** A drainage path can cross any of the 12 face seams. Build the receiver graph across the seams from the face-edge index maps and run one global accumulation, or run per face with a halo of 256 cells and reconcile at the seams. Do not run them only per face.
- **Droplets and thermal are local.** Run per face in tiles with a 64 to 128 cell overlap and blend the overlaps with a cosine window. The seam is then invisible as long as the masks are blended too.
- **Memory.** At 2048² per face, one float32 map is 4.2M × 6 × 4 B ≈ 100 MB. Eight maps is about 800 MB. At 4096² it is about 400 MB per map.

Cost summary:

| Method | Good at | Artefacts | Cost, 2048² (estimate) | Seams |
|---|---|---|---|---|
| Droplets (Lague) | Gullies, valley floors, texture | Pits, speckle, needs blur | 2–5M droplets, ~seconds on 8 cores | Tile and blend |
| Virtual pipes | Plausible water flow | Parameter sensitive | Few hundred steps, GPU-friendly | Tile and blend |
| Thermal | Talus under cliffs | Smoothing, over-flattening | Hundreds of iterations, cheap | Tile and blend |
| Stream power | Dendritic networks, ridges, base shape | Needs K and uplift tuning | O(seconds) per iteration | Global graph |
| Fewes filter | Fast gullies and ridges | Uniform look, not hydrological | GPU, very fast | Per-point, no seams |
| Schott 2024 | Upsampling with consistency | Code is C/C++, cost UNVERIFIED | UNVERIFIED | Tile |

---

## 3. What makes terrain look real (geomorphology for artists)

No geomorphology sources were fetched for this section. Facts are UNVERIFIED from general knowledge, and the Wikipedia pages listed are the places to check.

- **Dendritic drainage.** Branching valleys that always lead downhill to a larger valley and to the sea. Stream power gives this for free (2d). [Wikipedia: "Drainage basin", "Stream power law" pages to check]
- **V versus U valleys.** Rivers cut V-shaped valleys. Glaciers cut U-shaped troughs with a flat floor and steep walls. Fjords are glacial troughs flooded by the sea. Procedural trick: a cross-section `h(x) = H * (|x|/W)^p` with `p` around 2 to 3 and a flat floor gives U valleys. Apply it along the existing drainage.
- **Alluvial fans and deltas.** Form where a steep stream meets flat ground and drops its load. Gaea's "Inhibition" control produces fans beside deep channels [src: Gaea Erosion reference]. Procedural trick: deposit where the slope falls below a threshold and the flow is high.
- **Braided rivers and floodplains.** Flat floor, many shallow channels. Produced by deposition on low-slope, high-accumulation cells and by many droplets in the same place.
- **Talus slopes.** Debris piled at the angle of repose under cliffs, roughly 30 to 35 degrees. Thermal erosion (2c) gives this directly. [Musgrave 1989, src]
- **Plateaus, mesas and strata.** Hard rock layers make cliff bands; soft layers make terraces. Procedural trick: terrace a height map with `h' = floor(h/s)*s + s * smoothstep(0.8, 1.0, frac(h/s))` and vary the erodibility `K` by layer index, so the hard bands erode slowly.
- **Ridgelines and cirques.** Ridges are the divides between drainage basins. Cirques are bowls at a glacier head; a bowl carved into a peak with a steep rim gives the same silhouette.
- **Moraines.** Ridges of debris at a glacier's edge. Cheap: a curved ridge at the outer end of a valley, with low amplitude.
- **Shield volcano.** Very low slope cone, about 5 to 10 degrees. Stratovolcano: steeper cone, about 30 degrees, with a summit crater.
- **Caldera with lake.** Trick: a cone with its top cut, `h = min(h, rim)`, then a radial depression inside radius `r_c` filled to a lake level.
- **Lava flows and cinder cones.** Lava: thin flat lobes with a sharp front, a band of low slope. Cinder cone: small, steep, symmetric cone with a crater.
- **Basalt columns.** Hexagonal or polygonal columns from ridged or Voronoi cells, quantised in height. Procedural trick: `F2 - F1` Voronoi, stretched vertically, on cliff faces only.
- **Sea stacks and arches.** Need a 3D mesh, not a heightfield. Place separate meshes where the coast is steep and wave energy is high (fetch proxy).
- **Black sand beaches.** Deposition mask on low coastal slopes, plus a dark basaltic albedo. Cheap.
- **Sandur (outwash plain).** Flat fan in front of a glacier outlet, braided channels. Build as a fan (2d) with a braided deposition mask.
- **Tuyas (table mountains).** Flat-topped volcano built under a glacier. Trick: a steep-sided plateau with a hard capping layer on top.
- **Rift valleys and fissures (Thingvellir-style).** Parallel faults with a down-dropped block between them. Trick: `h -= D * smoothstep(w, 0, |dist_to_fault|)` along two lines, or thresholded ridged noise, `|ridged(p)| < eps` lowers the height.
- **Geothermal areas.** Mostly colour and mask work: pale alteration rock, steam vents, mud pools. Height change is small.

---

## 4. Designing a small world that is memorable

- **Outer Wilds.** The GDC 2020 talk "Curiosity-Driven Exploration: The Design of 'Outer Wilds'" by Alex Beachum and Loan Verneau is listed [snip: https://www.gdconf.com/news/attend-gdc-and-learn-how-outer-wilds-nailed-curiosity-driven-game-design]. Verneau notes there are no markers on screen or minimap, so content must be identifiable on arrival [snip]. The rule "one strong idea per planet" is **UNVERIFIED**.
- **Breath of the Wild, "triangle rule" (CEDEC 2017).** Mountains and hills are shaped as triangles so the player chooses to climb or go around. Triangles also block sightlines to surprise the player [snip: https://nintendoeverything.com/nintendo-on-the-behind-the-scenes-creation-of-zelda-breath-of-the-wild; https://zeldauniverse.net/2017/10/09/a-deep-dive-into-nintendos-development-of-breath-of-the-wild/]. Structures are ordered by visibility and placed to pull the player in different directions [snip]. The original slides were not read.
- **Elden Ring.** Six regions, each ruled by a demigod, with no required order; the open world uses ruins instead of towns [snip: https://blog.playstation.com/2022/01/28/an-interview-with-fromsoftwares-hidetaka-miyazaki/]. Landmark-visibility specifics: UNVERIFIED.
- **Disney "weenies".** A tall visual landmark in each land that pulls the visitor forward. Each land has a distinct vertical landmark [src: https://www.cca.qc.ca/cca.media/files/9013/7952/CUTdisney_pressrelease.pdf].
- **Death Stranding, Shadow of the Colossus.** Not researched for this report: UNVERIFIED.

**Design rules to adopt (author's synthesis, not sourced):**

1. **5 to 8 regions**, each with a unique silhouette, colour and landform. For Dawnhollow: caldera lake, black-sand coast, basalt cliffs, glacier and cirques, sandur plain, rift valley, tuya plateau.
2. **A landmark visible from most of the planet.** Test by ray-casting the heightmap from random points: count how often each landmark is in line of sight. Target at least 60 percent coverage of the land.
3. **Contrast of scale.** One very tall feature (the caldera rim) and many small ones (sea stacks, columns).
4. **Negative space.** Keep about a third of the land empty, so the landmarks read.
5. **Triangle rule for traversal.** Place a steep ridge between two landmarks to force a choice.

---

## 5. Fictional and novel landforms (physically plausible on a small world)

First, a physics check that matters more than any feature (my calculation, uniform density, not sourced):

- Surface gravity `g = (4/3)*pi*G*rho*R`. With rho = 3000 kg/m³ and R = 5000 m, `g ≈ 4.2 mm/s² (≈0.0004 g)`. With rho = 5500 kg/m³ (Earth-like rock), `g ≈ 7.7 mm/s²`.
- Escape speed `sqrt(2*g*R) ≈ 6.5 m/s` (rho = 3000). A jump at 3 m/s rises about 1 km.
- Self-supporting spire height for rock with compressive strength 100 MPa is about `sigma/(rho*g) ≈ 8 km` at rho = 3000. Gravity is not the limit on spire height; the planet's radius is.
- A body this small cannot hold a dense atmosphere or a stable sea. The sea and the sky need a fictional justification. This is a design decision, not a physics fact.

| Feature | Plausibility | How it could look |
|---|---|---|
| Extreme tides and tidal flats | Plausible in principle: a large moon close to a small body makes a big tidal range. The magnitude is SPECULATION. | Wide glistening flats at low tide, a shoreline that moves every few hours. |
| Tidal bore | Plausible in a funnel-shaped estuary with a big range. SPECULATION for this planet. | A wall of water running up a narrow inlet. |
| Tidally heated volcanism | Real mechanism (flexing from an eccentric orbit), as on Io. Whether a 5 km body can keep heat is SPECULATION. | Steam columns and lava along the rift and the caldera. |
| Tall steep spires and overhangs | Gravity does not limit height (see above). Overhangs are held by rock strength. Plausible. | Needle spires, arches, dramatic cantilevers. |
| Giant crystal or columnar formations | Columns are real (basalt). Giant crystal is SPECULATION. | Hexagonal crystal columns the size of towers, pale and translucent. |
| Impact structures | Plausible: a small body collects impacts, and a complex crater has a central peak and a ring. | A circular lake with a central island in a ring of hills. |
| Aurora | Needs a magnetosphere. A weak field around a small body is SPECULATION. | Green curtains over the poles. |
| Bioluminescence | SPECULATION (fiction). | Glowing shoreline at night. |
| Ring-shaped atoll | Plausible: volcanic rims that subside form ring islands. Real geology. | A ring island around a lagoon on the sea. |

---

## 6. From height map to beauty

**Map roles.**

| Map | Use at render time | Resolution need |
|---|---|---|
| Flow (accumulation) | Rivers where accumulation exceeds a threshold, wet ground, dense vegetation along banks. Do not use it raw for texture: Gaea warns it looks "CG" [src]. | 2–4 m, 16-bit |
| Deposition | Sand, soil, flat valley floors; black sand where deposition is high near the coast. | 4 m |
| Wear | Bare rock on cut faces and streams. | 4 m |
| Slope and curvature | Snow where slope is low and altitude high; scree and talus at the base of steep slopes; moss on moderate slopes near water. GRW uses slope, height, flow, density, deposition and curvature rules [src: 80.lv]. | 2 m for curvature |
| Altitude and aspect | Colour by height band; north-facing slopes stay snowy longer (in the northern hemisphere; flip for ours if the planet has a different tilt). | 8 m is enough |
| Macro variation | Low-frequency noise at 1 to 5 km to break repetition. | 16 m |

**Resolution advice.** Heights at 2 m per sample are enough for erosion features (Unity suggests 1025 or more for detailed erosion [snip]). Masks can be 2–4 m. Macro variation is cheap at 16 m. The bake should be 4K per face or streamed as virtual textures.

---

## Recommended pipeline (5 km planet)

1. **Base shape.** Cube-sphere, 6 faces, 2048² each (about 3.8 m per sample on the sphere: 7854 m / 2048). Use the existing domain-warped noise for the coarse shape. Paint an uplift map `U` from 6 to 8 hand-placed ridge curves and the rift. Estimate: seconds.
2. **Large-scale fluvial erosion.** Global graph across the 12 seams. Priority-Flood, D8, stream power with `m = 0.5`, `n = 1`, `dt` chosen so the largest valley incises about 200 to 400 m (UNVERIFIED target). Run 200 to 500 iterations. Start at 1024² to tune, then run at 2048². Estimate: 10 to 30 minutes for 500 iterations at 25M cells (ESTIMATE). Tune `K` on the 1024 run first.
3. **Thermal erosion.** Jacobi update, talus tan(35 degrees) = 0.70, transfer fraction 0.5, 100 to 200 iterations. Estimate: under 2 minutes.
4. **Droplet detail.** 2 to 5M droplets at 2048² (about 1 to 2 droplets per pixel, matching the Lague density), `erosionRadius` scaled to about 6 to 8 cells (scaled from 3 at 255²; UNVERIFIED), inertia 0.05, capacity factor 4, erode 0.3, deposit 0.3, evaporate 0.01, gravity 4, lifetime 30. Tile per face with blending. Estimate: under a minute on 8 cores.
5. **Masks.** Flow (accumulation thresholded), wear (sum of erosion), deposition, slope, curvature, aspect, altitude, snow accumulation estimate (height over snowline and slope under 35 degrees). Write float16 or 16-bit at 2 to 4 m.
6. **Landmarks.** Stamp the designed features (caldera, rift, tuya, sea stacks, impact ring) after erosion, then run 20 to 50 local thermal and droplet iterations on the stamped area so the features look eroded and not pasted on. Estimate: minutes.
7. **Validate by reading maps** (not by rendering). Check that every valley reaches the sea, every landmark has line of sight, and no seam shows in the flow map.

**Total run-time estimate:** roughly 15 to 45 minutes on the Mac for a full planet, dominated by stream power. ESTIMATE only; benchmark step 2 first.

---

## Ten landform ideas, ranked by visual impact per unit of work

1. **Caldera lake with a ring of islands.** Cone with its top cut, radial depression, lake level. Cheap, very readable from orbit and from the ground.
2. **Strata cliffs and tuya mesa.** Terracing and hard-layer erodibility. Cheap, strong silhouette.
3. **Fjords.** U-shaped trough (2d or section 3) plus sea level. Cheap once rivers exist.
4. **Rift valley with fissures.** Two faults and a down-dropped block, ridged-noise cracks. Cheap, unique look.
5. **Black sand beaches.** Deposition mask on low coastal slopes plus dark albedo. Very cheap.
6. **Basalt column cliffs.** Voronoi columns on steep faces only. Medium work, high texture payoff.
7. **Sandur outwash plain.** Fan and braided deposition below a glacier. Medium work.
8. **Sea stacks and arches.** Separate meshes on the coast. Medium to high work, very memorable.
9. **Impact crater lake.** Complex crater function. Medium work, cheap for ring atolls.
10. **Tidal flats and bores.** Needs a tide system in game and shader work. High work, highest novelty, and the physics needs a decision (section 5).
