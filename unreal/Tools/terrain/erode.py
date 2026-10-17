"""Water shaping the land: stream-power erosion on the whole planet at once.

The cube-sphere's grid points are joined into one graph (edges and corners of the cube are single
points with neighbours on every face that meets there), so rivers cross the cube's seams as if
they were not there.

Each step:
 1. Fill pits just enough that every point has a way down to the sea (Priority-Flood with a tiny
    slope, Barnes et al. 2014), and send each point's water to its steepest lower neighbour (D8).
 2. Add up the area draining through every point: the size of the river there.
 3. Lower each point at a rate K * area^m * slope (the stream power law, m = 0.5), solved implicitly
    from the sea upward so that large steps stay stable (Braun and Willett 2013).
 4. Let slopes steeper than scree can stand slump (thermal erosion to the talus angle).

Big rivers cut fast and small ones slowly, so valleys join into branching networks that all run to
the sea, with ridges left between them: the pattern that makes land read as land.
"""
import math
import time

import numpy as np
from numba import njit, prange

import cube


def graph(n):
    """Global ids for grid points [6, n+1, n+1], neighbours [count, 8] (-1 padded), positions [count, 3] (unit)."""
    dirs = cube.directions(n)
    m = n + 1
    gid = np.full((6, m, m), -1, dtype=np.int64)
    border = np.zeros((6, m, m), bool)
    border[:, 0, :] = border[:, -1, :] = border[:, :, 0] = border[:, :, -1] = True
    keys = np.round(dirs[border] * 1e6).astype(np.int64)
    _, inverse = np.unique(keys, axis=0, return_inverse=True)
    inverse = inverse.ravel()
    shared = int(inverse.max()) + 1
    gid[border] = inverse
    interior = ~border
    gid[interior] = shared + np.arange(int(interior.sum()))
    count = shared + int(interior.sum())
    position = np.zeros((count, 3), dtype=np.float64)
    position[gid.ravel()] = dirs.reshape(-1, 3)
    return gid.astype(np.int32), _neighbours(gid.astype(np.int32), count), position


@njit(cache=True)
def _neighbours(gid, count):
    nbr = -np.ones((count, 8), dtype=np.int32)
    faces, m, _ = gid.shape
    for f in range(faces):
        for j in range(m):
            for i in range(m):
                a = gid[f, j, i]
                for dj in range(-1, 2):
                    for di in range(-1, 2):
                        if dj == 0 and di == 0:
                            continue
                        jj, ii = j + dj, i + di
                        if jj < 0 or jj >= m or ii < 0 or ii >= m:
                            continue
                        b = gid[f, jj, ii]
                        if b == a:
                            continue
                        for k in range(8):
                            if nbr[a, k] == b:
                                break
                            if nbr[a, k] < 0:
                                nbr[a, k] = b
                                break
    return nbr


@njit(cache=True)
def _distances(nbr, position, radius_m):
    dist = np.zeros(nbr.shape, dtype=np.float32)
    for a in range(nbr.shape[0]):
        for k in range(8):
            b = nbr[a, k]
            if b >= 0:
                dx = position[a, 0] - position[b, 0]
                dy = position[a, 1] - position[b, 1]
                dz = position[a, 2] - position[b, 2]
                dist[a, k] = math.sqrt(dx * dx + dy * dy + dz * dz) * radius_m
    return dist


@njit(cache=True)
def _fill(h, nbr, dist, sea_level, epsilon):
    """Priority-Flood: the lowest surface above h on which water everywhere runs down to the sea."""
    count = h.shape[0]
    filled = h.copy()
    closed = np.zeros(count, dtype=np.bool_)
    heap_key = np.empty(count, dtype=np.float64)
    heap_id = np.empty(count, dtype=np.int32)
    size = 0
    for a in range(count):
        if h[a] <= sea_level:
            closed[a] = True
    # Seed with the sea's edge.
    for a in range(count):
        if not closed[a]:
            continue
        coastal = False
        for k in range(8):
            b = nbr[a, k]
            if b >= 0 and not closed[b]:
                coastal = True
                break
        if coastal:
            i = size
            size += 1
            heap_key[i] = h[a]
            heap_id[i] = a
            while i > 0:
                p = (i - 1) // 2
                if heap_key[p] <= heap_key[i]:
                    break
                heap_key[p], heap_key[i] = heap_key[i], heap_key[p]
                heap_id[p], heap_id[i] = heap_id[i], heap_id[p]
                i = p
    while size > 0:
        c = heap_id[0]
        level = heap_key[0]
        size -= 1
        heap_key[0] = heap_key[size]
        heap_id[0] = heap_id[size]
        i = 0
        while True:
            left, right, small = 2 * i + 1, 2 * i + 2, i
            if left < size and heap_key[left] < heap_key[small]:
                small = left
            if right < size and heap_key[right] < heap_key[small]:
                small = right
            if small == i:
                break
            heap_key[small], heap_key[i] = heap_key[i], heap_key[small]
            heap_id[small], heap_id[i] = heap_id[i], heap_id[small]
            i = small
        for k in range(8):
            b = nbr[c, k]
            if b < 0 or closed[b]:
                continue
            closed[b] = True
            filled[b] = max(h[b], level + epsilon * dist[c, k])
            i = size
            size += 1
            heap_key[i] = filled[b]
            heap_id[i] = b
            while i > 0:
                p = (i - 1) // 2
                if heap_key[p] <= heap_key[i]:
                    break
                heap_key[p], heap_key[i] = heap_key[i], heap_key[p]
                heap_id[p], heap_id[i] = heap_id[i], heap_id[p]
                i = p
    return filled


