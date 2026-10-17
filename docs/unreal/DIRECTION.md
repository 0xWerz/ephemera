# Ephemera in Unreal: direction

Decided with the user on 2026-10-12. This file is the brief every worker reads first.

## Decisions

| Topic | Decision |
|---|---|
| Engine | Unreal Engine 5.8.3, C++ module `Ephemera` in `unreal/` |
| Target | Windows PC, RTX 3060, first. The Mac (M4 Pro) is the authoring machine and runs a lighter preset |
| Look | Photorealistic. The stylised Godot meshes and materials are dropped, not converted |
| Assets | Fab / Megascans, free items only. The user claims them with their Epic account |
| Scope | One vertical slice first: Dawnhollow's village and Launch Reed, the Seedling, take-off, flight to Hush |
| Scale | Planets about 5x larger (Dawnhollow radius about 500 m). Daylings stay, redesigned as believable small furred creatures |
| Feel | Player and ship numbers come from `docs/research/ow_mechanics.md` and `ow_reference.md` |

Outer Wilds is Mobius Digital's IP. This is a non-commercial fan project: research numbers only, never the
original's assets or code.

## What carries over from Godot, and what does not

- Carries over: story, dialogue, the Inheritance, day-loop rules, endings, orbits, time dilation, world
  layout as a design reference (`unreal/Content/Data/story.json`, `world.json`).
- Does not carry over: meshes, materials, shaders, the runtime "build the world from JSON" approach.

## Rendering targets (PC, RTX 3060, 1440p output)

- Nanite for all static geometry, including foliage and terrain.
- Lumen global illumination and reflections with hardware ray tracing; software Lumen on the Mac.
- Virtual shadow maps.
- Temporal Super Resolution, rendering about 67% and upscaling to 1440p. DLSS as an option later.
- Sky Atmosphere per planet, volumetric clouds where a world has weather, exponential height fog,
  auto exposure with physical light units.
- Substrate materials where they earn their cost (fur, wet surfaces, glass).
- Budget: 60 fps at 1440p output on the 3060 in the village; never below 45.

## Architecture rules

- Worlds are authored content (levels, Level Instances, PCG graphs, Data Assets), not spawned from JSON
  at `BeginPlay`.
- Planets are actors moved by one solar-system subsystem; the player and ship ride a reference frame.
  Large World Coordinates are on; do not rebase the origin by hand.
- Gameplay values live in Data Assets or `UDeveloperSettings`, never as literals in `.cpp` files.
- Input is Enhanced Input with mapping contexts for on-foot, ship and UI; gamepad is first-class.
- UI is UMG with Common UI, not Canvas text.
- One class, one job. No file over about 400 lines without a reason.

## Working rules for agents

- Never open a visible window or steal focus on the Mac. Unreal runs headless (`-nullrhi`, `-unattended`,
  commandlets) or through the existing sandboxed launchers in `unreal/Tools/`.
- Write nothing to the internal disk. Scratch goes in `/Volumes/External/code/tests/outerwilds/.scratch/`.
- One heavy job at a time (`nice -n 15`). An Unreal build or cook counts as heavy.
- Report facts with file:line. Say plainly what was not verified.

## Core architecture: the reference frame

Decided 2026-10-12. This replaces the first port's "teleport every planet each substep and carry the
player by hand" approach, which invalidates Lumen's surface cache and the virtual shadow map cache
every frame and breaks standard character movement.

- **The reference body is static.** `UEphemeraFrameSubsystem` evaluates every body's pose on rails
  (Kepler orbit + spin, from `SimulationMath.h`) in an inertial frame, then re-expresses all of them
  relative to the *reference body*: the body whose sphere of influence the player is in. That body
  sits at the world origin and never moves or rotates. The sun, sky and other bodies move instead.
- **In open space** the reference is the star's inertial frame; planets are movable actors on rails.
- **Hand-over.** Crossing a sphere of influence changes the reference in one frame. Every
  `IEphemeraFrameRider` (player, ship, scout) has its pose and velocity re-expressed so nothing jumps
  relative to the camera. Moons never claim space inside their parent's shell (ground + air).
- **The rotating frame is honest.** Inside a planet's frame, things in flight get the Coriolis and
  centrifugal terms, so orbits and long jumps behave as they would seen from the ground.
- **Gravity** is a subsystem query (`GravityAt`). Characters use `SetGravityDirection`; the ship
  integrates it directly. Fields blend across the moon/parent boundary.
- **Sun and sky.** One directional light follows the star's direction in the current frame. The real
  `SkyAtmosphere` belongs to the reference planet (planet-centre transform mode); distant planets get a
  cheap atmosphere shell material.

## Scale

| | Godot | Unreal |
|---|---|---|
| Dawnhollow radius | 100 m | 500 m |
| Hush radius / orbit | 30 m / 255 m | 150 m / 1.6 km |
| Dawnhollow orbit | 1.3 km | 9 km |
| Star radius | 105 m | 1.5 km |
| Dayling eye height | 1.2 m | 1.2 m |

