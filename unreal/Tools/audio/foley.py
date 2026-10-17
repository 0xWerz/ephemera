"""The rest of the layered movement library: cloth, gear, vegetation, debris, water, breath and the movement events.

    Tools/audio/run.sh steps.py          (first: the events below are built partly from its steps and rolls)
    Tools/audio/run.sh foley.py          (everything, then the loudness audit)
    Tools/audio/run.sh foley.py cloth water     (only these groups)
    Tools/audio/run.sh foley.py audit    (only the audit)

Groups, in the order they are built (events need the groups before them; extras are the optional sweeteners):

    cloth      cloth_<light|medium|hard>_NN                 loop_cloth_walk  loop_cloth_jog
    gear       gear_tick_NN                                 loop_gear_bounce
    vegetation swish_grass_NN                               loop_brush_soft  loop_brush_hard
    debris     debris_<grit|snow>_NN                         (debris_stem is retired: see RETIRED)
    water      wade_<ankle|shin|knee>_NN  splash_NN  bubbles_NN   loop_wade  loop_underwater
    extras     squelch_NN  drip_NN  pebble_NN   loop_scree
    breath     breath_<calm|working|heavy|recovery>_NN
    events     start_ settle_ turn_ skid_ <soft|grit|snow>_NN   takeoff_NN   land_<soft|grit|snow|water>_<soft|medium|hard>_NN

Sections 6.1 to 6.3 and 6.6 of docs/research/footsteps_and_space_audio.md. Every sound is a real recording from the Sonniss
GDC bundles (see pro.py for the source and its licence), cut, filtered and levelled here, or several of them laid together
(a landing is an impact, a weight, a gear thump and a puff of grit: four recordings). Nothing is synthesised.

Levels are in the same measure as the steps (the RMS of the loudest 50 ms; the reference step is -25 dB, see steps.py). Nothing
that belongs to a gait tier carries the tier's level difference: the game adds those (creep -10 ... sprint +10 dB for the steps,
and brief 6.3's cloth-against-step offsets for the cloth), so the cloth weights are all at the walk-tier cloth level (-37 dB, the
walk step -12 dB) and differ only in what they are (light, medium, hard), and the two cloth beds are at one level. Gear, debris,
swish and the rest sit where they belong against the reference step. The events that are not gait tiers (landings by weight, the
take-off, the skid, the start, settle and turn) and wading by depth are at their own in-mix levels from the brief. Pools keep each
take's natural spread (+-2 dB). Beds (loops) are set by their steady RMS. True peaks stay under -1.5 dBFS.
"""
import pathlib
import sys
import wave
import zlib

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pro
import steps
from pro import RATE, OUT

HOP = steps.HOP
ROWS = None
CACHE = {}
CREDITS = {}                                      # group -> [(key, row)]
MANIFEST = {}
CURRENT = {"group": None, "key": None}

# What each thing is set to (dB RMS; "head" = loudest 50 ms, as for the steps; "full" = the whole take or the steady bed).
# Against the reference step (-25): cloth is at the walk-tier cloth level; gear, debris and the rest sit below the step.
# Every file is written at one reference level (the walk step's, about -25 dB RMS over its loudest 50 ms;
# loops over their whole length). How loud each layer is in the game is the game's to say
# (EphemeraAudio.cpp: GaitDb, ClothDb, BreathDb, the landing and loop levels), so nothing is counted twice.
_REFERENCE = -25.0
LEVELS = dict(
    cloth_light=_REFERENCE, cloth_medium=_REFERENCE, cloth_hard=_REFERENCE,
    gear_tick=_REFERENCE, swish_grass=_REFERENCE,
    debris_grit=_REFERENCE, debris_snow=_REFERENCE, debris_stem=_REFERENCE,
    wade_ankle=_REFERENCE, wade_shin=_REFERENCE, wade_knee=_REFERENCE, splash=(-28.0, -22.0), bubbles=_REFERENCE,
    start=_REFERENCE, settle=_REFERENCE, turn=_REFERENCE, skid=_REFERENCE, takeoff=_REFERENCE,
    land_soft=_REFERENCE, land_medium=_REFERENCE, land_hard=_REFERENCE,
    breath_calm=_REFERENCE, breath_working=_REFERENCE, breath_heavy=_REFERENCE, breath_recovery=_REFERENCE,
    loop_cloth_walk=_REFERENCE, loop_cloth_jog=_REFERENCE, loop_gear_bounce=_REFERENCE, loop_brush_soft=_REFERENCE, loop_brush_hard=_REFERENCE,
    loop_wade=_REFERENCE, loop_underwater=_REFERENCE, loop_scree=_REFERENCE,
    squelch=_REFERENCE, drip=_REFERENCE, pebble=_REFERENCE)


# --------------------------------------------------------------------------------------------------- plumbing

def rng_for(key):
    return np.random.default_rng(zlib.crc32(key.encode()))


def get(match, lowpass=None, highpass=None, pitch=None):
    """A recording from the bundles, shaped as the steps are (55 Hz high-pass, low end centred), cached for this group."""
    global ROWS
    ROWS = ROWS or pro.index()
    row = pro.find(ROWS, dict(file=match, match=match))
    CREDITS.setdefault(CURRENT["group"], [])
    if (CURRENT["key"], row["file"]) not in [(k, r["file"]) for k, r in CREDITS[CURRENT["group"]]]:
        CREDITS[CURRENT["group"]].append((CURRENT["key"], row))
    k = (match, lowpass, highpass, pitch)
    if k not in CACHE:
        CACHE[k] = steps.shape_recording(pro.load(pro.fetch(row)), lowpass, pitch, highpass or steps.HIGHPASS_HZ)
    return CACHE[k]


def source_names(key):
    return [r["file"] for k, r in CREDITS.get(CURRENT["group"], []) if k == key]


