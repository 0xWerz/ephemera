"""The game's sounds from professional recordings: fetches each chosen recording, cuts and shapes it, and writes
unreal/SourceAssets/audio/<name>.wav for Tools/build_audio.py to import.

    Tools/audio/run.sh pro.py            (everything)
    Tools/audio/run.sh pro.py loop_jet   (only the names given)

Why this exists: the first sound set was synthesised from filtered noise and the owner turned it down ("static
noise everywhere"; of the jetpack, "bleeds my ears"), and then turned down a jetpack shortlist made of fire, gas
burner and steam recordings ("get actual premium proper audio effects from proper places ... dont give me options
to choose u anaylze"). So every sound here is a recording made by a professional for the thing it is used as, the
choice is made here and written down beside each one, and nobody is asked to audition.

Nobody choosing could listen. The choices rest on what each recording is (its library, its name, the keywords its
maker gave it), on measurements (docs in .scratch/audio/sound_features.py: spectral slope, hiss above 4 kHz, narrow
tones, steadiness, crackle) and on reading spectrograms (.scratch/audio/see.py). Two rules came out of the owner's
verdicts: nothing that is an even bed of broadband noise (that is what reads as static, however dark it is made),
and a thing must be a recording of that thing.

Source: the Sonniss GDC Game Audio Bundles, as held file by file on the Internet Archive. Licence: free to use in a
game, with no credit required; the recordings may not be passed on as sound files, and may not be used to train AI.
The fetched originals stay in .scratch/audio/lib and are not part of the project.
"""
import csv
import json
import pathlib
import subprocess
import sys
import urllib.parse
import wave
from fractions import Fraction

import av
import numpy as np
from scipy import signal

WORKSPACE = pathlib.Path(__file__).resolve().parents[3]
SCRATCH = WORKSPACE / ".scratch/audio"
LIB = SCRATCH / "lib"
OUT = pathlib.Path(__file__).resolve().parents[2] / "SourceAssets/audio"
RATE = 48000

