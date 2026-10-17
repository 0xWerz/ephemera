#!/usr/bin/env python3
"""Download CC0 Poly Haven textures and models for the Ephemera / Unreal project.

Run as:  uv run --with pillow python -B fetch_polyhaven.py
Stdlib + Pillow only. Downloads are sequential. Temp files live in .scratch/polyhaven_tmp.
"""
import hashlib
import http.client
import json
import math
import os
import shutil
import sys
import time
import urllib.parse
import urllib.request

from PIL import Image, ImageDraw, ImageFont

PROJECT = "/Volumes/External/code/tests/outerwilds"
OUT = os.path.join(PROJECT, "unreal", "SourceAssets", "polyhaven")
TMP = os.path.join(PROJECT, ".scratch", "polyhaven_tmp")
UA = "EphemeraFanProject/1.0"
LICENSE = "CC0 1.0, polyhaven.com"
BUDGET_BYTES = 9 * 10**9
MB = 10**6
GB = 10**9
RETRIES = 3
RETRY_WAIT = 5
ASSETS_API = "https://api.polyhaven.com/assets?t={}"
FILES_API = "https://api.polyhaven.com/files/{}"
THUMB_URL = "https://cdn.polyhaven.com/asset_img/thumbs/{}.png?height=256"
RETRYABLE = (OSError, http.client.HTTPException)

# label, keywords, excludes, N
TEX_RULES = [
    ("forest_floor", "forest, moss, leaves, roots", "brick, tile, concrete, asphalt, wall, plaster", 5),
    ("grass_meadow", "grass, meadow, clover", "brick, tile, concrete, asphalt, artificial", 3),
    ("mud_dirt", "mud, dirt, soil", "brick, tile, concrete, asphalt, road", 3),
    ("gravel_pebbles", "gravel, pebble, river", "brick, tile, concrete, asphalt, road", 3),
    ("sand", "sand, dune, beach", "brick, tile, concrete, sandstone wall", 2),
    ("rock_cliff", "cliff, rock face, rock_face, rock wall, boulder, slate, granite, basalt",
     "brick, tile, concrete, floor tiles, paving", 6),
    ("lunar_grey", "volcanic, lava, ash, moon, crater, burned", "brick, tile, concrete", 3),
    ("snow_ice", "snow, ice, frost", "brick, tile, concrete", 2),
    ("bark", "bark, tree trunk", "-", 4),
    ("wood_weathered", "weathered wood, old wood, planks, driftwood, rough wood",
     "laminate, parquet, floor, plywood", 5),
    ("woven", "wicker, rattan, thatch, straw, woven, bamboo, reed", "-", 3),
    ("fabric_rope", "burlap, canvas, hessian, rope, wool, leather", "-", 4),
    ("metal_worn", "rust, rusty, brass, copper, bronze, worn metal, scratched metal",
     "tile, floor, plate floor", 5),
]

MODEL_RULES = [
    ("rocks", "rock, boulder, stone, cliff", "wall, brick, statue, fountain, building", 10),
    ("trees", "tree, pine, fir, birch, oak, sapling", "stump, christmas, pot, bonsai", 8),
    ("stumps_logs", "stump, log, root, branch, dead wood, driftwood", "fireplace", 6),
    ("plants", "fern, grass, plant, flower, shrub, bush, weed, moss, ivy, reed, bamboo",
     "pot, potted, vase, planter, indoor", 12),
    ("mushrooms", "mushroom, fungus", "-", 3),
    ("camp_props", "lantern, barrel, crate, basket, bucket, rope, tent, campfire, firewood, wooden, sack, ladder",
     "plastic, modern", 10),
]


def die(msg):
    print("STOP: " + msg, flush=True)
    sys.exit(1)


def with_retries(fn, what):
    for attempt in range(RETRIES + 1):
        try:
            return fn()
        except RETRYABLE as e:
            if attempt == RETRIES:
                raise
            print(f"    HTTP retry {attempt + 1}/{RETRIES} ({what}): {e}", flush=True)
            time.sleep(RETRY_WAIT)


def get_json(url):
    def fn():
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.loads(r.read().decode("utf-8"))
    return with_retries(fn, url)


def download(url, dest):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    part = os.path.join(TMP, hashlib.sha1(dest.encode("utf-8")).hexdigest() + ".part")

    def fn():
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=120) as r, open(part, "wb") as f:
            shutil.copyfileobj(r, f, 1 << 20)
        os.replace(part, dest)
    with_retries(fn, url)


