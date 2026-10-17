"""Prints where things stand on the baked world. Usage: place.py <bake .npz>"""
import math
import sys

import numpy as np

import cube
import world

data = np.load(sys.argv[1])
height = data["height"]


def at(east_m, north_m, centre=world.HOLLOW):
    return np.array(world.offset(centre, east_m, north_m))


# The Cradle: the middle of the bench inside the caldera wall, on the bearing where it is widest.
best = None
for bearing in np.linspace(1.05, 1.45, 9):
    run = []
    for r in np.arange(450.0, 1100.0, 10.0):
        d = at(r * math.cos(bearing), r * math.sin(bearing))
        h = cube.sample(height, *d)
        if 6.0 < h < 34.0:
            run.append((r, h))
    if run and (best is None or len(run) > len(best[1])):
        best = (bearing, run)
bearing, run = best
r = run[len(run) // 2][0]
d = at(r * math.cos(bearing), r * math.sin(bearing))
print(f"cradle direction {d[0]:.5f},{d[1]:.5f},{d[2]:.5f}  bench {run[0][0]:.0f}..{run[-1][0]:.0f} m from the centre, {cube.sample(height, *d):.1f} m up")
