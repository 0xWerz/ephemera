"""Draws the game's icon: Dawnhollow at first light, its star and its moon.

    ../terrain/run.sh ../installer/icon.py <project folder>

Writes Build/Windows/Application.ico (the icon the Windows program and its installer carry) and a
PNG beside it for looking at. Drawn large and scaled down, so every size is smooth.
"""
import pathlib
import sys

import numpy as np
from PIL import Image

N = 1024


def smooth(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def over(base, colour, alpha):
    return base * (1.0 - alpha[..., None]) + np.asarray(colour)[None, None, :] * alpha[..., None]


def main():
    out = pathlib.Path(sys.argv[1]) / "Build/Windows"
    out.mkdir(parents=True, exist_ok=True)
    y, x = np.mgrid[0:N, 0:N]
    u = (x + 0.5) / N * 2.0 - 1.0
    v = (y + 0.5) / N * 2.0 - 1.0
    px = 2.0 / N

    # The tile: a rounded square of deep night, a little lighter toward the light.
    corner = 0.42
    q = np.maximum(np.abs(np.stack([u, v], -1)) - (1.0 - corner), 0.0)
    tile = 1.0 - smooth(corner - px, corner + px, np.hypot(q[..., 0], q[..., 1]))
    night = np.array([0.0035, 0.0055, 0.0095])
    image = np.broadcast_to(night, (N, N, 3)).copy()
    image += np.array([0.006, 0.011, 0.020]) * np.clip(1.0 - np.hypot(u + 0.75, v + 0.75) / 1.9, 0.0, 1.0)[..., None] ** 2

    light = np.array([-0.66, -0.60, -0.30])
    light /= np.linalg.norm(light)

    def sphere(cx, cy, r):
        dx, dy = (u - cx) / r, (v - cy) / r
        d = np.hypot(dx, dy)
        inside = 1.0 - smooth(1.0 - px / r, 1.0 + px / r, d)
        z = np.sqrt(np.clip(1.0 - dx * dx - dy * dy, 0.0, 1.0))
        lit = np.clip(dx * light[0] + dy * light[1] + z * light[2], 0.0, 1.0)
        return d, inside, lit

    # The star, with its glow.
    sx, sy = -0.60, -0.60
    ds = np.hypot(u - sx, v - sy)
    image += np.array([1.0, 0.62, 0.30])[None, None, :] * (0.30 * np.exp(-ds / 0.10) + 0.035 * np.exp(-ds / 0.45))[..., None]
    image = over(image, [1.0, 0.95, 0.82], 1.0 - smooth(0.085 - px, 0.085 + px, ds))

    # The planet: dark sea and land, first light along the limb that faces the star.
    cx, cy, r = 0.12, 0.16, 0.66
    d, inside, lit = sphere(cx, cy, r)
    # Air: a thin warm line outside the lit limb.
    side = np.hypot(light[0], light[1])
    toward = np.clip(((u - cx) * light[0] + (v - cy) * light[1]) / side / np.maximum(np.hypot(u - cx, v - cy), 1e-6), 0.0, 1.0)
    halo = np.exp(-np.clip(d - 1.0, 0.0, None) / 0.05) * (d > 1.0) * toward ** 2.0
    image += np.array([1.0, 0.55, 0.26])[None, None, :] * (0.9 * halo)[..., None]
    body = np.broadcast_to(np.array([0.016, 0.034, 0.042]), (N, N, 3)).copy()
    day = smooth(0.0, 0.40, lit)
    body = over(body, [0.16, 0.42, 0.38], day * 0.95)
    body = over(body, [1.0, 0.84, 0.60], smooth(0.30, 0.62, lit) * 0.92)
    # The warm line of dawn where day begins.
    body = over(body, [0.95, 0.40, 0.16], np.exp(-((lit - 0.05) / 0.045) ** 2) * 0.7 * (lit > 0.0))
    image = over(image, [0, 0, 0], inside)
    image += body * inside[..., None]

    # The moon.
    dm, moon_in, moon_lit = sphere(0.70, -0.50, 0.10)
    moon = np.array([0.045, 0.05, 0.06])[None, None, :] + np.array([0.80, 0.80, 0.78])[None, None, :] * smooth(0.0, 0.5, moon_lit)[..., None]
    image = image * (1.0 - moon_in[..., None]) + moon * moon_in[..., None]

    rgb = np.clip(image, 0.0, 1.0) ** (1.0 / 2.2)
    rgba = np.dstack([rgb, tile])
    picture = Image.fromarray(np.round(rgba * 255.0).astype(np.uint8), "RGBA")
    picture.resize((256, 256), Image.LANCZOS).save(out / "Application.png")
    picture.resize((256, 256), Image.LANCZOS).save(out / "Application.ico", sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)])
    print("wrote", out / "Application.ico")


main()