def fade(x, rise_ms=6.0, fall_s=0.08):
    x = x.copy()
    steps.fade_in(x, rise_ms)
    return steps.fade_out(x, min(fall_s, x.shape[1] / RATE / 2))


def head(x):
    """The loudest 50 ms of the whole take (a step's own measure looks at only its first 0.4 s)."""
    return steps.window_db(x, within=x.shape[1] / RATE + 1.0)


def full(x):
    return pro.level_db(x)


def level_pool(takes, target, measure=head, spread=steps.SPREAD):
    """The pool to its level, each take keeping its natural place around the pool's mean (within +-spread dB)."""
    lv = np.array([measure(t) for t in takes])
    d = np.clip(lv - lv.mean(), -spread, spread)
    d -= d.mean()
    return [t * 10 ** ((target + dd - l) / 20.0) for t, l, dd in zip(takes, lv, d)]


def spread_pick(items, n, key):
    """n of them, evenly through their order by `key` (so the pool keeps the whole range of what was cut)."""
    order = sorted(items, key=key)
    return [order[k] for k in steps.spread_pick(len(order), n)]


def mix(*layers):
    """layers: (stereo array, offset in seconds, gain in dB)."""
    n = max(int(o * RATE) + x.shape[1] for x, o, _ in layers)
    out = np.zeros((2, n))
    for x, o, g in layers:
        a = int(o * RATE)
        out[:, a:a + x.shape[1]] += x * 10 ** (g / 20.0)
    return out


def onset_events(x, length, rise=10.0, sep=150, above=12.0, lead=8, fall=0.08, rise_ms=4.0, tail=None):
    """One piece at every onset of a recording that has them (a gated library file, a handful of foley hits)."""
    mono = x.mean(axis=0)
    p = steps.power_ms(mono)
    db = steps.envelope_db(p)
    on, _ = steps.onset_candidates(p, db, min_rise=rise, sep=sep, above=above)
    pieces = []
    for k, i in enumerate(on):
        nxt = on[k + 1] if k + 1 < len(on) else len(p)
        a, b = max(i - lead, 0), min(nxt - 12, i + int(length * 1000))
        if b - a >= 40:
            pieces.append(fade(x[:, a * HOP:b * HOP], rise_ms, fall))
    return pieces


def active_slices(x, n, lengths, rng, active_db=-30.0, rise_ms=10.0, fall=0.08, min_active=0.85, frame=0.02):
    """n windows of random length from the stretches of a recording that are sounding (within active_db of its loud part)."""
    mono = x.mean(axis=0)
    h = int(frame * RATE)
    count = len(mono) // h
    lv = np.array([10 * np.log10(np.mean(mono[i * h:(i + 1) * h] ** 2) + 1e-12) for i in range(count)])
    act = lv > np.percentile(lv, 95) + active_db
    got, taken = [], np.zeros(count, dtype=bool)
    for _ in range(3000):
        if len(got) == n:
            break
        length = int(rng.uniform(*lengths) / frame)
        if count <= length + 2:
            break
        s = int(rng.integers(0, count - length))
        if act[s:s + length].mean() >= min_active and not taken[max(s - 2, 0):s + length + 2].any():
            taken[s:s + length] = True
            got.append(fade(x[:, s * h:(s + length) * h], rise_ms, fall))
    return got


def tail_trim(x, below_db=-45.0):
    """Cuts a take's end where it has fallen `below_db` under its own loudest part."""
    mono = np.abs(x).max(axis=0)
    loud = np.where(mono > mono.max() * 10 ** (below_db / 20.0))[0]
    return x[:, :int(loud[-1]) + 1] if len(loud) else x


def emit(key, takes, meta=None):
    """Writes key_01 ... key_NN (the old takes of the key first removed). Every take's true peak is held under the ceiling."""
    for old in OUT.glob(f"{key}_[0-9][0-9].wav"):
        old.unlink()
        MANIFEST.pop(old.stem, None)
    over = []
    for i, t in enumerate(takes, 1):
        t = steps.lead_trim(t, -45.0)                                   # leading silence would be latency
        over.append(steps.true_peak_db(t) - steps.PEAK_CEILING_DB)
        steps.write_take(f"{key}_{i:02d}", steps.limit_peak(t), dict(meta or {}, recordings=source_names(key)), MANIFEST)
    print(f"  {key:26s} {len(takes):2d} takes" + (f"   (limiter took the peaks down by up to {max(over):.1f} dB, {sum(1 for o in over if o > 0)} takes)" if max(over) > 0 else ""), flush=True)


def emit_loop(name, x, meta=None):
    over = steps.true_peak_db(x) - steps.PEAK_CEILING_DB
    steps.write_take(name, steps.limit_peak(x), dict(meta or {}, recordings=source_names(name)), MANIFEST)
    print(f"  {name:26s} loop {x.shape[1] / RATE:5.1f} s" + (f"   (limiter took the peaks down by up to {over:.1f} dB)" if over > 0 else ""), flush=True)


def calm_start(x, seconds, cross):
    """Where a bed is at its calmest: the window with the least crest (no snaps and thuds) and the least wander. -> seconds"""
    mono = x.mean(axis=0)
    n = int((seconds + cross) * RATE)
    h = int(0.1 * RATE)
    best, at = 1e9, 0
    for a in range(0, max(len(mono) - n, 1), RATE // 2):
        w = mono[a:a + n]
        lv = 10 * np.log10(np.array([np.mean(w[i:i + h] ** 2) for i in range(0, len(w) - h, h)]) + 1e-12)
        cost = (20 * np.log10(np.abs(w).max() + 1e-9) - 10 * np.log10(np.mean(w ** 2) + 1e-12)) + lv.std()
        if cost < best:
            best, at = cost, a
    return at / RATE


def make_loop(x, seconds, cross, rms, start=None):
    pick = dict(seconds=seconds, cross=cross, start=calm_start(x, seconds, cross) if start is None else start)
    loop = pro.make_loop(x, pick)
    return loop * 10 ** ((rms - full(loop)) / 20.0)


def load_out(name):
    path = OUT / f"{name}.wav"
    with wave.open(str(path)) as w:
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).T.astype(np.float64) / 32768.0
    return data


