"""Gradient noise on 3D points, for numba."""
import math

import numpy as np
from numba import njit


@njit(cache=True)
def _hash(x, y, z, seed):
    h = (x * 73856093) ^ (y * 19349663) ^ (z * 83492791) ^ (seed * 668265263)
    h &= 0xFFFFFFFF
    h ^= h >> 16
    h = (h * 0x7FEB352D) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 0x846CA68B) & 0xFFFFFFFF
    h ^= h >> 16
    return h


@njit(cache=True)
def _grad(h, x, y, z):
    k = h % 12
    if k == 0: return x + y
    if k == 1: return -x + y
    if k == 2: return x - y
    if k == 3: return -x - y
    if k == 4: return x + z
    if k == 5: return -x + z
    if k == 6: return x - z
    if k == 7: return -x - z
    if k == 8: return y + z
    if k == 9: return -y + z
    if k == 10: return y - z
    return -y - z


@njit(cache=True)
def perlin(x, y, z, seed):
    """About -1..1."""
    x0, y0, z0 = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - x0, y - y0, z - z0
    ix, iy, iz = int(x0), int(y0), int(z0)
    u = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
    v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    w = fz * fz * fz * (fz * (fz * 6 - 15) + 10)
    total = 0.0
    for c in range(8):
        dx, dy, dz = c & 1, (c >> 1) & 1, (c >> 2) & 1
        weight = (u if dx else 1 - u) * (v if dy else 1 - v) * (w if dz else 1 - w)
        total += weight * _grad(_hash(ix + dx, iy + dy, iz + dz, seed), fx - dx, fy - dy, fz - dz)
    return total


@njit(cache=True)
def fbm(x, y, z, octaves, seed, gain=0.5, lacunarity=2.03):
    total, amplitude, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total += amplitude * perlin(x, y, z, seed + o * 101)
        norm += amplitude
        amplitude *= gain
        x, y, z = x * lacunarity + 17.1, y * lacunarity - 5.7, z * lacunarity + 9.4
    return total / norm


@njit(cache=True)
def ridged(x, y, z, octaves, seed, gain=0.5, lacunarity=2.03):
    """0..1, crests where the noise crosses zero; each octave is weighted by the one before so ridges branch."""
    total, amplitude, norm, weight = 0.0, 1.0, 0.0, 1.0
    for o in range(octaves):
        s = 1.0 - abs(perlin(x, y, z, seed + o * 101))
        s *= s
        total += s * amplitude * weight
        norm += amplitude
        weight = min(max(s * 2.0, 0.0), 1.0)
        amplitude *= gain
        x, y, z = x * lacunarity + 17.1, y * lacunarity - 5.7, z * lacunarity + 9.4
    return total / norm


@njit(cache=True)
def cell(x, y, z, seed):
    """Worley: distance to the nearest feature point (0 at a point, about 1 between points) and that point's id."""
    x0, y0, z0 = int(math.floor(x)), int(math.floor(y)), int(math.floor(z))
    best, best_id = 9.0, 0
    for dz in range(-1, 2):
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                cx, cy, cz = x0 + dx, y0 + dy, z0 + dz
                h = _hash(cx, cy, cz, seed)
                px = cx + (h & 0xFFFF) / 65536.0
                py = cy + ((h >> 16) & 0xFFFF) / 65536.0
                pz = cz + (_hash(cx, cy, cz, seed + 7) & 0xFFFF) / 65536.0
                d = (x - px) ** 2 + (y - py) ** 2 + (z - pz) ** 2
                if d < best:
                    best, best_id = d, h
    return math.sqrt(best), best_id
