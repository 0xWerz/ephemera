"""Footsteps: the impact and roll layers of the layered step system, cut from professional foley walks.

    Tools/audio/run.sh steps.py                 (every surface, then the loudness audit)
    Tools/audio/run.sh steps.py moss gravel     (only these surfaces)
    Tools/audio/run.sh steps.py audit           (only the audit, of every key in SourceAssets/audio)

What it writes (a key is the file name without its _NN take number; the game groups takes by key):

    step_<surface>_<tier>_<foot>_NN.wav   the impact: surface moss sedge ash gravel basalt snow water,
                                          tier creep walk jog run sprint, foot l r
    roll_<class>_<tier>_NN.wav            the roll / toe-off that follows the impact: class soft grit snow

This is section 6.6 of docs/research/footsteps_and_space_audio.md, done as written:

  * Steps are found in each recording by their onsets (a long-window level rise refined by a short one), then followed
    through the recording with a periodic tracker (the recorded walkers keep a steady cadence), so a heel and its toe are
    not taken for two steps. Feet are tagged by parity in recording order.
  * Each step is split at its second onset (the toe) into an impact, ending 3 ms before it, and a roll, the rest. The two
    are separate pools, so the game can recombine an impact and a roll from different steps (the manifest,
    foley_manifest.json, keeps every file's source-step id). Where the toe follows the heel by under 20 ms, or the tier is
    sprint, the step stays whole as one impact (the brief's anti-flam rule).
  * A step's tier comes from its own energy and from the pace of the walk it was cut from (a creep take is a soft one, a
    sprint take a hard one: that is what the tiers are for). The gait level differences are NOT baked in: the game adds
    them (creep -10, walk 0, jog +4, run +8, sprint +10 dB). Every tier pool is set to the same reference, -25 dB RMS (the
    walk level of the old set), and every take keeps its natural spread around its pool's mean, within +-2 dB.
  * Stereo is kept. Below 120 Hz the two sides are summed (the thump is centred), 55 Hz high-pass, and the right foot's
    small bias (+0.5 dB, balance 0.10 toward its side) is baked in. True peaks stay under -1.5 dBFS.

Sources, licence: Sonniss GDC bundles through the index (see pro.py). Real recordings only. Where a surface has no
recording of its own (dry black sand / ash; bare basalt), the brief's nearest real substitutes are used and said so in
SURFACES below and in CREDITS.md.
"""
import json
import pathlib
import re
import sys
from fractions import Fraction

import numpy as np
from scipy import ndimage, signal

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pro
from pro import RATE, OUT

TIERS = ("creep", "walk", "jog", "run", "sprint")
GAIT_DB = (-10.0, 0.0, 4.0, 8.0, 10.0)             # what the game adds per tier (brief 6.3): NOT baked into the files
TIER_DB = (0.0, 0.0, 0.0, 0.0, 0.0)                # what is baked: every tier pool at the same reference
WALK_RMS = -25.0                                   # the reference, dB RMS (loudest 50 ms): the walk level of the old set
SPREAD = 2.0                                       # a take keeps its own level within +-2 dB of its pool's mean
IMPACT_TAKES = (6, 10, 14, 12, 8)                  # per surface and tier, both feet together (brief 6.2, L1)
ROLL_TAKES = (6, 8, 8, 6, 4)                       # per class and tier (brief 6.2, L2)
PACE_CENTRES = (1.4, 2.05, 2.7, 3.1, 3.4)          # steps a second at the middle of each tier (brief 6.3)
HIGHPASS_HZ = 55.0
MONO_BELOW_HZ = 120.0
BALANCE = 0.10                                     # toward the foot's own side
RIGHT_BIAS_DB = 0.5
PEAK_CEILING_DB = -1.5                             # true peak, with room under the -1 dBFS the brief asks for
LEVEL_WINDOW = 0.05                                # a step's level is its RMS over its loudest 50 ms (the impact). The RMS of
                                                   # the whole decaying take is not used: these takes peak 14 to 20 dB above
                                                   # it, and the +10 dB of a sprint would then sit over 0 dBFS
TOE_WINDOWS_MS = ((40, 320), (30, 320), (25, 250), (15, 200), None)   # where a recorded toe may follow its heel, by tier; the
                                                   # sprint impact stays whole (anti-flam: its roll is inside it)
ROLL_TOE_WINDOW_MS = (12, 320)                     # a roll can be taken from any step with a toe, sprint steps too
HOP = RATE // 1000                                 # one millisecond

MANIFEST = OUT / "foley_manifest.json"
KEY_RE = re.compile(r"^(?P<key>.+)_(?P<take>\d\d)$")
NEW_KEY_RE = re.compile(r"^(step_(moss|sedge|ash|gravel|basalt|snow|water)_(creep|walk|jog|run|sprint)_[lr]|roll_(soft|grit|snow)_(creep|walk|jog|run|sprint)"
                        r"|debris_(grit|snow|stem)|squelch|drip|pebble|loop_scree|cloth_(light|medium|hard)|loop_cloth_(walk|jog)|gear_tick|loop_gear_bounce|loop_brush_(soft|hard)"
                        r"|swish_grass|wade_(ankle|shin|knee)|loop_wade|splash|loop_underwater|bubbles|(start|settle|turn|skid)_(soft|grit|snow)"
                        r"|takeoff|land_(soft|grit|snow|water)_(soft|medium|hard)|breath_(calm|working|heavy|recovery))$")