# name -> what it is cut from and how. `file` is the recording's own file name in the bundles' index.
# kind "loop": the steadiest `seconds` of it, its end folded into its start over `cross` seconds.
# kind "shot": from its first sound, at most `seconds`, faded out over `fall`.
PICKS = {
    # ---- the jetpack. Held, it must be something you can listen to for a tank's seven seconds, many times over.
    # Verdicts so far: a jet made of filtered noise, "static noise that bleeds my ears"; then an engine hum (PMSFX's airy
    # intake over InspectorJ's held spaceship chord), "sounds like fucking star wars swords". Measured, that hum put 45% of
    # its energy into narrow peaks standing up to 19.8 dB over their neighbours (814 Hz), four of them on one harmonic
    # series, and the game slid its pitch 24% with thrust: a lightsaber's hum. A jetpack's thrust is a gas jet, a broadband
    # roar with body. So: a real gas burner, its ignition and its cut-off from a real gas burner, and nothing tonal.
    "loop_jet": dict(
        file="Gas Burner Stereo 4.wav", kind="loop", seconds=14.0, cross=2.5, rms=-28.0, highpass=40.0, presence=(500.0, 2500.0, 3.0),
        why="Digital Rain Lab's 'Driving Forces', a library of propulsion sources for vehicles and spacecraft: a gas burner roaring. Of the "
            "burner, torch, jet, thruster and rocket recordings in the bundles it is the one that is a roar and nothing else: no narrow "
            "peak more than 1.7 dB over its neighbours across its 25 s (the hum it replaces: 19.8 dB), 96% of its energy between 60 Hz "
            "and 3 kHz, 0.1% above 4 kHz (so no hiss), steady within 1.1 dB, with the 20-50 Hz flutter of a flame that makes a roar a "
            "roar rather than an even bed of noise. The jets, the turbines and the pulse jet all whine or buzz at one pitch (the RC jet "
            "turbine 44% above 4 kHz, the pulse jet 10 to 17 dB tonal); the fighters are fly-bys that slide in pitch. The band small speakers "
            "play is lifted 3 dB. The game opens a low-pass with thrust (brighter, not higher) and keeps the pitch within 2%."),
    "loop_jet_boost": dict(
        file="AEROJet_Blast Off Clean_344 Audio_Air Designed.wav", kind="loop", start=5.5, seconds=2.4, cross=0.5, rms=-27.0, lowpass=5500.0,
        why="344 Audio's 'Air Designed': a jet blast-off. Its last 2.9 s, at full thrust and level within 2 dB, laid over the burner while "
            "boosting (the game plays both): harder and brighter (centroid 850 Hz against the burner's 380), still without a tone (no "
            "peak over 4.7 dB). The top is rolled off at 5.5 kHz: at full it reaches 12 kHz, which is where a roar turns to hiss."),
    "jet_ignite": dict(
        file="OLD STUFF_Water Heater_01.wav", kind="shot", start=1.745, seconds=1.0, fall=0.45, peak=-7.0, highpass=30.0,
        why="GainWalkers' 'Old Stuff': an old gas water heater lighting, recorded close. From the gas rushing in, 25 ms before it catches: "
            "the whoomp of the flame lighting (centred near 80 Hz) and the first half-second of its roar. A real burner igniting; the "
            "igniter's click and valve pop before it are left out, so the whoomp lands on the press."),
    "jet_stop": dict(
        file="OLD STUFF_Water Heater_01.wav", kind="shot", start=46.795, seconds=0.45, fall=0.2, peak=-14.0, highpass=30.0,
        why="The same heater going out: from the moment its gas valve shuts (the valve's click is left out: the game plays this as the "
            "jets' roar has already died away) the flame collapses, 25 dB in a quarter of a second, with its last flutter. The jet "
            "going out was the held jet closed down by a filter; this is a burner's real cut-off."),
    "jet_empty": dict(
        file="Bluezone_BC0268_switch_button_click_ignition_002_03.wav", kind="shot", seconds=0.6, fall=0.15, peak=-12.0,
        why="Bluezone's switches: an igniter's click. Asking for thrust from an empty tank is exactly that, a click and nothing catching."),
    # ---- the air and the water. Wind was the one list the owner called "good ish".
    "loop_wind": dict(
        file="38 Wind in grass_Velebit mountain.WAV", kind="loop", seconds=45.0, cross=4.0, rms=-23.0, highpass=30.0,
        why="Ivo Vicic, a field recordist: wind through grass on an open mountain, which is what Dawnhollow's land is. Steady, deep, no hiss "
            "(0.1% above 4 kHz), no birds or people named in it."),
    # (High, thin air has no recording. The one tried, an "air tone" from the Scottish Highlands, is on its spectrogram an
    # even haze from 50 Hz to 6 kHz with mains hum under it: the very thing that was called static. Silence up there.)
    "loop_rush": dict(
        file="Skydiver - ONBRD - FLYING - Exit Plane Jump and Flying 2", match="Skydiver - ONBRD - FLYING - Exit Plane Jump and Flying 2",
        kind="loop", seconds=18.0, cross=2.0, rms=-24.0, highpass=35.0,
        why="Pole Position's skydiving library: a microphone worn in free fall. Air rushing past a falling body is this recording, not a "
            "likeness of it."),
    "loop_sea": dict(
        file="WATRWave_Medium Waves at Pebble Beach_JSE_RCoN_Stereo.wav", kind="loop", seconds=58.0, cross=5.0, rms=-26.0, highpass=30.0,
        why="Just Sound Effects on the rocky coast of Norway: medium waves on a pebble beach, a northern shore of dark stones like "
            "Dawnhollow's. The least hiss of the ten shores measured (0.6% above 4 kHz) and the waves come and go (it breathes by 4.6 dB)."),
    "loop_stream": dict(
        file="Stream - LIGHT - Medium Speed - Flow - Left-Heavy Trickles.wav", kind="loop", seconds=20.0, cross=2.0, rms=-28.0, highpass=60.0, lowpass=4500.0, centre=0.5,
        why="Pole Position in a winter forest: a light stream. Running water is bright by nature; this is the darkest and steadiest of the "
            "eight measured (4% above 4 kHz, half a decibel of wander). It leans left, so the two sides are drawn half-way together; and "
            "its top is closed at 4.5 kHz and it is set low, because close running water is the nearest thing in nature to static."),
    "loop_star": dict(
        file="DSGNDron_Slow Pulse Drone_JSE_CD.wav", kind="loop", seconds=48.0, cross=5.0, rms=-27.0,
        why="Just Sound Effects' cinematic drones: a slow pulse. Deep, entirely tonal, and it beats, as something that size should."),
    # (The score is not here any more: it is composed music, laid out by score.py. The held pads, single notes, swell
    # and whale calls that stood here were called "so bad and generic".)
    "fly_on": dict(file="Whoosh,Sound Design,Airy,Full,Soft,Buffet,Medium.wav", kind="shot", seconds=2.4, fall=1.0, peak=-11.0,
                   why="Airborne Sound's airy whooshes: soft and full, with nothing above 4 kHz."),
    "fly_off": dict(file="Whoosh_Source_Rope_03.wav", kind="shot", seconds=1.6, fall=0.7, peak=-12.0,
                    why="SoundBits' 'Tiny Transitions': a rope through the air, shorter and lower."),
}
# Files this module's full run leaves alone: the footsteps (steps.py), the movement library (foley.py), the older CC0 takes
# (make.py) and the music (score.py).
KEPT_PREFIXES = ("step_", "roll_", "suit_", "jump_", "land_", "lamp_", "ui_", "loop_music_", "debris_", "cloth_", "loop_cloth_", "gear_",
                 "loop_gear_", "loop_brush_", "swish_", "wade_", "loop_wade", "splash_", "loop_underwater", "bubbles_", "start_", "settle_",
                 "turn_", "skid_", "takeoff_", "breath_", "squelch_", "drip_", "pebble_", "loop_scree")
