"""Picks places worth photographing on the baked world. Usage: scout.py <bake .npz> <out .json>

Each place is a camera standing point and something to look at, both as planet-local directions,
found from the terrain itself (a shore under high ground, a valley floor below peaks, a summit).
"""
import json
import math
import sys

import numpy as np
from scipy import ndimage

import cube
import world

data = np.load(sys.argv[1])
height, area, ice, kind = data["height"], data["area"], data["ice"], data["kind"]
n = height.shape[1] - 1
dirs = cube.directions(n)
cell_m = 5000.0 * math.pi / 2 / n


def window(metres):
    return max(3, int(metres / cell_m) | 1)


def local(field, metres, how):
    return np.stack([how(face, size=window(metres), mode="nearest") for face in field])


high_near = local(height, 1800.0, ndimage.maximum_filter)
low_near = local(height, 500.0, ndimage.minimum_filter)
gy = np.gradient(height, cell_m, axis=1)
gx = np.gradient(height, cell_m, axis=2)
slope = np.hypot(gx, gy)
places = []


def name_dir(d):
    return ",".join(f"{c:.5f}" for c in d)


def pick(name, score, look, eye_m, look_up_m=0.0, note=""):
    """Takes the best-scoring grid point; `look` gives the target direction for it."""
    f, j, i = np.unravel_index(int(np.argmax(score)), score.shape)
    if score[f, j, i] <= 0:
        print(f"{name}: nothing suitable")
        return
    here = dirs[f, j, i]
    target = look(f, j, i)
    places.append({"name": name, "at": name_dir(here), "look": name_dir(target), "eye_m": eye_m, "look_up_m": look_up_m,
                   "ground_m": round(float(height[f, j, i]), 1), "note": note})


def highest_within(f, j, i, metres):
    w = window(metres) // 2
    j0, j1, i0, i1 = max(j - w, 0), min(j + w + 1, n + 1), max(i - w, 0), min(i + w + 1, n + 1)
    pj, pi = np.unravel_index(int(np.argmax(height[f, j0:j1, i0:i1])), (j1 - j0, i1 - i0))
    return dirs[f, j0 + pj, i0 + pi]


def toward(point):
    return lambda f, j, i: np.asarray(point)


flat = slope < 0.18
land = height > 0
angle_to = lambda c: np.arccos(np.clip(dirs @ np.asarray(c), -1, 1))
from_hollow = angle_to(world.HOLLOW) * 5000.0

# The Hollow from its inner shore, across the bay to the island.
pick("hollow_shore", ((height > 4) & (height < 12) & flat & (from_hollow < 1000) & (low_near < 0.5)) * (1200.0 - from_hollow), toward(world.HOLLOW), 1.7, 60.0)
# From the rim, down into the bay.
pick("hollow_rim", (from_hollow < 1300) * (from_hollow > 800) * height * (slope < 0.5), toward(world.HOLLOW), 2.0, -40.0)
# A valley floor in the fjord highlands, looking up at the peaks.
fjordland = angle_to(world.unit(0.80, 0.25, 0.52)) < 0.36
pick("fjord_valley", (fjordland & (height > 3) & (height < 30) & flat) * (high_near - 150.0) * np.log10(np.maximum(area, 10.0)),
     lambda f, j, i: highest_within(f, j, i, 1800.0), 1.7, 120.0)
# The water's edge of a drowned valley.
pick("fjord_shore", (fjordland & (height > 1.5) & (height < 5) & (low_near < -8)) * (high_near - 100.0), lambda f, j, i: highest_within(f, j, i, 1800.0), 1.5, 150.0)
# Under a table mountain.
tableland = angle_to(world.TABLES) < 0.45
pick("table_foot", (tableland & (height > 15) & (height < 80) & flat) * (high_near - 150.0), lambda f, j, i: highest_within(f, j, i, 900.0), 1.7, 40.0)
pick("table_top", (tableland & (height > 190) & flat) * height, toward(world.TABLES), 1.8, -30.0)
# The outwash plain, toward the ice.
plain = (kind == world.PLAIN) & (height > 3) & (height < 16)
pick("sandur", plain * np.log10(np.maximum(area, 10.0)) * (1.0 / (0.2 + angle_to(world.NORTH_CAP))), toward(world.NORTH_CAP), 1.6, 300.0)
# At the foot of the ice.
ice_edge = local(ice, 220.0, ndimage.maximum_filter)
pick("ice_front", ((ice < 0.02) & (ice_edge > 0.5) & (height > 30) & (slope < 0.3)) * (500.0 - height), toward(world.NORTH_CAP), 1.7, 380.0)
# In the rift, looking along it.
mid = lambda t: world.unit(*(np.asarray(world.RIFT_A) * (1 - t) + np.asarray(world.RIFT_B) * t))
pick("rift_floor", ((angle_to(mid(0.35)) < 0.05) & land & flat) * (400.0 - height), toward(mid(0.6)), 1.7, 60.0)
# The tidal flats.
pick("strand", ((kind == world.FLATS) & (height > 1.0) & (height < 3.5) & (low_near < 0.0)) * (1.0 / (0.1 + angle_to(world.STRAND))), toward(world.STRAND), 1.5, 10.0)
# The moss highlands, back toward the volcano.
pick("moss_heath", ((kind == world.MOSS) & (height > 60) & (height < 150) & (slope < 0.10)) * height, toward(world.HOLLOW), 1.7, 250.0)
# A summit in the highlands, over everything.
pick("summit", fjordland * height * (slope < 0.6), toward(world.HOLLOW), 2.0, 0.0)
json.dump(places, open(sys.argv[2], "w"), indent=1)
for place in places:
    print(place["name"], place["ground_m"], "m", place["at"])