Planet sizes scale 5x; orbital distances scale about 7x so flights take tens of seconds, as in the original.

## Planets are built, not sculpted

- `UEphemeraPlanetDefinition` (Data Asset): radius, gravity, sphere of influence, orbit, spin,
  atmosphere, terrain layers (noise + authored features such as craters and plateaus), biome rules,
  scatter rules.
- An editor commandlet (`EphemeraPlanetBuilder`) turns a definition into content: a cube-sphere
  terrain cut into Nanite static-mesh chunks with complex collision, biome weights in vertex colour,
  and instanced scatter (rocks, trees, plants) written into the level. Re-running it regenerates the
  planet deterministically. Hand-placed set pieces (the village, the Launch Reed) are Level Instances
  anchored to the surface.
- Terrain material: world-aligned (triplanar) blend of scanned surfaces by slope, height and biome,
  with Nanite displacement for centimetre detail.

## Milestones for the vertical slice

1. **M1 – Standing on Dawnhollow.** Terrain, materials, atmosphere, sun, scatter, the new player
   controller (Outer Wilds numbers, charged jump, gamepad). One screenshot the user can judge.
2. **M2 – Flight.** The Seedling, take-off, the frame hand-over, Hush.
3. **M3 – The village and Launch Reed** as photoreal set pieces; first Dayling.
4. **M4 – Windows.** Move to the PC, hardware ray tracing, DLSS, performance pass to the 60 fps budget.

## Asset sources

- Poly Haven (CC0): scanned surfaces, rocks, plants, props. Fetched by `unreal/Tools/fetch_polyhaven.py`
  into `unreal/SourceAssets/polyhaven/` with a manifest recording the licence of every file.
- Fab / Megascans free items: claimed by the user when a specific list is ready (trees especially).
- Our own models (buildings, the Seedling, Daylings) are made in Blender with scanned materials.

## Art target (user, 2026-10-12)

"AAAA realism... the best space game ever, Interstellar basically." Concretely:
- Physically grounded light: one hard sun, real falloff, deep black shadows in vacuum, exposure that
  adapts like a camera (stars are invisible next to a sunlit surface and fill the sky in shadow).
- Real scale cues: curvature, atmosphere limb, long shadows, haze; nothing reads as a miniature.
- Restrained, filmic lens: subtle bloom and flare from the sun only, mild grain, no stylised colour.
- Space is silent and stark; planets are tactile (wet rock, dust, bark, fog), shot like location plates.
- Physics that reads as true: inertia, orbits, rotating-frame effects, time dilation with a visible cause.

## Vision reset (user, 2026-10-13)

After seeing the first poster renders the user clarified the target. These replace the earlier
scale and landscape choices.

- **A super-realistic space game, fictional like Outer Wilds.** Realistic light, materials and
  atmosphere on invented worlds. Only the current bodies (the star, Dawnhollow, Hush) for now; get
  these right, add worlds later.
- **Planets about 4-6 km in radius** (was 500 m). From orbit you see landforms, weather and colour,
  not individual trees. At 500 m real-size trees stuck out of the globe like hair.
- **A rich starfield that is always visible in space**, even beside a bright planet or the sun.
  Daylight inside an atmosphere still hides it. (Physically correct exposure hid the stars; the user
  did not want that.)
- **Dawnhollow is Death Stranding / Iceland:** moss-covered lava fields, dark basalt outcrops and
  cliffs, black sand, shallow streams, low scrub, mist. Few or no tall trees.
- **Space views first:** the planet's surface colour and relief from orbit, clouds, the atmosphere's
  limb, the sun, the moon, the stars. Ground-level terrain quality follows.
- Reviews of renders: rocks must belong to the land (one geology, bedded in, scree around them);
  terrain needs real relief, not a photo on a plane; alpha-cut foliage reads blurry; grass must not
  sparkle per blade; no bloom rings or blown-out limbs.
- Open every finished image for the user with `open <file>` as soon as it is ready.

### Scale (replaces the earlier table)

| | Radius | Orbit |
|---|---|---|
| Aubade (star) | 15 km | centre |
| Dawnhollow | 5 km | 400 km from the star (the star is a 4.3 degree disc) |
| Hush (moon) | 1.4 km | 24 km from Dawnhollow |

Terrain at this size cannot be a uniform 1 m grid (hundreds of millions of vertices). The visible
terrain is a 4 m grid in Nanite chunks with fine relief from material displacement; collision is
generated at run time around the player from the same height function.

## Dawnhollow's surface (2026-10-13)

The user flew over the noise-made terrain and rejected it: nothing worth a picture, low-poly,
no diversity, not pleasant to look at. Their steer: research how great terrains are made, then
add our own novelty. Asked which landforms, they said to choose what is cool and fits, and to add
many. Novelty: Hush's giant tides, plus anything else creative. Relief: realistic proportions.
Research: `docs/research/terrain_design_and_erosion.md`, `ground_level_landscape.md`,
`planet_surface_from_altitude.md`.

