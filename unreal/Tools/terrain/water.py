"""Wave textures for water: repeating normal maps made from a wind-sea spectrum.

    run.sh water.py <outdir>

A water surface is the sum of very many sine waves, long and short, running roughly downwind; how
much of each there is has been measured at sea. Summing them from such a spectrum (Tessendorf's
method, one inverse Fourier transform) gives a patch of water that repeats exactly at its edges
and has the look sums of a few hand-picked sines never do: crests of every length, crossing.

Two maps are written, each 1024 x 1024:

  water_chop.png    short wind waves and ripples, fairly even in every direction
  water_swell.png   longer waves, strongly lined up with the wind

Each holds slopes, not a finished normal: red and green are the surface's slope along x and y
(0.5 = level, full range = +/- SLOPE_RANGE), blue is slope squared (x and y together, as a fraction
of SLOPE_RANGE squared), alpha the height. Blue is what lets distant water look right: as the
texture is read at lower detail the ripples average away, but their squared slope does not, and the
difference is exactly how much rougher the water must be drawn to glitter as widely as the ripples
would have made it (Olano and Baker, LEAN mapping, 2010).
"""
import pathlib
import sys

import numpy as np
from PIL import Image

SIZE = 1024


def patch(seed, wind_speed, spread, small_cut, rms_slope):
    """One repeating patch, taken as 2 pi units across. Returns height and its two slopes.

    wind_speed sets the longest waves (as a fraction of the patch); spread how tightly they follow
    the wind (x axis); small_cut damps waves shorter than that many texels; rms_slope is the
    steepness the result is scaled to.
    """
    rng = np.random.default_rng(seed)
    k1 = np.fft.fftfreq(SIZE, 1.0 / SIZE)
    kx, ky = np.meshgrid(k1, k1, indexing="xy")
    k = np.hypot(kx, ky)
    k[0, 0] = 1.0
    # Phillips spectrum: energy falls as k^-4, cut off below the longest wave the wind can raise.
    longest = wind_speed
    energy = np.exp(-1.0 / (k * longest) ** 2) / k ** 4
    cos_wind = kx / k
    energy *= np.abs(cos_wind) ** spread
    # Waves running against the wind are weak, not absent.
    energy *= np.where(cos_wind < 0, 0.25, 1.0)
    energy *= np.exp(-(k * small_cut / SIZE) ** 2)
    energy[0, 0] = 0.0
    spectrum = (rng.normal(size=k.shape) + 1j * rng.normal(size=k.shape)) * np.sqrt(energy)
    height = np.real(np.fft.ifft2(spectrum))
    slope_x = np.real(np.fft.ifft2(1j * kx * spectrum))
    slope_y = np.real(np.fft.ifft2(1j * ky * spectrum))
    gain = rms_slope / np.sqrt(np.mean(slope_x ** 2 + slope_y ** 2))
    height *= gain
    slope_x *= gain
    slope_y *= gain
    return height, slope_x, slope_y


SLOPE_RANGE = 1.0


def write(path, height, slope_x, slope_y):
    span = np.percentile(np.abs(height), 99.8)
    rgba = np.stack([
        slope_x / SLOPE_RANGE * 0.5 + 0.5,
        slope_y / SLOPE_RANGE * 0.5 + 0.5,
        (slope_x ** 2 + slope_y ** 2) / SLOPE_RANGE ** 2,
        height / span * 0.5 + 0.5,
    ], axis=-1)
    Image.fromarray(np.round(np.clip(rgba, 0.0, 1.0) * 255.0).astype(np.uint8), "RGBA").save(path)
    print(f"{path.name}: rms slope {np.sqrt(np.mean(slope_x ** 2 + slope_y ** 2)):.3f}, "
          f"clipped {np.mean((np.abs(slope_x) > SLOPE_RANGE) | (np.abs(slope_y) > SLOPE_RANGE)) * 100:.2f}%")


# ------------------------------------------------------------------------------------------------
# Moving water. A sliding picture of waves is the commonest fake there is: every crest goes the same
# way at the same speed and nothing ever forms or breaks up. On real water each wave runs at its own
# speed (deep water: the speed of a wave of length L is sqrt(g L / 2 pi), so long waves outrun short
# ones), and the sum of them is always changing: crests build where trains meet and fade where they
# part. That is what the animated maps are: the same spectrum, each wave advanced at its own speed
# (Tessendorf, "Simulating Ocean Water", 2001), sampled at FRAMES moments through LOOP seconds. Every
# wave's frequency is rounded to a whole number of turns in LOOP, so the last moment runs straight on
# into the first. One map is laid over the sea at several sizes (see OCEAN_CODE): played back at a
# rate that grows as the square root of its size, which keeps the physics right at every size.
#
# Written as raw bytes, FRAMES slices of SIZE x SIZE texels, four bytes each in the order blue, green,
# red, alpha (red, green: slope; blue: slope squared; alpha: height), for a volume texture whose third
# coordinate is time (EphemeraCloudBuilder::ImportWaveVolume).
ANIM_SIZE = 128
FRAMES = 128         # a moment every 0.19 s: the shortest waves kept (2.4 s) turn under half a radian between moments
LOOP = 24.0          # seconds
ANIM_METRES = 100.0  # how big the map is taken to be when its waves are timed
G = 9.81


