"""Dawnhollow's designed shape, before erosion: where the land is and what each region is.

Heights in metres above mean sea level, as a function of planet-local direction. See
docs/unreal/DIRECTION.md, "Dawnhollow's surface", for the regions. Everything here is large and
deliberate; the small natural detail (valleys that join, ridges, scree) is erosion's job.

Besides height, the design says what the rock is like, because that decides what erosion makes
of it: `hard` rock stands as cliffs, `strata` is how strongly the ground is built of alternating
hard and soft lava layers (benches and cliff bands), and `young` ground (fresh lava, cinder
cones) has not been there long enough to wear at all.
"""
import math

import numpy as np
from numba import njit, prange

import noise

RADIUS_M = 5000.0


def unit(x, y, z):
    v = np.array([x, y, z], dtype=np.float64)
    return v / np.linalg.norm(v)


# Places (unit directions).
HOLLOW = unit(0.955, -0.215, 0.20)            # centre of the caldera
NORTH_CAP = unit(0.10, 0.22, 1.0)
RIFT_A = unit(-0.93, 0.02, -0.12)             # the rift runs from A to B
RIFT_B = unit(-0.52, 0.72, -0.46)
TABLES = unit(-0.18, -0.92, 0.34)
STRAND = unit(0.30, -0.42, -0.86)

# What a piece of land is like.
SHIELD, GLACIATED, MOSS, ICE_PLATEAU, PLAIN, RIFTLAND, TABLELAND, FLATS, ISLET = range(9)

# Landmasses are built from overlapping lobes: centre, angular radius (radians; 0.2 rad is 1 km),
# how high the interior stands (m), and its character.
LANDS = np.array([
    [*HOLLOW, 0.37, 150.0, SHIELD],
    [*unit(0.80, 0.25, 0.52), 0.33, 250.0, GLACIATED],          # the fjord highlands
    [*unit(0.62, 0.52, 0.60), 0.20, 200.0, GLACIATED],
    [*unit(0.84, -0.50, -0.20), 0.33, 130.0, MOSS],              # the moss highlands
    [*unit(0.93, 0.08, 0.05), 0.24, 120.0, MOSS],
    [*NORTH_CAP, 0.40, 330.0, ICE_PLATEAU],
    [*unit(-0.28, 0.05, 0.95), 0.25, 290.0, ICE_PLATEAU],
    [*unit(0.05, 0.74, 0.67), 0.24, 14.0, PLAIN],                # the sandur
    [*RIFT_A, 0.35, 150.0, RIFTLAND],
    [*unit(-0.78, 0.40, -0.30), 0.33, 165.0, RIFTLAND],
    [*RIFT_B, 0.35, 150.0, RIFTLAND],
    [*TABLES, 0.33, 105.0, TABLELAND],
    [*unit(-0.46, -0.82, 0.12), 0.22, 95.0, TABLELAND],
    [*STRAND, 0.35, 34.0, FLATS],
    [*unit(0.00, -0.22, -0.98), 0.26, 24.0, FLATS],
    # Scattered islands.
    [*unit(0.35, 0.90, 0.10), 0.11, 90.0, ISLET], [*unit(0.20, 0.78, -0.40), 0.07, 60.0, ISLET], [*unit(-0.30, -0.35, 0.88), 0.09, 80.0, ISLET],
    [*unit(0.55, -0.70, 0.46), 0.06, 70.0, ISLET], [*unit(-0.75, -0.55, -0.35), 0.10, 100.0, ISLET], [*unit(0.05, 0.25, -0.97), 0.08, 50.0, ISLET],
    [*unit(-0.40, 0.88, 0.25), 0.08, 75.0, ISLET], [*unit(0.70, 0.55, -0.45), 0.07, 55.0, ISLET], [*unit(-0.97, -0.20, 0.30), 0.07, 65.0, ISLET],
    [*unit(0.45, -0.10, -0.75), 0.05, 40.0, ISLET], [*unit(-0.10, 0.55, -0.80), 0.06, 45.0, ISLET],
])