**Method.** Terrain stops being a pure noise function. A designed base shape is eroded by
simulation offline (water carving valleys that join and run to the sea, scree settling under
cliffs), and the result is baked: heights plus maps of where water flows, where sediment settles
and where rock is worn bare. The game reads heights from the bake; the material and the scatter
read the maps. Fine detail under the bake's resolution comes from displacement and scanned rock.

**Regions** (each its own silhouette and colour, a landmark visible from most of the planet):

| Region | What it is |
|---|---|
| The Hollow | The planet's namesake: a caldera about 2 km across with steep inner walls and a lake, a small cone island in it. The village shelf (the Cradle) sits on its inner shore. |
| The Fjord Coast | Glacier-cut valleys running out from the caldera massif into deep narrow inlets; cliffs, sea stacks, black-sand coves. |
| The Ice Cap and the Sandur | A glacier on the high plateau; braided rivers across a wide black outwash plain to the sea. |
| The Rift | A long fissure valley: steam vents, hot pools ringed in ochre and turquoise, fresh black lava, cinder cones. |
| The Moss Highlands | Old lava under deep green moss, mounded, with streams in the folds. |
| The Table Lands | Flat-topped mountains and stepped cliffs where hard lava layers stand out. |
| The Strand | The wide shallow shelf where the tide runs: flats, channels and causeways that flood and drain. |

**Novelty.**
- *Hush's tides.* The moon is close and large: the sea bulges toward and away from it by several
  metres and the bulge travels round the planet as Hush orbits. Flats, causeways and sea caves
  appear and drown on a clock the player can read in the sky.
- *Aurora* over the poles (a warm active star, a small world): a ring of light on the night side,
  seen from orbit and from the ground.
- Further ideas to try once the land is right: moss that glows faintly at dusk; tide-cut arches.

**Relief** stays in realistic proportion: slopes at natural angles, cliffs where rock layers or
ice made them, peaks to about 450 m.

### How the ground is drawn (built 2026-10-13)

The user's reference for ground-level quality is Death Stranding: real rock, dense ground, thick
air, moving water, muted colour. What now stands in for each:

- **Rock as geometry.** The terrain mesh has a point every 8 m. The planet material moves it
  outward by up to 6.5 m with Nanite tessellation: lava beds of uneven thickness standing out as
  ledges, the massive ones broken by weathered joints into blocks, the scans' own height maps on
  top, and rolling, hummocked "micro relief" on open ground. The same text is compiled into the
  displacement stage and the shading stage (`PLANET_RELIEF` in `Tools/build_slice.py`), and the
  micro relief is repeated in C++ (`FEphemeraTerrain::MicroReliefM`) so that grass, stones, the
  player and the camera stand on the displaced ground.
- **Two speeds.** On the Mac that geometry is too slow to play with (2 to 9 fps against 20), so
  `Config/Mac/MacEngine.ini` turns tessellation off and the relief is drawn as shading only.
  Photographs (`unreal.py shot` / `shots`) always switch it on. The PC target should run it;
  that is unmeasured.
- **Weather.** `unreal.py --weather overcast | fronts | broken` (play, shot, shots). Ground
  photographs use `overcast`. Clouds are still off unless a weather is named: seen from orbit they
  are not yet good enough to be the default (thinned with distance they are soft but shapeless).
- **Sea.** Single-layer water near the camera (clear shallows, dark depths), the older painted sea
  beyond a few kilometres; waves from spectrum-made slope maps (`Tools/terrain/water.py`).
- **Streams.** A water sheet along the baked flow channels (`BuildStreams`), beds of gravel or
  rocks. They are subtle: the erosion cut V-shaped gullies, not valley floors. Rivers worth
  photographing need flat-floored valleys designed into the bake.
- **Ground cover.** Dense turf, meadow grass, pale sedge and red-brown heath near the camera;
  boulders, rocks and scree out to 650 m.

- **Standing on it.** The baked terrain has no collision meshes; an invisible 96 m sheet of the
  same ground is rebuilt around the player as they move (`UEphemeraGroundPatchComponent`). It
  carries the micro relief but not the rock relief, so on cliffs the feet sink into the blocks.

Still owed, in rough order of how much they would add: valley floors with real rivers, the glacier
(crevasses, blue ice, moraine), a subject in the frame (the village, the ship), clouds that hold up
from orbit so a weather can be the default, the PC performance pass.

## The look (rebuilt 2026-10-14, after "glossy, and kind of flat in lights")

