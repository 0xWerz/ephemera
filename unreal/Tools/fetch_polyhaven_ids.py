"""Fetches named Poly Haven texture sets (CC0) at 4K: colour, DirectX normal, AO/rough/metal, displacement.

    python3 -B fetch_polyhaven_ids.py <label> <id> [<id> ...]

Saved to SourceAssets/polyhaven/textures/<label>/<id>/ and appended to the manifest. Safe to re-run.
"""
import hashlib
import json
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "SourceAssets/polyhaven"
HEADERS = {"User-Agent": "EphemeraFanProject/1.0"}
WANTED = (("Diffuse", "jpg"), ("nor_dx", "jpg"), ("arm", "jpg"), ("Displacement", "png"))


def get(url):
    for _ in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=120) as response:
                return response.read()
        except Exception as error:  # noqa: BLE001
            last = error
            time.sleep(5)
    raise RuntimeError(f"{url}: {last}")


def main():
    label, ids = sys.argv[1], sys.argv[2:]
    manifest_path = ROOT / "manifest.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else []
    known = {entry["id"] for entry in manifest}
    for asset in ids:
        files = json.loads(get(f"https://api.polyhaven.com/files/{asset}"))
        folder = ROOT / "textures" / label / asset
        folder.mkdir(parents=True, exist_ok=True)
        saved, total = [], 0
        for key, extension in WANTED:
            entry = (files.get(key, {}).get("4k") or files.get(key, {}).get("2k") or {}).get(extension)
            if not entry:
                print(f"{asset}: no {key}")
                continue
            target = folder / entry["url"].rsplit("/", 1)[-1]
            if not (target.exists() and hashlib.md5(target.read_bytes()).hexdigest() == entry["md5"]):
                data = get(entry["url"])
                if hashlib.md5(data).hexdigest() != entry["md5"]:
                    raise RuntimeError(f"{target.name}: checksum mismatch")
                target.write_bytes(data)
            saved.append(str(target.relative_to(ROOT)))
            total += target.stat().st_size
        print(f"{label}/{asset}: {total / 1e6:.0f} MB", flush=True)
        if asset not in known:
            manifest.append({"id": asset, "kind": "texture", "label": label, "license": "CC0 1.0, polyhaven.com",
                             "page": f"https://polyhaven.com/a/{asset}", "files": saved, "bytes": total})
    manifest_path.write_text(json.dumps(manifest, indent=1))


if __name__ == "__main__":
    main()
