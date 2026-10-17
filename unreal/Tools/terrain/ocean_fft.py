"""Moving sea geometry: the waves the near sea's mesh is raised by.

    run.sh ocean_fft.py <outdir>

water.py makes the sea's moving normals: pictures of slope, which shade a flat sphere. Nothing in them
rises or falls. This bakes the waves themselves, as Tessendorf does ("Simulating Ocean Water", 2001):
a sea is a sum of very many waves, each running at the speed its length gives it (deep water: the
angular frequency of a wave of wavenumber k is sqrt(g k)), with as much of each as a measured
spectrum says. One inverse Fourier transform per moment sums them over a patch of water that repeats
exactly at its edges; every wave's frequency is rounded to a whole number of turns in the loop, so the
last moment runs straight on into the first.

A wave on deep water is not a sine: the water gathers under the crest and spreads out under the
trough, so crests are sharp and troughs broad. Tessendorf's "choppy" displacement gets that by moving
each point of the surface sideways toward the crest, by lambda times the height's Hilbert transform.
Where that crowds the water together until the surface folds (the Jacobian of the sideways map falls
toward zero) the wave is breaking: that is where the foam is.

Two bands of one sea (docs/research/ocean_and_underwater.md 3.2), each its own patch:

  swell  512 m, 64 s loop: a long swell come from far off (100 m waves, 1.2 m high); this world is too
         small for its own wind to raise one, which is why the sea needs it to look alive
  wind   96 m, 24 s loop: the sea this world's own wind raises over 8 km of water at 10 m/s
         (JONSWAP, fetch-limited: 11 m waves, 0.44 m high)

Their lengths and loops share no factor, so the two together repeat only after 192 s and kilometres.
Each band keeps only the waves its patch is right for (the swell band up to k = 0.2, 31 m; the wind
band from there to k = 1.2, 5.2 m); shorter ones are the moving normal maps' business (OCEAN_CODE).

Written for each band, as raw half floats (little-endian, four per texel, x fastest, then y, then
time; for EphemeraCloudBuilder::ImportWaveVolumeHalf, a volume texture whose third coordinate is time):

  sea_<band>_move.raw    sideways displacement x, y; height; Jacobian - 1           (metres)
  sea_<band>_slope.raw   slope of the displaced surface x, y; foam (0..1); height

and sea_waves.json: each band's numbers, and its strongest waves one by one, for the game to sum the
same sea on the CPU where it needs the height of the water (AEphemeraSeaSurfaceComponent).
"""
import json
import pathlib
import sys

import numpy as np

G = 10.5             # m/s^2, Dawnhollow's surface gravity
SEED = 29

# The sea, as two systems of waves (docs/research/ocean_and_underwater.md 3.2). Heights are significant
# heights (four times the standard deviation of the surface). heading: the way the waves run, radians
# from the patch's x axis. spread: s in the cos^2s(angle / 2) spreading of their directions.
SWELL = {"hs": 1.2, "tp": 7.7, "gamma": 6.0, "spread": 30.0, "heading": 0.0}
WIND = {"hs": 0.44, "wind": 10.0, "fetch": 8000.0, "gamma": 3.3, "spread": 8.0, "heading": 0.44}

# The bands. patch: metres across; texels: across the patch; loop: seconds; frames: moments in the loop
# (at least eight to a turn of its quickest wave, so linear blending between moments loses under 8 percent
# of it); k: the band's share of the spectrum (radians per metre); chop: Tessendorf's lambda; spacing: the
# coarsest spacing of the near sea's mesh that still draws the band (about six vertices to its main waves).
BANDS = [
    {"name": "swell", "patch": 512.0, "texels": 128, "loop": 64.0, "frames": 128, "k": (0.0, 0.2), "chop": 0.35, "spacing": 16.0},
    {"name": "wind", "patch": 96.0, "texels": 128, "loop": 24.0, "frames": 112, "k": (0.2, 1.2), "chop": 0.9, "spacing": 2.0},
]

# Foam: water crowded together by more than all but this fraction of the wind band's surface (the
# Jacobian below that percentile) is breaking. A whitecap stays a few seconds after its wave has gone
# on, fading; the foam is kept where the water rests, so it stays with the water as the wave passes.
FOAM_SOURCE = 0.012
FOAM_SECONDS = 2.5
# The CPU's copy of each band: its strongest waves, until they hold this much of its variance.
CPU_SHARE = 0.996
CPU_MOST = 900


def jonswap(omega, peak, gamma):
    """JONSWAP's shape (the overall scale is set later from the wanted height)."""
    sigma = np.where(omega <= peak, 0.07, 0.09)
    r = np.exp(-((omega - peak) ** 2) / (2.0 * sigma ** 2 * peak ** 2))
    with np.errstate(divide="ignore", over="ignore", invalid="ignore"):
        s = omega ** -5.0 * np.exp(-1.25 * (peak / omega) ** 4) * gamma ** r
    return np.where(omega > 0.0, s, 0.0)