# Made from another (name -> the PICKS name it is made from): falling and closing down in 0.9 s. Empty since the jet's
# cut-off became a recording of one (2026-10-14).
DERIVED = {}


def index():
    rows = list(csv.DictReader(open(SCRATCH / "audition/index.tsv"), delimiter="\t"))
    return rows


def find(rows, pick):
    wanted = pick.get("match") or pick["file"]
    hits = [r for r in rows if (r["file"] == wanted or (pick.get("match") and wanted in r["file"])) and (not pick.get("library") or pick["library"] in r["library"])]
    if not hits:
        raise SystemExit(f"not in the bundles' index: {wanted}")
    return hits[0]


def fetch(row):
    """The recording as its lossless FLAC (made by the archive from the original WAV), kept in .scratch/audio/lib."""
    target = LIB / row["item"] / (row["library"] + "/" + pathlib.Path(row["file"]).with_suffix(".flac").name)
    if target.exists() and target.stat().st_size > 10000:
        return target
    meta = json.loads((SCRATCH / "sonniss/ia" / f"{row['item']}.json").read_text())
    original = row["library"] + "/" + row["file"]
    flac = next((f["name"] for f in meta["files"] if f.get("original") == original and f.get("format") == "Flac"), None)
    source = flac or original
    if not flac:
        target = target.with_suffix(pathlib.Path(row["file"]).suffix)
    target.parent.mkdir(parents=True, exist_ok=True)
    url = f"https://archive.org/download/{row['item']}/" + urllib.parse.quote(source)
    subprocess.run(["curl", "-sS", "-L", "--retry", "5", "--retry-delay", "4", "--limit-rate", "4M", "--max-time", "1200", "-o", str(target), url], check=True)
    if target.stat().st_size < 10000:
        raise SystemExit(f"could not fetch {source}")
    return target


def load(path):
    container = av.open(str(path))
    resampler = av.AudioResampler(format="fltp", layout="stereo", rate=RATE)
    chunks = []
    for frame in container.decode(container.streams.audio[0]):
        chunks.extend(out.to_ndarray() for out in resampler.resample(frame))
    chunks.extend(out.to_ndarray() for out in (resampler.resample(None) or []))
    container.close()
    return np.concatenate(chunks, axis=1).astype(np.float64)


def level_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)


def steadiest(x, seconds):
    mono = x.mean(axis=0)
    hop = RATE // 10
    count = len(mono) // hop
    levels = np.array([level_db(mono[i * hop:(i + 1) * hop]) for i in range(count)])
    n = int(seconds * 10)
    if count <= n + 2:
        return 0
    loud = np.percentile(levels, 60)
    best, at = 1e9, 0
    for i in range(0, count - n, 2):
        part = levels[i:i + n]
        cost = np.std(part) + max(0.0, loud - 6.0 - part.mean())
        if cost < best:
            best, at = cost, i
    return at * hop


def butter(x, kind, hz, order=2):
    sos = signal.butter(order, hz, btype=kind, fs=RATE, output="sos")
    return signal.sosfiltfilt(sos, x, axis=1)


def presence(x, low, high, gain_db):
    """Lifts the band between low and high (a wide, gentle bell made of the band itself added back)."""
    band = butter(butter(x, "highpass", low, 1), "lowpass", high, 1)
    return x + band * (10 ** (gain_db / 20.0) - 1.0)


def shape(x, pick):
    x = x - x.mean(axis=1, keepdims=True)
    if pick.get("highpass"):
        x = butter(x, "highpass", pick["highpass"])
    if pick.get("lowpass"):
        x = butter(x, "lowpass", pick["lowpass"], 3)
    if pick.get("presence"):
        x = presence(x, *pick["presence"])
    if pick.get("centre"):
        k = pick["centre"] * 0.5
        left, right = x[0] * (1 - k) + x[1] * k, x[1] * (1 - k) + x[0] * k
        right = right * (np.sqrt(np.mean(left ** 2)) / (np.sqrt(np.mean(right ** 2)) + 1e-12))
        x = np.stack([left, right])
    return x