@njit(cache=True)
def _step(h, nbr, dist, k, hard, strata, phase, young, cell_area, sea_level, dt, m_exp, spacing, slump, creep):
    count = h.shape[0]
    # What the rock is like at each point's present height: hard where the design says so, and in
    # bands where the ground is built of lava layers (a hard bed every `spacing` metres). Hard rock
    # wears slowly and stands steep; that is all it takes for benches and cliff bands to appear.
    erodibility = np.empty(count, dtype=np.float64)
    talus = np.empty(count, dtype=np.float64)
    for a in range(count):
        level = (h[a] + phase[a]) / spacing
        band = level - math.floor(level)
        bed = 1.0 if band < 0.26 else (0.0 if band > 0.34 else (0.34 - band) / 0.08)
        hardness = max(hard[a], strata[a] * bed)
        erodibility[a] = k * (1.0 - 0.92 * hardness) * (1.0 - young[a])
        talus[a] = math.tan(math.radians(35.0 + 38.0 * hardness))
    filled = _fill(h, nbr, dist, sea_level, 1e-4)
    receiver = np.arange(count).astype(np.int32)
    run = np.ones(count, dtype=np.float32)
    for a in range(count):
        if h[a] <= sea_level:
            continue
        best = 0.0
        for k in range(8):
            b = nbr[a, k]
            if b < 0:
                continue
            s = (filled[a] - filled[b]) / dist[a, k]
            if s > best:
                best, receiver[a], run[a] = s, b, dist[a, k]
    order = np.argsort(filled)
    area = np.full(count, cell_area, dtype=np.float64)
    for q in range(count - 1, -1, -1):
        a = order[q]
        r = receiver[a]
        if r != a:
            area[r] += area[a]
    new = h.copy()
    for q in range(count):
        a = order[q]
        r = receiver[a]
        if r == a:
            continue
        c = erodibility[a] * dt * area[a] ** m_exp / run[a]
        value = (h[a] + c * new[r]) / (1.0 + c)
        # Never below the point the water runs to: pits fill instead of deepening.
        floor = new[r] + 2e-4 * run[a]
        new[a] = value if value > floor else floor
    # Soil creep: ground that is not hard rock slowly evens out, which rounds the ridges between
    # streams and softens valley sides. Without it every slope is a knife-edged badland.
    if creep > 0.0:
        crept = new.copy()
        for a in range(count):
            if new[a] <= sea_level:
                continue
            total, weight = 0.0, 0.0
            for q in range(8):
                b = nbr[a, q]
                if b >= 0:
                    total += new[b]
                    weight += 1.0
            level = (h[a] + phase[a]) / spacing
            band = level - math.floor(level)
            bed = 1.0 if band < 0.26 else (0.0 if band > 0.34 else (0.34 - band) / 0.08)
            soft = 1.0 - max(hard[a], strata[a] * bed)
            crept[a] = new[a] + (total / weight - new[a]) * creep * soft * (1.0 - young[a])
        new = crept
    # Scree: what stands steeper than the talus angle slumps to its lower neighbours.
    for sweep in range(slump):
        delta = np.zeros(count, dtype=np.float64)
        for a in range(count):
            if new[a] <= sea_level:
                continue
            for k in range(8):
                b = nbr[a, k]
                if b < 0:
                    continue
                excess = new[a] - new[b] - talus[a] * dist[a, k]
                if excess > 0.0:
                    move = excess * 0.08
                    delta[a] -= move
                    delta[b] += move
        for a in range(count):
            new[a] += delta[a]
    return new, area


def to_graph(field, gid, count):
    values = np.zeros(count, dtype=np.float64)
    values[gid.ravel()] = field.ravel()
    return values


def to_field(values, gid):
    return values[gid].astype(np.float32)


