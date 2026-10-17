#!/usr/bin/env python3
"""Makes every sound of the game: from field recordings where a real thing is heard (a boot on
sand, wind, water), and from arithmetic where nothing real exists (jets, the air rushing past,
the score).

    Tools/audio/run.sh make.py [name ...]      all of them, or only those whose names start so

Writes SourceAssets/audio/<name>.wav (48 kHz, 16 bit) with CREDITS.md beside them; Tools/build_audio.py
then imports them into the project. A name is a meaning (Player/EphemeraAudio.h): "step_sand_03" is
a take of a footstep on sand, "loop_wind" something that plays round and round.

The mood asked for is mystery and scale: "the aura of a huge planet and galaxy". So nothing here is
bright, quick or cute. The score is slow chords in D with no beat, built the way Paul Nasca's
PADsynth builds a pad (a note is a band of noise round each harmonic, which is what a hall full
of strings or voices is) and hung in a very long reverb; single glass notes drift over it. Every
loop is made periodic in the frequency domain, so it joins itself exactly: no crossfade to hear.

Nobody building this can listen to it. Each file is therefore measured as it is written (level,
brightness, how soon it starts, how well a loop's end meets its beginning) and the table printed
at the end is the check.
"""
import sys
import zlib
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import ndimage, signal

SR = 48000
PROJECT = Path(__file__).resolve().parents[2]
SRC = PROJECT.parent / ".scratch/audio/src"
OUT = PROJECT / "SourceAssets/audio"
ONLY = sys.argv[1:]
REPORT = []
USED = set()

CREDITS = {
    "kenney_impact": ("Impact Sounds", "Kenney (kenney.nl)", "https://kenney.nl/assets/impact-sounds", "CC0"),
    "kenney_rpg": ("RPG Audio", "Kenney (kenney.nl)", "https://kenney.nl/assets/rpg-audio", "CC0"),
    "kenney_interface": ("Interface Sounds", "Kenney (kenney.nl)", "https://kenney.nl/assets/interface-sounds", "CC0"),
    "oga_footsteps_grass_sand_stone_fantozzi": ("Fantozzi's Footsteps (Grass/Sand & Stone)", "Fantozzi", "https://opengameart.org/content/fantozzis-footsteps-grasssand-stone", "CC0"),
    "oga_footsteps_snow_gravel_corsica_s": ("42 Snow and Gravel Footsteps", "Corsica_S", "https://opengameart.org/content/42-snow-and-gravel-footsteps", "CC0"),
    "oga_sfx_100_v2_rubberduck": ("100 CC0 SFX #2", "rubberduck", "https://opengameart.org/content/100-cc0-sfx-2", "CC0"),
    "oga_water_splash_sand_peludo": ("Water Splash and sand footsteps", "Peludo", "https://opengameart.org/content/water-splash-and-sand-footsteps", "CC0"),
    "oga_jump_landing_mentalsanityoff": ("Jump landing sound", "MentalSanityOff", "https://opengameart.org/content/jump-landing-sound", "CC0"),
    "oga_beach_ocean_waves_jasinski": ("Beach Ocean Waves", "jasinski", "https://opengameart.org/content/beach-ocean-waves", "CC0"),
    "commons_wind_fisterra_pd": ("Vento fisterra", "Escoitar.org", "https://commons.wikimedia.org/wiki/File:Vento_fisterra.ogg", "public domain"),
    "commons_mars_wind_supercam_nasa_pd": ("Perseverance rover's SuperCam records wind on Mars", "NASA/JPL-Caltech/LANL/CNES/CNRS/ISAE-Supaero",
                                           "https://commons.wikimedia.org/wiki/File:Perseverance_rover's_SuperCam_records_wind_on_Mars.oga", "public domain"),
    "sun_soho": ("Sounds of the Sun (SOHO/MDI, 40 days of the Sun's surface rising and falling, sped up 42,000 times)", "A. Kosovichev, Stanford Experimental Physics Lab / NASA / ESA",
                 "https://www.nasa.gov/feature/goddard/2018/sounds-of-the-sun", "NASA media"),
    "van_allen_whistler": ("Whistler-mode waves heard by the Van Allen Probes", "NASA/University of Iowa",
                           "https://www.nasa.gov/feature/goddard/2017/nasa-listens-in-as-electrons-whistle-while-they-work", "NASA media"),
    "juno_auroras": ("Juno listens to Jupiter's auroras", "NASA/JPL-Caltech/SwRI/Univ. of Iowa", "https://science.nasa.gov/photojournal/juno-listens-to-jupiters-auroras-sing/", "NASA media"),
    "juno_ganymede_flyby": ("Audio of Juno's Ganymede flyby", "NASA/JPL-Caltech/SwRI/Univ of Iowa", "https://science.nasa.gov/photojournal/audio-of-junos-ganymede-flyby/", "NASA media"),
    "saturn_radio_eerie": ("Eerie sounds of Saturn's radio emissions (Cassini)", "NASA/JPL/University of Iowa", "https://science.nasa.gov/resource/eerie-sounds-of-saturns-radio-emissions/", "NASA media"),
}


# ------------------------------------------------------------------------------------ files ---

def wanted(name):
    return not ONLY or any(name.startswith(prefix) for prefix in ONLY)


