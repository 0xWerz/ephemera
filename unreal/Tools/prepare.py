#!/usr/bin/env python3
"""Snapshot Ephemera into portable glTF and JSON without modifying its project or saves."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "unreal"


def godot_command(explicit=None):
    if explicit:
        return [explicit]
    hidden = ROOT / "tools/hidden_test/godot.sh"
    if hidden.exists() and sys.platform == "darwin":
        return [str(hidden)]
    binary = os.environ.get("GODOT") or shutil.which("godot") or shutil.which("godot4")
    if not binary:
        raise SystemExit("Set GODOT to your Godot 4.6 executable, or pass --godot.")
    return [binary]


def prepare(godot=None):
    destination = PROJECT / "Migration"
    shadow = destination / "SnapshotProject"
    shadow.mkdir(parents=True, exist_ok=True)
    source = ROOT / "ephemera"
    config = (source / "project.godot").read_text()
    config = config.replace('run/main_scene="res://scenes/main.tscn"', 'run/main_scene="res://export.tscn"')
    config = config.replace('[application]', '[application]\nconfig/use_custom_user_dir=true\nconfig/custom_user_dir_name="EphemeraUnrealMigration"')
    (shadow / "project.godot").write_text(config)
    for name in ("scripts", "assets", "shaders", "scenes"):
        link = shadow / name
        if not link.exists():
            link.symlink_to(source / name, target_is_directory=True)
    shutil.copy2(PROJECT / "Tools/export_world.gd", shadow / "export_world.gd")
    (shadow / "export.tscn").write_text(
        '[gd_scene load_steps=2 format=3]\n'
        '[ext_resource type="Script" path="res://export_world.gd" id="1"]\n'
        '[node name="UnrealExporter" type="Node"]\nscript = ExtResource("1")\n'
    )
    command = godot_command(godot)
    # macOS ignores XDG variables for some engine paths. Enforce the disk boundary
    # at the OS level rather than relying on each dependency's cache defaults.
    if sys.platform == "darwin":
        sandbox = shutil.which("sandbox-exec")
        if not sandbox:
            raise SystemExit("sandbox-exec is required to enforce project-only writes on macOS.")
        profile = destination / "external-only.sb"
        profile.write_text(
            '(version 1)\n(allow default)\n(deny file-write*)\n'
            f'(allow file-write* (subpath {json.dumps(str(ROOT))}))\n'
            '(allow file-write* (literal "/dev/null"))\n'
        )
        command = [sandbox, "-f", str(profile)] + command
    environment = dict(os.environ)
    for variable, subdir in (("TMPDIR", "Temp"), ("XDG_CACHE_HOME", "Cache"), ("XDG_CONFIG_HOME", "Config"), ("XDG_DATA_HOME", "UserData")):
        folder = destination / subdir
        folder.mkdir(exist_ok=True)
        environment[variable] = str(folder)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    environment["CLANG_MODULE_CACHE_PATH"] = str(destination / "Cache/Clang")
    subprocess.run(command + ["--headless", "--path", str(shadow), "--editor", "--import"], check=True, timeout=600, env=environment)
    started = time.time()
    subprocess.run(command + ["--headless", "--path", str(shadow), "--quit-after", "2", "--", str(destination)], check=True, timeout=1800, env=environment)
    world_file = destination / "world.json"
    if not world_file.is_file() or world_file.stat().st_mtime < started:
        raise SystemExit("Godot did not produce world.json. Check its error output.")
    world = json.loads(world_file.read_text())
    if world["failures"]:
        raise SystemExit("Export errors: " + repr(world["failures"]))
    for name in ("world.json", "story.json"):
        shutil.copy2(destination / name, PROJECT / "Content/Data" / name)
    print(f"Prepared {len(world['meshes'])} meshes and {len(world['placements'])} placement batches.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot")
    args = parser.parse_args()
    prepare(args.godot)