@njit(cache=True)
def smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


@njit(cache=True)
def angle(x, y, z, c):
    return math.acos(min(max(x * c[0] + y * c[1] + z * c[2], -1.0), 1.0))


@njit(cache=True)
def frame(c):
    """East and north at a point (east = planet's +z crossed with the point)."""
    ex, ey = -c[1], c[0]
    el = math.sqrt(ex * ex + ey * ey) + 1e-12
    ex, ey = ex / el, ey / el
    return ex, ey, 0.0, -c[2] * ey, c[2] * ex, c[0] * ey - c[1] * ex


@njit(cache=True)
def bearing(x, y, z, c):
    """Angle round the point c, from east through north: 0..2pi."""
    ex, ey, ez, nx, ny, nz = frame(c)
    b = math.atan2(x * nx + y * ny + z * nz, x * ex + y * ey + z * ez)
    return b if b >= 0 else b + 2 * math.pi


@njit(cache=True)
def offset(c, east_m, north_m):
    """The direction a given distance east and north of c."""
    ex, ey, ez, nx, ny, nz = frame(c)
    px = c[0] + (ex * east_m + nx * north_m) / RADIUS_M
    py = c[1] + (ey * east_m + ny * north_m) / RADIUS_M
    pz = c[2] + (ez * east_m + nz * north_m) / RADIUS_M
    pl = math.sqrt(px * px + py * py + pz * pz)
    return px / pl, py / pl, pz / pl


@njit(cache=True)
def segment(x, y, z, a, b):
    """Distance (radians, signed by side) from the great-circle segment a-b, and how far along it (0..1)."""
    nx, ny, nz = a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]
    nl = math.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / nl, ny / nl, nz / nl
    off = math.asin(min(max(x * nx + y * ny + z * nz, -1.0), 1.0))
    s = math.sin(off)
    px, py, pz = x - nx * s, y - ny * s, z - nz * s
    pl = math.sqrt(px * px + py * py + pz * pz) + 1e-12
    px, py, pz = px / pl, py / pl, pz / pl
    total = math.acos(min(max(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0), 1.0))
    along = math.acos(min(max(px * a[0] + py * a[1] + pz * a[2], -1.0), 1.0))
    cx, cy, cz = a[1] * pz - a[2] * py, a[2] * px - a[0] * pz, a[0] * py - a[1] * px
    if cx * nx + cy * ny + cz * nz < 0:
        along = -along
    return off, along / total