def load(relative, root=SRC):
    """A recording as stereo at 48 kHz. Whatever libsndfile cannot open goes through PyAV."""
    path = root / relative
    USED.add(Path(relative).parts[0])
    try:
        data, rate = sf.read(path, always_2d=True, dtype="float64")
    except Exception:
        import av
        chunks = []
        with av.open(str(path)) as container:
            resampler = av.AudioResampler(format="flt", layout="stereo", rate=SR)
            for frame in container.decode(container.streams.audio[0]):
                for out in resampler.resample(frame):
                    chunks.append(out.to_ndarray().reshape(-1, 2))
        data, rate = np.concatenate(chunks).astype(np.float64), SR
    if rate != SR:
        common = np.gcd(SR, rate)
        data = signal.resample_poly(data, SR // common, rate // common, axis=0)
    if data.shape[1] == 1:
        data = np.repeat(data, 2, axis=1)
    return np.ascontiguousarray(data[:, :2])


def db(value):
    return 20.0 * np.log10(max(float(value), 1e-12))


def gain(decibels):
    return 10.0 ** (decibels / 20.0)


def rms(x):
    return float(np.sqrt(np.mean(np.square(x)) + 1e-20))


def measure(name, x, loop):
    mono = x.mean(axis=1)
    peak = np.abs(x).max() + 1e-12
    loud = np.nonzero(np.abs(mono) > peak * 0.1)[0]
    spectrum = np.abs(np.fft.rfft(mono[: min(len(mono), SR * 20)]))
    freqs = np.fft.rfftfreq(min(len(mono), SR * 20), 1.0 / SR)
    centroid = float((spectrum * freqs).sum() / (spectrum.sum() + 1e-12))
    low = float(spectrum[freqs < 150].sum() / (spectrum.sum() + 1e-12))
    # A loop's join: the jump from its last sample to its first, against the jumps inside it.
    seam = ""
    if loop:
        inside = np.percentile(np.abs(np.diff(mono)), 99.9) + 1e-9
        seam = f"join {abs(mono[0] - mono[-1]) / inside:4.2f}x"
    REPORT.append(f"{name:24s} {len(x) / SR:7.2f} s  peak {db(peak):6.1f}  rms {db(rms(x)):6.1f}  starts {loud[0] / SR * 1000 if len(loud) else -1:5.1f} ms  "
                  f"centre {centroid:6.0f} Hz  under 150 Hz {low * 100:3.0f}%  {seam}")


def save(name, x, loop=False):
    OUT.mkdir(parents=True, exist_ok=True)
    x = np.clip(np.asarray(x, dtype=np.float64), -1.0, 1.0)
    measure(name, x, loop)
    # Dither: the quietest tails (a reverb dying away) otherwise end in a gritty staircase.
    rng = np.random.default_rng(zlib.crc32(name.encode()))
    dither = (rng.random(x.shape) - rng.random(x.shape)) / 32768.0
    sf.write(OUT / f"{name}.wav", x + dither, SR, subtype="PCM_16")


# -------------------------------------------------------------------------------------- dsp ---

def band(x, low=None, high=None, order=2):
    """Butterworth high-pass, low-pass or both, run forwards and backwards (no smearing in time)."""
    if low and high:
        sos = signal.butter(order, [low, high], btype="bandpass", fs=SR, output="sos")
    elif low:
        sos = signal.butter(order, low, btype="highpass", fs=SR, output="sos")
    else:
        sos = signal.butter(order, high, btype="lowpass", fs=SR, output="sos")
    return signal.sosfiltfilt(sos, x, axis=0)


def trim(x, start_db=-32.0, end_db=-52.0, lead=0.0015, tail=0.012):
    """Cuts the silence before a sound to almost nothing (a step must be heard when the foot lands) and after it."""
    level = np.abs(x).max(axis=1)
    peak = level.max()
    first = np.nonzero(level > peak * gain(start_db))[0][0]
    last = np.nonzero(level > peak * gain(end_db))[0][-1]
    return x[max(first - int(lead * SR), 0): min(last + int(tail * SR), len(x))]


def fade(x, rise=0.001, fall=0.02):
    x = x.copy()
    a, b = min(int(rise * SR), len(x) // 2), min(int(fall * SR), len(x) // 2)
    if a:
        x[:a] *= (0.5 - 0.5 * np.cos(np.pi * np.arange(a) / a))[:, None]
    if b:
        x[-b:] *= (0.5 + 0.5 * np.cos(np.pi * np.arange(b) / b))[:, None]
    return x


def to_peak(x, decibels):
    return x * (gain(decibels) / (np.abs(x).max() + 1e-12))


def to_rms(x, decibels, ceiling=-2.5):
    """Sets the loudness, then holds the peaks under a ceiling by rounding them off (not by clipping)."""
    return rounded(x * (gain(decibels) / rms(x)), ceiling)


def rounded(x, ceiling):
    top = gain(ceiling)
    knee = top * 0.7
    over = np.abs(x) > knee
    x[over] = np.sign(x[over]) * (knee + (top - knee) * np.tanh((np.abs(x[over]) - knee) / (top - knee)))
    return x


def to_loudness(x, decibels, ceiling=-1.5):
    """
    As to_rms, but by how loud it is to an ear rather than by its power: what lies under 60 Hz is
    discounted and the top lifted a little (the weighting broadcast loudness is measured with).
    A chord whose power is mostly a note at 37 Hz is otherwise set far too quiet, and on small
    speakers, which cannot give that note at all, is hardly there.
    """
    freqs = np.maximum(np.fft.rfftfreq(len(x), 1.0 / SR), 1e-6)
    weight = 1.0 / np.sqrt(1.0 + (60.0 / freqs) ** 4) * (1.0 + 0.58 * np.clip(np.log2(freqs / 750.0) / 2.0, 0.0, 1.0))
    heard = rms(np.fft.irfft(np.fft.rfft(x, axis=0) * weight[:, None], len(x), axis=0))
    return rounded(x * (gain(decibels) / heard), ceiling)


def width(x, amount):
    """Stereo width: 0 mono, 1 as it is."""
    mid, side = x.mean(axis=1, keepdims=True), (x[:, :1] - x[:, 1:]) * 0.5
    return np.concatenate([mid + side * amount, mid - side * amount], axis=1)


def pitched(x, ratio):
    """Faster and higher (ratio > 1) or slower and lower: a different take from the same recording."""
    return signal.resample(x, int(round(len(x) / ratio)), axis=0)


def join(x, seconds):
    """Makes a recording into a loop: its last seconds are faded out over its first, at constant power."""
    n = int(seconds * SR)
    t = np.linspace(0.0, np.pi / 2, n)[:, None]
    body = x[:-n].copy()
    body[:n] = body[:n] * np.sin(t) + x[-n:] * np.cos(t)
    return body


def shot(x, peak=-3.0, longest=None, fall=0.03, low=45.0, wide=0.6, start_db=-26.0):
    x = trim(band(x, low=low), start_db=start_db)
    if longest and len(x) > longest * SR:
        x = x[: int(longest * SR)]
    return to_peak(fade(width(x, wide), 0.0008, fall), peak)


def family(prefix, takes, peak=-3.0):
    """Several takes of one sound, brought to the same loudness (the quiet ones up, the loud ones down, by at most 5 dB)."""
    if not takes:
        return
    levels = [db(rms(take)) for take in takes]
    middle = float(np.median(levels))
    for index, (take, level) in enumerate(zip(takes, levels), 1):
        take = take * gain(np.clip(middle - level, -5.0, 5.0))
        take = take * min(1.0, gain(peak) / (np.abs(take).max() + 1e-12))
        save(f"{prefix}_{index:02d}", take)


def noise(n, rng, tilt=0.0, low=20.0, high=20000.0, stereo=True):
    """
    Noise that repeats exactly every n samples, made in the frequency domain: random phases, an
    amplitude that falls by `tilt` dB per octave (0 white, 3 pink, 6 brown), nothing outside low..high.
    Because it is periodic it loops with no join at all, and stays so through any filter that is
    applied to its spectrum.
    """
    freqs = np.maximum(np.fft.rfftfreq(n, 1.0 / SR), 1e-6)
    # Soft edges, an octave wide (from half of `low` up to it, from `high` up to twice it), so a band does not ring.
    shape = (freqs / 1000.0) ** (-tilt / 6.0206) * np.clip(np.log2(freqs / low) + 1.0, 0.0, 1.0) * np.clip(np.log2(high / freqs) + 1.0, 0.0, 1.0)
    shape[0] = 0.0
    out = []
    for _ in range(2 if stereo else 1):
        out.append(np.fft.irfft(shape * np.exp(2j * np.pi * rng.random(len(freqs))), n))
    x = np.stack(out, axis=1) if stereo else np.repeat(out[0][:, None], 2, axis=1)
    return x / (rms(x) + 1e-12)


def colour(x, curve):
    """Multiplies a periodic sound's spectrum by curve(frequencies): a filter of any shape that keeps the loop exact."""
    freqs = np.fft.rfftfreq(len(x), 1.0 / SR)
    return np.fft.irfft(np.fft.rfft(x, axis=0) * curve(freqs)[:, None], len(x), axis=0)


def bump(freqs, centre, octaves, decibels):
    """A resonance: `decibels` at `centre`, `octaves` wide."""
    return gain(decibels * np.exp(-0.5 * (np.log2(np.maximum(freqs, 1e-6) / centre) / (octaves / 2.355)) ** 2))


def wander(n, rng, slowest, fastest, depth):
    """A slowly wandering level round 1 (turbulence, a gust), periodic over n samples."""
    return 1.0 + depth * noise(n, rng, tilt=3.0, low=slowest, high=fastest, stereo=False)[:, :1] / 2.5


def hall(seconds, rng, dark=0.45, delay=0.03):
    """
    The impulse response of a very large space: noise dying away, the high notes sooner than the
    low (`dark` is how much sooner), arriving a moment after the sound itself.
    """
    n = int((seconds * 1.6 + delay) * SR)
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for low, high, share in ((30.0, 400.0, 1.25), (400.0, 2500.0, 1.0), (2500.0, 16000.0, dark)):
        x = np.stack([rng.standard_normal(n), rng.standard_normal(n)], axis=1)
        x = signal.sosfilt(signal.butter(2, [low, high], btype="bandpass", fs=SR, output="sos"), x, axis=0)
        out += x * np.exp(-6.91 * t / (seconds * share))[:, None]
    start = int(delay * SR)
    out[:start] = 0.0
    out[start: start + int(0.02 * SR)] *= np.linspace(0.0, 1.0, int(0.02 * SR))[:, None]
    return out / np.sqrt(np.sum(out ** 2) / 2.0)


def in_hall(x, response, wet, wrap):
    """The sound heard in that space. wrap: the sound is a loop and its echo runs on round into its own beginning."""
    if wrap:
        n = len(x)
        folded = np.zeros((n, 2))
        for start in range(0, len(response), n):
            piece = response[start: start + n]
            folded[: len(piece)] += piece
        echo = np.fft.irfft(np.fft.rfft(x, axis=0) * np.fft.rfft(folded, axis=0), n, axis=0)
        return x * (1.0 - wet) + echo * wet
    echo = np.stack([signal.fftconvolve(x[:, c], response[:, c]) for c in range(2)], axis=1)
    dry = np.zeros_like(echo)
    dry[: len(x)] = x
    return dry * (1.0 - wet) + echo * wet


def pad(n, pitch, harmonics, cents, rng, spread=1.0):
    """
    Paul Nasca's PADsynth. A note is not a line at each harmonic but a band of noise round it,
    `cents` wide (wider up the series by `spread`): many players or many voices on one note,
    never quite together. One inverse transform gives n samples of it that repeat exactly.
    """
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    shape = np.zeros_like(freqs)
    for multiple, level in harmonics:
        centre = pitch * multiple
        if centre > SR * 0.45:
            continue
        sigma = max((2.0 ** (cents / 1200.0) - 1.0) * pitch * multiple ** spread / 2.0, SR / n)
        a, b = np.searchsorted(freqs, [centre - 5 * sigma, centre + 5 * sigma])
        shape[a:b] += level * np.exp(-0.5 * ((freqs[a:b] - centre) / sigma) ** 2) / np.sqrt(sigma)
    x = np.stack([np.fft.irfft(shape * np.exp(2j * np.pi * rng.random(len(freqs))), n) for _ in range(2)], axis=1)
    return x / (rms(x) + 1e-12)


def swell(n, cycles, phase, sharp=1.0):
    """A level that rises and falls `cycles` times over the loop; the sharper, the longer it stays away between."""
    t = np.arange(n) / n
    return ((0.5 - 0.5 * np.cos(2 * np.pi * (cycles * t + phase))) ** sharp)[:, None]


STRINGS = [(k, k ** -2.0) for k in range(1, 13)]
VOICES = [(1, 1.0), (2, 0.5), (3, 0.3), (4, 0.12), (5, 0.1), (6, 0.05), (8, 0.03)]
DEEP = [(1, 1.0), (2, 0.5), (3, 0.25), (4, 0.1)]
# D and the notes round it (Hz).
D1, A1, D2, A2 = 36.71, 55.0, 73.42, 110.0
D3, F3, G3, A3, B3 = 146.83, 174.61, 196.0, 220.0, 246.94
D4, E4, F4, FS4, A4, B4 = 293.66, 329.63, 349.23, 369.99, 440.0, 493.88
D5, E5, F5, FS5, GS5, A5, B5 = 587.33, 659.26, 698.46, 739.99, 830.61, 880.0, 987.77
CS6, D6, E6 = 1108.73, 1174.66, 1318.51


def glass(pitch, seconds, rng, attack=0.006, soft=False):
    """One struck note of glass (or, soft, something rounder and further away), dry."""
    n = int(seconds * SR)
    t = np.arange(n) / SR
    partials = ((1.0, 1.0, 1.0), (2.0, 0.22, 0.6), (3.0, 0.07, 0.4)) if soft else ((1.0, 1.0, 1.0), (2.76, 0.42, 0.45), (5.4, 0.2, 0.25), (8.93, 0.08, 0.15))
    x = np.zeros(n)
    for ratio, level, lasting in partials:
        if pitch * ratio < SR * 0.45:
            x += level * np.sin(2 * np.pi * pitch * ratio * t + rng.random() * 6.283) * np.exp(-t / (seconds * 0.22 * lasting))
    x *= np.minimum(t / attack, 1.0) ** 2
    lean = 0.5 + 0.3 * (rng.random() - 0.5)
    return np.stack([x * np.sqrt(1.0 - lean), x * np.sqrt(lean)], axis=1)


# ------------------------------------------------------------------------------- recordings ---

def steps():
    grass = [shot(load(f"kenney_impact/extracted/Audio/footstep_grass_{i:03d}.ogg"), longest=0.55, fall=0.12) for i in range(5)]
    family("step_moss", grass)
    names = [f"{side}{i}" for i in (1, 2, 3) for side in "LR"]
    fantozzi = "oga_footsteps_grass_sand_stone_fantozzi/extracted/Fantozzi-footsteps/flac"
    family("step_sand", [shot(load(f"{fantozzi}/Fantozzi-Sand{name}.flac")) for name in names])
    family("step_rock", [shot(load(f"{fantozzi}/Fantozzi-Stone{name}.flac")) for name in names])
    # Corsica_S's walk over frozen ground: the cleanest eight of each kind (the loudest, so the least hiss once levelled).
    corsica = "oga_footsteps_snow_gravel_corsica_s/extracted/Corsica_S-Walking_in_Snow"
    for prefix, pattern, count in (("step_gravel", "Corsica_S-Walking_on_snow_covered_gravel_{:02d}.flac", 18),
                                   ("step_snow", "Corsica_S-Walking_on_snow_covered_gravel_and_ice_{:02d}.flac", 24)):
        raw = [load(f"{corsica}/{pattern.format(i)}") for i in range(1, count + 1)]
        raw.sort(key=lambda x: -np.abs(x).max())
        family(prefix, [shot(x, longest=0.6, fall=0.1) for x in raw[:8]])
    wet = [shot(load(f"oga_sfx_100_v2_rubberduck/extracted/sfx100v2_footstep_wet_{i:02d}.ogg"), fall=0.06) for i in (1, 2, 3)]
    # Two more from a splash, cut short and taken down: a foot in the shallows, not a body in the sea.
    for name, ratio in (("splash1_0.wav", 1.12), ("splash2_0.wav", 0.94)):
        splash = shot(pitched(load(f"oga_water_splash_sand_peludo/{name}"), ratio), longest=0.5, fall=0.3)
        wet.append(band(splash, high=6000.0))
    family("step_water", wet)


def body():
    family("jump", [shot(load(f"kenney_rpg/extracted/Audio/cloth{i}.ogg"), peak=-6.0, longest=0.45, fall=0.1) for i in (1, 2, 3, 4)], peak=-6.0)
    thud = load("oga_jump_landing_mentalsanityoff/jumpland.wav")
    # (The recording opens with the scuff of the jump before it; the landing must start on the thud itself.)
    family("land", [shot(pitched(thud, ratio), peak=-2.0, low=30.0, fall=0.08, start_db=-12.0) for ratio in (1.0, 0.92, 1.07)], peak=-2.0)
    click = load("kenney_interface/extracted/Audio/switch_002.ogg")
    save("lamp_on", shot(click, peak=-5.0, longest=0.3, fall=0.05, wide=0.3))
    save("lamp_off", shot(pitched(click, 0.84), peak=-8.0, longest=0.3, fall=0.05, wide=0.3))


def wind():
    rng = np.random.default_rng(11)
    # The steadiest 70 seconds of a real wind off the Atlantic (no clipped gust, nothing too faint),
    # made into a loop. A real wind is the base because nothing made up breathes like one.
    x = band(load("commons_wind_fisterra_pd/Vento_fisterra.ogg"), low=85.0, order=3)   # under that is the wind beating on the microphone, not the wind
    length, hop = 70 * SR, 5 * SR
    best, best_score = 0, -1e9
    for start in range(0, len(x) - length, hop):
        piece = x[start: start + length]
        seconds = np.sqrt(np.mean(piece[: length // SR * SR].reshape(-1, SR, 2) ** 2, axis=(1, 2)))
        score = db(seconds.mean()) - 2.0 * (db(seconds.max()) - db(seconds.min())) / 10.0 - (60.0 if np.abs(piece).max() > 0.98 else 0.0)
        if score > best_score:
            best, best_score = start, score
    loop = join(x[best: best + length], 6.0)
    loop = loop / rms(loop)
    # The planet under it: where the gusts rise, the land itself seems to sound, low and far off
    # (a band of noise on D and on A, which is what wind over a great hollow is). It is felt more
    # than heard: 20 dB under the wind.
    n = len(loop)
    seconds = int(0.8 * SR)
    power = np.sqrt(ndimage.uniform_filter1d(np.mean(loop ** 2, axis=1), size=seconds, mode="wrap"))[:, None]
    hum = (pad(n, D2, DEEP, 35.0, rng) + 0.6 * pad(n, A2, DEEP, 35.0, rng) + 0.35 * pad(n, D3, VOICES, 50.0, rng))
    hum = in_hall(hum / rms(hum) * (power / power.mean()) ** 1.5, hall(7.0, rng), 0.6, wrap=True)
    save("loop_wind", to_rms(loop + hum / rms(hum) * gain(-20.0), -21.0), loop=True)

    # Thin air, high up: the wind as a microphone on Mars heard it, which is how wind sounds when
    # there is almost nothing to carry it.
    try:
        mars = band(load("commons_mars_wind_supercam_nasa_pd/Perseverance_rover_s_SuperCam_records_wind_on_Mars.oga"), low=50.0, high=3500.0)
        take = min(len(mars), 40 * SR)
        mars = mars[(len(mars) - take) // 2: (len(mars) + take) // 2]
        mars = join(mars, min(4.0, take / SR / 4))
        mars /= rms(mars)
        # Nearly all of that recording is under 150 Hz, which small speakers cannot give: a breath
        # of hollow air rides on its gusts so that it is heard on anything.
        gusts = np.sqrt(ndimage.uniform_filter1d(np.mean(mars ** 2, axis=1), size=SR // 2, mode="wrap"))[:, None]
        hollow = colour(noise(len(mars), rng, tilt=4.0, low=180.0, high=2600.0), lambda f: bump(f, 520.0, 1.4, 6.0)) * (gusts / gusts.mean()) ** 1.3
        save("loop_thin", to_rms(mars + 0.45 * hollow / rms(hollow), -24.0), loop=True)
    except Exception as error:
        print("no thin-air wind:", error)

    # Air going past a falling or flying body: no gusts, only speed, with the flutter of cloth in it.
    n = 8 * SR
    rush = colour(noise(n, rng, tilt=3.5, low=120.0, high=9000.0), lambda f: bump(f, 900.0, 2.5, 7.0))
    rush *= wander(n, rng, 2.0, 14.0, 0.5) * wander(n, rng, 0.2, 1.5, 0.3)
    save("loop_rush", to_rms(rush, -20.0), loop=True)


def water():
    rng = np.random.default_rng(23)
    stream = band(load("oga_sfx_100_v2_rubberduck/extracted/sfx100v2_loop_water_03.ogg"), low=140.0)
    save("loop_stream", to_rms(join(stream, 0.8), -24.0), loop=True)

    # The sea's edge: each wave a body of water arriving (low, swelling for a couple of seconds),
    # breaking (bright), and drawing back through the shingle (a long hiss). Nine of them in 44
    # seconds, no two alike, over the low unbroken sound of the sea further out; and among them
    # three real waves from a real beach.
    n = 44 * SR
    sea = 0.5 * noise(n, rng, tilt=6.0, low=40.0, high=700.0) * wander(n, rng, 0.05, 0.3, 0.6)
    sea += 0.035 * noise(n, rng, tilt=2.0, low=1200.0, high=9000.0) * wander(n, rng, 0.05, 0.4, 0.5)
    for index in range(9):
        length = int(rng.uniform(8.0, 11.0) * SR)
        t = np.arange(length) / SR
        breaks = rng.uniform(1.6, 2.8)
        swell_in = np.where(t < breaks, (t / breaks) ** 2.2, np.exp(-(t - breaks) / rng.uniform(1.0, 1.6)))
        foam = np.where(t < breaks, 0.0, (1.0 - np.exp(-(t - breaks) / 0.25)) * np.exp(-(t - breaks) / rng.uniform(1.8, 2.8)))
        size = rng.uniform(0.6, 1.0)
        wave = size * (noise(length, rng, tilt=4.0, low=90.0, high=2200.0) * swell_in[:, None] + 0.55 * noise(length, rng, tilt=1.5, low=1500.0, high=9000.0) * foam[:, None])
        lean = rng.uniform(0.25, 0.75)
        wave *= np.array([np.sqrt(1.0 - lean), np.sqrt(lean)]) * 1.4
        wave = fade(wave, 0.01, 3.0)
        at = (int(index * n / 9 + rng.uniform(-1.2, 1.2) * SR)) % n
        where = (at + np.arange(length)) % n
        sea[where] += wave
    real = [fade(trim(band(load(f"oga_beach_ocean_waves_jasinski/wave_{i:02d}_cc0-18363__jasinski__alkaibeach.flac"), low=80.0), end_db=-40.0), 0.3, 0.8) for i in (1, 2, 4)]
    for index, wave in enumerate(real):
        where = (int((index + 0.5) * n / 3) + np.arange(len(wave))) % n
        sea[where] += wave / rms(wave) * 0.55
    save("loop_sea", to_rms(sea, -23.0), loop=True)


# ------------------------------------------------------------------------------- arithmetic ---

def jets():
    rng = np.random.default_rng(37)
    # A burner, not a hiss: most of it is under 200 Hz and is felt; over that a roar with two
    # resonances (the throat of a nozzle), over that a little tearing air. All of it shudders the
    # way a flame does.
    n = 6 * SR
    rumble = noise(n, rng, tilt=6.0, low=28.0, high=220.0)
    roar = colour(noise(n, rng, tilt=4.0, low=150.0, high=3500.0), lambda f: bump(f, 310.0, 0.9, 8.0) * bump(f, 640.0, 0.7, 5.0))
    tear = noise(n, rng, tilt=1.0, low=2200.0, high=10000.0)
    jet = (1.0 * rumble + 0.7 * roar + 0.11 * tear) * wander(n, rng, 5.0, 40.0, 0.35) * wander(n, rng, 0.3, 2.0, 0.12)
    jet = width(jet, 0.7)
    save("loop_jet", to_rms(jet, -18.0), loop=True)

    # Boost: the same fire driven harder. Brighter, a turbine's two thin whistles in it, a faster shudder.
    n = 4 * SR
    hard = colour(noise(n, rng, tilt=2.5, low=300.0, high=7000.0), lambda f: bump(f, 900.0, 1.2, 6.0))
    whistle = pad(n, 1480.0, [(1, 1.0), (2, 0.4)], 14.0, rng) * 0.18
    boost = (hard + whistle + 0.5 * noise(n, rng, tilt=6.0, low=30.0, high=160.0)) * wander(n, rng, 9.0, 60.0, 0.4)
    save("loop_jet_boost", to_rms(width(boost, 0.7), -19.0), loop=True)

    def burst(seconds, shape, low, high, tilt=3.0):
        length = int(seconds * SR)
        return noise(length, rng, tilt=tilt, low=low, high=high) * shape(np.arange(length) / SR)[:, None]

    def thump(seconds, start, end, level):
        t = np.arange(int(seconds * SR)) / SR
        sweep = start * (end / start) ** (t / seconds)
        x = level * np.sin(2 * np.pi * np.cumsum(sweep) / SR) * np.exp(-t / (seconds * 0.3)) * np.minimum(t / 0.003, 1.0)
        return np.repeat(x[:, None], 2, axis=1)

    # Lighting: a bang of gas catching, bright for a tenth of a second, falling into the roar the loop takes over.
    light = burst(0.7, lambda t: np.minimum(t / 0.004, 1.0) * (0.25 + 0.75 * np.exp(-t / 0.09)) * np.clip((0.7 - t) / 0.35, 0.0, 1.0), 120.0, 9000.0, tilt=2.0)
    light += burst(0.7, lambda t: np.minimum(t / 0.02, 1.0) * np.exp(-t / 0.3), 30.0, 240.0, tilt=6.0) * 1.3
    kick = thump(0.22, 95.0, 42.0, 2.2)
    light[: len(kick)] += kick
    save("jet_ignite", to_peak(fade(light, 0.0005, 0.2), -3.0))

    # Cutting out: the fire starved (it darkens as it dies), and the valve shutting behind it.
    dies = burst(0.8, lambda t: np.exp(-t / 0.11), 60.0, 5000.0) + burst(0.8, lambda t: np.exp(-t / 0.2), 30.0, 300.0, tilt=6.0)
    valve = thump(0.12, 140.0, 70.0, 0.9)
    at = int(0.07 * SR)
    dies[at: at + len(valve)] += valve
    save("jet_stop", to_peak(fade(dies, 0.0005, 0.3), -5.0))

    # Empty: three coughs, each weaker.
    cough = np.zeros((int(0.7 * SR), 2))
    for at, level in ((0.0, 1.0), (0.15, 0.6), (0.33, 0.33)):
        puff = burst(0.2, lambda t: np.minimum(t / 0.003, 1.0) * np.exp(-t / 0.035), 90.0, 2600.0) * level
        cough[int(at * SR): int(at * SR) + len(puff)] += puff
    save("jet_empty", to_peak(fade(cough, 0.0005, 0.1), -6.0))

    # Where there is no air there is only the suit: a hum too low to notice until it is all there
    # is, beating slowly against itself, and the air it moves coming and going like breath.
    n = 12 * SR
    t = (np.arange(n) / SR)[:, None]
    hum = np.sin(2 * np.pi * 55.0 * t) + 0.8 * np.sin(2 * np.pi * (55.0 + 1.0 / 12.0) * t + 1.0) + 0.25 * np.sin(2 * np.pi * 110.0 * t) + 0.08 * np.sin(2 * np.pi * 165.0 * t)
    air = noise(n, rng, tilt=3.0, low=350.0, high=3200.0) * (0.35 + 0.65 * swell(n, 3, 0.0, 1.5)) * 0.12
    save("loop_suit", to_rms(np.repeat(hum, 2, axis=1) / rms(hum) + air, -26.0), loop=True)


def interface():
    rng = np.random.default_rng(41)
    room = hall(2.6, rng, dark=0.5, delay=0.012)

    def two_notes(first, second, name):
        x = np.zeros((int(1.9 * SR), 2))
        for pitch, at, level in ((first, 0.0, 1.0), (second, 0.11, 0.8)):
            note = glass(pitch, 1.5, rng, attack=0.012, soft=True) * level
            x[int(at * SR): int(at * SR) + len(note)] += note
        save(name, to_peak(fade(in_hall(x, room, 0.4, wrap=False)[: int(2.4 * SR)], 0.001, 0.6), -9.0))

    # The menu opening and closing: two round notes a fifth apart, up to open and down to close, far off.
    two_notes(D5, A5, "ui_open")
    two_notes(A5, D5, "ui_close")

    def breath(name, start, end):
        # Into free flight and out of it: air drawn through a narrowing gap, rising to leave the ground, falling to come back.
        length = int(1.1 * SR)
        t = np.arange(length) / SR
        x = noise(length, rng, tilt=3.0, low=80.0, high=12000.0)
        centre = start * (end / start) ** (t / t[-1])
        sweep = np.zeros_like(x)
        for a in range(0, length, 512):
            piece = slice(max(a - 512, 0), min(a + 1024, length))
            if piece.stop - piece.start < 256:
                continue
            sos = signal.butter(2, [centre[a] / 1.5, centre[a] * 1.5], btype="bandpass", fs=SR, output="sos")
            filtered = signal.sosfiltfilt(sos, x[piece], axis=0)
            window = np.hanning(len(filtered))[:, None]
            sweep[piece] += filtered * window
        # There on the key: full in a fortieth of a second, then it trails away.
        sweep *= (np.minimum(t / 0.025, 1.0) * np.exp(-t / 0.3))[:, None]
        tone = glass(end / 2 if end > start else end, 1.0, rng, attack=0.03, soft=True)
        sweep[: len(tone)] += tone * rms(sweep) * 1.2
        save(name, to_peak(fade(in_hall(sweep, room, 0.35, wrap=False)[: int(1.8 * SR)], 0.002, 0.6), -9.0))

    breath("fly_on", 220.0, 1760.0)
    breath("fly_off", 1760.0, 220.0)


# ------------------------------------------------------------------------------------ score ---

def chord(n, rng, notes):
    """Notes that come and go on their own slow cycles, so the chord is never the same twice in a loop: (pitch, harmonics, cents, level dB, cycles, phase, sharpness)."""
    out = np.zeros((n, 2))
    for pitch, harmonics, cents, level, cycles, phase, sharp in notes:
        out += pad(n, pitch, harmonics, cents, rng) * gain(level) * (swell(n, cycles, phase, sharp) if cycles else 1.0)
    return out


def score():
    rng = np.random.default_rng(53)
    # Day, on the ground: D with its fifth and ninth and a major third that comes and goes, and
    # once in each turn the raised fourth, which is the note that makes a sky feel larger. No beat,
    # no tune: a chord that breathes, in a space with a ten-second echo.
    n = 96 * SR
    day = chord(n, rng, [
        (D2, DEEP, 9.0, -9.0, 1, 0.0, 0.5), (D1, DEEP, 9.0, -16.0, 0, 0.0, 1.0),
        (D3, STRINGS, 28.0, -5.0, 2, 0.0, 1.0), (A3, STRINGS, 28.0, -6.0, 3, 0.3, 1.0), (B3, STRINGS, 28.0, -11.0, 1, 0.4, 2.0),
        (D4, STRINGS, 30.0, -11.0, 4, 0.15, 2.0), (E4, STRINGS, 30.0, -9.0, 2, 0.55, 1.5), (FS4, STRINGS, 30.0, -10.0, 3, 0.8, 2.0),
        (A4, VOICES, 55.0, -10.0, 3, 0.65, 3.0), (D5, VOICES, 60.0, -12.0, 2, 0.2, 3.0), (E5, VOICES, 60.0, -15.0, 4, 0.45, 4.0),
        (FS5, VOICES, 60.0, -15.0, 5, 0.9, 4.0), (GS5, VOICES, 65.0, -18.0, 1, 0.7, 5.0)])
    save("loop_music_day", to_loudness(in_hall(day, hall(10.0, rng), 0.6, wrap=True), -24.0), loop=True)

    # Night: the same D, but the third is minor and low, the sixth is the bright one of the Dorian
    # mode (which is what keeps it strange rather than sad), and there is more dark between the notes.
    n = 104 * SR
    night = chord(n, rng, [
        (D2, DEEP, 9.0, -8.0, 1, 0.5, 0.5), (A1, DEEP, 9.0, -13.0, 2, 0.1, 1.0), (D1, DEEP, 9.0, -16.0, 0, 0.0, 1.0),
        (D3, STRINGS, 26.0, -6.0, 2, 0.25, 1.5), (F3, STRINGS, 26.0, -9.0, 3, 0.6, 2.0), (G3, STRINGS, 26.0, -12.0, 2, 0.85, 2.5),
        (A3, STRINGS, 26.0, -8.0, 1, 0.0, 1.0), (E4, STRINGS, 28.0, -12.0, 1, 0.45, 3.0), (B3, STRINGS, 28.0, -14.0, 3, 0.1, 3.5),
        (A4, VOICES, 55.0, -14.0, 2, 0.7, 4.0), (D5, VOICES, 60.0, -16.0, 3, 0.35, 4.5), (F5, VOICES, 60.0, -19.0, 1, 0.15, 5.0)])
    night = colour(night, lambda f: 1.0 / np.sqrt(1.0 + (f / 3200.0) ** 4))
    save("loop_music_night", to_loudness(in_hall(night, hall(11.0, rng, dark=0.35), 0.62, wrap=True), -25.0), loop=True)

    # Space: no third at all. Fifths stacked on fifths from the bottom of hearing to the top of it
    # (D A E B F# C# G#), which is the sound of nothing being near; the low D never leaves, and the
    # high notes are few, thin and a long way off.
    n = 112 * SR
    space = chord(n, rng, [
        (D1, DEEP, 8.0, -12.0, 1, 0.0, 0.4), (D2, DEEP, 8.0, -9.0, 2, 0.3, 0.6), (A2, DEEP, 10.0, -11.0, 3, 0.7, 1.5),
        (D3, STRINGS, 22.0, -7.0, 1, 0.2, 1.5), (A3, STRINGS, 22.0, -8.0, 2, 0.6, 2.0), (E4, STRINGS, 24.0, -10.0, 3, 0.05, 2.5), (B4, VOICES, 45.0, -12.0, 2, 0.4, 3.0),
        (FS5, VOICES, 70.0, -15.0, 3, 0.8, 4.0), (A5, VOICES, 70.0, -17.0, 2, 0.15, 5.0), (GS5, VOICES, 70.0, -19.0, 1, 0.55, 5.0),
        (CS6, VOICES, 80.0, -18.0, 2, 0.9, 5.0), (E6, VOICES, 80.0, -20.0, 1, 0.3, 6.0)])
    save("loop_music_space", to_loudness(in_hall(space, hall(13.0, rng, dark=0.55), 0.68, wrap=True), -25.0), loop=True)

    # Notes that drift over the chord, one at a time, from the notes every one of the three chords
    # can take (D E A B). On the ground they are struck glass; in space something rounder and lower.
    far = hall(9.0, rng, dark=0.5, delay=0.04)
    for index, pitch in enumerate((D4, E4, A4, B4, D5, E5, A5, B5), 1):
        note = in_hall(glass(pitch, 7.0, rng, attack=0.004 + 0.004 * rng.random()), far, 0.72, wrap=False)[: int(11 * SR)]
        save(f"mote_ground_{index:02d}", to_peak(fade(note, 0.001, 4.0), -12.0))
    for index, pitch in enumerate((A3, D4, E4, A4, B4, D5), 1):
        note = in_hall(glass(pitch, 8.0, rng, attack=0.09, soft=True), far, 0.78, wrap=False)[: int(12 * SR)]
        save(f"mote_space_{index:02d}", to_peak(fade(note, 0.001, 4.0), -12.0))

    # Leaving the air for the first time: everything at once, slowly. The low D, the whole stack of
    # fifths rising under it for eleven seconds, one struck note at the top, and a long way down.
    n = 30 * SR
    t = (np.arange(n) / SR)[:, None]
    rise = np.where(t < 11.0, (t / 11.0) ** 2.0, np.exp(-(t - 11.0) / 5.5)) * np.clip((30.0 - t) / 6.0, 0.0, 1.0)
    awe = chord(n, rng, [(D1, DEEP, 8.0, -9.0, 0, 0, 1), (D2, DEEP, 8.0, -5.0, 0, 0, 1), (A2, DEEP, 10.0, -7.0, 0, 0, 1), (D3, STRINGS, 26.0, -6.0, 0, 0, 1),
                         (A3, STRINGS, 26.0, -7.0, 0, 0, 1), (E4, STRINGS, 28.0, -9.0, 0, 0, 1), (B4, STRINGS, 28.0, -11.0, 0, 0, 1), (FS5, VOICES, 60.0, -14.0, 0, 0, 1),
                         (CS6, VOICES, 70.0, -18.0, 0, 0, 1)]) * rise
    bell = glass(D5, 8.0, rng) + 0.7 * glass(A5, 8.0, rng)
    at = int(10.8 * SR)
    awe[at: at + len(bell)] += bell * rms(awe[at: at + SR]) * 1.5
    awe = in_hall(awe, hall(12.0, rng), 0.6, wrap=False)[: int(40 * SR)]
    save("awe", to_peak(fade(awe, 0.5, 8.0), -5.0))


def cosmos():
    """Recordings of space itself (NASA's instruments, shifted into hearing): the star's hum, and voices heard far out."""
    rng = np.random.default_rng(67)
    nasa = SRC.parent / "src_nasa"

    # The star. Forty days of the Sun's surface rising and falling, as SOHO measured it: one low
    # note, 142 Hz at the speed NASA released it. Here it is tuned the third of a tone up to the
    # score's D, doubled an octave below, and set in the roar of a furnace the size of a world.
    sun = band(load("sun_soho/sun_sonification.wav", nasa), low=45.0, high=3000.0)
    high = pitched(sun, D3 / 142.25)
    low = pitched(sun, D2 / 142.25)[: len(high)]
    hum = join(high / rms(high) + 0.8 * low / rms(low), 5.0)
    n = len(hum)
    roar = noise(n, rng, tilt=6.0, low=24.0, high=420.0) * wander(n, rng, 0.03, 0.4, 0.5) + 0.2 * noise(n, rng, tilt=3.0, low=300.0, high=4000.0) * wander(n, rng, 0.5, 6.0, 0.5)
    star = in_hall(hum / rms(hum) + 0.8 * roar, hall(8.0, rng), 0.5, wrap=True)
    save("loop_star", to_loudness(star, -23.0), loop=True)

    def voice(name, relative, start, seconds):
        # What an instrument heard out there, with the hiss of the instrument taken off it (whatever
        # in each band is no louder than that band usually is) and the rest left to ring in a very
        # large space: a voice from a long way off, not a recording played back.
        x = load(relative, nasa)[int(start * SR): int((start + seconds) * SR)]
        x = band(x, low=220.0, high=6000.0)
        f, _, spec = signal.stft(x, SR, nperseg=2048, noverlap=1536, axis=0)
        level = np.abs(spec)
        floor = np.median(level, axis=2, keepdims=True) * 2.2
        spec *= np.clip((level - floor) / (level + 1e-12), 0.0, 1.0) ** 1.5
        _, clean = signal.istft(spec, SR, nperseg=2048, noverlap=1536, time_axis=2, freq_axis=0)
        clean = fade(np.ascontiguousarray(clean.T if clean.shape[0] == 2 else clean)[: len(x)], 2.5, 3.5)
        far = in_hall(clean / (rms(clean) + 1e-9), hall(10.0, rng, dark=0.5, delay=0.05), 0.8, wrap=False)[: len(x) + 8 * SR]
        save(name, to_peak(fade(far, 0.5, 6.0), -12.0))

    voice("voice_space_01", "van_allen_whistler/whistler_waves_2017.mp3", 1.0, 22.0)
    voice("voice_space_02", "juno_auroras/PIA21037_audio.wav", 28.5, 23.0)
    voice("voice_space_03", "juno_ganymede_flyby/PIA25030_audio.wav", 6.0, 26.0)
    voice("voice_space_04", "saturn_radio_eerie/PIA07966.wav", 22.0, 26.0)


def credits():
    lines = ["# Sounds", "",
             "Made by `Tools/audio/make.py`. Everything not listed here is synthesised by that script (the jets, the air",
             "rushing past, the sea's waves, the suit, the interface notes and the whole score).", "",
             "Recordings used. Those marked CC0 or public domain are free of any condition; NASA's are not copyrighted and are",
             "used under NASA's media guidelines, with the credit NASA gives:", ""]
    for key in sorted(USED):
        if key in CREDITS:
            name, author, page, licence = CREDITS[key]
            lines.append(f"- **{name}** by {author} ({licence}): {page}")
    (OUT / "CREDITS.md").write_text("\n".join(lines) + "\n")


def main():
    parts = {"step": steps, "jump": body, "land": body, "lamp": body, "loop_wind": wind, "loop_thin": wind, "loop_rush": wind, "loop_stream": water, "loop_sea": water,
             "loop_jet": jets, "jet": jets, "loop_suit": jets, "ui": interface, "fly": interface, "loop_music": score, "mote": score, "awe": score,
             "loop_star": cosmos, "voice": cosmos}
    done = []
    for prefix, part in parts.items():
        if part not in done and (not ONLY or any(prefix.startswith(only) or only.startswith(prefix) for only in ONLY)):
            done.append(part)
            part()
    if not ONLY:
        credits()
    print("\n".join(REPORT))
    print(f"{len(REPORT)} sounds in {OUT}")


if __name__ == "__main__":
    main()