def animated(seed, peak, spread, against, shortest, rms_slope):
    """peak: the strongest waves' length as a fraction of the map; spread: how closely they follow the
    wind (cos^spread); against: how much runs against the wind; shortest: the shortest wave kept, as a
    fraction of the map (shorter ones come from the map laid over the sea again at a smaller size, and
    are left out here because they turn over faster than the frames can follow)."""
    rng = np.random.default_rng(seed)
    n1 = np.fft.fftfreq(ANIM_SIZE, 1.0 / ANIM_SIZE)
    nx, ny = np.meshgrid(n1, n1, indexing="xy")
    n = np.hypot(nx, ny)
    n_safe = np.where(n == 0, 1.0, n)
    # A wind-sea spectrum peaked at `peak` (Phillips' k^-4 tail, with the long-wave cut-off placed so
    # the peak falls there), lined up with the wind along x.
    n_peak = 1.0 / peak
    energy = np.exp(-(n_peak / n_safe) ** 2 * 2.0) / n_safe ** 4
    cos_wind = nx / n_safe
    energy *= np.where(cos_wind >= 0, np.abs(cos_wind) ** spread, against * np.abs(cos_wind) ** spread)
    n_short = 1.0 / shortest
    energy *= np.exp(-(n / n_short) ** 4)
    energy[n == 0] = 0.0
    h0 = (rng.normal(size=n.shape) + 1j * rng.normal(size=n.shape)) * np.sqrt(energy * 0.5)
    # h0(-k), for the real-valued sum: index of -k on the FFT grid.
    flip = lambda a: np.roll(np.flip(a, axis=(0, 1)), 1, axis=(0, 1))
    h0_neg = np.conj(flip(h0))
    k = n * 2.0 * np.pi / ANIM_METRES
    omega = np.sqrt(G * k)
    turns = np.maximum(np.round(omega * LOOP / (2.0 * np.pi)), np.where(n > 0, 1.0, 0.0))
    omega = turns * 2.0 * np.pi / LOOP
    kx = nx * 2.0 * np.pi / ANIM_METRES
    ky = ny * 2.0 * np.pi / ANIM_METRES
    heights, sx, sy = [], [], []
    for f in range(FRAMES):
        t = LOOP * f / FRAMES
        h = h0 * np.exp(1j * omega * t) + h0_neg * np.exp(-1j * omega * t)
        heights.append(np.real(np.fft.ifft2(h)))
        sx.append(np.real(np.fft.ifft2(1j * kx * h)))
        sy.append(np.real(np.fft.ifft2(1j * ky * h)))
    heights, sx, sy = np.array(heights), np.array(sx), np.array(sy)
    gain = rms_slope / np.sqrt(np.mean(sx ** 2 + sy ** 2))
    return heights * gain, sx * gain, sy * gain


def write_animated(path, height, slope_x, slope_y):
    span = np.percentile(np.abs(height), 99.8)
    channels = [
        (slope_x ** 2 + slope_y ** 2) / SLOPE_RANGE ** 2,      # blue
        slope_y / SLOPE_RANGE * 0.5 + 0.5,                      # green
        slope_x / SLOPE_RANGE * 0.5 + 0.5,                      # red
        height / span * 0.5 + 0.5,                              # alpha
    ]
    bgra = np.stack(channels, axis=-1)
    path.write_bytes(np.round(np.clip(bgra, 0.0, 1.0) * 255.0).astype(np.uint8).tobytes())
    # How much each moment differs from the one before: a loop that joins shows the same step at the seam.
    steps = np.sqrt(np.mean((np.roll(slope_x, -1, axis=0) - slope_x) ** 2, axis=(1, 2)))
    print(f"{path.name}: {FRAMES} x {ANIM_SIZE}^2, rms slope {np.sqrt(np.mean(slope_x ** 2 + slope_y ** 2)):.3f}, "
          f"step between moments {steps[:-1].mean():.4f}, across the loop's seam {steps[-1]:.4f}")


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    write(out / "water_chop.png", *patch(seed=11, wind_speed=1.0 / 9.0, spread=1.0, small_cut=2.4, rms_slope=0.25))
    write(out / "water_swell.png", *patch(seed=5, wind_speed=1.0 / 3.5, spread=4.0, small_cut=5.0, rms_slope=0.25))
    write_animated(out / "water_swell_anim.raw", *animated(seed=5, peak=0.35, spread=6.0, against=0.05, shortest=0.09, rms_slope=0.25))
    write_animated(out / "water_chop_anim.raw", *animated(seed=11, peak=0.22, spread=2.0, against=0.25, shortest=0.09, rms_slope=0.25))


main()
