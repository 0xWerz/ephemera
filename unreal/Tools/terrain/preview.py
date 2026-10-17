"""Pictures of a height field: lit globes from a few sides, to judge a world's shape without the engine."""
import math

import numpy as np
from numba import njit, prange
from PIL import Image

import cube


@njit(parallel=True, cache=True)
def _globe(field, flow, ice, young, size, view, right, up, sun, radius_m, exaggeration):
    image = np.zeros((size, size, 3), dtype=np.float32)
    step = 2.5 / radius_m * (512.0 / (field.shape[1] - 1)) * 6.0
    for py in prange(size):
        for px in range(size):
            sx = (px + 0.5) / size * 2.1 - 1.05
            sy = 1.05 - (py + 0.5) / size * 2.1
            rr = sx * sx + sy * sy
            if rr >= 1.0:
                continue
            sz = math.sqrt(1.0 - rr)
            x = right[0] * sx + up[0] * sy + view[0] * sz
            y = right[1] * sx + up[1] * sy + view[1] * sz
            z = right[2] * sx + up[2] * sy + view[2] * sz
            h = cube.sample(field, x, y, z)
            # Two tangents, for the slope.
            tx, ty, tz = -y, x, 0.0
            tl = math.sqrt(tx * tx + ty * ty) + 1e-9
            tx, ty = tx / tl, ty / tl
            bx, by, bz = y * tz - z * ty, z * tx - x * tz, x * ty - y * tx
            ha = cube.sample(field, x + tx * step, y + ty * step, z + tz * step) - cube.sample(field, x - tx * step, y - ty * step, z - tz * step)
            hb = cube.sample(field, x + bx * step, y + by * step, z + bz * step) - cube.sample(field, x - bx * step, y - by * step, z - bz * step)
            run = 2.0 * step * radius_m
            ga, gb = ha / run * exaggeration, hb / run * exaggeration
            nx, ny, nz = x - tx * ga - bx * gb, y - ty * ga - by * gb, z - tz * ga - bz * gb
            nl = math.sqrt(nx * nx + ny * ny + nz * nz)
            light = max((nx * sun[0] + ny * sun[1] + nz * sun[2]) / nl, 0.0) * 0.9 + 0.12
            slope = math.sqrt(ha * ha + hb * hb) / run
            if h < 0.0:
                depth = min(-h / 60.0, 1.0)
                r, g, b = 0.10 - 0.07 * depth, 0.34 - 0.22 * depth, 0.40 - 0.22 * depth
                light = 0.75 + 0.25 * light
            else:
                r, g, b = 0.20, 0.30, 0.10                          # moss
                if h < 6.0:
                    r, g, b = 0.07, 0.07, 0.08                      # black sand
                rock = min(max((slope - 0.35) / 0.3, 0.0), 1.0)
                r, g, b = r + (0.16 - r) * rock, g + (0.15 - g) * rock, b + (0.15 - b) * rock
                snow = min(max((h - 330.0 + 250.0 * abs(z) ** 3) / 40.0, 0.0), 1.0) * (1.0 - rock * 0.7)
                r, g, b = r + (0.85 - r) * snow, g + (0.87 - g) * snow, b + (0.9 - b) * snow
            if young.shape[0] > 1 and h >= 0.0:
                fresh = min(cube.sample(young, x, y, z), 1.0)
                r, g, b = r + (0.035 - r) * fresh, g + (0.03 - g) * fresh, b + (0.035 - b) * fresh
            if ice.shape[0] > 1:
                frozen = min(cube.sample(ice, x, y, z), 1.0)
                r, g, b = r + (0.80 - r) * frozen, g + (0.90 - g) * frozen, b + (0.98 - b) * frozen
            if flow.shape[0] > 1:
                wet = min(cube.sample(flow, x, y, z), 1.0)
                if h >= 0.0:
                    r, g, b = r + (0.05 - r) * wet, g + (0.25 - g) * wet, b + (0.45 - b) * wet
            image[py, px, 0] = r * light * 2.2
            image[py, px, 1] = g * light * 2.2
            image[py, px, 2] = b * light * 2.2
    return image


def _or_none(field):
    return np.zeros((1, 1, 1), np.float32) if field is None else field.astype(np.float32)


def globes(field, path, flow=None, ice=None, young=None, size=720, radius_m=5000.0, exaggeration=1.0, views=None):
    """A sheet of orthographic views. views: list of (name, direction the camera sits in)."""
    views = views or [("hollow", (0.955, -0.215, 0.20)), ("north", (0.1, 0.22, 1.0)), ("rift", (-0.75, 0.4, -0.3)),
                      ("tables", (-0.18, -0.92, 0.34)), ("strand", (0.3, -0.42, -0.86)), ("far", (0.2, 0.9, -0.2))]
    flow, ice, young = _or_none(flow), _or_none(ice), _or_none(young)
    tiles = []
    for _, v in views:
        v = np.array(v, float)
        v /= np.linalg.norm(v)
        helper = np.array([0.0, 0.0, 1.0]) if abs(v[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
        right = np.cross(helper, v)
        right /= np.linalg.norm(right)
        up = np.cross(v, right)
        sun = v * 0.55 + right * -0.6 + up * 0.58
        sun /= np.linalg.norm(sun)
        tiles.append(_globe(field.astype(np.float32), flow, ice, young, size, v, right, up, sun, radius_m, exaggeration))
    columns = 3
    rows = (len(tiles) + columns - 1) // columns
    sheet = np.zeros((rows * size, columns * size, 3), np.float32)
    for k, tile in enumerate(tiles):
        sheet[(k // columns) * size:(k // columns + 1) * size, (k % columns) * size:(k % columns + 1) * size] = tile
    Image.fromarray((np.clip(sheet, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).save(path, quality=88)


def close(field, path, centre, flow=None, ice=None, young=None, size=1100, radius_m=5000.0, span=0.5):
    """One region, enlarged: the view is centred on a direction and covers `span` of the planet's radius."""
    v = np.array(centre, float)
    v /= np.linalg.norm(v)
    helper = np.array([0.0, 0.0, 1.0]) if abs(v[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    right = np.cross(helper, v)
    right /= np.linalg.norm(right)
    up = np.cross(v, right)
    sun = v * 0.5 + right * -0.62 + up * 0.6
    sun /= np.linalg.norm(sun)
    flow, ice, young = _or_none(flow), _or_none(ice), _or_none(young)
    # Reuse the globe renderer by scaling the screen: render a globe and crop its middle.
    full = int(size / span)
    image = _globe(field.astype(np.float32), flow, ice, young, full, v, right, up, sun, radius_m, 1.0)
    a = (full - size) // 2
    Image.fromarray((np.clip(image[a:a + size, a:a + size], 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).save(path, quality=90)