OWN_RE = re.compile(r"^(step_(moss|sedge|ash|gravel|basalt|snow|water)_(creep|walk|jog|run|sprint)_[lr]|roll_(soft|grit|snow)_(creep|walk|jog|run|sprint))_\d\d$")

# surface -> class (for rolls and for the game's debris and events), and what each is cut from.
#   match   the recording's own file name (or a part of it) in the bundles' index
#   mode    track: a walk, steps followed through it by its cadence; free: separate steps at irregular spacing;
#           whole: the file is one step
#   gait    where this recording sits, 0 creep ... 4 sprint, when its pace says nothing (whole, free). The others are
#           placed by their measured pace.
#   part    ("dull"|"bright", fraction): keep that share of the steps by spectral centroid (two surfaces share a walk)
#   pitch   semitones (a substitute made a little lower and heavier)
SURFACES = {
    # Moss and sedge (2026-10-14): the owner, on walking them, "like im walking into a plastic bag". Measured, the sedge steps
    # put 48 to 68% of their energy above 4 kHz (centroid 5.4 to 7.3 kHz), and their creep steps and the soft rolls cut from
    # them carried a crinkle of 54 to 80 per mille (sharp 3-10 kHz transients against the whole sound): PMSFX's single steps
    # in DRY grass, shrubs and pine needles crinkle at 58 to 91 transients a second. Gone. What these grounds are is wet moss
    # cushions and turf: a soft, low, damp thud with a little texture. So the thud is a walk in sneakers on carpet (The Sound
    # Pack Tree; 0.2% above 4 kHz, centroid 340 Hz), the texture a grass walk and wet mud closed at 3.5 / 4.5 kHz, and a
    # bare foot on grass under all of it for weight.
    "moss": dict(cls="soft", lowpass=3500.0, body=True, sources=[
        dict(match="1806 - Footsteps - Sneakers on Carpet - 90 fpm - Loop", mode="track", part=("dull", 0.5)),
        dict(match="169_Foley_Footsteps_Grass_Sneaker_Walk_Fast_Run_Jog_Close", mode="track", part=("dull", 0.5)),
        dict(match="mud 1 loop", mode="track")],
        why="A soft, low, damp thud: The Sound Pack Tree's walk in sneakers on carpet (its duller half: the thud, nearly nothing above "
            "4 kHz), Tovusound's foley walk on grass (the damper half of its steps) and RYK's steps in mud, all closed at 3.5 kHz, with a bare-foot thump "
            "under (Joshua Reinhardt). Moss is quiet and wet (brief 7.1); no dry grass."),
    "sedge": dict(cls="soft", lowpass=4500.0, body=True, sources=[
        dict(match="1806 - Footsteps - Sneakers on Carpet - 90 fpm - Loop", mode="track", part=("bright", 0.5)),
        dict(match="169_Foley_Footsteps_Grass_Sneaker_Walk_Fast_Run_Jog_Close", mode="track", part=("bright", 0.5))],
        why="Turf with sedge in it: the brighter half of the same carpet and grass walks, closed at 4.5 kHz (a little more blade than the "
            "moss, still no crinkle), with the same bare-foot thump under. The dry-grass singles that stood here crinkled."),
    "ash": dict(cls="grit", lowpass=3500.0, substitute=True, sources=[
        dict(match="PM_SDNG_Stereo_Walk_Seamless_Loop_1", mode="track", part=("dull", 0.55), pitch=-2.5)],
        why="SUBSTITUTE: there is no recording of dry black sand or ash. PMSFX's dirt-and-gravel walk, the dirt-dominated steps (the duller "
            "55%), closed at 3.5 kHz and made 2.5 semitones lower, as the brief's section 7.2 prescribes."),
    "gravel": dict(cls="grit", sources=[
        dict(match="PM_SDNG_Stereo_Walk_Seamless_Loop_1", mode="track", part=("bright", 0.45)),
        dict(match="S23_SFX_Footsteps_Gravel_Loafers_Loops_Walk_Normal", mode="track"),
        dict(match="PM_SDNG_Single_Step_Footstep_19", mode="whole", gait=1.5),
        dict(match="PM_SDNG_Single_Step_Footstep_46", mode="whole", gait=3.2)],
        why="PMSFX 'Steps: dirt and gravel' (the brighter steps of its walk and two singles) and Studio 23's loafers on gravel."),
    "basalt": dict(cls="grit", substitute=True, sources=[
        dict(match="BOOT_CONC CLEAN_WALK.R", mode="track"),
        dict(match="015_Foley_Footsteps_Asphalt_Boot_Walk_Fast_Run_Jog_Close", mode="track"),
        dict(match="FS Ground Civilian Walk N03", mode="whole", gait=1.3),
        dict(match="FS Ground Civilian Walk N05", mode="whole", gait=1.6),
        dict(match="FS Concrete Soldier Crouch N02", mode="whole", gait=0.2),
        dict(match="FS Concrete Soldier Crouch N05", mode="whole", gait=0.4)],
        why="SUBSTITUTE: there is no recording of footsteps on bare basalt. Boots on clean hard ground, the nearest in the bundles "
            "(Joshua Reinhardt's walk; Tovusound's asphalt walk and run; Levan Nadashvili's firm-ground and crouched steps; brief 7.4)."),
    "snow": dict(cls="snow", sources=[
        dict(match="0002_Footsteps_Walking_Crusty_Snow_Male_Shoes_Normal_Pace", mode="track"),
        dict(match="S23_SFX_Footsteps_Snow_Singles_Sequence", mode="track"),
        dict(match="snow_foley_walking_on_snow_02", mode="track"),
        dict(match="footstep_multiple_snow_boot_run_01_var03", mode="track"),
        dict(match="Snow Footstep Light Slow", mode="track", gait=0.3),
        dict(match="Ice Footsteps Hard Packed", mode="track", gait=1.3),
        dict(match="Bluezone_BC0264_snow_footsteps_walk_light_008", mode="whole", gait=0.6),
        dict(match="Bluezone_BC0264_snow_footsteps_walk_013", mode="whole", gait=1.4),
        dict(match="Bluezone_BC0264_snow_footsteps_walk_deep_008", mode="whole", gait=1.0),
        dict(match="Bluezone_BC0264_snow_footsteps_run_004", mode="whole", gait=3.2),
        dict(match="Footsteps_Icy_Normal_Single_Fienup_006", mode="whole", gait=1.8),
        dict(match="footstep_ice_crunchy_run_01", mode="whole", gait=3.6),
        dict(match="footstep_ice_crunchy_run_02", mode="whole", gait=3.8)],
        why="Wav Junction, Studio 23, Pole Position, Soundrangers, Alexander Fokshtein, Bluezone, Soundopolis and Gamemaster: crusty, "
            "fresh and icy snow, walked and run (brief 7.6)."),
    "water": dict(cls="water", sources=[
        dict(match="0014_Footsteps_water_puddle_single_splashes", mode="free", gait=1.8),
        dict(match="Bluezone_BC0256_water_splash_small_008", mode="whole", gait=3.0),
        dict(match="mud_splat_heavy_03", mode="whole", gait=2.6),
        dict(match="LIQMvmt_Liquid Movement Plop Slosh Single 07", mode="whole", gait=1.2)],
        why="Wav Junction's single steps into a puddle, with Bluezone's small splash, Soundrangers' heavy watery splat and Hzandbits' "
            "plop and slosh for the hurried steps (brief 7.11). Few real shallow-water steps exist; this pool is the smallest."),
}