def spreading(theta, heading, s):
    """cos^2s of half the angle from the heading, normalised over the circle."""
    grid = np.linspace(-np.pi, np.pi, 8192, endpoint=False)
    norm = np.sum(np.cos(grid / 2.0) ** (2.0 * s)) * (2.0 * np.pi / grid.size)
    off = np.angle(np.exp(1j * (theta - heading)))
    return np.cos(off / 2.0) ** (2.0 * s) / norm


def peak_of(system):
    if "tp" in system:
        return 2.0 * np.pi / system["tp"]
    # Fetch-limited peak (JONSWAP): omega_p = 22 (g^2 / (U F))^(1/3).
    return 22.0 * (G * G / (system["wind"] * system["fetch"])) ** (1.0 / 3.0)


def grid(band):
    n = band["texels"]
    f = np.fft.fftfreq(n, 1.0 / n)
    nx, nz = np.meshgrid(f, f, indexing="xy")       # [row = z][column = x]
    dk = 2.0 * np.pi / band["patch"]
    kx, kz = nx * dk, nz * dk
    k = np.hypot(kx, kz)
    return kx, kz, k, dk


def density(system, kx, kz, k):
    """Variance per unit area of wavenumber space: E(k, theta) = S(omega) (d omega / dk) D(theta) / k."""
    omega = np.sqrt(G * k)
    with np.errstate(divide="ignore", invalid="ignore"):
        e = jonswap(omega, peak_of(system), system["gamma"]) * (G / (2.0 * np.maximum(omega, 1e-9))) / np.maximum(k, 1e-9)
    e *= spreading(np.arctan2(kz, kx), system["heading"], system["spread"])
    return np.where(k > 0.0, e, 0.0)


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    # Each system scaled to its height over the bins of both bands together (each bin belongs to one band).
    shares = []
    for band in BANDS:
        kx, kz, k, dk = grid(band)
        inside = (k > band["k"][0]) & (k <= band["k"][1])
        shares.append({name: np.where(inside, density(system, kx, kz, k) * dk * dk, 0.0)
                       for name, system in (("swell", SWELL), ("wind", WIND))})
    scale = {}
    for name, system in (("swell", SWELL), ("wind", WIND)):
        total = sum(float(np.sum(s[name])) for s in shares)
        scale[name] = (system["hs"] / 4.0) ** 2 / total

    rng = np.random.default_rng(SEED)
    baked, summary = [], []
    for band, share in zip(BANDS, shares):
        kx, kz, k, dk = grid(band)
        n, frames, loop, chop = band["texels"], band["frames"], band["loop"], band["chop"]
        variance = share["swell"] * scale["swell"] + share["wind"] * scale["wind"]
        # One complex amplitude per wave: the surface is the sum over all of them of
        # Re(A exp(i (k.x - omega t))), each a wave running along its own k.
        amp = np.sqrt(variance) * (rng.normal(size=k.shape) + 1j * rng.normal(size=k.shape))
        amp[variance <= 0.0] = 0.0
        turn = 2.0 * np.pi / loop
        omega = np.maximum(np.round(np.sqrt(G * k) / turn), 1.0) * turn
        safe = np.where(k > 0.0, k, 1.0)
        ux, uz = kx / safe, kz / safe
        whole = n * n
        fields = {key: np.empty((frames, n, n), dtype=np.float32) for key in ("h", "dx", "dz", "hx", "hz", "xx", "zz", "xz")}
        for f in range(frames):
            z = amp * np.exp(-1j * omega * (loop * f / frames))
            back = lambda spectrum: np.real(np.fft.ifft2(spectrum)) * whole
            fields["h"][f] = back(z)
            # Sideways toward the crest: D = lambda Re(i k/|k| A e^(i theta)) (Tessendorf's sign, for this
            # transform's convention: the water under a crest crowds in, the Jacobian there falls below 1).
            fields["dx"][f] = chop * back(1j * ux * z)
            fields["dz"][f] = chop * back(1j * uz * z)
            fields["hx"][f] = back(1j * kx * z)
            fields["hz"][f] = back(1j * kz * z)
            fields["xx"][f] = chop * back(-kx * ux * z)
            fields["zz"][f] = chop * back(-kz * uz * z)
            fields["xz"][f] = chop * back(-kx * uz * z)
        h, hx, hz, xx, zz, xz = (fields[key] for key in ("h", "hx", "hz", "xx", "zz", "xz"))
        jac = (1.0 + xx) * (1.0 + zz) - xz * xz
        # The slope of the surface as the displacement leaves it (the normal of the moved surface,
        # its two tangents crossed), not of the height alone: crests are steeper than the sine they came from.
        held = np.maximum(jac, 0.2)
        sx = np.clip(((1.0 + zz) * hx - hz * xz) / held, -4.0, 4.0)
        sz = np.clip(((1.0 + xx) * hz - hx * xz) / held, -4.0, 4.0)
        # The crest should be where the water crowds: check the sign of the sideways move by finite differences.
        fd = (np.roll(fields["dx"], -1, axis=2) - np.roll(fields["dx"], 1, axis=2)) / (2.0 * band["patch"] / n)
        crest = np.corrcoef(h.ravel(), jac.ravel())[0, 1]
        del fields["hx"], fields["hz"], fields["zz"], fields["xz"], hx, hz, zz, xz, held
        baked.append({"band": band, "omega": omega, "amp": amp, "h": h, "dx": fields["dx"], "dz": fields["dz"],
                      "jac": jac, "sx": sx, "sz": sz, "kx": kx, "kz": kz, "k": k, "turn": turn})
        summary.append(f"{band['name']}: Hs {4.0 * h.std():.2f} m, rms slope {np.sqrt(np.mean(sx ** 2 + sz ** 2)):.3f}, "
                       f"Jacobian min {jac.min():.2f} (corr. with height {crest:+.2f}, should be negative), "
                       f"dDx/dx vs finite difference {np.abs(fd - xx).max():.3f} m/m at most")

    # Foam from the wind band's crowding; the same threshold for the swell, which (rightly) barely reaches it.
    wind = baked[1]
    start, full = np.percentile(wind["jac"], 100.0 * FOAM_SOURCE), np.percentile(wind["jac"], 0.2)
    for b in baked:
        band = b["band"]
        source = np.clip((start - b["jac"]) / max(start - full, 0.05), 0.0, 1.0)
        dt = band["loop"] / band["frames"]
        fade = np.exp(-dt / FOAM_SECONDS)
        foam = np.zeros_like(source[0])
        held = np.empty_like(source)
        for lap in range(3):       # round the loop until the end runs into the start
            for f in range(band["frames"]):
                foam = np.maximum(source[f], foam * fade)
                held[f] = foam
        b["foam"] = held

    bands_json = []
    for b, line in zip(baked, summary):
        band = b["band"]
        name = band["name"]
        move = np.stack([b["dx"], b["dz"], b["h"], b["jac"] - 1.0], axis=-1).astype("<f2")
        slope = np.stack([b["sx"], b["sz"], b["foam"], b["h"]], axis=-1).astype("<f2")
        (out / f"sea_{name}_move.raw").write_bytes(move.tobytes())
        (out / f"sea_{name}_slope.raw").write_bytes(slope.tobytes())
        # A loop that joins steps from its last moment to its first as from any moment to the next.
        steps = np.sqrt(np.mean((np.roll(b["h"], -1, axis=0) - b["h"]) ** 2, axis=(1, 2)))
        coverage = float(np.mean(b["foam"] > 0.25))

        # The CPU's copy: the strongest waves, one by one.
        power = np.abs(b["amp"]) ** 2
        order = np.argsort(power.ravel())[::-1]
        cumulative = np.cumsum(power.ravel()[order]) / max(power.sum(), 1e-30)
        count = int(min(CPU_MOST, np.searchsorted(cumulative, CPU_SHARE) + 1))
        chosen = order[:count]
        waves = np.stack([b["kx"].ravel()[chosen], b["kz"].ravel()[chosen], b["omega"].ravel()[chosen],
                          b["amp"].real.ravel()[chosen], b["amp"].imag.ravel()[chosen]], axis=-1)
        # How far the copy is from the texture: the same sums at a few texels and moments.
        probe = np.random.default_rng(3)
        worst = 0.0
        for _ in range(24):
            i, j, f = probe.integers(band["texels"]), probe.integers(band["texels"]), probe.integers(band["frames"])
            x, z, t = i * band["patch"] / band["texels"], j * band["patch"] / band["texels"], band["loop"] * f / band["frames"]
            phase = waves[:, 0] * x + waves[:, 1] * z - waves[:, 2] * t
            height = np.sum(waves[:, 3] * np.cos(phase) - waves[:, 4] * np.sin(phase))
            worst = max(worst, abs(height - b["h"][f, j, i]))
        hs = 4.0 * float(b["h"].std())
        peak = 2.0 * np.pi * G / peak_of(SWELL if name == "swell" else WIND) ** 2
        print(f"sea_{name}: {band['frames']} x {band['texels']}^2, {line}, foam over {coverage * 100:.1f}% "
              f"(frame step {steps[:-1].mean():.4f} m, across the loop's seam {steps[-1]:.4f} m); "
              f"CPU copy {count} waves, {cumulative[count - 1] * 100:.1f}% of the variance, worst height error {worst * 100:.1f} cm")
        bands_json.append({
            "name": name, "move": f"sea_{name}_move.raw", "slope": f"sea_{name}_slope.raw",
            "patch_m": band["patch"], "texels": band["texels"], "loop_s": band["loop"], "frames": band["frames"],
            "chop": band["chop"], "max_spacing_m": band["spacing"],
            "shortest_m": 2.0 * np.pi / band["k"][1],
            "peak_m": float(peak), "hs_m": hs,
            # Where it breaks and is gone: a wave breaks once it is higher than 0.78 of the depth.
            "break_depth_m": hs / 0.78,
            "mss": float(np.mean(b["sx"] ** 2 + b["sz"] ** 2)),
            "waves": [round(float(v), 7) for v in waves.ravel()],
        })
    (out / "sea_waves.json").write_text(json.dumps({"g": G, "seed": SEED, "bands": bands_json}, indent=1))
    print(f"sea_waves.json: {sum(len(b['waves']) // 5 for b in bands_json)} waves for the CPU")


main()