def make_loop(x, pick):
    seconds, cross = pick["seconds"], pick["cross"]
    n, c = int(seconds * RATE), int(cross * RATE)
    start = int(pick["start"] * RATE) if "start" in pick else steadiest(x, seconds + cross)
    part = x[:, start:start + n + c]
    if part.shape[1] < n + c:
        raise SystemExit(f"recording too short for a {seconds} s loop")
    out = part[:, :n].copy()
    t = np.linspace(0.0, np.pi / 2, c)
    # The stretch after the loop's end is folded under its beginning: the end then runs on into the start.
    out[:, :c] = part[:, :c] * np.sin(t) + part[:, n:n + c] * np.cos(t)
    return out


def make_shot(x, pick):
    mono = np.abs(x).max(axis=0)
    if "start" in pick:
        first = int(pick["start"] * RATE)
    else:
        loud = np.where(mono > mono.max() * 10 ** (-38 / 20.0))[0]
        first = max(int(loud[0]) - int(0.004 * RATE), 0) if len(loud) else 0
    part = x[:, first:first + int(pick["seconds"] * RATE)].copy()
    rise = min(int(0.004 * RATE), part.shape[1])
    part[:, :rise] *= np.linspace(0.0, 1.0, rise)
    fall = min(int(pick.get("fall", 0.2) * RATE), part.shape[1])
    part[:, -fall:] *= np.cos(np.linspace(0.0, np.pi / 2, fall)) ** 2
    return part


def repitch(x, ratio):
    """Plays it faster (ratio above 1: higher and shorter) or slower."""
    fraction = Fraction(1.0 / ratio).limit_denominator(240)
    return signal.resample_poly(x, fraction.numerator, fraction.denominator, axis=1)


def tune_to_g(x):
    """Moves a single note the little way to the nearest of G A B D E, so it sits on the beds (which are on G)."""
    mono = x.mean(axis=0)[:RATE * 3]
    spectrum = np.abs(np.fft.rfft(mono * np.hanning(len(mono))))
    freqs = np.fft.rfftfreq(len(mono), 1.0 / RATE)
    keep = (freqs > 150) & (freqs < 2500)
    pitch = freqs[keep][np.argmax(spectrum[keep])]
    note = 12 * np.log2(pitch / 196.0)                    # semitones above G3
    scale = np.array([0, 2, 4, 7, 9])
    octave, within = np.floor(note / 12.0), note % 12.0
    choices = np.concatenate([scale, [12]])
    nearest = choices[np.argmin(np.abs(choices - within))] + 12 * octave
    shift = nearest - note
    return repitch(x, 2 ** (shift / 12.0)), pitch, shift


def finish(x, pick):
    if "rms" in pick and not pick.get("under"):
        x = x * 10 ** ((pick["rms"] - level_db(x)) / 20.0)
    top = np.abs(x).max()
    limit = 10 ** (pick.get("peak", -1.5) / 20.0)
    if "peak" in pick or top > limit:
        x = x * (limit / top)
    return x


