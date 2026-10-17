#!/usr/bin/env python3
"""Install Epic's Mac UE 5.8 build entirely within this external workspace."""
import argparse
from datetime import datetime, timezone
import getpass
import json
import logging
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from unreal import WORKSPACE, confined, environment

ROOT = WORKSPACE / ".unreal"
VENV = ROOT / "Installer/venv"
PYTHON = VENV / "bin/python"
CONFIG = ROOT / "Config/legendary"
ENGINE = ROOT / "UE_5.8"
EDITOR = ENGINE / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
VERIFIED = ROOT / "Installer/verified-release.json"
LEGENDARY_VERSION = "0.21.1"
LOGIN_URL = "https://legendary.gl/epiclogin"
INSTALL_TAGS = ("", "engine_source", "templates")


def local_environment():
    # Resolve directories before use so a symlink cannot redirect these writes.
    paths = [ROOT, VENV, CONFIG, ENGINE, WORKSPACE / "unreal/Saved"]
    for path in paths:
        if not path.resolve().is_relative_to(WORKSPACE):
            raise SystemExit("Installation/cache paths must remain inside the workspace: " + str(path))
    env = environment()
    mappings = {
        "TMPDIR": "Temp", "TMP": "Temp", "TEMP": "Temp",
        "UV_CACHE_DIR": "Cache/uv", "UV_PYTHON_INSTALL_DIR": "Python",
        "PIP_CACHE_DIR": "Cache/pip", "XDG_CACHE_HOME": "Cache",
        "XDG_CONFIG_HOME": "Config", "XDG_DATA_HOME": "Data",
        "LEGENDARY_CONFIG_PATH": "Config/legendary",
        "PYTHONPYCACHEPREFIX": "Cache/pycache",
    }
    for key, suffix in mappings.items():
        path = ROOT / suffix
        if not path.resolve().is_relative_to(WORKSPACE):
            raise SystemExit("Cache path escapes the workspace: " + str(path))
        path.mkdir(parents=True, exist_ok=True)
        env[key] = str(path)
    CONFIG.chmod(0o700)
    env.update(UV_PYTHON_DOWNLOADS="never", UV_NO_CONFIG="1", PYTHONNOUSERSITE="1")
    return env


def setup(env):
    if PYTHON.exists() and (VENV / "bin/legendary").exists():
        return
    uv = shutil.which("uv")
    interpreter = shutil.which("python3.13") or shutil.which("python3.12") or shutil.which("python3.11")
    if not uv or not interpreter:
        raise SystemExit("An existing uv and Python 3.11+ are required to prepare the local installer.")
    subprocess.run(confined([uv, "venv", "--python", interpreter, str(VENV)]), env=env, check=True)
    subprocess.run(confined([uv, "pip", "install", "--python", str(PYTHON),
                            "legendary-gl==" + LEGENDARY_VERSION]), env=env, check=True)


def authenticated(core, interactive):
    try:
        if core.login():
            return True
    except Exception as error:
        # Avoid dumping authentication responses or saved credentials.
        logging.getLogger(__name__).debug("Saved sign-in unavailable: %s", type(error).__name__)
    if not interactive:
        print("Epic sign-in is required. Run ./install_unreal.command in your terminal.")
        return False
    print("\nSign in to Epic in your browser:")
    print(LOGIN_URL)
    print("Paste the authorizationCode (or its JSON response) here. Input is hidden.")
    print("Your password stays on Epic's website; this installer does not open a browser.")
    code = getpass.getpass("Epic authorization code: ").strip()
    if not code:
        return False
    if code.startswith("{"):
        try:
            code = json.loads(code)["authorizationCode"]
        except (ValueError, KeyError):
            print("The response did not contain an authorizationCode.")
            return False
    else:
        code = code.strip('"')
    # Use the API directly so the one-time code never appears in process arguments.
    try:
        return core.auth_code(code)
    finally:
        code = ""
        for path in CONFIG.glob("user.json*"):
            if path.is_file():
                path.chmod(0o600)


