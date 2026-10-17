"""Builds Dawnhollow's terrain offline. Usage: bake.py <faces n: 256, 512, 1024 or 2048> <outdir>

Works up through resolutions: the land is first worn at 256 samples per face edge (the big valleys,
which need the most time), then refined at each doubling (smaller tributaries each time).
Rivers first cut to a sea 60 m lower than today's; today's sea then drowns their lower valleys,
which is how fjords, rias and the channels across tidal flats come to be.
"""
import sys
import time

import numpy as np

import cube
import erode
import preview
import world

target = int(sys.argv[1])
out = sys.argv[2]
started = time.time()
rng = np.random.default_rng(7)


def note(text):
    print(f"[{time.time() - started:5.0f} s] {text}", flush=True)


# (resolution, steps with the sea at its low stand, steps with the sea where it is now)
plan = [(256, 110, 0), (512, 60, 0), (1024, 34, 6), (2048, 14, 5)]
plan = [stage for stage in plan if stage[0] <= target]
plan[-1] = (plan[-1][0], plan[-1][1], max(plan[-1][2], 6))
height = None
for n, low_steps, high_steps in plan:
    dirs = cube.directions(n)
    design = world.build(dirs)
    if height is None:
        height = design["height"] + np.where(design["height"] > -60.0, world.roughness(dirs), 0.0)
    else:
        height = erode.upsample(height)
        # Each finer grid gets a little unevenness at its own scale for new tributaries to start from.
        height += np.where(height > -60.0, rng.normal(0.0, 0.35, height.shape), 0.0).astype(np.float32)
        # The sea floor below the low stand is the design's, at full detail.
        height = np.where(design["height"] < -62.0, design["height"], height)
    height = cube.weld(height.astype(np.float32))
    common = dict(hard=design["hard"], strata=design["strata"], young=design["young"], phase=world.phase(dirs), dt=500.0)
    height, area = erode.run(height, steps=low_steps, sea_level=-60.0, **common)
    if high_steps:
        height, area = erode.run(height, steps=high_steps, sea_level=0.0, **common)
    note(f"{n}: eroded, {height.min():.0f}..{height.max():.0f} m, land {(height > 0).mean() * 100:.0f}%")

# Valley floors along the rivers (erosion alone leaves V-shaped gullies), wider in some reaches
# than others; then two gentle steps so the new valley sides settle to slopes that can stand.
import noise
from numba import njit, prange


@njit(parallel=True, cache=True)
def reaches(dirs, seed):
    out = np.empty(dirs.shape[:3], dtype=np.float32)
    for f in prange(6):
        for j in range(dirs.shape[1]):
            for i in range(dirs.shape[2]):
                x, y, z = dirs[f, j, i]
                out[f, j, i] = 1.0 + 0.55 * noise.fbm(x * 9.0, y * 9.0, z * 9.0, 3, seed)
    return out


height, floor, area = erode.floors(height, np.clip(reaches(dirs, 311), 0.45, 1.7))
height, area = erode.run(height, steps=2, dt=60.0, creep=0.01, sea_level=0.0, hard=design["hard"], strata=design["strata"],
                         young=design["young"], phase=world.phase(dirs))
note(f"valley floors: {(floor > 0).mean() * 100:.1f}% of the grid")
rock = height
surface, ice = world.ice(dirs, rock, design["kind"])
flow = np.clip((np.log10(np.maximum(area, 1.0)) - 4.7) / 1.3, 0.0, 1.0) * (surface > 0) * (ice < 0.5)
# A river is as wide as the water in it: the mask the game draws water and gravel from is spread
# sideways from the bigger channels, over the floor made for them.
from scipy import ndimage as _nd
wide = np.stack([_nd.maximum_filter(face, size=3, mode="nearest") for face in np.where(flow > 0.55, flow, 0.0)])
flow = np.maximum(flow, 0.9 * wide * (floor > 0.55))
np.savez_compressed(f"{out}/dawnhollow_{target}.npz", height=surface.astype(np.float32), rock=rock.astype(np.float32), area=area.astype(np.float32),
                    floor=floor.astype(np.float32),
                    ice=ice, young=design["young"], hard=design["hard"], kind=design["kind"], base=design["height"])
# What covers the ground, from the kind of land it is: 0 bare sand and gravel, 0.5 lichened lava,
# 1 deep moss. Softened so one kind shades into the next, thinner on high and on steep ground.
from scipy import ndimage
by_kind = np.array([0.90, 0.72, 1.00, 0.55, 0.00, 0.55, 0.80, 0.12, 0.85], dtype=np.float32)      # world.SHIELD .. world.ISLET
cover = by_kind[np.clip(design["kind"], 0, 8)]
cover = np.stack([ndimage.gaussian_filter(face, 6.0 * target / 1024.0, mode="nearest") for face in cover])
cover = 0.5 + (cover - 0.5) * np.clip(1.25 - np.maximum(surface, 0.0) / 420.0, 0.2, 1.0)
cover = np.where(surface < 4.0, np.minimum(cover, 0.5 - 0.5 * np.clip((4.0 - surface) / 4.0, 0, 1)), cover)
# A valley floor is gravel bars and thin turf close to the river, greener toward its edges.
cover = np.where(ice < 0.5, cover - 0.22 * np.clip((floor - 0.45) / 0.4, 0.0, 1.0), cover)


def byte(field):
    return (np.clip(field, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


low, high = float(surface.min()) - 1.0, float(surface.max()) + 1.0
with open(f"{out}/dawnhollow_{target}.ephf", "wb") as file:
    file.write(b"EPHF")
    file.write(np.array([target], dtype="<i4").tobytes())
    file.write(np.array([low, high], dtype="<f4").tobytes())
    file.write(((surface - low) / (high - low) * 65535.0 + 0.5).astype("<u2").tobytes())
    for field in (flow, design["young"], ice, cover):
        file.write(byte(field).tobytes())
note("saved")
shown = dict(flow=flow, ice=ice, young=design["young"])
preview.globes(surface, f"{out}/globes.jpg", **shown)
for name, centre in (("hollow", world.HOLLOW), ("north", world.NORTH_CAP), ("rift", world.unit(-0.78, 0.40, -0.30)), ("tables", world.TABLES),
                     ("strand", world.STRAND), ("fjords", world.unit(0.80, 0.25, 0.52))):
    preview.close(surface, f"{out}/close_{name}.jpg", centre, span=0.62, size=900, **shown)
note("previews written")