# --------------------------------------------------------------------------------------------------- measuring

def true_peak_db(x):
    """Peak in dBFS after 4x oversampling (what a converter would reconstruct)."""
    return 20 * np.log10(np.abs(signal.resample_poly(x, 4, 1, axis=1)).max() + 1e-12)


def window_db(x, seconds=LEVEL_WINDOW, within=0.4):
    """RMS of the loudest window of `seconds` (hop a quarter of it) in the first `within` seconds."""
    x = x[:, :int(within * RATE)]
    n = int(seconds * RATE)
    if x.shape[1] <= n:
        return pro.level_db(x)
    hop = max(n // 4, 1)
    return max(pro.level_db(x[:, i:i + n]) for i in range(0, x.shape[1] - n + 1, hop))


def limit_peak(x, ceiling_db=PEAK_CEILING_DB):
    """A look-ahead peak limiter (2 ms, smoothed): only the peaks over the ceiling are pulled down, the body of the take is
    untouched. A last plain gain makes certain of the true peak."""
    lin = 10 ** (ceiling_db / 20.0)
    need = np.minimum(1.0, lin / (np.abs(x).max(axis=0) + 1e-12))
    if need.min() < 1.0:
        w = int(0.002 * RATE)
        g = ndimage.minimum_filter1d(need, 2 * w + 1)
        kernel = np.hanning(2 * w + 1)
        g = np.convolve(g, kernel / kernel.sum(), mode="same")
        x = x * np.minimum(g, 1.0)
    top = true_peak_db(x)
    if top > ceiling_db:
        x = x * 10 ** ((ceiling_db - top) / 20.0)
    return x


def centroid_hz(x, seconds=0.15):
    mono = x[:, :int(seconds * RATE)].mean(axis=0)
    if len(mono) < 256:
        return 0.0
    spectrum = np.abs(np.fft.rfft(mono * np.hanning(len(mono)))) ** 2
    freqs = np.fft.rfftfreq(len(mono), 1.0 / RATE)
    return float((spectrum * freqs).sum() / (spectrum.sum() + 1e-12))


def foot_gains(foot):
    """Per side gains of the foot's baked balance (BALANCE toward its own side)."""
    own, other = 1.0, 1.0 - BALANCE
    return (other, own) if foot == "r" else (own, other)


# --------------------------------------------------------------------------------------------------- shaping

def shape_recording(x, lowpass=None, pitch=None, highpass=HIGHPASS_HZ):
    """High-pass, the low end summed to mono, an optional closing low-pass, an optional pitch change."""
    if pitch:
        x = pro.repitch(x, 2.0 ** (pitch / 12.0))
    x = x - x.mean(axis=1, keepdims=True)
    x = pro.butter(x, "highpass", max(highpass, HIGHPASS_HZ))
    low = pro.butter(x.mean(axis=0, keepdims=True), "lowpass", MONO_BELOW_HZ)
    x = (x - pro.butter(x, "lowpass", MONO_BELOW_HZ)) + low
    if lowpass:
        x = pro.butter(x, "lowpass", lowpass, 3)
    return x


def lead_trim(x, below_db=-40.0):
    """Cuts the quiet before a take's first sound (down to `below_db` under its peak), leaving 3 ms: it would be latency."""
    mono = np.abs(x).max(axis=0)
    loud = np.where(mono > mono.max() * 10 ** (below_db / 20.0))[0]
    return x[:, max(int(loud[0]) - int(0.003 * RATE), 0):] if len(loud) else x


def fade_in(x, ms=3.0):
    n = min(int(ms * RATE / 1000), x.shape[1])
    x[:, :n] *= np.linspace(0.0, 1.0, n)
    return x


def fade_out(x, seconds):
    n = min(int(seconds * RATE), x.shape[1] // 2)
    if n > 0:
        x[:, -n:] *= np.cos(np.linspace(0.0, np.pi / 2, n)) ** 2
    return x


# --------------------------------------------------------------------------------------------------- finding steps

def power_ms(mono):
    x = pro.butter(mono[None], "highpass", 80.0)[0]
    n = len(x) // HOP
    return (x[:n * HOP] ** 2).reshape(n, HOP).mean(axis=1)


def win_mean(p, a, b):
    c = np.concatenate([[0.0], np.cumsum(p)])
    n = len(p)
    t = np.arange(n)
    lo, hi = np.clip(t + a, 0, n), np.clip(t + b, 0, n)
    return (c[hi] - c[lo]) / np.maximum(hi - lo, 1)


def envelope_db(p, win=4):
    return 10 * np.log10(np.convolve(p, np.ones(win) / win, mode="same") + 1e-12)


def short_rise(db, within=12):
    low = ndimage.minimum_filter1d(db, within + 1, origin=within // 2)
    return db - np.concatenate([[db[0]], low[:-1]])


def short_candidates(db, min_rise=6.0, above=12.0, sep=30):
    s = short_rise(db)
    floor = np.percentile(db, 10)
    peaks, _ = signal.find_peaks(s, height=min_rise, distance=sep)
    return np.array([i for i in peaks if db[min(i + 4, len(db) - 1)] >= floor + above], dtype=int), s


def onset_candidates(p, db, min_rise=9.0, sep=120, above=15.0):
    """Where a step may begin: the level over the next 20 ms against the 100 ms before it, then the exact moment from the
    4 ms level. Returns (indices in ms, rise in dB)."""
    floor_p = np.percentile(p, 5) + 1e-12
    l1, l0 = win_mean(p, 0, 20), win_mean(p, -100, -10)
    rise = 10 * np.log10((l1 + floor_p) / (l0 + floor_p))
    floor_db = 10 * np.log10(np.percentile(p, 10) + 1e-12)
    peaks, _ = signal.find_peaks(rise, height=min_rise, distance=sep)
    peaks = [i for i in peaks if 10 * np.log10(l1[i] + 1e-12) >= floor_db + above]
    s = short_rise(db)
    out, strength = [], []
    for i in peaks:
        a, b = max(i - 25, 0), min(i + 10, len(s))
        j = a + int(np.argmax(s[a:b]))
        if j not in out:
            out.append(j)
            strength.append(rise[i])
    order = np.argsort(out)
    return np.array(out, dtype=int)[order], np.array(strength)[order]


def period(times, w, lo=0.28, hi=1.2):
    """The cadence of the walk: where the intervals between onsets pile up (the shortest of the strong peaks)."""
    bins = np.arange(lo, hi + 0.005, 0.005)
    acc = np.zeros(len(bins))
    for i in range(len(times)):
        d = times - times[i]
        sel = (d > lo - 0.03) & (d < hi + 0.03)
        for dj, wj in zip(d[sel], w[sel]):
            acc += w[i] * wj * np.exp(-0.5 * ((bins - dj) / 0.012) ** 2)
    peaks, _ = signal.find_peaks(acc, height=0.6 * acc.max())
    return float(bins[peaks[0] if len(peaks) else int(np.argmax(acc))])


def track(times, w, T, lam=12.0, cap=45.0):
    """The chain of onsets that best keeps the walk's cadence (dynamic programming, as a beat tracker does)."""
    n = len(times)
    best, prev = np.zeros(n), -np.ones(n, dtype=int)
    for i in range(n):
        top, at = 0.0, -1
        for j in range(i - 1, -1, -1):
            d = times[i] - times[j]
            if d > 4 * T:
                break
            if d < 0.45 * T:
                continue
            value = best[j] - min(lam * (np.log(d / T) / 0.3) ** 2, cap)
            if value > top:
                top, at = value, j
        best[i], prev[i] = w[i] + top, at
    i, seq = int(np.argmax(best)), []
    while i >= 0:
        seq.append(i)
        i = prev[i]
    return sorted(seq)


def find_steps(x, source):
    """-> (onsets in ms, pace in steps a second or None, envelope in dB at 1 ms, power at 1 ms)."""
    mono = x.mean(axis=0)
    p = power_ms(mono)
    db = envelope_db(p)
    if source["mode"] == "whole":
        peak = np.abs(mono).max()
        loud = np.where(np.abs(mono) > peak * 10 ** (-36 / 20.0))[0]
        return np.array([max(int(loud[0] / HOP) - 4, 0)]), None, db, p
    if source["mode"] == "free":
        on, _ = onset_candidates(p, db, min_rise=14.0, sep=200, above=18.0)
        return on, None, db, p
    on, w = onset_candidates(p, db)
    if len(on) < 4:
        return on, None, db, p
    T = source.get("period") or period(on / 1000.0, np.minimum(w, 30.0))
    keep = track(on / 1000.0, np.minimum(w, 30.0), T)
    return on[keep], 1.0 / T, db, p


def toe_candidates(cand, s, db, i, end, peak_db):
    """Every later rise inside the step that is worth a roll: [(ms after the onset, rise in dB)]. The impact before it
    must not be much weaker than what follows (else the 'toe' is the step itself and the 'heel' only a touch)."""
    out = []
    for c in cand:
        if not (i + 15 <= c <= min(end - 40, i + 320)) or s[c] < 6.0:
            continue
        after = db[c:min(c + 80, len(db))].max()
        if after < peak_db - 20.0 or db[i:c].max() < after - 6.0:
            continue
        out.append((int(c - i), float(s[c])))
    return out


class Step:
    """One recorded step, cut whole; the impact/roll split is decided by the tier later."""

    def __init__(self, uid, source, foot, x, toe, pace, pace_given, centroid, lvl, rank):
        self.uid, self.source, self.foot, self.x, self.toe = uid, source, foot, x, toe
        self.pace, self.pace_given, self.centroid, self.lvl, self.rank = pace, pace_given, centroid, lvl, rank
        self.tier = None
        self.gain_db = 0.0
        self.coord = 0.0


def cut_steps(name, index, source, x, rows_info):
    """All the clean steps of one recording, with their feet by parity in recording order."""
    on, pace, db, p = find_steps(x, source)
    cand, s_rise = short_candidates(db, min_rise=5.0, above=6.0, sep=15)
    longest = source.get("longest", 0.9 if source["mode"] != "track" else 0.7)
    steps = []
    for k, i in enumerate(on):
        following = on[k + 1] if k + 1 < len(on) else None
        a = max(i - 10, 0)
        end = (following - 25) if following is not None else min(len(p), i + int(longest * 1000))
        end = min(end, i + int(longest * 1000))
        if end - a < 120:
            continue
        peak_db = db[i:i + 60].max()
        pre = 10 * np.log10(p[max(i - 40, 0):max(i - 10, 1)].mean() + 1e-12)
        if source["mode"] == "track" and pre > peak_db - 10.0:
            continue                                    # the last step's tail is still sounding: not a clean cut
        toe = toe_candidates(cand, s_rise, db, i, end, peak_db)
        piece = x[:, a * HOP:end * HOP].copy()
        steps.append(Step(f"{name}:{index}:{k:03d}", rows_info, "l" if k % 2 == 0 else "r", piece,
                          toe, pace, pace is not None, centroid_hz(piece), 0.0, k))
    return steps


# --------------------------------------------------------------------------------------------------- tiers and levels

def pace_coordinate(pace):
    return float(np.interp(pace, PACE_CENTRES, np.arange(5)))


def rank_to_tier(n, counts=IMPACT_TAKES):
    """Splits n ranked steps into five tiers, in proportion to the takes the brief asks for each."""
    edges = np.round(np.cumsum(counts) / sum(counts) * n).astype(int)
    tier = np.zeros(n, dtype=int)
    start = 0
    for t, e in enumerate(edges):
        tier[start:e] = t
        start = e
    return tier


def spread_pick(n_slice, want):
    if n_slice <= want:
        return list(range(n_slice))
    return sorted(set(int(round(v)) for v in np.linspace(0, n_slice - 1, want)))


def assign_tiers(steps):
    """Each step's place on the tier scale: the pace of its recording, nudged by its own energy among its recording's
    steps (a harder step in a walk is nearer a jog); then ranked, and the ranking cut into tiers."""
    by_source = {}
    for s in steps:
        by_source.setdefault(s.source, []).append(s)
    for group in by_source.values():
        lv = np.array([s.lvl for s in group])
        pct = (np.argsort(np.argsort(lv)) + 0.5) / len(lv) if len(lv) >= 6 else np.full(len(lv), 0.5)
        for s, q in zip(group, pct):
            base = pace_coordinate(s.pace) if s.pace_given else s.gait_hint
            s.coord = base + 0.6 * (2 * q - 1)
    for foot in ("l", "r"):
        ranked = sorted((s for s in steps if s.foot == foot), key=lambda s: s.coord)
        for s, t in zip(ranked, rank_to_tier(len(ranked))):
            s.tier = int(t)


def set_levels(steps):
    """Each tier pool to its own level (its mean is exactly the tier's), each take keeping its natural place around the mean
    (within +-SPREAD)."""
    for t in range(5):
        pool = [s for s in steps if s.tier == t]
        if not pool:
            continue
        lv = np.array([s.lvl for s in pool])
        d = np.clip(lv - lv.mean(), -SPREAD, SPREAD)
        d -= d.mean()
        for s, dev, own in zip(pool, d, lv):
            s.gain_db = WALK_RMS + TIER_DB[t] + float(dev) - float(own)


# --------------------------------------------------------------------------------------------------- building a surface

BODY_MATCH = "Bare_Feet_Grass_RUN.R"


def body_layer(rows):
    """The bare-foot thump under moss (brief 7.1): Joshua Reinhardt's run on grass, only its low end, as short thumps."""
    row = pro.find(rows, dict(file=BODY_MATCH, match=BODY_MATCH))
    x = pro.load(pro.fetch(row))
    x = shape_recording(x, lowpass=250.0)
    on, w = onset_candidates(power_ms(x.mean(axis=0)), envelope_db(power_ms(x.mean(axis=0))), min_rise=9.0)
    thumps = []
    for i in on:
        piece = x[:, max(i - 6, 0) * HOP:(i + 140) * HOP].copy()
        if piece.shape[1] > int(0.12 * RATE):
            thumps.append(fade_out(fade_in(piece, 3.0), 0.05))
    return thumps, row


def build_surface(name, spec, rows, credits):
    steps, index = [], 0
    thumps = []
    if spec.get("body"):
        thumps, body_row = body_layer(rows)
        credits.append((name, body_row, "the bare-foot thump under the steps"))
    for source in spec["sources"]:
        row = pro.find(rows, dict(file=source["match"], match=source["match"]))
        credits.append((name, row, ""))
        x = shape_recording(pro.load(pro.fetch(row)), spec.get("lowpass"), source.get("pitch"))
        got = cut_steps(name, index, source, x, row["file"])
        for s in got:
            s.gait_hint = source.get("gait", 2.0)
        if source.get("part") and got:
            kind, share = source["part"]
            order = sorted(got, key=lambda s: s.centroid)
            keep = order[:int(round(len(order) * share))] if kind == "dull" else order[len(order) - int(round(len(order) * share)):]
            got = [s for s in got if s in keep]
        steps.extend(got)
        index += 1
    if not steps:
        raise SystemExit(f"no steps found for {name}")
    rng = np.random.default_rng(len(name) * 7919)
    # Single-step files have no order to take parity from: they alternate in turn (steps of the same file are not
    # affected: their parity is their place in the walk).
    singles = [s for s in steps if not s.pace_given]
    for k, s in enumerate(singles):
        s.foot = "l" if k % 2 == 0 else "r"
    for s in steps:
        if thumps:
            t = thumps[int(rng.integers(len(thumps)))]
            n = min(s.x.shape[1], t.shape[1])
            seg = s.x[:, :n]
            want = pro.level_db(seg) - 9.0
            s.x[:, :n] = seg + t[:, :n] * 10 ** ((want - pro.level_db(t[:, :n])) / 20.0)
        s.raw = s.x.copy()                              # rolls have no foot of their own: they stay neutral
        gl, gr = foot_gains(s.foot)
        s.x = s.x * np.array([[gl], [gr]])
        s.lvl = window_db(s.x)
    assign_tiers(steps)
    set_levels(steps)                                   # provisional: from the whole tier slice (rolls use these)
    return steps


def finish_take(x, gain_db, foot):
    """Level, the right foot's bias, the ceiling; returns the finished stereo take."""
    x = x * 10 ** ((gain_db + (RIGHT_BIAS_DB if foot == "r" else 0.0)) / 20.0)
    return x


def choose_toe(s, window):
    """The step's toe: the strongest later onset inside the window. -> ms after the onset or None."""
    if window is None:
        return None
    here = [(d, r) for d, r in s.toe if window[0] <= d <= window[1]]
    return max(here, key=lambda dr: dr[1])[0] if here else None


def split_step(s, roll_only=False):
    """-> (impact, roll or None, toe delay in ms or None). Impacts come from the foot-balanced step; rolls from the neutral
    one (they have no foot of their own) and from any tier. A step with no toe in its window stays whole."""
    whole = (s.raw if roll_only else s.x).copy()
    delay = choose_toe(s, ROLL_TOE_WINDOW_MS if roll_only else TOE_WINDOWS_MS[s.tier])
    if delay is not None:
        cut = int((10 + delay - 3) * HOP)               # the cut starts 10 ms before the onset; the impact ends 3 ms before the toe
        if cut >= int(0.05 * RATE) and whole.shape[1] - cut >= int(0.06 * RATE):
            impact = fade_out(fade_in(whole[:, :cut].copy(), 3.0), 0.003)
            roll = fade_out(fade_in(whole[:, cut:].copy(), 3.0), 0.09)
            if window_db(impact) >= window_db(whole) - 3.0 and (not roll_only or window_db(roll) >= window_db(whole) - 20.0):
                return impact, roll, delay
    return fade_out(fade_in(whole, 3.0), 0.09), None, None


def load_manifest():
    return json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {}


def save_manifest(manifest):
    MANIFEST.write_text(json.dumps(dict(sorted(manifest.items())), indent=0))


def write_take(name, x, meta, manifest):
    pro.write(name, x)
    manifest[name] = dict(meta, rms=round(float(pro.level_db(x)), 2), peak=round(float(true_peak_db(x)), 2),
                          seconds=round(x.shape[1] / RATE, 3))


def clean_own():
    for path in OUT.glob("*.wav"):
        if OWN_RE.match(path.stem):
            path.unlink()


OVER = {}


def run(only):
    rows = pro.index()
    credits, all_steps = [], {}
    for name, spec in SURFACES.items():
        if only and name not in only:
            continue
        all_steps[name] = build_surface(name, spec, rows, credits)
        print(f"{name:7s} {len(all_steps[name]):3d} steps found, tiers "
              + " ".join(f"{TIERS[t]}:{sum(1 for s in all_steps[name] if s.tier == t)}" for t in range(5)), flush=True)
    manifest = load_manifest()
    if not only:
        clean_own()
        manifest = {k: v for k, v in manifest.items() if not OWN_RE.match(k)}
    else:
        for name in all_steps:
            for path in OUT.glob(f"step_{name}_*.wav"):
                if OWN_RE.match(path.stem):
                    path.unlink()
                    manifest.pop(path.stem, None)
    # impacts: the takes are chosen first (spread evenly through each tier's ranking, per foot), then each tier's chosen
    # pool is set to the tier's level
    chosen = {}
    for name, steps in all_steps.items():
        for t in range(5):
            for foot in ("l", "r"):
                slice_ = sorted((s for s in steps if s.tier == t and s.foot == foot), key=lambda s: s.coord)
                want = (IMPACT_TAKES[t] + 1) // 2
                chosen[(name, t, foot)] = [slice_[k] for k in spread_pick(len(slice_), want)]
        set_levels([s for (n, t, f), v in chosen.items() if n == name for s in v])
    for name, steps in all_steps.items():
        for t in range(5):
            for foot in ("l", "r"):
                for n, s in enumerate(chosen[(name, t, foot)], 1):
                    impact, _, delay = split_step(s)
                    x = lead_trim(finish_take(impact, s.gain_db, foot))
                    OVER.setdefault((name, TIERS[t]), []).append(true_peak_db(x) - PEAK_CEILING_DB)
                    x = limit_peak(x)
                    write_take(f"step_{name}_{TIERS[t]}_{foot}_{n:02d}", x,
                               dict(source_step=s.uid, recording=s.source, foot=foot, tier=TIERS[t], gain_db=round(s.gain_db, 2),
                                    step_rms=round(s.lvl + s.gain_db, 2), toe_ms=delay,
                                    centroid_hz=round(s.centroid)), manifest)
    # rolls, by class: every step with a toe, from all the surfaces of the class (so only a class whose surfaces were
    # all rebuilt in this run: a roll pool draws on all the surfaces of its class)
    for cls in [c for c in ("soft", "grit", "snow") if all(n in all_steps for n, s in SURFACES.items() if s["cls"] == c)]:
        for path in OUT.glob(f"roll_{cls}_*.wav"):
            if OWN_RE.match(path.stem):
                path.unlink()
                manifest.pop(path.stem, None)
        pool = [s for name, steps in all_steps.items() if SURFACES[name]["cls"] == cls for s in steps]
        for t in range(5):
            cand = sorted((s for s in pool if s.tier == t and split_step(s, True)[1] is not None), key=lambda s: s.coord)
            for n, k in enumerate(spread_pick(len(cand), ROLL_TAKES[t]), 1):
                s = cand[k]
                _, roll, delay = split_step(s, True)
                x = lead_trim(finish_take(roll, s.gain_db, "l"))
                x = limit_peak(x)
                write_take(f"roll_{cls}_{TIERS[t]}_{n:02d}", x,
                           dict(source_step=s.uid, recording=s.source, foot=s.foot, tier=TIERS[t], toe_ms=delay), manifest)
        print(f"roll_{cls}: " + " ".join(f"{TIERS[t]}:{sum(1 for k in manifest if k.startswith(f'roll_{cls}_{TIERS[t]}_'))}" for t in range(5)), flush=True)
    save_manifest(manifest)
    for (name, tier), v in sorted(OVER.items()):
        if max(v) > 0:
            print(f"   peak limiter: {name:7s} {tier:7s} took the loudest peaks down by up to {max(v):.1f} dB, {sum(1 for a in v if a > 0)}/{len(v)} takes")
    write_credits(credits, only)


# --------------------------------------------------------------------------------------------------- credits and audit

def update_credits_section(title, body):
    """Replaces (or adds) one '## title' section of CREDITS.md, leaving every other section as it is."""
    path = OUT / "CREDITS.md"
    text = path.read_text() if path.exists() else "# Sounds\n"
    marker = f"\n## {title}\n"
    start = text.find(marker)
    if start < 0:
        text = text.rstrip("\n") + "\n" + marker + body
    else:
        after = text.find("\n## ", start + len(marker))
        text = text[:start] + marker + body + ("\n" + text[after + 1:] if after >= 0 else "")
    path.write_text(text)


def write_credits(credits, only):
    """The section for every surface as SURFACES now has it (read from the index, so a run of some surfaces keeps the rest)."""
    rows = pro.index()
    seen, lines = set(), []
    for name, spec in SURFACES.items():
        matches = ([BODY_MATCH] if spec.get("body") else []) + [source["match"] for source in spec["sources"]]
        for match in matches:
            row = pro.find(rows, dict(file=match, match=match))
            if (name, row["file"]) in seen:
                continue
            seen.add((name, row["file"]))
            lines.append(f"| `step_{name}_*` and its rolls | {row['file']} | {row['library']} | {row['year']} |")
    body = ("\nThe layered footsteps, cut from professional foley walks by `Tools/audio/steps.py` (same bundles, same licence): "
            "`step_<surface>_<tier>_<foot>_NN` (impacts) and `roll_<class>_<tier>_NN` (rolls). Each surface, why, and where a substitute stands in:\n\n"
            + "\n".join(f"- `{n}`{' (SUBSTITUTE)' if s.get('substitute') else ''}: {s['why'].replace('SUBSTITUTE: ', '')}" for n, s in SURFACES.items())
            + "\n\n| Sound | Recording | Library | Bundle |\n|---|---|---|---|\n" + "\n".join(lines) + "\n\n"
            "Rolls are cut from the same steps (from the soft class: moss and sedge; grit: ash, gravel, basalt; snow: snow).\n"
            "The older `step_<surface>_NN`, `land_NN` and `suit_step_NN` files are the previous cut of the same recordings and "
            "stay until the game reads the new keys.\n")
    update_credits_section("Footsteps and movement", body)


def audit(only_prefixes=None):
    """Per key: takes, RMS (whole file) and the loudest-50 ms RMS (the measure the tiers are set in), true peak (min .. max),
    and the tier spacing of the step keys against the walk tier."""
    import wave
    keys = {}
    for path in sorted(OUT.glob("*.wav")):
        m = KEY_RE.match(path.stem)
        key = m.group("key") if m else path.stem
        if not NEW_KEY_RE.match(key) or (only_prefixes and not key.startswith(tuple(only_prefixes))):
            continue
        with wave.open(str(path)) as w:
            data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).T.astype(np.float64) / 32768.0
        within = data.shape[1] / RATE if not key.startswith(("step_", "roll_")) else LEVEL_WINDOW * 8
        keys.setdefault(key, []).append((pro.level_db(data), window_db(data, within=within), true_peak_db(data), data.shape[1] / RATE))
    print(f"\n{'key':28s} {'n':>3s} {'RMS dB min / mean / max':>25s} {'loud-50ms':>10s} {'true peak dB min / max':>23s} {'length s':>9s}")
    over = 0
    for key, v in keys.items():
        r, l50, pk, sec = (np.array([row[i] for row in v]) for i in range(4))
        flag = "  PEAK OVER -1" if pk.max() > -1.0 else ""
        over += pk.max() > -1.0
        print(f"{key:28s} {len(v):3d} {r.min():8.1f} {r.mean():8.1f} {r.max():8.1f} {l50.mean():10.1f} {pk.min():11.1f} {pk.max():9.1f} {sec.mean():9.2f}{flag}")
    total = sum(len(v) for v in keys.values())
    print(f"\n{len(keys)} keys, {total} files; {over} keys with a true peak over -1 dBFS")
    tiers = {}
    for key, v in keys.items():
        m = re.match(r"step_(\w+?)_(creep|walk|jog|run|sprint)_[lr]$", key)
        if m:
            tiers.setdefault(m.group(1), {}).setdefault(m.group(2), []).extend(row[1] for row in v)
    if tiers:
        print("\nStep tiers as written, loudest-50 ms RMS against the walk tier (every tier pool is set to the same reference, so 0 for"
              " all; the game adds creep -10, jog +4, run +8, sprint +10; the peak limiter takes a little off the loudest takes):")
        for surface in SURFACES:
            if surface in tiers and "walk" in tiers[surface]:
                walk = np.mean(tiers[surface]["walk"])
                print(f"  {surface:7s} walk {walk:6.1f} dB   " + "  ".join(f"{t} {np.mean(tiers[surface][t]) - walk:+5.1f}" for t in TIERS if t in tiers[surface] and t != "walk")
                      + f"   takes " + "/".join(str(len(tiers[surface].get(t, []))) for t in TIERS))
        pooled = {t: np.mean([x for sv in tiers.values() for x in sv.get(t, [])]) for t in TIERS}
        print("  all     " + "  ".join(f"{t} {pooled[t] - pooled['walk']:+5.1f}" for t in TIERS))


def main(args):
    if args and args[0] == "audit":
        audit(args[1:] or None)
        return
    run(set(args))
    audit(("step_", "roll_"))


if __name__ == "__main__":
    main(sys.argv[1:])