def worker(args):
    if args.status or args.setup:
        print("Installer: Legendary " + LEGENDARY_VERSION + " (workspace-local)")
        print("Engine destination:", ENGINE)
        status = "installed and verified" if EDITOR.exists() and VERIFIED.exists() else (
            "files present; download/verification pending" if EDITOR.exists() else "not installed")
        print("Unreal Editor:", status)
        try:
            session = json.loads((CONFIG / "user.json").read_text())
            saved = isinstance(session, dict) and bool(session.get("refresh_token"))
        except (OSError, ValueError):
            saved = False
        print("Epic session:", "saved locally" if saved else "sign-in required")
        print("Installer writes are restricted to:", WORKSPACE)
        return 0

    logging.basicConfig(level=logging.INFO, format="%(message)s")
    from legendary.core import LegendaryCore
    core = LegendaryCore()
    core.lgd.config.set("Legendary", "install_dir", str(ROOT))
    core.lgd.config.set("Legendary", "mac_install_dir", str(ROOT))
    core.lgd.config.set("Legendary", "install_platform_fallback", "false")
    core.lgd.config.set("Legendary", "locale", "en-US")
    core.lgd.config.set("Legendary", "disable_update_check", "true")
    core.lgd.config.set("Legendary", "disable_update_notice", "true")
    try:
        if not authenticated(core, sys.stdin.isatty() and not args.non_interactive):
            return 2
        if args.sign_in:
            print("Epic sign-in saved on the external drive.")
            return 0
        # Legendary's install command refreshes the games list with UE hidden.
        # Populate Unreal metadata explicitly before asking it to install an engine.
        if core.get_game("UE_5.8", platform="Mac") is None:
            core.get_game_and_dlc_list(platform="Mac", skip_ue=False)
        game = core.get_game("UE_5.8", platform="Mac")
        if game is None or "Mac" not in game.asset_infos:
            print("Your Epic account does not currently expose Unreal 5.8 for Mac.")
            print("Complete Unreal account/license setup at https://www.unrealengine.com/download")
            return 1
        # Check Epic's live Mac catalog so this command cannot silently install an
        # older engine branch after a newer stable release becomes available.
        releases = []
        for asset in core.get_assets(update_assets=True, platform="Mac"):
            if not re.fullmatch(r"UE_\d+\.\d+", asset.app_name):
                continue
            version = re.match(r"(\d+)\.(\d+)\.(\d+)-\d+\+\+\+UE\d+\+Release-", asset.build_version)
            if version and not re.search(r"preview|earlyaccess|beta", asset.build_version, re.IGNORECASE):
                releases.append((tuple(map(int, version.groups())), asset.app_name, asset.build_version))
        if not releases:
            print("Unable to confirm a stable Unreal release from Epic's catalog.")
            return 1
        latest, app_name, build_version = max(releases)
        if app_name != "UE_5.8":
            print("A newer stable engine is available:", build_version)
            print("Update the project's engine target before downloading it.")
            return 1
        (ROOT / "Installer/selected-release.json").write_text(json.dumps({
            "version": ".".join(map(str, latest)), "app_name": app_name,
            "epic_build": build_version, "platform": "Mac", "install_tags": INSTALL_TAGS,
        }, indent=2) + "\n")
        print("Latest stable Mac release:", ".".join(map(str, latest)), flush=True)
    finally:
        core.exit()

    print("\nInstalling Unreal Engine 5.8 for Mac in", ENGINE, flush=True)
    command = [str(PYTHON), "-B", "-m", "legendary.cli", "-y", "install", "UE_5.8",
               "--platform", "Mac", "--base-path", str(ROOT), "--game-folder", "UE_5.8",
               "--skip-dlcs", "--disable-sdl", "--max-workers", "32"]
    for tag in INSTALL_TAGS:
        command.extend(["--install-tag", tag])
    if args.repair:
        command.extend(["--repair", "--repair-and-update"])
    # -y also skips prerequisite installers. No global SDKs or launcher are installed.
    result = subprocess.run(command, cwd=WORKSPACE)
    if result.returncode:
        return result.returncode
    if not EDITOR.is_file():
        print("Unreal installation did not finish. Rerun this command to resume.")
        print("If Epic reports missing access, sign in at https://www.unrealengine.com/download")
        print("and complete Unreal account/license setup, then retry.")
        return 1
    if not verify():
        print("Verification failed. Run ./install_unreal.command --repair")
        return 1
    release = json.loads((ROOT / "Installer/selected-release.json").read_text())
    release["verified_at"] = datetime.now(timezone.utc).isoformat()
    VERIFIED.write_text(json.dumps(release, indent=2) + "\n")
    print("\nUnreal Engine 5.8 is installed and its file hashes are verified.")
    print("Next: ./play_unreal.sh build")
    return 0


def verify():
    from legendary.core import LegendaryCore
    from legendary.lfs.utils import validate_files
    from legendary.models.game import VerifyResult
    core = LegendaryCore()
    try:
        data, _ = core.get_installed_manifest("UE_5.8")
        if data is None:
            print("Epic's installed file manifest is missing.")
            return False
        manifest = core.load_manifest(data)
        if not manifest.decrypt({}):
            print("Unable to read the installed file manifest.")
            return False
        files = [(file.filename, file.sha_hash.hex()) for file in manifest.file_manifest_list.elements
                 if not file.install_tags or any(tag in INSTALL_TAGS for tag in file.install_tags)]
        failed = 0
        for index, (result, path, _, _) in enumerate(validate_files(str(ENGINE), files), start=1):
            if result != VerifyResult.HASH_MATCH:
                failed += 1
                print("Verification failed:", path)
            if index % 1000 == 0:
                print("Verified", index, "of", len(files), "files", flush=True)
        if failed:
            print("Files requiring repair:", failed)
            print("Run ./install_unreal.command --repair before building.")
        if failed or not files:
            return False
        engine_version = json.loads((ENGINE / "Engine/Build/Build.version").read_text())
        actual = ".".join(str(engine_version[key]) for key in ("MajorVersion", "MinorVersion", "PatchVersion"))
        expected = json.loads((ROOT / "Installer/selected-release.json").read_text())["version"]
        if actual != expected:
            print("Installed engine version differs from the selected stable release:", actual, expected)
            return False
        return True
    finally:
        core.exit()


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--setup", action="store_true", help="Prepare the local downloader without signing in.")
    group.add_argument("--status", action="store_true", help="Show local installation status.")
    group.add_argument("--sign-in", action="store_true", help="Sign in without downloading the engine yet.")
    group.add_argument("--repair", action="store_true", help="Verify and repair an existing installation.")
    parser.add_argument("--non-interactive", action="store_true", help="Exit if Epic sign-in is missing.")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if sys.platform != "darwin":
        raise SystemExit("This installer downloads the Mac build of Unreal Engine.")
    if args.worker:
        return worker(args)
    env = local_environment()
    setup(env)
    command = confined([str(PYTHON), "-B", str(Path(__file__).resolve()), "--worker", *sys.argv[1:]])
    os.execvpe(command[0], command, env)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nStopped. Rerun ./install_unreal.command to resume.")
        sys.exit(130)