def run(field, steps, dt, k=2.0e-5, m_exp=0.5, radius_m=5000.0, hard=None, strata=None, young=None, phase=None, spacing=42.0,
        slump=2, creep=0.022, sea_level=0.0, report=None, quiet=False):
    """field: [6, n+1, n+1] heights (m). hard, strata, young: same shape, 0..1 (see world.py). phase: metres
    by which the lava layers are shifted from place to place, so cliff bands are not level round the planet.
    sea_level: where rivers end; lower than today's sea to cut valleys that the sea then drowns.

    Returns the eroded field and the drainage area field (m2).
    """
    n = field.shape[1] - 1
    started = time.time()
    gid, nbr, position = graph(n)
    count = position.shape[0]
    dist = _distances(nbr, position, radius_m)
    zeros = np.zeros(field.shape, np.float32)
    h = to_graph(field, gid, count)
    hard_g = to_graph(hard if hard is not None else zeros, gid, count)
    strata_g = to_graph(strata if strata is not None else zeros, gid, count)
    young_g = to_graph(young if young is not None else zeros, gid, count)
    phase_g = to_graph(phase if phase is not None else zeros, gid, count)
    cell_area = 4.0 * math.pi * radius_m ** 2 / count
    area = np.zeros(count)
    for step in range(steps):
        h, area = _step(h, nbr, dist, k, hard_g, strata_g, phase_g, young_g, cell_area, sea_level, dt, m_exp, spacing, slump, creep)
        if report and (step + 1) % report == 0 and not quiet:
            print(f"  erosion step {step + 1}/{steps}: {time.time() - started:.0f} s, top {h.max():.0f} m", flush=True)
    return to_field(h, gid), to_field(area, gid)


@njit(cache=True)
def _floors(h, nbr, dist, cell_area, sea_level, source_area, width_m, width_max, wobble):
    count = h.shape[0]
    filled = _fill(h, nbr, dist, sea_level, 1e-4)
    receiver = np.arange(count).astype(np.int32)
    run = np.ones(count, dtype=np.float32)
    for a in range(count):
        if h[a] <= sea_level:
            continue
        best = 0.0
        for k in range(8):
            b = nbr[a, k]
            if b < 0:
                continue
            s = (filled[a] - filled[b]) / dist[a, k]
            if s > best:
                best, receiver[a], run[a] = s, b, dist[a, k]
    order = np.argsort(filled)
    area = np.full(count, cell_area, dtype=np.float64)
    for q in range(count - 1, -1, -1):
        a = order[q]
        r = receiver[a]
        if r != a:
            area[r] += area[a]
    # Every point's river: the first point down its own water's path that carries a river's worth
    # of water, how far away that is along the path, and how high above it the point stands.
    river = -np.ones(count, dtype=np.int32)
    reach = np.zeros(count, dtype=np.float64)
    for q in range(count):
        a = order[q]
        r = receiver[a]
        if h[a] <= sea_level:
            continue
        if area[a] >= source_area:
            river[a] = a
        elif r != a and river[r] >= 0:
            river[a] = river[r]
            reach[a] = reach[r] + run[a]
    new = h.copy()
    floor = np.zeros(count, dtype=np.float32)
    for a in range(count):
        d = river[a]
        if d < 0:
            continue
        width = min(width_m * math.sqrt(area[d] / source_area), width_max) * wobble[a]
        if reach[a] >= width:
            continue
        t = reach[a] / width
        rise = h[a] - filled[d]
        if rise > 0.0:
            # Level beside the river, steepening to meet the old hillside at the floor's edge.
            new[a] = filled[d] + rise * t ** 2.4
        floor[a] = 1.0 - t
    return new, floor, area


def floors(field, wobble, radius_m=5000.0, sea_level=0.0, source_area=1.2e5, width_m=55.0, width_max=240.0):
    """Gives the rivers valley floors.

    Stream-power erosion alone cuts V-shaped gullies: it has no rivers swinging from side to side
    and no gravel settling out, which is what makes the flat floor a real valley has. Here every
    point within reach of a river (a point draining source_area or more) is brought down toward
    the river's own level: nearly flat beside it, steepening to meet the old slope at the floor's
    edge. The floor is width_m either side of the smallest river and grows with the square root of
    the water in it, up to width_max; wobble (same shape as field, about 1) varies it from place to
    place so no valley is a uniform trough.

    Returns the new heights, how far inside a floor each point lies (0 none .. 1 at the river), and
    the drainage area field.
    """
    n = field.shape[1] - 1
    gid, nbr, position = graph(n)
    count = position.shape[0]
    dist = _distances(nbr, position, radius_m)
    cell_area = 4.0 * math.pi * radius_m ** 2 / count
    new, floor, area = _floors(to_graph(field, gid, count), nbr, dist, cell_area, sea_level, source_area, width_m, width_max,
                               to_graph(wobble, gid, count))
    return to_field(new, gid), to_field(floor.astype(np.float64), gid), to_field(area, gid)


def upsample(field):
    """Doubles a field's resolution (n -> 2n) by linear interpolation along the grid."""
    m = field.shape[1]
    out = np.empty((6, 2 * m - 1, 2 * m - 1), dtype=field.dtype)
    out[:, ::2, ::2] = field
    out[:, 1::2, ::2] = 0.5 * (field[:, :-1, :] + field[:, 1:, :])
    out[:, :, 1::2] = 0.5 * (out[:, :, :-2:2] + out[:, :, 2::2])
    return out