def pool_out(prefix):
    files = sorted(OUT.glob(f"{prefix}[0-9][0-9].wav"))
    if not files:
        raise SystemExit(f"{prefix}NN is not in {OUT}: run steps.py and the earlier foley.py groups first")
    return [load_out(f.stem) for f in files]


def pick_one(pool, rng):
    return pool[int(rng.integers(len(pool)))]


# --------------------------------------------------------------------------------------------------- groups

# Cloth (2026-10-14). The owner, walking: "like im walking into a plastic bag". What was measured under every step: a
# nylon soft-shell jacket's flutter (cloth_medium), a zip-strap whoosh named "High" in cloth_light (30% of it above 4 kHz),
# a rough leather jacket rustling at 27 sharp 3-10 kHz transients a second (cloth_hard) and, as the jog's bed, an army-gear
# walk of webbing and buckles whose transients carried 57% of its 3-10 kHz energy (crinkle 98 per mille of the whole
# sound). Now only cotton, wool, canvas and supple leather, each closed at 4 to 5 kHz: what is left of a garment moving
# is its soft swish and thump.
CLOTH_TOP = 5000.0


def build_cloth():
    """Cloth: three weights of one-shots (the suit moving under a step) and two beds."""
    CURRENT["group"] = "cloth"
    key = "cloth_light"; CURRENT["key"] = key
    rng = rng_for(key)
    tee24 = get("FOLYClth_ClothMovement24_InMotionAudio_FoleyT-Shirt", lowpass=4000.0)
    tee29 = get("FOLYClth_ClothMovement29_InMotionAudio_FoleyT-Shirt", lowpass=4500.0)
    singles = [get(m, lowpass=CLOTH_TOP) for m in ("FOLYClth_SinglePats04_InMotionAudio_FoleyT-Shirt", "CLOTHING_MATERIAL_MOVEMENT_08",
                                                   "shake_clothing_001", "Scrape_Cloth_10",
                                                   "CLOTHFlp_Action Inventory Open Flip Cloth Canvas Bag Slide Light 02")]
    pieces = (active_slices(tee24, 2, (0.30, 0.65), rng, -26.0) + active_slices(tee29, 2, (0.30, 0.65), rng, -26.0)
              + [fade(tail_trim(s), 5, 0.1) for s in singles])
    takes = spread_pick(pieces, 8, key=lambda t: head(t))
    emit(key, level_pool(takes, LEVELS[key]))

    key = "cloth_medium"; CURRENT["key"] = key
    rng = rng_for(key)
    blanket = get("Blanket-Lift_06", lowpass=CLOTH_TOP)                                  # a cotton blanket flapped
    jacket = get("person_removing_jacket_001", lowpass=CLOTH_TOP)
    swing = get("Whoosh_Cloth_Leather_Fight_174", lowpass=CLOTH_TOP)                     # a heavy jacket swung
    tee24 = get("FOLYClth_ClothMovement24_InMotionAudio_FoleyT-Shirt", lowpass=4000.0)
    pieces = (active_slices(blanket, 3, (0.35, 0.70), rng, -24.0, min_active=0.7) + active_slices(jacket, 3, (0.35, 0.70), rng, -24.0, min_active=0.7)
              + [fade(tail_trim(swing), 6, 0.15)] + active_slices(tee24, 1, (0.4, 0.7), rng, -24.0))
    emit(key, level_pool(spread_pick(pieces, 8, key=lambda t: head(t)), LEVELS[key]))

    key = "cloth_hard"; CURRENT["key"] = key
    rng = rng_for(key)
    armour = get("LeatherArmor_Rustle_03", lowpass=CLOTH_TOP)
    hero = get("HERO - CK - ARMOR Leather Walk", lowpass=4000.0, highpass=200.0)
    swing = get("Whoosh_Cloth_Leather_Fight_174", lowpass=CLOTH_TOP)
    blanket = get("Blanket-Lift_06", lowpass=CLOTH_TOP)
    pieces = (active_slices(hero, 4, (0.40, 0.80), rng, -20.0) + [fade(tail_trim(armour), 6, 0.12), fade(tail_trim(swing), 6, 0.15)]
              + active_slices(blanket, 2, (0.45, 0.80), rng, -22.0, min_active=0.7))
    emit(key, level_pool(spread_pick(pieces, 8, key=lambda t: head(t)), LEVELS[key]))

    # beds: the suit moving as the player walks and jogs. One leather-armour walk for both (one suit), different stretches.
    key = "loop_cloth_walk"; CURRENT["key"] = key
    hero = get("HERO - CK - ARMOR Leather Walk", lowpass=CLOTH_TOP, highpass=250.0)   # above the thump of its own footfalls
    walk_at = calm_start(hero, 10.0, 0.6)
    emit_loop(key, make_loop(hero, 10.0, 0.6, LEVELS[key], start=walk_at))
    key = "loop_cloth_jog"; CURRENT["key"] = key
    hero = get("HERO - CK - ARMOR Leather Walk", lowpass=4000.0, highpass=300.0)
    length = hero.shape[1] / RATE
    jog_at = 0.0 if walk_at > length / 2 else max(length - 10.6 - 0.05, 0.0)       # the other end of the walk
    emit_loop(key, make_loop(hero, 10.0, 0.6, LEVELS[key], start=jog_at))