def md5_file(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_verified(url, md5, dest):
    """Skip if present with matching md5; otherwise download, verify, retry once on mismatch."""
    if os.path.exists(dest) and md5_file(dest) == md5:
        return
    for attempt in (1, 2):
        download(url, dest)
        if md5_file(dest) == md5:
            return
        print(f"    MD5 mismatch on {os.path.basename(dest)} (attempt {attempt}); deleting", flush=True)
        os.remove(dest)
    raise RuntimeError(f"md5 mismatch twice for {os.path.basename(dest)}")


def ensure_thumb(aid):
    dest = os.path.join(OUT, "thumbs", aid + ".png")
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return
    download(THUMB_URL.format(aid), dest)


def basename_of(url):
    return os.path.basename(urllib.parse.urlparse(url).path)


def rel_ok(rel):
    return bool(rel) and not rel.startswith("/") and ".." not in rel.split("/")


def pick_map(files, name, fmt):
    """Return (resolution, info) for a map at 4k, falling back to 2k."""
    m = files.get(name)
    if not isinstance(m, dict):
        return None, None
    for res in ("4k", "2k"):
        r = m.get(res)
        if isinstance(r, dict) and isinstance(r.get(fmt), dict):
            return res, r[fmt]
    return None, None


def plan_texture(aid, label, files):
    notes = []
    wanted = [("Diffuse", "jpg"), ("nor_dx", "jpg")]
    arm_res, arm = pick_map(files, "arm", "jpg")
    if arm is not None:
        wanted.append(("arm", "jpg"))
    else:
        notes.append("arm missing -> Rough+AO")
        wanted += [("Rough", "jpg"), ("AO", "jpg")]
    wanted.append(("Displacement", "png"))
    items = []
    for name, fmt in wanted:
        if name == "arm":
            res, info = arm_res, arm
        else:
            res, info = pick_map(files, name, fmt)
        if info is None:
            notes.append(f"no {name}.{fmt}")
            continue
        if res != "4k":
            notes.append(f"{name} at {res}")
        rel = f"textures/{label}/{aid}/{basename_of(info['url'])}"
        items.append({"url": info["url"], "md5": info["md5"], "size": info["size"], "rel": rel})
    if not items:
        raise ValueError("texture has no usable maps")
    return items, notes


def plan_model(aid, label, files):
    g = files.get("gltf")
    if not isinstance(g, dict):
        raise ValueError("model has no 'gltf' key")
    notes = []
    chosen = None
    for res in ("2k", "1k"):
        r = g.get(res)
        if isinstance(r, dict) and isinstance(r.get("gltf"), dict):
            chosen = (res, r["gltf"])
            break
    if chosen is None:
        raise ValueError("no 2k/1k gltf")
    res, f = chosen
    if res != "2k":
        notes.append(f"gltf at {res}")
    gltf_rel = f"models/{label}/{aid}/{basename_of(f['url'])}"
    items = [{"url": f["url"], "md5": f["md5"], "size": f["size"], "rel": gltf_rel}]
    include = f.get("include")
    if not isinstance(include, dict) or not include:
        raise ValueError("gltf entry has no include dict")
    for rel, info in include.items():
        if not rel_ok(rel):
            raise ValueError(f"unsafe include path {rel!r}")
        items.append({"url": info["url"], "md5": info["md5"], "size": info["size"],
                      "rel": f"models/{label}/{aid}/{rel}"})
    return items, notes


def fields_of(aid, info):
    vals = [aid, info.get("name") or ""] + list(info.get("categories") or []) + list(info.get("tags") or [])
    return [str(v).lower() for v in vals]


def kw_list(s):
    if s.strip() == "-":
        return []
    return [k.strip().lower() for k in s.split(",") if k.strip()]


def select(listing, rules):
    taken = set()
    out = []
    for label, kws_s, ex_s, n in rules:
        kws, exs = kw_list(kws_s), kw_list(ex_s)
        cands = []
        for aid, info in listing.items():
            if aid in taken:
                continue
            fs = fields_of(aid, info)
            if any(k in f for k in kws for f in fs) and not any(e in f for e in exs for f in fs):
                cands.append((aid, info))
        cands.sort(key=lambda p: (-(p[1].get("download_count") or 0), p[0]))
        chosen = cands[:n]
        taken.update(a for a, _ in chosen)
        out.append((label, n, len(cands), chosen))
    return out


def validate_listing(kind, listing):
    if not isinstance(listing, dict) or not listing:
        die(f"{kind} listing is not a non-empty object; type={type(listing).__name__}")
    for aid, info in listing.items():
        if not isinstance(info, dict):
            die(f"{kind} listing entry {aid} is not an object; keys={list(listing)[:5]}")
        if not isinstance(info.get("categories", []) or [], list) or \
           not isinstance(info.get("tags", []) or [], list) or \
           not isinstance(info.get("download_count", 0) or 0, (int, float)):
            die(f"{kind} listing entry {aid} has unexpected field types; entry keys={list(info.keys())}")


def contact_sheet(entries, out_path):
    cols, cell, text_h = 8, 256, 24
    if not entries:
        print(f"no entries for {os.path.basename(out_path)}; skipped", flush=True)
        return
    rows = math.ceil(len(entries) / cols)
    img = Image.new("RGB", (cols * cell, rows * cell), (40, 40, 40))
    draw = ImageDraw.Draw(img)
    font = ImageFont.load_default()
    for i, e in enumerate(entries):
        x = (i % cols) * cell
        y = (i // cols) * cell
        thumb_path = os.path.join(OUT, "thumbs", e["id"] + ".png")
        if os.path.exists(thumb_path):
            th = Image.open(thumb_path).convert("RGB")
            th.thumbnail((cell, cell - text_h))
            img.paste(th, (x + (cell - th.width) // 2, y + (cell - text_h - th.height) // 2))
        tw = draw.textlength(e["id"], font=font)
        draw.text((x + (cell - tw) // 2, y + cell - text_h + 6), e["id"], fill=(255, 255, 255), font=font)
    img.save(out_path, "JPEG", quality=90)
    print(f"wrote {out_path} ({len(entries)} cells)", flush=True)


def main():
    os.makedirs(TMP, exist_ok=True)
    os.makedirs(os.path.join(OUT, "thumbs"), exist_ok=True)

    tex_listing = get_json(ASSETS_API.format("textures"))
    validate_listing("texture", tex_listing)
    mod_listing = get_json(ASSETS_API.format("models"))
    validate_listing("model", mod_listing)
    print(f"listings: {len(tex_listing)} textures, {len(mod_listing)} models", flush=True)

    kinds = [
        ("texture", select(tex_listing, TEX_RULES)),
        ("model", select(mod_listing, MODEL_RULES)),
    ]
    for kind, selected in kinds:
        for label, n, found, chosen in selected:
            print(f"rule {kind}/{label}: found {found}, requested {n}, taking {len(chosen)}", flush=True)

    state = {"total": 0, "stop": False}
    manifest = []
    failed = []
    stats = {}

    for kind, selected in kinds:
        kind_dir = kind + "s"
        for label, n, found, chosen in selected:
            stats[(kind, label)] = {"count": 0, "bytes": 0, "requested": n, "found": found}
            if state["stop"]:
                break
            for aid, info in chosen:
                if state["stop"]:
                    break
                try:
                    files = get_json(FILES_API.format(aid))
                    if not isinstance(files, dict):
                        raise ValueError("files response is not an object")
                    if kind == "texture":
                        items, notes = plan_texture(aid, label, files)
                    else:
                        items, notes = plan_model(aid, label, files)
                except Exception as e:
                    print(f"  FAILED planning {aid}: {e}", flush=True)
                    failed.append(aid)
                    continue

                planned = sum(int(it["size"]) for it in items)
                if state["total"] + planned > BUDGET_BYTES:
                    print(f"STOP: adding {aid} ({planned / MB:.1f} MB) would pass the 9 GB budget "
                          f"at {state['total'] / GB:.3f} GB. No further assets are added.", flush=True)
                    state["stop"] = True
                    break

                asset_dir = os.path.join(OUT, kind_dir, label, aid)
                try:
                    for it in items:
                        ensure_verified(it["url"], it["md5"], os.path.join(OUT, it["rel"]))
                    ensure_thumb(aid)
                except Exception as e:
                    print(f"  FAILED {aid}: {e}", flush=True)
                    failed.append(aid)
                    shutil.rmtree(asset_dir, ignore_errors=True)
                    continue

                size = sum(os.path.getsize(os.path.join(OUT, it["rel"])) for it in items)
                state["total"] += size
                entry = {
                    "id": aid,
                    "name": info.get("name"),
                    "kind": kind,
                    "label": label,
                    "categories": info.get("categories") or [],
                    "tags": info.get("tags") or [],
                    "download_count": info.get("download_count"),
                    "license": LICENSE,
                    "page": f"https://polyhaven.com/a/{aid}",
                    "files": [it["rel"] for it in items],
                    "bytes": size,
                }
                manifest.append(entry)
                st = stats[(kind, label)]
                st["count"] += 1
                st["bytes"] += size
                note = f"  [{'; '.join(notes)}]" if notes else ""
                print(f"[{kind}/{label}] {aid}  {size / MB:.1f} MB  total {state['total'] / GB:.3f} GB{note}",
                      flush=True)

    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    contact_sheet([m for m in manifest if m["kind"] == "texture"],
                  os.path.join(OUT, "contact_textures.jpg"))
    contact_sheet([m for m in manifest if m["kind"] == "model"],
                  os.path.join(OUT, "contact_models.jpg"))

    print("\nSUMMARY", flush=True)
    print(f"{'kind':8} {'label':16} {'count':>5} {'requested':>9} {'found':>5} {'MB':>10}", flush=True)
    for kind, selected in kinds:
        for label, n, found, _ in selected:
            st = stats[(kind, label)]
            print(f"{kind:8} {label:16} {st['count']:>5} {st['requested']:>9} {st['found']:>5} "
                  f"{st['bytes'] / MB:>10.1f}", flush=True)
    grand = state["total"]
    print(f"\nGRAND TOTAL: {len(manifest)} assets, {grand / GB:.3f} GB "
          f"(budget stop: {'yes' if state['stop'] else 'no'})", flush=True)
    print(f"failed: {failed}", flush=True)


if __name__ == "__main__":
    main()
