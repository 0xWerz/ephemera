"""The score: composed music, laid out as the three long loops the game fades between (day, night, space).

    Tools/audio/run.sh score.py

The first score was three synthesised chords; the second, three held pads from a sound library. The owner's verdict on
the second was "so bad and generic". A pad is not music. These are pieces written by a composer, Scott Buckley
(scottbuckley.com.au), who releases his work under Creative Commons Attribution 4.0: free to use, with his name on it
(it is in the game's menu and in SourceAssets/audio/CREDITS.md). They were chosen from the 159 in his library by his
own description of each and by measurement (.scratch/audio/music_shape.py): slow, dark, no percussion, no great
build; strings, a solo instrument, piano, soft synth.

Music that never stops is wallpaper. Each place has two pieces with a minute and a half of nothing after each, and the
game is silent for the first half minute: the world is heard first.
"""
import pathlib
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pro
from pro import RATE

TRACKS = pro.SCRATCH / "music/tracks"
BASE = "https://www.scottbuckley.com.au/library/wp-content/uploads/"
AGENT = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36"
# title -> (file, why this piece, in the composer's own words where quoted)
PIECES = {
    "Borealis": ("sb_borealis.mp3", '"A contemplative, slow-moving ambient track, with slight uplifting tone. Inspired by the quiet stillness of the boreal forests."'),
    "Permafrost": ("Permafrost.mp3", '"A meditative, ebbing ambient work for strings, synth and solo violin." The evenest of all twelve measured: it never rises more than a decibel.'),
    "Shadows and Dust": ("ShadowsAndDust.mp3", '"An ethereal, enigmatic ambient track featuring synth drones and winds, blended with strings and synth textures."'),
    "The Long Dark": ("TheLongDark.mp3", '"A melancholic ambient piano track with strings and atmospheric synth": the long cold darkness of a northern winter.'),
    "Decoherence": ("sb_decoherence.mp3", '"An atmospheric, haunting ambient work featuring slowly morphing synths, ominous strings and solo viola. Perfect for deep space travel."'),
    "Adrift Among Infinite Stars": ("AdriftAmongInfiniteStars.mp3", '"A contemplative, minimalist piece for piano, synths, strings and solo violin": for looking up at the evening sky.'),
}
PLACES = {
    "loop_music_day": ("Borealis", "Permafrost"),
    "loop_music_night": ("Shadows and Dust", "The Long Dark"),
    "loop_music_space": ("Decoherence", "Adrift Among Infinite Stars"),
}
LEAD = 30.0       # seconds of nothing before the first piece
REST = 90.0       # and after each
LEVEL = -23.0     # a piece's level over its whole length, dB


def silence(out, seconds):
    out.writeframes(np.zeros((int(seconds * RATE), 2), "<i2").tobytes())


def main():
    pro.OUT.mkdir(parents=True, exist_ok=True)
    for name, titles in PLACES.items():
        total = 0.0
        with wave.open(str(pro.OUT / f"{name}.wav"), "wb") as out:
            out.setnchannels(2)
            out.setsampwidth(2)
            out.setframerate(RATE)
            silence(out, LEAD)
            total += LEAD
            for title in titles:
                path = TRACKS / PIECES[title][0]
                if not path.exists():
                    raise SystemExit(f"{path} is missing: fetch it from the composer's library page for '{title}'")
                x = pro.load(path).astype(np.float32)
                x = x * 10 ** ((LEVEL - pro.level_db(x)) / 20.0)
                top = float(np.abs(x).max())
                if top > 0.89:
                    x = x * (0.89 / top)
                data = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
                out.writeframes(data.T.tobytes())
                total += x.shape[1] / RATE
                silence(out, REST)
                total += REST
        print(f"{name:18s} {total / 60:5.1f} min: " + ", ".join(titles), flush=True)
    credits = pro.OUT / "CREDITS.md"
    text = credits.read_text() if credits.exists() else "# Sounds\n"
    marker = "\n## Music\n"
    text = text.split(marker)[0].rstrip("\n") + "\n" + marker + "\nBy Scott Buckley, released under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/), www.scottbuckley.com.au. Laid out by `Tools/audio/score.py`.\n\n"
    for name, titles in PLACES.items():
        for title in titles:
            text += f"- '{title}' by Scott Buckley (`{name}`): {PIECES[title][1]}\n"
    credits.write_text(text)


if __name__ == "__main__":
    main()