def build_gear():
    CURRENT["group"] = "gear"
    key = "gear_tick"; CURRENT["key"] = key
    rng = rng_for(key)
    groups = []
    # One tick of metal hardware, a tenth of a second at most (2026-10-14). The takes were a quarter-second of rattle, from a
    # plastic buckle and a whole jogging backpack among others, open to 12 kHz (79% above 4 kHz, a crinkle of 303 per mille)
    # on a third of all walking steps: over the dark moss steps that is a rustle, not a tick.
    for match, length, rise, sep in (("SBvfe1_Jiggling Belt Buckle 004", 0.09, 8.0, 70), ("EQUISprt_Climbing Gear, Carabiner, Screwing Lock 01", 0.09, 8.0, 70),
                                     ("foley_soldier_gear_equipment_metal_cloth_heavy_movement_light_08", 0.09, 8.0, 80)):
        x = get(match, lowpass=7000.0, highpass=600.0)
        got = onset_events(x, length, rise=rise, sep=sep, fall=0.04)
        got = [g for g in got if g.shape[1] > int(0.05 * RATE) and head(g) > -60.0]
        groups.append(spread_pick(got, 4, key=head) if len(got) >= 4 else got)
    pieces = [p for g in groups for p in g]
    takes = spread_pick(pieces, 10, key=lambda t: (head(t), t.shape[1]))
    emit(key, level_pool(takes, LEVELS[key]))
    key = "loop_gear_bounce"; CURRENT["key"] = key
    # Closed at 3 kHz (2026-10-14): open to 10 kHz, 58% of this bed was above 4 kHz, the buckles' rattle, a crinkle of 236
    # per mille under every jog. What is left is the pack's leather thumping against the back.
    pack = get("PR LEATHER BACKPACK_JOG_416", lowpass=3000.0, highpass=150.0)
    emit_loop(key, make_loop(pack, 10.0, 0.5, LEVELS[key]))


# Vegetation (2026-10-14): the brushes were corn leaves and stalks (papery, 56-60% above 4 kHz), broadleaf reeds (their
# own maker calls them "brittle, crinkle, crackling": 72 sharp transients a second, crinkle 291 per mille as the stiff-plant
# bed) and green leaves open to 6.5-8 kHz. Plants against the legs on these islands are wet grass and sedge. All of it is
# now the green leaves alone (green, not dry: no crackle in them below 3 kHz), closed at 2.5 to 3 kHz: a soft swish.
BRUSH_TOP = 2500.0


def build_vegetation():
    CURRENT["group"] = "vegetation"
    key = "swish_grass"; CURRENT["key"] = key
    rng = rng_for(key)
    green = get("RUSTLE Studio performed green leaves twigs brush moves", lowpass=BRUSH_TOP, highpass=150.0)
    pieces = active_slices(green, 10, (0.3, 0.6), rng, -22.0, rise_ms=30.0, fall=0.15)
    takes = spread_pick(pieces, 10, key=lambda t: (head(t), t.shape[1]))
    emit(key, level_pool(takes, LEVELS[key]))

    key = "loop_brush_soft"; CURRENT["key"] = key
    green = get("RUSTLE Studio performed green leaves twigs brush moves", lowpass=BRUSH_TOP, highpass=120.0)
    soft_at = calm_start(green, 14.0, 2.5)
    emit_loop(key, make_loop(green, 14.0, 2.5, LEVELS[key], start=soft_at))
    # Stiffer stalks: the same plants pushed harder, a little more of their top kept, from another stretch of the take.
    key = "loop_brush_hard"; CURRENT["key"] = key
    green = get("RUSTLE Studio performed green leaves twigs brush moves", lowpass=3000.0, highpass=150.0)
    half = green.shape[1] // 2
    other = (0 if soft_at * RATE > half else half)
    hard_at = other / RATE + calm_start(green[:, other:other + half], 14.0, 2.5)
    emit_loop(key, make_loop(green, 14.0, 2.5, LEVELS[key], start=hard_at))


def build_debris():
    CURRENT["group"] = "debris"
    key = "debris_grit"; CURRENT["key"] = key
    rng = rng_for(key)
    pieces = []
    for match, how in (("Tatak_ROCKS_Single Rock Impact on Stones", dict(length=0.45, rise=10.0, sep=300)),
                       ("Rock Hit and Bounce Dirt_RBDII 06", dict(length=0.35, rise=8.0, sep=100)),
                       ("Hit Rock into Gravel_RBDII 06", dict(length=0.4, rise=8.0, sep=100)),
                       ("SBvfe2_Medium Rock Dropping 011", dict(length=0.35, rise=6.0, sep=100)),
                       ("Small Crushed Rock - DROP - Thick - Stereo", dict(length=0.35, rise=12.0, sep=800))):
        x = get(match, highpass=120.0)
        got = onset_events(x, how["length"], rise=how["rise"], sep=how["sep"], fall=0.12)
        pieces += spread_pick(got, 3, key=head) if len(got) > 3 else got
    for match in ("Tatak_ROCKS_Pebbles Dust Tumbling Rolling Debris", "Scree Rock_RBD 06", "Crumbling Rock Slide_RBDII 07"):
        pieces += active_slices(get(match, highpass=120.0), 2, (0.18, 0.40), rng, -24.0, rise_ms=3.0, fall=0.1)
    takes = spread_pick(pieces, 12, key=lambda t: (t.shape[1], head(t)))
    emit(key, level_pool(takes, LEVELS[key]))

    key = "debris_snow"; CURRENT["key"] = key
    rng = rng_for(key)
    pieces = []
    for match, how in (("ICEBrk_FROZEN FARMLAND Footstep on frozen field", dict(length=0.5, rise=5.0, sep=100)),
                       ("Ice,Creak,Sheet,Crunch,Step,Snow,Various10", dict(length=0.5, rise=6.0, sep=100)),
                       ("Ice,Impact,Throw,Snowball,Step,Snow,Stalactite,Various12.M", dict(length=0.4, rise=6.0, sep=100))):
        x = get(match, highpass=150.0)
        got = onset_events(x, how["length"], rise=how["rise"], sep=how["sep"], fall=0.1)
        pieces += got[:3] if got else [fade(tail_trim(x), 4, 0.1)]
    for match in ("Ice Footsteps Hard Packed", "Snow Footstep Light Slow", "Snow Slide Medium"):
        pieces += active_slices(get(match, highpass=150.0), 2, (0.15, 0.35), rng, -22.0, rise_ms=3.0, fall=0.1)
    emit(key, level_pool(spread_pick(pieces, 8, key=lambda t: (t.shape[1], head(t))), LEVELS[key]))
    # (debris_stem, the snap of a dry stem under a step in the sedge, is gone: it was dry branches and corn stalks chosen
    # for being the sharpest snaps, 61% above 4 kHz and a crinkle of 310 per mille, and a stem's snap cannot be made
    # anything but a crackle. Without its files the game plays no stem layer; RETIRED below removes them.)


