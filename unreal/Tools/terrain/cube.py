"""The cube-sphere grid the planet builder uses, in numpy/numba.

Six faces, each with a normal N and two edge directions U and V. A point (u, v) in -1..1 on a face
looks along normalize(N + U tan(u pi/4) + V tan(v pi/4)): the tangent warp makes grid cells nearly
equal in area. The same faces, order and warp as EphemeraPlanetBuilder.cpp, so a height stored at
grid point (face, i, j) is exactly the height of the mesh vertex there.

A height field is an array [6, n+1, n+1] (v index first, then u): grid points, edges shared by
neighbouring faces.
"""
import math

import numpy as np
from numba import njit, prange

FACES = np.array([
    [[1, 0, 0], [0, 1, 0], [0, 0, 1]], [[-1, 0, 0], [0, 0, 1], [0, 1, 0]],
    [[0, 1, 0], [0, 0, 1], [1, 0, 0]], [[0, -1, 0], [1, 0, 0], [0, 0, 1]],
    [[0, 0, 1], [1, 0, 0], [0, 1, 0]], [[0, 0, -1], [0, 1, 0], [1, 0, 0]],
], dtype=np.float64)                      # [face][N, U, V][xyz]
QUARTER_PI = math.pi / 4.0


def directions(n):
    """Unit directions of every grid point: [6, n+1, n+1, 3]."""
    t = np.tan(np.linspace(-1.0, 1.0, n + 1) * QUARTER_PI)
    out = np.empty((6, n + 1, n + 1, 3))
    for f in range(6):
        normal, u, v = FACES[f]
        p = normal[None, None, :] + u[None, None, :] * t[None, :, None] + v[None, None, :] * t[:, None, None]
        out[f] = p / np.linalg.norm(p, axis=2, keepdims=True)
    return out


@njit(cache=True)
def locate(x, y, z):
    """Direction -> (face, u, v) with u, v in -1..1."""
    ax, ay, az = abs(x), abs(y), abs(z)
    if ax >= ay and ax >= az:
        face = 0 if x > 0 else 1
    elif ay >= az:
        face = 2 if y > 0 else 3
    else:
        face = 4 if z > 0 else 5
    n = FACES[face, 0]
    eu = FACES[face, 1]
    ev = FACES[face, 2]
    along = x * n[0] + y * n[1] + z * n[2]
    u = math.atan((x * eu[0] + y * eu[1] + z * eu[2]) / along) / QUARTER_PI
    v = math.atan((x * ev[0] + y * ev[1] + z * ev[2]) / along) / QUARTER_PI
    return face, u, v


@njit(cache=True)
def sample(field, x, y, z):
    """Bilinear height along a direction."""
    n = field.shape[1] - 1
    face, u, v = locate(x, y, z)
    gu = min(max((u + 1.0) * 0.5 * n, 0.0), n - 1e-9)
    gv = min(max((v + 1.0) * 0.5 * n, 0.0), n - 1e-9)
    i, j = int(gu), int(gv)
    fu, fv = gu - i, gv - j
    a = field[face, j, i] * (1 - fu) + field[face, j, i + 1] * fu
    b = field[face, j + 1, i] * (1 - fu) + field[face, j + 1, i + 1] * fu
    return a * (1 - fv) + b * fv


def weld(field):
    """Makes the copies of each shared edge point agree (their mean), after a pass that wrote to faces separately."""
    n = field.shape[1] - 1
    dirs = np.round(directions(n) * 1e6).astype(np.int64)
    border = np.zeros((6, n + 1, n + 1), bool)
    border[:, 0, :] = border[:, -1, :] = border[:, :, 0] = border[:, :, -1] = True
    keys = dirs[border]
    values = field[border]
    _, inverse = np.unique(keys, axis=0, return_inverse=True)
    inverse = inverse.ravel()
    total = np.bincount(inverse, weights=values)
    count = np.bincount(inverse)
    field[border] = (total / count)[inverse]
    return field