The user set Alan Wake 2 as the bar ("their forests, graphics, lights, and how the game generally is processed and looks
are way better than ours") and asked for its methods to be studied and recalibrated for this game. Four studies:
`docs/research/look_audit_ours.md` (what our own code did wrong, line by line), `aw2_rendering.md` (how Northlight builds
a frame and the Unreal equivalents), `aw2_forests_and_environment.md`, and `look_gap_vs_aw2.md` (our frames measured
against press stills of Alan Wake 2 and Death Stranding, with targets). Only published material was used. The rules that
came out of it, each of which was a measured fault before:

- **Ground is matt and as light as it really is.** Albedo is measured albedo (dry basalt 5%, moss and turf 5 to 8%); set
  darker, the sky's reflection outweighs the ground's own colour and the land looks varnished. Moss and lichened lava have a
  roughness floor (0.92, 0.84) whatever a scan's roughness map says; each kind of ground has its own specular, lowered again
  in its pits. "Glossy" was this, and a sky light multiplied by 2.6.
- **Wet is a place, not a state.** Rock is dark and has a dull sheen only in seeps and beside streams, sand only in the
  swash. Wet means darker first.
- **No blade of grass is lighter than the turf it grows from.** A blade paler than its ground is a bright line and ten
  thousand are a glare (measured: 5 to 18% of ground pixels standing 12 L* above their surround, against 1 to 2% in the
  references; now 1 to 3%). Blades are lit mostly as the ground under them is (their normals bent two thirds toward
  "up"), are matt, and keep most of their root shading for the light to do.
- **Shade is shade.** The sky light is the sky's own (scale 1.2), none of it leaks through rock, and the sun's shadows are
  drawn from a disc a third the star's width so that a stone's shadow survives a stride from the stone.
- **Mist you can see into.** `AEphemeraSkyRig` has the engine's fog cells switched on by a height-fog component with next
  to no fog of its own (its fog thins along the world's Z axis, useless on a round world, and without a cut-off it blacked
  out half the sky) and filled by `MistVolume`, a box that goes with the camera whose material (`M_Mist`) gives the air's
  thickness from height over the sea, in banks. The sun's shadows, the clouds' shadows and the lamp's beam all show in it.
  Each weather has its own amount (`fog=`). It cost about one frame a second on the RTX 3060.
- **The camera meters the whole frame under a sky** (35 to 90% of the histogram, bias +0.3; in space still the bright half
  only), and the curve has a firm toe. Lifting the shadows to "protect" them was tried and turns the picture to milk.
- **Every number of the grade can be tried without a build**: `unreal.py shots --sky "bias=0.3,contrast=1.08,fog=0.5,..."`
  (keys in `AEphemeraSkyRig::ApplyGrade` and `ApplyTuning`). `.scratch/lookgap/score.py` scores a frame against the
  measured targets; `.scratch/look/` has the seven standing views, before and after.
- **Haze is faint.** The first mist (0.5 /km, with the engine's full fade of distant land) drew the user's first words on
  seeing it: "grey ish washed out especially the mountains". Mist is now 0.10 to 0.28 /km by weather, a quarter of that by
  night, and distant land fades at 0.38 of the engine's rate (`airdepth`): this world's whole sky is packed into a shell
  1.4 km deep, so a cliff two kilometres off has as much air in front of it as the horizon sky has altogether.
- **A game begins at mid-morning** (`StartTimeSeconds` 180), not at first light, when the land is black under a bright sky.
- **Night is dark** (user: "way bright and not a dark vibe at all ... clouds appear weirdly super bright feels even
  broken"; the moon "like the fucking sun but white"). Three things made it bright, none of them the night lights:
  the engine lights clouds with the sun on the night side (the planet does not shadow them in any of its modes, so
  `M_Clouds` now takes its own brightness down where the line to the sun passes through the world); the real-time sky
  capture holds the neighbour world in full sun, some thirty lux of it (the sky light is cut to a fortieth by night);
  and the meter was free to open to EV -2 (by night it stops at -1, reads the bright half of the frame and opens a third
  of a stop a second). The moon seen from the night is drawn by its own light, held to what the exposure in use can show
  (`DiscCap` in `M_Planet_Hush`), so it has a face. Night colour is drained and cooled. At dusk and dawn the shade is
  lifted so the land is not a cut-out under a bright sky.
- **Clouds are mist close to** (user: "they give the vibe that they are blocks"): inside 520 m they thin toward a sixth
  and fray at a finer grain; their edges are thinner than their hearts and their bases uneven.
- **Lighting must not jump.** The sun's cloud-shadow map is resized in few large steps (each resize remakes every
  shadow), and Lumen's reach is the engine's own until a longer one has been tested in motion.
- The cases the user meets first are rendered before a build goes out: `.scratch/look/list2.txt` (the start, dawn, day
  views) and `list_night.txt` (deep night on the ground, the moon, low orbit over the night side, dusk, in and under cloud).
- Look work is done on the PC, in the gaps when nobody is playing there (user, 2026-10-14).

### Life and staging (built 2026-10-14, user: "do all of those 6")

The grade was not the gap; the world was still and empty. Six things, each a rule from now on:

- **Things move.** Grass bends to a wind that comes over the land in gusts (`GRASS_WIND_CODE`; only within 40 m, where it
  can be seen). The air holds motes: a cloud of 1800 small squares that goes 8 m each way with the eye, whose material
  (`M_Motes`) keeps each one where it is in the world, carries it on the grass's wind and turns it to the camera. They are
  lit as the air is lit (the translucency lighting volume). Most are dust, kept no brighter than the sky (a white speck on
  blue is a fault on the lens), so it shows against shade under a low sun and in the lamp's beam at night; three in a
  hundred are seed fluff, pale tufts the wind carries three times as fast, which is what is seen crossing a sunlit
  plain. A mote is never drawn under three pixels wide (single pixels were eaten by the anti-aliasing), and all are let
  go above a run's speed. `motes=` in `-EphemeraSky` sets how strongly they show.
- **The jets are seen.** A blue-white glow under the body (a point light), a flame under each side of the pack (engine
  cones, `M_JetFlame`, the same brightness on screen day and night), and the ground's own dust thrown out by a landing and
  blown out by the jets within five metres of the ground (`M_Puff`: 48 tufts, in the colour of what is underfoot: snow
  white, the sand dark, spray from the sea). Heat shimmer is not built.
- **The day has a colour script**: warm light and cool shade under a low sun, rose at twilight, drained and blue by night
  (`UpdateSky`; `script=` scales it).
- **The night sky has a galaxy**: a band with a warm core, dust lanes and gas clouds in the star material.
- **Things stand in the middle distance** (`stand_rocks`, placed once for the whole world): stacks of the sand's own basalt
  in the shallows of every coast, wet at the foot and turfed on top; standing rocks on the plains; low walls of outcrop on
  the gentler slopes. A rock the size of a house is drawn with its stone at three sizes laid over each other so no repeat
  shows, and a stack's stone is darker than the loose stones' (in full sun against black sand the same stone read as
  concrete). A ruin and the ship are still to come: they need models, not rocks stood on end.
- **The water's edge moves**: the surf runs up and draws back. Foam is whole only at the very edge and is eaten into
  lace and then flecks outward (laid on evenly, on a flat strand, it was a spotted white sheet a hundred metres wide).
  Sand the sea is on now, or has just left, is a mirror: a film over it holds the sky and the stacks. Where that is
  follows the tide (five metres here), which the game tells every ground material at once through `MPC_World`'s
  `MoonTide` (`AEphemeraBody::SetTide`).

### After the first Windows test of 0.5.0 (2026-10-14, about 05:00)

The user's verdicts, in their words: 25 fps where Alan Wake 2 and The Last of Us run faster and look better; "sea/water
looks like shit, its movement is not realistic at all"; "still some flickering in the floor"; footsteps "not diverse and
real ... it sounds like minecraft"; grass "moves weirdly, not linked to the surface"; "walking into the sea it literally
just makes me go under"; under the sea "empty"; cloud shadows "look weird"; night "awful ... sky is pixelated ... like a
color grading swipe solid layer ... while the sea looks super bright". Every fix below came from a sourced research brief
in `docs/research/` (`performance_ue5_rtx3060.md`, `ocean_and_underwater.md`, `night_cloudshadows_stability.md`,
`footsteps_and_space_audio.md`), as the user now requires for anything not obvious.

- **Frame rate.** Most of the gap to those games is resolution: on a 3060 they render at 540-960p and upscale. Our own
  costs went too: grass is out of the ray-tracing scene, reflections use Lumen's lighting cache (no hit lighting), the
  "Highest" tier's TSR history is 100% not 200%, foliage is not traced for reflections, sun shadows and the fog grid
  are a step coarser (`Config/DefaultScalability.ini`), the star dome is not drawn by day, the lamp reaches 60 m.
  The menu's render resolution is now Native / Quality 77% / Balanced 67% / Performance 58% (TSR rebuilds the rest).
- **Night.** Clouds in the planet's shadow are lit by the night's own light (moon + sky, about 2e-5 of the sun), not
  10 stops over white; the eye closes at most 1 stop for bright things; the blue-grey belongs to the shade only;
  about four thousand stars, each at least a pixel wide (not 15,000 blinking single pixels), a fainter galaxy with
  grain; less grain by night. And the night has its own beauty: the surf glows faint blue (bioluminescent plankton).
- **Cloud shadows.** Three times the map's texels, twice the steps, never smaller than the cloud shell, a little light in
  their cores, and independent of the camera (the near-thinning applies only to what the eye sees).
- **Ground flicker.** Normal-map detail is baked into roughness mips (Toksvig) for every scanned surface; TSR's flicker
  period and thin-geometry detection are on; wet sand is a mirror only within 20 m.
- **Grass.** Blades bend about their roots and keep their length; a tuft flutters as one body; the wind fades out before
  the distance where the engine stops moving grass.
- **The sea.** Its waves are a looping, dispersion-correct simulation (each wave at its own speed), not pictures slid
  along; it has a body colour (coastal-shelf optics instead of near-black). The body floats (Outer Wilds' numbers:
  1.1x buoyancy, water drag, jets work under water without fuel), swims along the look, wades at half pace, splashes.
  Under it: light lost with depth, the water's own murk and glow, Snell's window overhead, wave-focused caustics,
  muffled sound. Displaced waves on a camera-following grid are being built next.
- **Footsteps.** Layered AAA foley (`Tools/audio/steps.py`, `foley.py`; runtime `UEphemeraPlayerAudio::PlayStep`): five
  speed tiers with 20 dB between creep and sprint, left/right, impact + roll + grit + cloth + gear, surface blending,
  start/settle/turn/skid, landing tiers, breath, wading and underwater beds. Every file is cut at one reference level
  (-25 dB) and the game alone sets each layer's level. 64 voices.
- **Lighting: Lumen stays, on the ray-tracing hardware** (decided 2026-10-14 by A/B in the packaged game, three modes,
  same views, frame pairs for flicker). By day all three look the same and, with grass out of the ray-tracing scene,
  hardware rays cost nothing over software tracing (40 vs 39 fps at 1080p); classic raster lighting (no live bounce,
  DFAO sky shadowing) gave 44 fps but lit the night like day and flickered more. Cheaper cloud tracing
  (`r.VolumetricRenderTarget.Mode=0`) was rejected: grainy cloud edges. `-EphemeraLighting=rt|traced|classic` and
  `-EphemeraShotPair=<frames>` remain for later comparisons (`.scratch/look/flicker.py`).
- **Render resolution:** native by default. (For an hour it defaulted to 77% on 1440p screens, 39 fps against 30; the
  owner found far mountains soft and blocky at 77%. Settings Version 4 put it back; Quality 77% stays in the menu.)
- **After the second Windows test (2026-10-14, about 10:00):** black screen under water was invalid numbers: the
  underwater view's sky pixels (infinite position) and the jet glow's day boost (2.8 million lumens a metre from the
  seabed) both overflowed and poisoned the exposure. The view now takes its direction from the camera and refuses
  out-of-range values; the jet glow is at most 8x brighter by day and weaker under water; the water's glow is a real
  brightness lit by the actual sun, sky and moon. Shore: foam is soft froth, the last hand's depth fades into wet sand,
  waves die away in the last 80 cm of depth (their crests rose out of the beach as dark diamonds). Jet sound is a gas
  burner's roar with its pitch held within 2% (the old hum, slid 24% in pitch, read as a lightsaber); footsteps lost
  their crinkle (dry-grass steps, reed and corn-leaf rustle, a nylon jacket).
- **The near sea** is displaced geometry: two looping FFT bands (`Tools/terrain/ocean_fft.py`) on a camera-following grid
  (`UEphemeraSeaSurfaceComponent`), joined to the far sphere by a masked hole; `AEphemeraBody::SeaSurfaceM` gives the
  same height on the CPU (floating, the underwater switch). `-EphemeraNoSeaGrid` restores the old sea. Whitecaps are off
  in this light breeze (`WhitecapGain`). Under water the view is an art-directed teal-to-deep-blue (physically decayed
  light went black and the exposure burned everything else white); the drawn surface seen from below is treated as the
  surface (window or mirror), not lit as seabed.

## Playing it (built 2026-10-13, after the first sessions on the PC)

- **Loading.** The game starts behind a loading screen with a bar and does not lift it until the
  world is ready to be looked at, in stages (`AEphemeraPlayer::UpdateLoading`): engine done
  compiling and the player placed; everything that grows within sight grown; one slow full turn
  made while hidden, so every direction's textures, fine geometry and shader pipelines are asked
  for before anyone can watch them arrive; then nothing still streaming for two seconds.
- **Free flight is a camera, not a body** (V). No world's gravity turns it or pulls it. Two things
  keep it usable on round, moving worlds, and neither is gravity:
  - it *rides* with whatever body is nearest (`RideNearestBody`): each time the frame moves the
    bodies, the camera is moved exactly as the nearest one was. Only the frame's own world is at
    rest; seen from Dawnhollow's turning ground the star crosses at 1.9 km/s, and a camera left at
    rest in the frame watched what it had flown to slide away, or was run down by it;
  - near a surface it turns by exactly the angle its own movement took it round the world
    (`FollowTheCurve`), so the horizon stays where it was instead of tipping a degree every 90 m.
  It is never stood upright on arrival. Press V again and the body rights itself as it falls.
  The star cannot be entered.
- **Night is lit** (`AEphemeraSkyRig::UpdateNight`). Night at the eye means the star is hidden by
  the bulk of the nearest world: sunset on the ground, the world's shadow in orbit. Then:
  - *Shine*: the sunlight the neighbouring world throws back (Hush over Dawnhollow, Dawnhollow
    over Hush), from its direction, by its phase, with shadows near the ground. Worked out
    physically it would be daylight to an adapted eye, so it is compressed to at most 2.0 lux.
    The neighbour's own shadow casting is off while its light casts shadows (it would otherwise
    stand in its own light);
  - *NightFill*: starlight and airglow as 0.45 lux from overhead, so a moonless night is dark and
    still a place;
  - in a world's shadow in space the camera's fixed daylight exposure gives way to the eye's.
- **Shadows reach through the planet.** `r.Shadow.Virtual.Clipmap.FirstLevel=8, ZRangeScale=5000`:
  with the engine's defaults the finest shadow squares see 640 m along the light, and at midnight
  the grass at your feet stood in full sunlight coming up through the ground.
- **The lamp** (F) is 8,000 lumens: it owns the night and shows under cloud; in full sun it is
  lost, as a real one is. Keys that change a mode say so at the foot of the screen.
- **Installer.** `unreal.py package` then `unreal.py installer` (Inno Setup 6 on the PC) gives
  `Builds/Installer/Ephemera-Setup-<version>.exe`: per-user install, no administrator prompt.

## Controls, sound and score (built 2026-10-13, after "it feels rigid and delayed")

The first controller copied Outer Wilds to the number and the user found it stiff, slow to answer and unpleasant. The
replacement and the reasons for every number are in `docs/research/controls_feel.md`. What must stay true:

- **Every key does something on the frame it goes down.** The jump is on the press (hold it and the jets take over after
  0.15 s; let go early and it is a hop). Nothing waits for a release, a charge or an animation.
- **Latency is a feature.** Vertical sync is off by default, the game thread no longer runs a frame ahead
  (`r.OneFrameThreadLag=0`, `r.GTSyncType=1`), and `FEphemeraFramePacer` makes the game wait for the graphics card instead of
  queueing frames behind it ("Low latency" in the menu). An average frame rate says nothing about this: the log's
  `Ephemera frames:` line every ten seconds (usual, 99 in 100, longest, stutters) is what to read after a session.
- **The jets lift.** 17 m/s² against Dawnhollow's 10.5; a 7 s tank that refills in 1.6 s on the ground; a thin line at the
  foot of the screen shows it only while it is not full.
- **The view has a body under it**, all scaled by one setting ("camera motion", 0 for none): landing dip, stride sway in
  step with the footsteps, a lean into a strafe, a wider view at speed.
- **Only the player's hand turns the camera** (user, 2026-10-13: the view "flips and steers by itself", "gives me
  nausea"). In free flight nothing rotates the view: not the ground curving away beneath, not a world's pull, not the
  frame the player rides. **G** levels the horizon when asked. In the air no world swings the body round; it is set
  upright only on landing and for a third of a second on leaving free flight.
- **Tests cannot press keys**, so `unreal.py shot --drive "w:2,w+shift:2,space:0.1,:1"` holds them by script and the log
  says ten times a second what the body did (speed, height, fuel, the eye). That is how feel is checked without hands.

Sound: **the first set was rejected and is being replaced** (user, 2026-10-13 and 14: "static noise everywhere and
nothing is generally immersive, tasteful, or intuitive"; of the jetpack, "bleeds my ears"). What that set was, so it is
not made again: jets, rushing air, sea, suit hum and the whole score synthesised from filtered noise by
`unreal/Tools/audio/make.py`, a wind recording with a synthesised hum under it, and NASA radio recordings. It passed
every measurement and failed the only test that counts. The rules now:

- The mood the user asked for is **mystery and scale: "the aura of a huge planet and galaxy"**. Nothing bright, quick or
  cute, and nothing from the Godot version's audio (a cosy folk game; the user ruled it out).
- **No sound is synthesised from noise, and nobody building this can listen.** Sounds are professional recordings (the
  Sonniss GDC bundles, held file by file on the Internet Archive: free to use in a game, not to pass on as files, not for
  training AI). `unreal/Tools/audio/pro.py` fetches each chosen recording, cuts and shapes it, and says beside each one
  why it was chosen.
- **The user does not audition.** A page of 203 previews to pick from was tried on 2026-10-14 and refused: "dont give me
  options to choose u anaylze". Choices are made by what a recording is (its library, its name, its maker's keywords),
  by measurement (`.scratch/audio/sound_features.py`: spectral slope, hiss above 4 kHz, narrow tones, steadiness,
  crackle) and by reading spectrograms (`.scratch/audio/see.py`, the nearest thing to listening available); then they go
  into the game and the user reacts to the game.
- **A thing must be a recording of that thing.** The jetpack shortlist that was refused ("all shit ... get actual
  premium proper audio effects from proper places") was fire, gas-burner and steam recordings picked for measuring low
  and soft. The jet is now two recordings made to be spacecraft engines (an intake's air over a held engine tone), with
  a designed blast-off for lighting it; the rush of falling is a microphone worn by a skydiver; the wind is wind in
  grass on a mountain (wind was the one list called "good ish").
- **Nothing is an even bed of broadband noise**, however dark: that is what reads as static. Every bed of the score is
  pure tone (harmonic lines and nothing between them in its spectrogram), all three on G so they fade into one another.
- **Levels:** the old held jet was the loudest sound in the game (-19.7 dB RMS at gain 0.75). The new one is 7 dB under it.
- What has no recording yet is silent (`WITHHELD` in `unreal/Tools/build_audio.py`: the suit's hum). Silence is better
  than a hiss.
- The plumbing stays (`Tools/build_audio.py` imports, `Player/EphemeraAudio.*` plays). A sound's file name is its meaning:
  `step_sand_03` is a take of a footstep on sand, `loop_wind` a loop. The game asks for `step_sand` and gets a take. Add
  a recording with the right name and it is in the game; a name with no file is silent.
- **Footsteps come from the terrain itself** (`GroundUnderfoot`: the same cover, ice, flow and sea level the ground's
  material is drawn from), one per stride, the stride lengthening with speed.
- **What a sound passes through matters more than where it is.** As the air thins every outside sound is quietened and
  dulled by one rule; on Hush a step is a thud through the suit and there is no wind at all.

## Playing it in a browser (pixel streaming, built 2026-10-13)

**Off since 2026-10-14** (user: "stop it with the streaming until i tell u so. i want from now on to actually test the
game in windows"). Nothing below is started unless asked for; builds are checked by hidden runs of the packaged game
and the user plays `F:\Ephemera\Builds\Windows\Ephemera.exe` on the PC itself.

The user asked for the game to be playable from a web browser, streamed from the PC. It is Unreal's Pixel Streaming 2 (the
plugin is in the Windows build; research and every setting's source in `docs/research/pixel_streaming.md`).

- **On the PC**: double-click `Stream Ephemera.bat` on the desktop (it runs `Tools\stream\stream.ps1 -Serve`). It starts
  Epic's signalling server (built once by `Tools\stream\setup.ps1` into `F:\Ephemera\Stream`) and the game with no window,
  prints the address, restarts the game if it ever stops, and ends the stream on a key press. One game is drawn; everyone
  who opens the page sees it and can play it. A second game for a second visitor would need a second graphics card.
- **The picture**: 1920x1080 at the game's frame rate (codec and bit rate below). The stream
  instance draws 75% of the pixels (47 fps instead of 38 on the RTX 3060; after encoding the two cannot be told apart) and
  always uses the "fronts" sky (overcast costs a quarter of the frame rate). `-Scale 100`, `-Weather`, `-MaxMbps` change that.
- **The picture is VP8, encoded on the processor, with a floor under it.** The graphics card's H.264 encoder cannot be
  used in this engine version: with a viewer connected it aborts the game, with no error and no crash report (exit
  0xC0000409 inside `nvEncodeAPI64.dll`), one to three minutes in, at any frame rate and any bit rate (2026-10-14: six
  deaths in four watched runs; a frame cap was tried first and was not the cause), and with the engine's capture threads
  switched off instead it hangs the card within seconds. A test that leaves after twelve seconds proves nothing here: the
  encoder only runs while somebody watches (`.scratch/ue/fix/stream_cap_test.sh` keeps a hidden viewer connected).
- **It stalls before it blurs** (user, 2026-10-14: "id rather it cut/pause than low bitrate to look like shit"): the
  stream may not use less than 22 Mbit/s nor draw a frame worse than quality 55, and it carries no film grain (an encoder
  spends its bits on grain and has none left for the grass). Measured so: 1080p at the game's 47 frames a second,
  27 Mbit/s, no dropped frames, and the ground crawls by 1.4 grey values between frames (VP9 was tried: three times the
  colour crawl at the same bit rate).
- **Sound** goes to the browser and is muted on the PC itself (`Tools\stream\mute.ps1` mutes the game in Windows' volume
  mixer, which is after the stream has taken it).
- **Measured** from a hidden Chrome on the Mac (`unreal/Tools/stream/probe/probe.mjs`, run with bun): 1080p arriving at the
  game's frame rate, 6 ms network round trip on the home network, 20-30 ms waiting to be shown, 3-5 ms to decode, no lost
  packets; a key pressed in the browser opens the game's menu.
- **The frame pacer is what makes it streamable.** Without a window nothing held the game back, and it drew sixteen frames
  in a rush and then stalled a quarter of a second.
- **Reach** (the user's choice, 2026-10-13: "home only", plus Tailscale): the PC's firewall lets the page be fetched from
  the home network and from Tailscale devices only. Nothing is open to the internet. On the home network it is
  `http://windows-pc/`; in the tailnet it is `https://your-pc.your-tailnet.ts.net/` (`tailscale serve --bg 80`, set once on
  the PC, with a real certificate) or `http://100.x.y.z/`. The picture itself travels directly between the game and the
  browser, over the LAN where there is one and inside Tailscale where there is not (both paths were seen to work). A friend
  gets in by being invited to the tailnet, or by the PC being shared with them from Tailscale's admin page.
- One crash was seen in the first test (an access violation in the engine's render-graph tracing on a worker thread, half
  a minute after the browser left). It did not recur in five further runs; the stream instance now sets up render passes
  in order (`r.RDG.AsyncSetupQueue=0`), and the launcher restarts a game that stops.