@njit(cache=True)
def design(x, y, z, lands, hollow, north_cap, rift_a, rift_b, tables, strand, seed):
    """Returns height (m), hard, strata, young (each 0..1) and the land's kind."""
    hard, strata, young = 0.0, 0.12, 0.0

    # ---- Where the land is. Coasts wander: the lookup is pushed about, and the edge itself is
    # ragged at two sizes, so every landmass has its bays and headlands.
    wx = x + 0.13 * noise.fbm(x * 1.7 + 3.1, y * 1.7, z * 1.7, 4, seed + 1)
    wy = y + 0.13 * noise.fbm(x * 1.7, y * 1.7 + 7.7, z * 1.7, 4, seed + 2)
    wz = z + 0.13 * noise.fbm(x * 1.7, y * 1.7, z * 1.7 + 5.3, 4, seed + 3)
    wl = math.sqrt(wx * wx + wy * wy + wz * wz)
    wx, wy, wz = wx / wl, wy / wl, wz / wl
    ragged = 0.30 * noise.fbm(x * 3.4, y * 3.4, z * 3.4, 4, seed + 4) + 0.13 * noise.fbm(x * 11.0, y * 11.0, z * 11.0, 4, seed + 5)
    ragged += 0.10                                                  # every landmass a little larger
    inland, interior, kind = -9.0, 0.0, -1
    blend_total, blend_height = 0.0, 0.0
    for k in range(lands.shape[0]):
        c = 1.0 - angle(wx, wy, wz, lands[k, :3]) / lands[k, 3] + ragged
        if c > inland:
            inland, kind = c, int(lands[k, 5])
        if c > -0.25:                                   # heights of overlapping lobes blend
            w = (c + 0.25) ** 2
            blend_total += w
            blend_height += w * lands[k, 4]
    if blend_total > 0.0:
        interior = blend_height / blend_total
    if inland < 0:
        h = -340.0 * smoothstep(0.0, 0.95, -inland) - 4.0 * smoothstep(0.0, 0.05, -inland)
    else:
        h = interior * smoothstep(0.0, 0.7, inland) ** 1.2 + 3.0 * smoothstep(0.0, 0.05, inland)
    land = smoothstep(-0.03, 0.08, inland)
    deep = smoothstep(0.08, 0.6, inland)

    # ---- Character of each kind of land.
    if kind == GLACIATED:
        h += land * deep * 210.0 * noise.ridged(x * 5.2, y * 5.2, z * 5.2, 5, seed + 12)
        strata = 0.45
    elif kind == MOSS:
        h += land * 45.0 * noise.fbm(x * 8.0, y * 8.0, z * 8.0, 4, seed + 13)
        # Hummocks: old lava under the moss, mounded.
        d, _ = noise.cell(x * 85.0, y * 85.0, z * 85.0, seed + 14)
        h += land * 7.0 * (1.0 - smoothstep(0.0, 0.75, d))
        strata = 0.2
    elif kind == ICE_PLATEAU:
        h += land * deep * 90.0 * noise.ridged(x * 6.0, y * 6.0, z * 6.0, 4, seed + 15)
        strata = 0.4
    elif kind == RIFTLAND:
        h += land * 50.0 * noise.fbm(x * 7.0, y * 7.0, z * 7.0, 4, seed + 16) + land * deep * 60.0 * noise.ridged(x * 4.0, y * 4.0, z * 4.0, 4, seed + 17)
        strata = 0.25
    elif kind == TABLELAND:
        h += land * 30.0 * noise.fbm(x * 8.0, y * 8.0, z * 8.0, 4, seed + 18)
        strata = 1.0
    elif kind == FLATS or kind == PLAIN:
        h += land * 6.0 * noise.fbm(x * 9.0, y * 9.0, z * 9.0, 4, seed + 19)
        strata = 0.0
    else:
        h += land * 40.0 * noise.fbm(x * 9.0, y * 9.0, z * 9.0, 5, seed + 10) * (0.4 + 0.6 * deep)
        strata = 0.25

    # ---- The Hollow: a shield volcano whose summit fell in, breached to the sea on one side.
    r = angle(x, y, z, hollow) * RADIUS_M
    if r < 3300.0:
        b = bearing(x, y, z, hollow)
        cb, sb = math.cos(b), math.sin(b)
        rim = 1040.0 * (1.0 + 0.09 * noise.fbm(cb * 1.3 + 9.0, sb * 1.3, 0.3, 3, seed + 20) + 0.03 * noise.fbm(cb * 5.0, sb * 5.0, 2.2, 2, seed + 23))
        crest = 1.0 + 0.20 * noise.fbm(cb * 2.6, sb * 2.6, 4.1, 3, seed + 21)
        gap = math.exp(-((math.sin((b - 4.35) * 0.5)) / 0.075) ** 2)                    # the breach
        if r > rim:
            shield = 320.0 * crest * (1.0 - smoothstep(rim, 2500.0, r)) ** 1.35
            h = max(h, 0.0) * smoothstep(rim, 2300.0, r) + shield + 28.0 * smoothstep(rim, rim + 500.0, r) * (1.0 - smoothstep(1500.0, 2500.0, r))
            hard = max(hard, 0.9 * (1.0 - smoothstep(rim, rim + 70.0, r)))
            # Shield lava is porous: rain sinks into it, so its flanks are cut by fewer, larger valleys.
            hard = max(hard, 0.4 * (1.0 - smoothstep(1700.0, 2500.0, r)))
            # The channel runs on out through the flank to the open sea.
            h += (-34.0 - h) * gap * (1.0 - smoothstep(2600.0, 3300.0, r))
        else:
            floor = -52.0 + 16.0 * noise.fbm(x * 30.0, y * 30.0, z * 30.0, 3, seed + 22) - 25.0 * gap
            island = 118.0 * (1.0 - smoothstep(40.0, 330.0, r * (1.0 + 0.25 * noise.fbm(cb * 2.0, sb * 2.0, 7.0, 2, seed + 24)))) \
                - 16.0 * (1.0 - smoothstep(0.0, 75.0, r))
            # A bench at the foot of the wall, widest where the village stands.
            home = math.exp(-((math.sin((b - 1.25) * 0.5)) / 0.17) ** 2)
            width = 140.0 + 300.0 * home + 60.0 * noise.fbm(cb * 4.0, sb * 4.0, 1.0, 2, seed + 25)
            shelf = smoothstep(rim - width - 110.0, rim - width, r)
            bench = (13.0 + 6.0 * home + 4.0 * noise.fbm(x * 60.0, y * 60.0, z * 60.0, 3, seed + 26)) * shelf
            # The bench is a lava shelf: it has stood up to the sea and it stands up to the rain.
            hard = max(hard, (0.7 + 0.25 * home) * shelf * (1.0 - smoothstep(rim - 170.0, rim - 60.0, r)))
            wall = smoothstep(rim - 170.0, rim, r)
            inner = floor + island + (bench - floor) * shelf            # the bench stands above the water, not on the bay floor
            h = inner + (320.0 * crest - inner) * wall ** 1.5
            h += (-34.0 - h) * gap * smoothstep(rim - 520.0, rim - 260.0, r)
            hard = max(hard, 0.9 * wall)
            strata = 0.6 * wall
        kind = SHIELD if r < 2500.0 else kind

    # ---- The ice plateau's outwash plain keeps its gentle grade to the sea.
    if kind == PLAIN and h > 0.0:
        a = angle(x, y, z, north_cap)
        h = 1.5 + 18.0 * (1.0 - smoothstep(0.30, 0.55, a)) + 1.2 * noise.fbm(x * 40.0, y * 40.0, z * 40.0, 3, seed + 31)

    # ---- The Rift: a trench between two fault scarps, cinder cones and fresh lava along it.
    off, t = segment(x, y, z, rift_a, rift_b)
    if -0.06 < t < 1.06 and abs(off) * RADIUS_M < 1500.0 and h > -20.0:
        ends = smoothstep(-0.06, 0.10, t) * (1.0 - smoothstep(0.90, 1.06, t))
        bend = 260.0 * noise.fbm(t * 3.0, 1.7, 0.2, 2, seed + 40) + 60.0 * noise.fbm(t * 14.0, 0.3, 4.2, 2, seed + 42)
        o = abs(off * RADIUS_M + bend)
        trench = (1.0 - smoothstep(140.0, 215.0, o)) * ends
        h -= 80.0 * trench * land
        h += 26.0 * math.exp(-((o - 290.0) / 120.0) ** 2) * ends * land                 # shoulders tilted up
        hard = max(hard, 0.75 * math.exp(-((o - 190.0) / 45.0) ** 2) * ends)
        # Fresh lava spilled across the floor and out over the shoulders in places.
        flow = smoothstep(0.05, 0.3, noise.fbm(x * 16.0 + 4.0, y * 16.0, z * 16.0, 3, seed + 43)) * (1.0 - smoothstep(250.0, 1100.0, o)) * ends
        young = max(young, flow)
        h += 5.0 * flow * land + land * ends * 3.0 * flow * noise.fbm(x * 120.0, y * 120.0, z * 120.0, 2, seed + 44)
    for k in range(13):
        tk = 0.05 + 0.073 * k + 0.02 * math.sin(k * 5.1)
        mx = rift_a[0] * (1 - tk) + rift_b[0] * tk
        my = rift_a[1] * (1 - tk) + rift_b[1] * tk
        mz = rift_a[2] * (1 - tk) + rift_b[2] * tk
        ml = math.sqrt(mx * mx + my * my + mz * mz)
        mid = np.array([mx / ml, my / ml, mz / ml])
        cx, cy, cz = offset(mid, 240.0 * math.sin(k * 2.3 + 0.5), 240.0 * math.cos(k * 1.7))
        rc = math.acos(min(max(x * cx + y * cy + z * cz, -1.0), 1.0)) * RADIUS_M
        size = 80.0 + 75.0 * (0.5 + 0.5 * math.sin(k * 4.7 + 1.0))
        if rc < size * 1.7 and h > -5.0:
            cone = (45.0 + 0.4 * size) * (1.0 - smoothstep(0.0, size * 1.6, rc)) ** 1.15
            crater = (16.0 + 0.12 * size) * (1.0 - smoothstep(0.0, size * 0.36, rc))
            h += cone - crater
            young = max(young, 1.0 - smoothstep(size * 1.0, size * 1.7, rc))

    # ---- The Table Lands: flat-topped mountains capped with hard lava.
    if angle(x, y, z, tables) < 0.50 and h > 2.0:
        for k in range(9):
            east = 1450.0 * math.cos(k * 2.4 + 0.3) * (0.25 + 0.75 * ((k * 7) % 5) / 4.0)
            north = 1450.0 * math.sin(k * 2.4 + 0.3) * (0.25 + 0.75 * ((k * 3) % 5) / 4.0)
            cx, cy, cz = offset(tables, east, north)
            rm = math.acos(min(max(x * cx + y * cy + z * cz, -1.0), 1.0)) * RADIUS_M
            size = 190.0 + 85.0 * ((k * 5) % 4)
            # A table mountain's outline is whatever the ice left: lobed and embayed, never a circle.
            edge = size * (1.0 + 0.42 * noise.fbm(x * 13.0 + k * 3.7, y * 13.0, z * 13.0, 3, seed + 50) + 0.14 * noise.fbm(x * 45.0, y * 45.0 + k, z * 45.0, 3, seed + 52))
            if rm < edge + 320.0:
                top = 200.0 + 26.0 * ((k * 3) % 4) + 7.0 * noise.fbm(x * 25.0, y * 25.0, z * 25.0, 3, seed + 51)
                mesa = 1.0 - smoothstep(edge, edge + 40.0, rm)
                # Some carry a second, smaller tier.
                tier = (1.0 - smoothstep(edge * 0.45, edge * 0.45 + 30.0, rm)) * (1.0 if k % 3 == 0 else 0.0)
                apron = 80.0 * (1.0 - smoothstep(edge, edge + 320.0, rm)) ** 1.6
                h = max(h, (h + apron) * (1.0 - mesa) + (top + 55.0 * tier) * mesa)
                # The cap is hard; the cliff under it only fairly so, and streams notch it.
                hard = max(hard, 0.92 * (1.0 - smoothstep(edge - 60.0, edge - 15.0, rm)), 0.6 * mesa)

    # ---- The Strand: a very wide, very shallow shelf where the tide runs.
    a = angle(x, y, z, strand)
    if a < 0.66 and h < 14.0 and h > -80.0:
        shelf = smoothstep(0.66, 0.44, a)
        flats = -3.5 + 8.0 * smoothstep(-0.14, 0.10, inland) + 2.0 * noise.fbm(x * 26.0, y * 26.0, z * 26.0, 4, seed + 60)
        h += (flats - h) * shelf * (1.0 - smoothstep(5.0, 14.0, h))
    return h, hard, strata, young, kind