def write(name, x):
    OUT.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(abs(hash(name)) % (2 ** 32))
    dither = (rng.random(x.shape) - rng.random(x.shape)) / 32768.0
    data = np.clip(np.round((x + dither) * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(str(OUT / f"{name}.wav"), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(data.T.tobytes())


def main(only):
    rows = index()
    made, credits = {}, []
    for name, pick in PICKS.items():
        row = find(rows, pick)
        credits.append((name, row, pick["why"]))
        if only and name not in only and not (name in DERIVED.values() and any(k in only for k in DERIVED)):
            continue
        x = load(fetch(row))
        note = ""
        if pick.get("pitch"):
            x = repitch(x, pick["pitch"])
        x = shape(x, pick)
        x = make_loop(x, pick) if pick["kind"] == "loop" else make_shot(x, pick)
        if pick.get("under"):
            # A second recording laid under the first, as its own loop of the same length.
            layer = dict(pick["under"], kind="loop", seconds=pick["seconds"], cross=pick["cross"])
            layer_row = find(rows, layer)
            credits.append((name + " (under)", layer_row, ""))
            y = make_loop(shape(load(fetch(layer_row)), layer), layer)
            x = x * 10 ** ((pick["rms"] - level_db(x)) / 20.0) + y * 10 ** ((layer["rms"] - level_db(y)) / 20.0)
        if pick.get("tune"):
            x, pitch, shift = tune_to_g(x)
            note = f"  (its note {pitch:.0f} Hz, moved {shift:+.2f} semitones)"
        x = finish(x, pick)
        made[name] = x
        write(name, x)
        print(f"{name:18s} {x.shape[1] / RATE:6.1f} s  level {level_db(x):6.1f} dB  peak {20 * np.log10(np.abs(x).max()):6.1f} dB  from {row['library'][:40]}{note}", flush=True)
    for name, source in DERIVED.items():
        if only and name not in only:
            continue
        x = made[source][:, :int(0.9 * RATE)].copy()
        pieces = np.array_split(np.arange(x.shape[1]), 9)
        for i, where in enumerate(pieces):
            x[:, where] = butter(x, "lowpass", 2600.0 * 0.72 ** i + 180.0)[:, where]
        x *= np.exp(-np.arange(x.shape[1]) / (0.22 * RATE))
        x[:, -int(0.1 * RATE):] *= np.linspace(1.0, 0.0, int(0.1 * RATE))
        x = finish(x, dict(peak=-14.0))
        write(name, x)
        print(f"{name:18s} {x.shape[1] / RATE:6.1f} s  peak {20 * np.log10(np.abs(x).max()):6.1f} dB  made from {source}", flush=True)
    if only:
        update_credit_rows(credits, set(only) | {name for name, source in DERIVED.items() if source in only})
    else:
        write_credits(credits)
        # What this set and the kept footsteps do not account for is the old noise-made set: gone from the folder,
        # so it cannot come back into a build. The footstep and movement library (steps.py, foley.py: step_, roll_, debris_,
        # cloth_, gear_, swish_, wade_, splash_, bubbles_, start_, settle_, turn_, skid_, takeoff_, land_, breath_ and the loops
        # loop_cloth_, loop_gear_, loop_brush_, loop_wade, loop_underwater, loop_scree, squelch_, drip_, pebble_) is kept too.
        kept = set(PICKS) | set(DERIVED)
        for path in sorted(OUT.glob("*.wav")):
            if path.stem not in kept and not path.stem.startswith(KEPT_PREFIXES):
                path.unlink()
                print("removed", path.name)


def write_credits(credits):
    lines = ["# Sounds", "",
             "Recordings by professional sound recordists and designers, from the Sonniss GDC Game Audio Bundles (free to use in a game; not to be",
             "passed on as sound files; not for training AI). Chosen and cut by `Tools/audio/pro.py`, which also says why each was chosen.", "",
             "| Sound | Recording | Library | Bundle |", "|---|---|---|---|"]
    for name, row, _ in credits:
        lines.append(f"| `{name}` | {row['file']} | {row['library']} | {row['year']} |")
    lines += [f"| `{name}` | made from `{source}` | | |" for name, source in DERIVED.items()]
    lines += ["",
              "Footsteps, jumps, landings, the lamp's switch and the menu's clicks are earlier CC0 recordings (Kenney, Fantozzi, Corsica_S,",
              "MentalSanityOff, rubberduck and Peludo on OpenGameArt), made into takes by `Tools/audio/make.py`.", ""]
    # Footsteps (steps.py) and the music (score.py) add their own sections below this one: they are kept.
    path = OUT / "CREDITS.md"
    rest = ""
    if path.exists():
        was = path.read_text()
        cut = min((was.find(m) for m in ("\n## Footsteps and movement\n", "\n## Foley library\n", "\n## Music\n") if was.find(m) >= 0), default=-1)
        rest = was[cut:] if cut >= 0 else ""
    path.write_text("\n".join(lines) + rest)


def update_credit_rows(credits, names):
    """After a run of some names: their rows in CREDITS.md's first table replaced (the rest of the file as it was). The rows
    of a name that is now made otherwise, or no longer made from another, go."""
    path = OUT / "CREDITS.md"
    if not path.exists():
        return
    def sound(line):
        return line.split("`")[1].split(" (")[0] if line.startswith("| `") else None
    new = [f"| `{name}` | {row['file']} | {row['library']} | {row['year']} |" for name, row, _ in credits if name.split(" (")[0] in names]
    out, placed = [], False
    for line in path.read_text().split("\n"):
        if sound(line) in names and not line.startswith("| `step_"):
            if not placed:
                out += new
                placed = True
            continue
        out.append(line)
    if not placed:
        at = max(i for i, line in enumerate(out) if line.startswith("| `") and "`step_" not in line) + 1
        out[at:at] = new
    path.write_text("\n".join(out))


if __name__ == "__main__":
    main(set(sys.argv[1:]))
