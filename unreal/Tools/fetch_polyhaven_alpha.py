"""Fetches the opacity maps for Poly Haven plant scans.

The glTF packages reference only colour, normal and AO/roughness maps; the cut-out mask for leaves
is a separate "Alpha" (or "<part>_alpha") map. Without it every leaf card renders as a solid
rectangle. Saved next to the scan's other textures as <name>_alpha_2k.jpg. Safe to re-run.
"""
import hashlib
import json
import sys
import time
import urllib.request
from pathlib import Path

MODELS = Path(__file__).resolve().parents[1] / "SourceAssets/polyhaven/models"
HEADERS = {"User-Agent": "EphemeraFanProject/1.0"}


def get(url):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=60) as response:
                return response.read()
        except Exception as error:  # noqa: BLE001 - report and retry
            last = error
            time.sleep(5)
    raise RuntimeError(f"{url}: {last}")


def main():
    fetched = 0
    for folder in sorted(p for p in MODELS.glob("*/*") if p.is_dir() and (p / "textures").is_dir()):
        files = json.loads(get(f"https://api.polyhaven.com/files/{folder.name}"))
        for key, by_resolution in files.items():
            if key.lower() != "alpha" and not key.lower().endswith("_alpha"):
                continue
            entry = (by_resolution.get("2k") or by_resolution.get("1k") or {}).get("jpg")
            if not entry:
                continue
            target = folder / "textures" / entry["url"].rsplit("/", 1)[-1]
            if target.exists() and hashlib.md5(target.read_bytes()).hexdigest() == entry["md5"]:
                continue
            data = get(entry["url"])
            if hashlib.md5(data).hexdigest() != entry["md5"]:
                raise RuntimeError(f"{target.name}: checksum mismatch")
            target.write_bytes(data)
            fetched += 1
            print(f"{folder.name}: {target.name} ({len(data) / 1e6:.1f} MB)", flush=True)
    print(f"done: {fetched} alpha maps fetched")


if __name__ == "__main__":
    sys.exit(main())