# Keys this library no longer makes: their files are removed on every run, so the game skips the layer.
RETIRED = ("debris_stem",)


def retire():
    for key in RETIRED:
        for path in OUT.glob(f"{key}_[0-9][0-9].wav"):
            path.unlink()
            MANIFEST.pop(path.stem, None)
            print(f"  removed {path.name}", flush=True)


def build_water():
    CURRENT["group"] = "water"
    # ---- wading: a step through water. The depth classes of brief section 5.
    key = "wade_ankle"; CURRENT["key"] = key
    rng = rng_for(key)
    puddle = get("0014_Footsteps_water_puddle_single_splashes", highpass=90.0)
    on, _, db, p = steps.find_steps(puddle, dict(mode="free"))
    hits = []
    for k, i in enumerate(on):
        nxt = on[k + 1] if k + 1 < len(on) else len(p)
        b = min(nxt - 20, i + 700)
        if b - i > 120:
            hits.append(fade(puddle[:, max(i - 8, 0) * HOP:b * HOP], 4, 0.15))
    hits.sort(key=head)
    small = get("Bluezone_BC0256_water_splash_small_008", highpass=120.0)
    drop = get("Bluezone_BC0256_water_splash_drop_006", highpass=120.0)
    ankle = hits[:len(hits) * 6 // 10:max(1, len(hits) * 6 // 10 // 6)][:6] + [fade(tail_trim(drop), 4, 0.15)] + active_slices(small, 1, (0.4, 0.8), rng, -26.0, 4.0, 0.2)
    emit(key, level_pool(ankle, LEVELS[key]))

    key = "wade_shin"; CURRENT["key"] = key
    rng = rng_for(key)
    splat = get("mud_splat_heavy_03", highpass=90.0)
    plop = get("LIQMvmt_Liquid Movement Plop Slosh Single 07", highpass=90.0)
    marching = get("EFX EXT GROUP Marching Puddle Mud 01", highpass=90.0)
    hard = hits[len(hits) * 6 // 10:]
    shin = (spread_pick(hard, 4, key=head) + [fade(tail_trim(splat), 4, 0.15)] + active_slices(plop, 1, (0.5, 0.9), rng, -22.0, 6.0, 0.2)
            + onset_events(marching, 0.8, rise=9.0, sep=400, fall=0.2)[:2])
    if len(shin) < 8:                                            # the rest from the harder puddle steps
        shin += [h for h in spread_pick(hard, 8, key=head) if not any(h is t for t in shin)][:8 - len(shin)]
    emit(key, level_pool(shin, LEVELS[key]))

    key = "wade_knee"; CURRENT["key"] = key
    rng = rng_for(key)
    lion = get("Water_Splash_SeaLion_Fienup_001", lowpass=4500.0, highpass=80.0, pitch=-3.0)
    tub = get("Water,Splashes,Bath Tub,Roomy,Dunk,Slow,Hard,x3", lowpass=4500.0, highpass=80.0, pitch=-3.0)
    lake = get("Glacial Lake - Large Size Rock Splashes - 5 Meters", lowpass=4000.0, highpass=80.0, pitch=-2.0)
    knee = (active_slices(lion, 3, (0.7, 1.2), rng, -24.0, 20.0, 0.3) + active_slices(tub, 3, (0.7, 1.2), rng, -24.0, 20.0, 0.3)
            + onset_events(lake, 1.1, rise=12.0, sep=2000, fall=0.35, rise_ms=10.0)[:2])
    emit(key, level_pool(knee, LEVELS[key]))

    key = "loop_wade"; CURRENT["key"] = key
    wash = get("Water,Inlet,Wash,Bubbling,Between,Rocks,Liquid,Bubbles,Loop_01", lowpass=5000.0, highpass=100.0)
    emit_loop(key, make_loop(wash, 20.0, 3.0, LEVELS[key]))

    # ---- entering water: small to large, in that order, so the game can pick by how fast the player went in
    key = "splash"; CURRENT["key"] = key
    pieces = []
    for match, how in (("Bluezone_BC0256_water_splash_small_008", dict(length=1.2, rise=10.0, sep=300)), ("LIQUID HIT Lake, Branch, Splash, Lake, Seaweed in Water, Small, CU 04", dict(length=1.2, rise=10.0, sep=300)),
                       ("Bluezone_BC0256_water_splash_008", dict(length=1.6, rise=12.0, sep=600)), ("Splash_Dive_Fienup_002", dict(length=1.8, rise=14.0, sep=2000)),
                       ("Glacial Lake - Large Size Rock Splashes - 5 Meters", dict(length=1.8, rise=14.0, sep=2000)), ("Water_Splash_SeaLion_Fienup_001", dict(length=1.2, rise=8.0, sep=600))):
        x = get(match, highpass=70.0)
        got = onset_events(x, how["length"], rise=how["rise"], sep=how["sep"], fall=0.5, rise_ms=3.0)
        pieces += got[:2] if got else [fade(tail_trim(x), 3, 0.4)]
    pieces = spread_pick(pieces, 8, key=lambda t: head(t))            # small to large
    lo, hi = LEVELS["splash"]
    emit(key, [t * 10 ** ((lvl - head(t)) / 20.0) for t, lvl in zip(pieces, np.linspace(lo, hi, len(pieces)))])

    key = "loop_underwater"; CURRENT["key"] = key
    bed = get("UWM027", highpass=60.0)
    emit_loop(key, make_loop(bed, 40.0, 4.0, LEVELS[key]))

    key = "bubbles"; CURRENT["key"] = key
    rng = rng_for(key)
    pieces = []
    for match, n in (("24 Bubbles_regulator_submerged 4_stereo_AKG", 3), ("UWM008", 2), ("Underwater_Bubbles_Deep_Fienup_016", 1), ("Underwater_Bubbles_Fienup_026", 1), ("water_bubble_19", 1)):
        x = get(match, highpass=80.0)
        got = active_slices(x, n, (0.6, 1.6), rng, -22.0, 20.0, 0.3) if x.shape[1] > 4 * RATE else [fade(tail_trim(x), 8, 0.25)]
        pieces += got
    emit(key, level_pool(pieces[:8], LEVELS[key], measure=full))


def build_extras():
    """Optional sweeteners the game can use where they exist: a squelch under a wet step, a scree bed, drips on surfacing, a kicked pebble."""
    CURRENT["group"] = "extras"
    key = "squelch"; CURRENT["key"] = key
    pieces = []
    for match, length in (("mud_splat_heavy_03", 0.6), ("Bluezone_BC0261_mountain_dweller_creature_orc_footstep_mud_texture_003", 0.7),
                          ("Bluezone_BC0272_creature_texture_mud_006", 0.7), ("LIQMvmt_Liquid Movement Plop Slosh Single 07", 0.7), ("mud 1 loop", 0.5)):
        x = get(match, highpass=120.0, lowpass=7000.0)
        got = onset_events(x, length, rise=8.0, sep=250, fall=0.15, rise_ms=6.0)
        pieces += got[:2] if got else [fade(tail_trim(x), 6, 0.15)]
    emit(key, level_pool(spread_pick(pieces, 8, key=lambda t: (t.shape[1], head(t))), LEVELS[key]))

    key = "drip"; CURRENT["key"] = key
    pieces = []
    for match, n in (("Bluezone_BC0256_water_splash_drop_006", 2), ("cave_drips_05_30_loop", 3), ("Water_Drain_Sewer_Fienup_005", 3), ("WATRDrip_SingleDrip03_InMotionAudio_CaveDesign", 1)):
        x = get(match, highpass=250.0)
        got = [g for g in onset_events(x, 0.3, rise=12.0, sep=250, fall=0.12, rise_ms=1.5) if head(g) > -75.0]
        pieces += spread_pick(got, n, key=head) if len(got) > n else (got or [fade(tail_trim(x), 2, 0.1)])
    emit(key, level_pool(pieces[:8], LEVELS[key]))

    key = "pebble"; CURRENT["key"] = key
    pieces = []
    for match, length, rise, sep in (("Rock Hit and Bounce Dirt_RBDII 06", 0.9, 8.0, 800), ("Hit Rock into Gravel_RBDII 06", 0.8, 8.0, 800),
                                     ("SBvfe2_Medium Rock Dropping 011", 0.6, 6.0, 800), ("Tatak_ROCKS_Single Rock Impact on Stones", 0.8, 10.0, 1500)):
        x = get(match, highpass=150.0)
        got = onset_events(x, length, rise=rise, sep=sep, fall=0.25, rise_ms=2.0)
        pieces += got[:3] if got else [fade(tail_trim(x), 2, 0.2)]
    for k in range(2):                                           # the first clacks of the bouncing stone alone
        x = get("Rock Hit and Bounce Dirt_RBDII 06", highpass=150.0)
        pieces += [fade(g[:, :int(0.3 * RATE)], 2, 0.1) for g in onset_events(x, 0.3, rise=8.0, sep=100, fall=0.1)[k:k + 1]]
    emit(key, level_pool(spread_pick(pieces, 8, key=lambda t: (t.shape[1], head(t))), LEVELS[key]))

    key = "loop_scree"; CURRENT["key"] = key
    scree = get("Scree Rock_RBD 06", lowpass=8000.0, highpass=150.0)
    emit_loop(key, make_loop(scree, 7.0, 1.5, LEVELS[key]))


def breath_cycles(x, longest, shortest, prominence=8.0, smooth=0.12):
    """One piece per breath: the stretches between the troughs of the level."""
    from scipy import signal
    mono = x.mean(axis=0)
    h = int(0.02 * RATE)
    n = len(mono) // h
    lv = 10 * np.log10(np.array([np.mean(mono[i * h:(i + 1) * h] ** 2) for i in range(n)]) + 1e-12)
    k = max(int(smooth / 0.02), 1)
    sm = np.convolve(lv, np.ones(k) / k, mode="same")
    troughs, _ = signal.find_peaks(-sm, prominence=prominence, distance=int(shortest / 0.02))
    out = []
    for a, b in zip(troughs[:-1], troughs[1:]):
        if shortest <= (b - a) * 0.02 <= longest and sm[a:b].max() > np.percentile(sm, 60):
            out.append(fade(x[:, a * h:b * h], 40.0, 0.12))
    return out


def build_breath():
    CURRENT["group"] = "breath"
    plans = (("breath_calm", "HMNBrth_Human, Adult, Male, Breathe, Mouth, Slow,_UberDuo_INHL", (2.5, 6.0), dict(lowpass=6000.0, highpass=100.0)),
             ("breath_working", "HUMAN BREATH Male_Deep Mouth Inhale and Powerful Strong Exhale with Acceleration_A", (0.9, 2.2), {}),
             ("breath_heavy", "Male 1 - Breathing - Gasping - Ehrlund M1", (1.0, 3.0), {}),
             ("breath_recovery", "HUMAN BREATH Male_Deep Opened Mouth Breath with S Formed Lips Long Exhale_F", (2.5, 7.0), dict(highpass=100.0)))
    for key, match, (shortest, longest), shape in plans:
        CURRENT["key"] = key
        x = get(match, **shape)
        cycles = breath_cycles(x, longest, shortest, prominence=6.0 if key == "breath_calm" else 10.0)
        if key == "breath_working":
            cycles = cycles[:max(len(cycles) // 2, 3)]            # the early, slower breaths of its acceleration
        if len(cycles) < 3:
            raise SystemExit(f"{key}: only {len(cycles)} breaths found")
        emit(key, level_pool(spread_pick(cycles, 3, key=full), LEVELS[key], measure=full))


# --------------------------------------------------------------------------------------------------- events

CLASS_SURFACES = dict(soft=("moss", "sedge"), grit=("gravel", "basalt", "ash"), snow=("snow",), water=("water",))


def surface_pool(cls, tier, kind="step"):
    items = []
    for s in CLASS_SURFACES[cls]:
        for foot in ("l", "r"):
            try:
                items += pool_out(f"step_{s}_{tier}_{foot}_")
            except SystemExit:
                pass
    if not items:
        raise SystemExit(f"no {cls} steps of tier {tier}: run steps.py first")
    return items


def build_events():
    CURRENT["group"] = "events"
    light, medium, hard = pool_out("cloth_light_"), pool_out("cloth_medium_"), pool_out("cloth_hard_")
    ticks = pool_out("gear_tick_")
    slides = {}
    CURRENT["key"] = "slides"
    # A sole dragged over soft ground: a heavy log dragged over a forest floor, closed at 3.5 kHz (0.2% left above 4 kHz).
    # The dry-grass drag that stood here crinkled (46% of its 3-10 kHz energy in sharp transients).
    slides["soft"] = get("HeavyWood_Sliding_on_ForestFloor_01_04", lowpass=3500.0, highpass=120.0)
    slides["grit"] = [get(m, lowpass=7000.0, highpass=150.0) for m in ("rockslide_02", "Crumbling Rock Slide_RBDII 07", "Scree Rock_RBD 06")]
    slides["snow"] = get("Snow Slide Medium", highpass=120.0)
    for cls in ("soft", "grit", "snow"):
        rolls_c, rolls_w, rolls_r = (pool_out(f"roll_{cls}_{t}_") for t in ("creep", "walk", "run"))
        creep = surface_pool(cls, "creep")
        debris = pool_out({"grit": "debris_grit_", "snow": "debris_snow_"}[cls]) if cls != "soft" else []    # nothing loose in moss

        def slide_pieces(n, lengths, rng, depth):
            src = slides[cls]
            if cls == "grit":
                got = [p for k in range(n) for p in active_slices(src[k % 3], 1, lengths, rng, depth, rise_ms=6.0, fall=0.1)]
            else:
                got = active_slices(src, n, lengths, rng, depth, rise_ms=6.0, fall=0.1)
            return got or [fade(tail_trim(steps.lead_trim(src if cls != "grit" else src[0])), 6.0, 0.15)]

        key = f"start_{cls}"; CURRENT["key"] = key
        rng = rng_for(key)
        takes = [mix((pick_one(light, rng), 0.0, 0.0), (pick_one(rolls_c + rolls_w, rng), 0.04, -4.0)) for _ in range(6)]
        emit(key, level_pool([fade(t, 8, 0.15) for t in takes], LEVELS["start"]))

        key = f"settle_{cls}"; CURRENT["key"] = key
        rng = rng_for(key)
        takes = [mix((pick_one(creep, rng), 0.0, 0.0), (pick_one(light, rng), 0.02, -7.0)) for _ in range(6)]
        emit(key, level_pool([fade(t, 3, 0.12) for t in takes], LEVELS["settle"]))

        key = f"turn_{cls}"; CURRENT["key"] = key
        rng = rng_for(key)
        scuffs = slide_pieces(6, (0.30, 0.55), rng, -22.0)
        takes = [mix((scuffs[k % len(scuffs)], 0.0, 0.0), (pick_one(rolls_w, rng), 0.03, -5.0), (pick_one(light + medium, rng), 0.0, -8.0)) for k in range(6)]
        emit(key, level_pool([fade(t, 10, 0.15) for t in takes], LEVELS["turn"]))

        key = f"skid_{cls}"; CURRENT["key"] = key
        rng = rng_for(key)
        drags = slide_pieces(6, (0.55, 0.95), rng, -22.0)
        takes = [mix(*[(drags[k % len(drags)], 0.0, 0.0), (pick_one(rolls_r, rng), 0.0, -3.0)] + ([(pick_one(debris, rng), 0.05, -6.0)] if debris else []))
                 for k in range(6)]
        emit(key, level_pool([fade(t, 6, 0.2) for t in takes], LEVELS["skid"]))

    # ---- take-off: the push-off of a run-tier step, cloth swish 20 ms after, a gear tick
    key = "takeoff"; CURRENT["key"] = key
    rng = rng_for(key)
    push = [t for cls in ("soft", "grit") for t in surface_pool(cls, "run")]
    swish = [fade(tail_trim(get(m, lowpass=CLOTH_TOP)), 5, 0.12) for m in ("Whoosh,Organic,Fabric,Glove,Airy,Breathy,Punchy", "Whoosh_Cloth_Leather_Fight_174")] + medium
    takes = [mix((pick_one(push, rng), 0.0, 0.0), (pick_one(swish, rng), 0.02, -9.0), (pick_one(ticks, rng), 0.03, -12.0)) for _ in range(6)]
    emit(key, level_pool([fade(t, 3, 0.15) for t in takes], LEVELS["takeoff"]))

    # ---- landings: an impact; a second foot after 25 to 45 ms; a gear thump; a puff of debris; for a hard one the body's
    # weight (Red Libraries' body falls, their low end) and the crouching cloth
    CURRENT["key"] = "landings"
    def first(x, seconds, fall):
        x = steps.lead_trim(x)
        return fade(x[:, :int(seconds * RATE)], 2.0, fall)

    body_dirt = first(get("RL_bodyfall_Dirt_M4_Close_Stereo_Hard_Impact_10", lowpass=450.0), 0.6, 0.25)
    body_conc = first(get("RL_bodyfall_Concrete_Generic_Feet_Mid_Mono_Med_Impact_02", lowpass=450.0), 0.6, 0.25)
    sub = first(get("RL_bodyfall_Dirt_M4_Close_Stereo_Hard_Impact_10", lowpass=110.0), 0.6, 0.25)
    thump = first(get("ARMOR Body Drop Chain Leather Short 02", lowpass=1100.0), 0.5, 0.2)
    landings = [fade(p, 3, 0.35) for p in onset_events(get("289_Foley_Footsteps_Rocks_Sneaker_Jump_Land_On_Two_Feet_Close"), 0.7, rise=14.0, sep=700, fall=0.3)]
    stomps = [fade(p, 3, 0.35) for p in onset_events(get("Snow Impact Stomp Hard"), 0.8, rise=14.0, sep=500, fall=0.3)]
    splashes = [fade(p, 3, 0.4) for m in ("Bluezone_BC0256_water_splash_small_008", "LIQUID HIT Lake, Branch, Splash, Lake, Seaweed in Water, Small, CU 04", "Bluezone_BC0256_water_splash_008", "Splash_Dive_Fienup_002")
                for p in onset_events(get(m, highpass=70.0), 1.0, rise=12.0, sep=1500, fall=0.4)[:2]]
    mains = dict(soft=surface_pool("soft", "run") + surface_pool("soft", "sprint"),
                 grit=landings, snow=stomps, water=splashes)
    lite = dict(soft=mains["soft"], grit=surface_pool("grit", "run"), snow=surface_pool("snow", "run"), water=surface_pool("water", "run"))
    puffs = dict(soft=[], grit=pool_out("debris_grit_"), snow=pool_out("debris_snow_"), water=[])
    for cls in ("soft", "grit", "snow", "water"):
        for weight in ("soft", "medium", "hard"):
            key = f"land_{cls}_{weight}"; CURRENT["key"] = key
            rng = rng_for(key)
            takes = []
            for k in range(6):
                if weight == "soft":
                    layers = [(pick_one(lite[cls], rng), 0.0, 0.0), (pick_one(light, rng), 0.02, -12.0)]
                else:
                    a, b = pick_one(mains[cls], rng), pick_one(mains[cls] if cls != "soft" else lite[cls], rng)
                    layers = [(a, 0.0, 0.0), (b, float(rng.uniform(0.025, 0.045)), -2.0), (pick_one(medium if weight == "medium" else hard, rng), 0.01, -12.0),
                              (thump, 0.005, -10.0 if weight == "medium" else -7.0)]
                    if puffs[cls]:
                        layers.append((pick_one(puffs[cls], rng), 0.03, -14.0 if weight == "medium" else -10.0))
                    if weight == "hard":
                        layers += [(body_conc if k % 2 else body_dirt, 0.0, -4.0), (sub, 0.0, -2.0)]
                takes.append(fade(tail_trim(mix(*layers)), 2, 0.3))
            emit(key, level_pool(takes, LEVELS[f"land_{weight}"]))


GROUPS = dict(cloth=build_cloth, gear=build_gear, vegetation=build_vegetation, debris=build_debris, water=build_water, extras=build_extras,
              breath=build_breath, events=build_events)


def kept_rows(rebuilt):
    """The table rows of CREDITS.md's foley section for the groups not rebuilt in this run, by group."""
    path = OUT / "CREDITS.md"
    text = path.read_text() if path.exists() else ""
    start = text.find("\n## Foley library\n")
    if start < 0:
        return {}
    end = text.find("\n## ", start + 5)
    rows = {}
    for line in text[start:end if end >= 0 else len(text)].splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if line.startswith("| ") and len(cells) == 4 and cells[0] in GROUPS and cells[0] not in rebuilt:
            rows.setdefault(cells[0], []).append(line)
    return rows


def write_credits(rebuilt):
    old = kept_rows(rebuilt)
    lines, seen = [], set()
    for group in GROUPS:
        if group not in rebuilt:
            lines += old.get(group, [])
            continue
        for key, row in CREDITS.get(group, []):
            if (row["file"], group) in seen:
                continue
            seen.add((row["file"], group))
            lines.append(f"| {group} | {row['file']} | {row['library']} | {row['year']} |")
    body = ("\nThe cloth, gear, vegetation, debris, water, breath and movement-event sounds of the layered step system, cut and laid together by "
            "`Tools/audio/foley.py` (same bundles, same licence). Every one is made of real recordings, listed here; nothing is synthesised. "
            "The events (`start_`, `settle_`, `turn_`, `skid_`, `takeoff_`, `land_`) are also made of the steps and rolls of `steps.py`.\n\n"
            "| Group | Recording | Library | Bundle |\n|---|---|---|---|\n" + "\n".join(lines) + "\n")
    steps.update_credits_section("Foley library", body)


def main(args):
    if args and args[0] == "audit":
        steps.audit(args[1:] or None)
        return
    global MANIFEST
    MANIFEST = steps.load_manifest()
    unknown = [g for g in args if g not in GROUPS]
    if unknown:
        raise SystemExit(f"unknown group {' '.join(unknown)}: {' '.join(GROUPS)}")
    chosen = [g for g in GROUPS if not args or g in args]
    retire()
    for g in chosen:
        print(f"{g}:", flush=True)
        GROUPS[g]()
        CACHE.clear()
        steps.save_manifest(MANIFEST)
    steps.save_manifest(MANIFEST)
    write_credits(set(chosen))
    steps.audit()


if __name__ == "__main__":
    main(sys.argv[1:])