@njit(parallel=True, cache=True)
def _build(dirs, lands, hollow, north_cap, rift_a, rift_b, tables, strand, seed):
    shape = dirs.shape[:3]
    height = np.empty(shape, dtype=np.float32)
    hard = np.empty(shape, dtype=np.float32)
    strata = np.empty(shape, dtype=np.float32)
    young = np.empty(shape, dtype=np.float32)
    kind = np.empty(shape, dtype=np.int8)
    for f in prange(6):
        for j in range(shape[1]):
            for i in range(shape[2]):
                d = dirs[f, j, i]
                height[f, j, i], hard[f, j, i], strata[f, j, i], young[f, j, i], kind[f, j, i] = design(
                    d[0], d[1], d[2], lands, hollow, north_cap, rift_a, rift_b, tables, strand, seed)
    return height, hard, strata, young, kind


def build(dirs, seed=7):
    """The designed world as fields over the cube grid: a dict of height, hard, strata, young, kind."""
    names = ("height", "hard", "strata", "young", "kind")
    return dict(zip(names, _build(dirs, LANDS, HOLLOW, NORTH_CAP, RIFT_A, RIFT_B, TABLES, STRAND, seed)))


@njit(parallel=True, cache=True)
def _roughness(dirs, seed):
    out = np.empty(dirs.shape[:3], dtype=np.float32)
    for f in prange(6):
        for j in range(dirs.shape[1]):
            for i in range(dirs.shape[2]):
                x, y, z = dirs[f, j, i]
                out[f, j, i] = 9.0 * noise.fbm(x * 38.0, y * 38.0, z * 38.0, 4, seed + 70) + 3.0 * noise.fbm(x * 150.0, y * 150.0, z * 150.0, 3, seed + 71)
    return out


@njit(parallel=True, cache=True)
def _phase(dirs, seed):
    out = np.empty(dirs.shape[:3], dtype=np.float32)
    for f in prange(6):
        for j in range(dirs.shape[1]):
            for i in range(dirs.shape[2]):
                x, y, z = dirs[f, j, i]
                out[f, j, i] = 60.0 * noise.fbm(x * 2.5, y * 2.5, z * 2.5, 3, seed + 75)
    return out


def phase(dirs, seed=7):
    """How far the lava layers are shifted up or down from place to place, in metres."""
    return _phase(dirs, seed)


def roughness(dirs, seed=7):
    """Metres of small-scale unevenness, for erosion to work on."""
    return _roughness(dirs, seed)


@njit(parallel=True, cache=True)
def _ice(dirs, height, kind, north_cap, seed):
    surface = height.copy()
    ice = np.zeros(height.shape, dtype=np.float32)
    for f in prange(6):
        for j in range(dirs.shape[1]):
            for i in range(dirs.shape[2]):
                x, y, z = dirs[f, j, i]
                if kind[f, j, i] != ICE_PLATEAU and height[f, j, i] < 250.0:
                    continue
                a = angle(x, y, z, north_cap) + 0.035 * noise.fbm(x * 6.0, y * 6.0, z * 6.0, 3, seed + 80)
                if a > 0.40:
                    continue
                # A dome of ice: thickest in the middle, thinning to a margin that reaches further
                # down where the ground under it is low (outlet glaciers in the valleys).
                rock = height[f, j, i]
                top = 190.0 + 270.0 * math.sqrt(max(1.0 - (a / 0.40) ** 2, 0.0)) + 3.0 * noise.fbm(x * 50.0, y * 50.0, z * 50.0, 3, seed + 81)
                # The ice thins to nothing at its edge: a ramp down to the rock, not a wall.
                top = top + (rock - top) * smoothstep(0.32, 0.40, a)
                # Ice lies thick on the high ground and thins away down the valleys: no ice below
                # about 110 m, full depth above 220 m. (Without this it filled every valley to the
                # brim and stood in walls hundreds of metres high where the land fell to the sea.)
                top = rock + (top - rock) * smoothstep(110.0, 220.0, rock)
                if top > rock + 0.5:
                    surface[f, j, i] = top
                    ice[f, j, i] = min((top - rock) / 10.0, 1.0)
    return surface, ice


def ice(dirs, height, kind, seed=7):
    """Lays the ice cap over the eroded rock. Returns the surface and where it is ice (0..1)."""
    return _ice(dirs, height, kind, NORTH_CAP, seed)
