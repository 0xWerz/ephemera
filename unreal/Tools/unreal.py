#!/usr/bin/env python3
"""Build/import/open Ephemera, with writes confined to this external workspace."""
import argparse
import json
import configparser
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parent


def environment():
    env = dict(os.environ)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    if sys.platform != "darwin":
        # Everything below keeps the Mac's internal disk clean; elsewhere the tools use their own places.
        return env
    mappings = {
        "TMPDIR": "Temp", "TEMP": "Temp", "TMP": "Temp",
        "XDG_CACHE_HOME": "Cache", "XDG_CONFIG_HOME": "Config",
        "XDG_DATA_HOME": "UserData", "DOTNET_CLI_HOME": "Dotnet",
        "NUGET_PACKAGES": "Nuget", "CLANG_MODULE_CACHE_PATH": "Cache/Clang",
        "DOTNET_BUNDLE_EXTRACT_BASE_DIR": "Dotnet/Bundles",
        "MSBUILDUSEREXTENSIONSPATH": "MSBuild",
        "CFFIXED_USER_HOME": "Foundation",
        "UBA_ROOT": "BuildAccelerator",
        "PIP_CACHE_DIR": "Cache/pip",
        "uebp_LogFolder": "UATLogs", "uebp_FinalLogFolder": "UATLogs",
        "UE-LocalDataCachePath": "DerivedDataCache",
        "UE-SharedDataCachePath": "None",
    }
    for key, folder in mappings.items():
        path = PROJECT / "Saved/Local" / folder
        path.mkdir(parents=True, exist_ok=True)
        env[key] = str(path) if folder != "None" else "None"
    # macOS .NET uses Cocoa's application support paths rather than XDG paths.
    # Keep those per-process paths local without changing the user's HOME.
    for folder in ("Library/Application Support", "Library/Caches", "Library/Preferences", "Documents"):
        (Path(env["CFFIXED_USER_HOME"]) / folder).mkdir(parents=True, exist_ok=True)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["PYTHONNOUSERSITE"] = "1"
    env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    env["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1"
    # Xcode is already validated by our build command; avoid editor-started UBT.
    env["UE_SKIP_UBT_SDK_SETUP"] = "1"
    # xcrun otherwise uses Darwin's internal-drive temp folder even with TMPDIR.
    env["xcrun_db"] = str(PROJECT / "Saved/Local/Cache/xcrun_db")
    return env


def confined(command):
    if sys.platform != "darwin":
        return command
    binary = shutil.which("sandbox-exec")
    if not binary:
        raise SystemExit("sandbox-exec is required for the external-disk write restriction.")
    profile = PROJECT / "Saved/external-only.sb"
    profile.parent.mkdir(parents=True, exist_ok=True)
    profile.write_text(
        '(version 1)\n(allow default)\n(deny file-write*)\n'
        f'(allow file-write* (subpath {json.dumps(str(WORKSPACE))}))\n'
        '(allow file-write* (literal "/dev/null"))\n'
    )
    return [binary, "-f", str(profile)] + command


def hidden(command):
    """macOS only: run so the process can never show a window on screen or take focus.

    Unreal has no -RenderOffScreen on macOS, so the focus-safe library built for the Godot
    runner is injected instead. sandbox-exec is a protected system binary and drops DYLD_*
    variables, so a small non-system Python trampoline sets them after the sandbox step.
    """
    lib = WORKSPACE / "tools/hidden_test/libnoactivate_ue.dylib"
    python = shutil.which("python3", path="/opt/homebrew/bin:/usr/local/bin")
    if sys.platform != "darwin" or not lib.exists() or not python:
        raise SystemExit("Hidden runs need macOS, tools/hidden_test/libnoactivate_ue.dylib and a Homebrew python3.")
    trampoline = ("import os,sys;os.environ['DYLD_INSERT_LIBRARIES']=sys.argv[1];"
                  "os.execv(sys.argv[2],sys.argv[2:])")
    return [python, "-B", "-c", trampoline, str(lib)] + command


def run(command, env, hide=False):
    command = [str(p) for p in command]
    if hide and sys.platform == "win32":
        # Windows can render with no window at all, which is also what lets a render be started
        # from a remote shell that has no desktop to show one on.
        # (-ForceRes: without a desktop the window is otherwise cut down to whatever small screen
        # the session pretends to have, and the picture with it.)
        command += ["-RenderOffScreen", "-ForceRes"]
    elif hide:
        command = hidden(command)
    print("Running:", " ".join(command), flush=True)
    return subprocess.run(confined(command), env=env, cwd=PROJECT, check=True)


def fresh_report(path, started):
    if not path.exists() or path.stat().st_mtime < started:
        raise SystemExit(f"Unreal did not produce a fresh result: {path}")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def report_broken_materials():
    """A material that fails to compile is silently drawn with the grey default: say so loudly."""
    log = PROJECT / "Saved/Shot.log"
    if not log.exists():
        return
    lines = log.read_text(errors="replace").splitlines()
    for index, line in enumerate(lines):
        if "Failed to compile Material" in line:
            print("BROKEN MATERIAL:", line.split("Warning: ", 1)[-1], flush=True)
            for detail in lines[index + 1:index + 4]:
                if "error" in detail:
                    print("   ", detail.strip(), flush=True)


def photo_cvars(given):
    """Console variables for a photograph: full quality whatever the platform plays at, then the caller's own."""
    # The ground's relief as geometry (off by default on the Mac, see Config/Mac/MacEngine.ini), and
    # clouds stepped finely enough to be smooth in a still.
    quality = "r.Nanite.Tessellation=1,r.VolumetricCloud.SampleMinCount=64"
    return f"{quality},{given}" if given else quality


# Named skies for --weather. The game owns what each one is (AEphemeraSkyRig::WeatherPresets).
WEATHER = ("clear", "fronts", "overcast", "broken")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "import", "play", "open", "test", "smoke", "package", "installer", "check", "slice", "shot", "shots"))
    parser.add_argument("--preview", action="store_true", help="slice: build terrain at a quarter of the grid resolution (fast, for trying shapes).")
    parser.add_argument("--list", help="shots: text file, one shot per line: name|time_seconds|photo spec.")
    parser.add_argument("--outdir", help="shots: folder for the PNGs (default Saved/Shots).")
    parser.add_argument("--show", action="store_true", help="shots: open each image in the default viewer as soon as it is written.")
    parser.add_argument("--settle", type=float, default=7.0, help="shots: seconds to let lighting settle after each camera move.")
    parser.add_argument("--legacy", action="store_true", help="play: the first port's JSON-built map instead of the new slice.")
    parser.add_argument("--fullscreen", action="store_true", help="play: borderless fullscreen instead of a 1600x900 window.")
    parser.add_argument("--out", help="shot: PNG to write (default Saved/Shots/shot.png).")
    parser.add_argument("--time", type=float, help="shot: solar-system time in seconds.")
    parser.add_argument("--look", help="shot: view as pitch,yaw in degrees.")
    parser.add_argument("--photo", help="shot: free camera, body|x,y,z|height_m|target|fov|yaw,pitch|roll (see EphemeraCapture.h).")
    parser.add_argument("--size", default="1600x900", help="shot: output size as WxH.")
    parser.add_argument("--start", help="shot: spawn direction on the start body as x,y,z (planet-local).")
    parser.add_argument("--no-df", action="store_true", help="slice: build terrain without distance fields (much faster to "
                        "iterate on a world's shape; lighting from the land is cruder). Final content needs them.")
    parser.add_argument("--script", help="slice: run this editor Python script in place of the content build. "
                        "It must log '[slice] done' when it has finished.")
    parser.add_argument("--sky", help="shot/shots: sky look overrides for this render, as key=value,key=value "
                        "(see AEphemeraSkyRig::Tuning).")
    parser.add_argument("--cvars", help="shot/shots/play: console variables to override, as name=value,name=value.")
    parser.add_argument("--weather", choices=WEATHER, help="play/shot/shots: a named sky. Photographs default to clear; play "
                        "defaults to what the settings menu last chose (fronts at first). --sky settings are added on top.")
    parser.add_argument("--profile", action="store_true", help="shot: dump a GPU profile to Saved/Shot.log.")
    parser.add_argument("--menu", action="store_true", help="shot (player view, no --photo): open the settings menu and include "
                        "the overlay in the picture.")
    parser.add_argument("--delay", type=float, default=12.0, help="shot: seconds to settle after shaders compile.")
    parser.add_argument("--ui", action="store_true", help="shot (player view): include what is drawn over the world (loading screen, readouts).")
    parser.add_argument("--lamp", action="store_true", help="shot (player view): start with the hand lamp lit.")
    parser.add_argument("--flyup", action="store_true", help="shot with --fly: keep the rise key held, which from that pose flies along the ground.")
    parser.add_argument("--fly", help="shot (player view): start in free flight, as body,metres over it (e.g. aubade,20000).")
    parser.add_argument("--sound", action="store_true", help="shot: run the sound for real (every sound plays and is logged) with the output held at "
                        "silence, instead of with no sound device at all.")
    parser.add_argument("--record", help="shot with --drive: save every frame of the scripted run in this folder (f_00000.png ...) and quit when it ends.")
    parser.add_argument("--record-from", type=float, default=0.0, help="shot --record: seconds into the script to start saving frames.")
    parser.add_argument("--scale", type=float, help="shot: render resolution in percent for this run (the game's -EphemeraScale).")
    parser.add_argument("--fps", type=float, help="shot: step the game exactly 1/fps seconds a frame however long one takes to draw (for --record).")
    parser.add_argument("--drive", help="shot (player view): hold keys down by a script once loading is over, as keys:seconds,... with keys "
                        "any of w a s d shift space ctrl fly joined by + (e.g. w:2,w+shift:2,space:0.1,:1). The log says what the body did.")
    parser.add_argument("--engine", help="UE installation root containing Engine/. Must be inside this workspace.")
    parser.add_argument("--nanite", action="store_true", help="Enable Nanite for eligible opaque meshes (M2+ Mac or supported PC).")
    args = parser.parse_args()
    engine = Path(args.engine or os.environ.get("UNREAL_ENGINE", str(WORKSPACE / ".unreal/UE_5.8"))).resolve()
    if sys.platform == "darwin":
        # The Mac's rule: nothing is written to the internal disk, and the engine writes beside itself.
        try:
            engine.relative_to(WORKSPACE)
        except ValueError:
            raise SystemExit(f"Install Unreal inside {WORKSPACE}/.unreal; the engine also writes intermediate files.")
    env = environment()
    if args.action in ("play", "smoke", "open", "shot", "shots"):
        # Apple's downloaded Metal component is registered in existing Cocoa
        # preferences. Read that registration; UserDir/DDC/TMPDIR stay external
        # and the OS write guard still applies to this process and its children.
        env.pop("CFFIXED_USER_HOME", None)
    env["EPHEMERA_NANITE"] = "1" if args.nanite else "0"
    platform = "Mac" if sys.platform == "darwin" else "Win64" if sys.platform == "win32" else "Linux"
    if platform == "Mac":
        editor = engine / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
    elif platform == "Win64":
        editor = engine / "Engine/Binaries/Win64/UnrealEditor.exe"
    else:
        editor = engine / "Engine/Binaries/Linux/UnrealEditor"
    if not editor.exists():
        raise SystemExit(
            f"Unreal Editor is not installed at {editor}.\n"
            f"Install UE 5.8 in {WORKSPACE}/.unreal/UE_5.8, then rerun this command.\n"
            "No engine has been downloaded or installed automatically."
        )
    if args.action == "check":
        print("Unreal Editor found:", editor)
        return
    project_file = PROJECT / "Ephemera.uproject"
    runtime = PROJECT / "Saved/Local/UserData"
    common = [project_file, f"-UserDir={runtime}/", f"-UserSettingsDir={runtime}/",
              "-ddc=EphemeraLocal", "-NoZenAutoLaunch", "-NoSplash", "-NoTraceServer", "-traceautostart=0"]
    if args.action == "build":
        script = engine / f"Engine/Build/BatchFiles/{platform}/Build.sh" if platform != "Win64" else engine / "Engine/Build/BatchFiles/Build.bat"
        from external_build import prepare
        build = prepare(engine, env, confined, WORKSPACE) or [script]
        run([*build, "EphemeraEditor", platform, "Development", f"-Project={project_file}",
             "-WaitMutex", "-NoHotReloadFromIDE", f"-Log={PROJECT}/Saved/Build.log"], env)
    elif args.action == "import":
        started = time.time()
        run([editor, *common, "-Unattended", "-NullRHI", "-NoSound",
             f"-ExecutePythonScript={PROJECT}/Tools/import_assets.py",
             f"-AbsLog={PROJECT}/Saved/Import.log"], env)
        manifest = PROJECT / "Content/Data/assets.json"
        imported = fresh_report(manifest, started)
        world = json.loads((PROJECT / "Content/Data/world.json").read_text())
        expected = {row["id"] for row in world["meshes"]}
        if set(imported["meshes"]) != expected or not (PROJECT / "Content/Maps/Aubade.umap").exists():
            raise SystemExit(f"Import is incomplete. See {PROJECT}/Saved/Import.log")
    elif args.action == "shots":
        # Many screenshots from ONE launch: starting the game is most of a single shot's cost.
        import threading
        if not args.list:
            raise SystemExit("shots needs --list <file> (one shot per line: name|time_seconds|photo spec)")
        shots = [line.strip() for line in Path(args.list).read_text().splitlines() if line.strip() and not line.startswith("#")]
        outdir = Path(args.outdir).resolve() if args.outdir else PROJECT / "Saved/Shots"
        outdir.mkdir(parents=True, exist_ok=True)
        names = [line.split("|", 1)[0] for line in shots]
        for name in names:
            (outdir / f"{name}.png").unlink(missing_ok=True)
        done = threading.Event()

        def show_as_written():
            # A PNG is complete once its size has stopped changing between two looks.
            shown, sizes = set(), {}
            while True:
                for name in names:
                    image = outdir / f"{name}.png"
                    if name in shown or not image.exists():
                        continue
                    size = image.stat().st_size
                    if size > 0 and sizes.get(name) == size:
                        shown.add(name)
                        print("Image ready:", image, flush=True)
                        if args.show and sys.platform == "darwin":
                            subprocess.run(["open", str(image)], check=False)
                    sizes[name] = size
                if done.is_set() and len(shown) == sum(1 for n in names if (outdir / f"{n}.png").exists()):
                    return
                time.sleep(1.0)

        watcher = threading.Thread(target=show_as_written, daemon=True)
        watcher.start()
        extra = [f"-EphemeraPhotoList={Path(args.list).resolve()}", f"-EphemeraShotDir={outdir}",
                 f"-EphemeraShotDelay={args.delay}", f"-EphemeraShotSettle={args.settle}"]
        extra.append(f"-DPCVars={photo_cvars(args.cvars)}")
        extra.append(f"-EphemeraWeather={args.weather or 'clear'}")
        if args.sky:
            extra.append(f"-EphemeraSky={args.sky}")
        try:
            run([editor, project_file, "/Game/Ephemera/Maps/System", *common[1:], "-game", "-Windowed",
                 f"-ResX={args.size.split('x')[0]}", f"-ResY={args.size.split('x')[1]}", "-NoSound", "-Unattended", *extra,
                 f"-AbsLog={PROJECT}/Saved/Shot.log"], env, hide=sys.platform in ("darwin", "win32"))
        finally:
            done.set()
            watcher.join(timeout=20)
        report_broken_materials()
        missing = [name for name in names if not (outdir / f"{name}.png").exists()]
        if missing:
            raise SystemExit(f"Missing screenshots: {', '.join(missing)}. See {PROJECT}/Saved/Shot.log")
        print(f"{len(names)} screenshots in {outdir}")
    elif args.action == "slice":
        # Builds the vertical slice's content (surfaces, materials, bodies, terrain, input, map).
        log = PROJECT / "Saved/Slice.log"
        env["EPHEMERA_PREVIEW"] = "1" if args.preview else "0"
        env["EPHEMERA_NO_DF"] = "1" if args.no_df else "0"
        log.unlink(missing_ok=True)
        command = confined([str(p) for p in [editor, *common, "-Unattended", "-NullRHI", "-NoSound",
                                             f"-ExecutePythonScript={Path(args.script).resolve() if args.script else PROJECT / 'Tools/build_slice.py'}",
                                             f"-AbsLog={log}", *(["-EphemeraNoDistanceFields"] if args.no_df else [])]])
        print("Running:", " ".join(command), flush=True)
        process = subprocess.Popen(command, env=env, cwd=PROJECT)
        # The script's last act is to log "[slice] done". The editor's own shutdown after that can
        # hang for many minutes on macOS, with everything already saved: give it a moment, then end it.
        finished_at = None
        while process.poll() is None:
            time.sleep(2.0)
            if finished_at is None and log.exists():
                written = log.read_text(errors="replace")
                if "[slice] done" in written or "Python script executed with errors" in written:
                    finished_at = time.time()
            if finished_at and time.time() - finished_at > 25:
                process.kill()
                break
        process.wait()
        text = log.read_text(errors="replace")
        if "[slice] done" not in text:
            problems = [line for line in text.splitlines() if "Error:" in line][-12:]
            raise SystemExit("Slice build did not finish. Last errors:\n" + "\n".join(problems) + f"\nSee {log}")
        print("Slice content built.")
    elif args.action == "shot":
        # One screenshot of the slice map, rendered with the window invisible (see hidden()).
        out = Path(args.out).resolve() if args.out else PROJECT / "Saved/Shots/shot.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.unlink(missing_ok=True)
        extra = [f"-EphemeraShot={out}", f"-EphemeraShotDelay={args.delay}"]
        if args.time is not None:
            extra.append(f"-EphemeraTime={args.time}")
        if args.look:
            extra.append(f"-EphemeraLook={args.look}")
        if args.start:
            extra.append(f"-EphemeraStart={args.start}")
        if args.photo:
            extra.append(f"-EphemeraPhoto={args.photo}")
        extra.append(f"-DPCVars={photo_cvars(args.cvars)}")
        extra.append(f"-EphemeraWeather={args.weather or 'clear'}")
        if args.sky:
            extra.append(f"-EphemeraSky={args.sky}")
        if args.profile:
            extra.append("-EphemeraProfile")
        if args.menu:
            extra.append("-EphemeraMenu")
        if args.ui:
            extra.append("-EphemeraShotUI")
        if args.lamp:
            extra.append("-EphemeraLamp")
        if args.fly:
            extra.append(f"-EphemeraFly={args.fly}")
        if args.flyup:
            extra.append("-EphemeraFlyUp")
        if args.drive:
            extra.append(f"-EphemeraDrive={args.drive}")
        if args.record:
            extra += [f"-EphemeraRecord={Path(args.record).resolve()}", f"-EphemeraRecordFrom={args.record_from}"]
        if args.fps:
            extra += ["-benchmark", f"-fps={args.fps}"]
        if args.scale:
            extra.append(f"-EphemeraScale={args.scale}")
        run([editor, project_file, "/Game/Ephemera/Maps/System", *common[1:], "-game", "-Windowed",
             f"-ResX={args.size.split('x')[0]}", f"-ResY={args.size.split('x')[1]}", "-EphemeraMute" if args.sound else "-NoSound", "-Unattended", *extra,
             f"-AbsLog={PROJECT}/Saved/Shot.log"], env, hide=sys.platform in ("darwin", "win32"))
        report_broken_materials()
        if args.record:
            frames = sorted(Path(args.record).resolve().glob("f_*.png"))
            print(f"Recorded {len(frames)} frames in {Path(args.record).resolve()}")
            return
        if not out.exists():
            raise SystemExit(f"No screenshot was written. See {PROJECT}/Saved/Shot.log")
        print("Screenshot:", out)
    elif args.action == "test":
        started = time.time()
        run([editor, *common, "-Unattended", "-NullRHI", "-NoSound",
             "-ExecCmds=Automation RunTests Ephemera", "-TestExit=Automation Test Queue Empty",
             f"-ReportExportPath={PROJECT}/Saved/Automation",
             f"-AbsLog={PROJECT}/Saved/Automation.log"], env)
        report = fresh_report(PROJECT / "Saved/Automation/index.json", started)
        paths = {test["fullTestPath"] for test in report.get("tests", [])}
        expected = {"Ephemera.Migration.Coordinates", "Ephemera.Migration.ImportedContent"}
        if (not expected.issubset(paths) or report.get("failed", 1) or report.get("notRun", 1)
                or report.get("inProcess", 1) or report.get("succeeded", 0) + report.get("succeededWithWarnings", 0) < 2):
            raise SystemExit(f"Unreal automation did not pass. See {PROJECT}/Saved/Automation/index.json")
        print("Unreal migration automation passed.")
    elif args.action == "play" and not args.legacy:
        # The photoreal slice: the authored solar-system map with the new player.
        if not (PROJECT / "Content/Ephemera/Maps/System.umap").exists():
            raise SystemExit("The slice has not been built yet. Run: python3 -B unreal/Tools/unreal.py slice")
        size = ["-Fullscreen"] if args.fullscreen else ["-Windowed", "-ResX=1600", "-ResY=900"]
        extra = ([f"-EphemeraWeather={args.weather}"] if args.weather else []) + ([f"-EphemeraSky={args.sky}"] if args.sky else []) \
            + ([f"-DPCVars={args.cvars}"] if args.cvars else [])
        run([editor, project_file, "/Game/Ephemera/Maps/System", *common[1:], "-game", *size, *extra,
             f"-AbsLog={PROJECT}/Saved/Play.log"], env)
    elif args.action in ("play", "smoke"):
        manifest = PROJECT / "Content/Data/assets.json"
        level = PROJECT / "Content/Maps/Aubade.umap"
        if not manifest.exists() or not level.exists():
            raise SystemExit("The starting world has not been imported yet. Run ./play_unreal.sh import first.")
        extra = []
        log = "Play"
        if args.action == "smoke":
            started = time.time()
            for name in ("runtime.json", "spawn.png", "SaveGames/ephemera.json"):
                (PROJECT / "Saved/Smoke" / name).unlink(missing_ok=True)
            extra = ["-EphemeraSmoke", "-Unattended"]
            log = "Smoke"
        # Smoke runs are tests: on macOS they must never appear on screen or take focus.
        hide = args.action == "smoke" and sys.platform == "darwin"
        placement = ["-WinX=30000", "-WinY=30000", "-NoSound"] if hide else []
        run([editor, project_file, "/Game/Maps/Aubade", *common[1:], "-game", "-Windowed",
             "-ResX=1440", "-ResY=900", *placement, *extra, f"-AbsLog={PROJECT}/Saved/{log}.log"], env, hide=hide)
        if args.action == "smoke":
            report = fresh_report(PROJECT / "Saved/Smoke/runtime.json", started)
            if not report.get("passed"):
                raise SystemExit(f"Runtime smoke failed. See {PROJECT}/Saved/Smoke/runtime.json")
            log_text = (PROJECT / "Saved/Smoke.log").read_text(errors="replace")
            if any(problem in log_text for problem in ("Failed to compile Material", "missing usage flag", "Fatal error:")):
                raise SystemExit(f"Runtime rendering has errors. See {PROJECT}/Saved/Smoke.log")
            print("Runtime smoke passed; rendered screenshot: ", PROJECT / "Saved/Smoke/spawn.png")
    elif args.action == "package":
        script = engine / "Engine/Build/BatchFiles/RunUAT.sh" if platform != "Win64" else engine / "Engine/Build/BatchFiles/RunUAT.bat"
        # A game that starts from its own exe: compiled without the editor, content cooked for the
        # platform (every shader built beforehand), packed, and put in Builds/<platform>/.
        run([script, "BuildCookRun", f"-project={project_file}", f"-platform={platform}",
             "-clientconfig=Development", "-build", "-cook", "-stage", "-pak", "-iostore", "-compressed", "-prereqs",
             "-archive", f"-archivedirectory={PROJECT}/Builds", "-unattended", "-nop4", "-utf8output", "-ddc=EphemeraLocal"], env)
    elif args.action == "installer":
        # Windows only: packs Builds/Windows (made by "package") into a setup program with Inno Setup.
        places = [Path(os.environ.get(name, "")) / "Inno Setup 6/ISCC.exe" for name in ("ProgramFiles(x86)", "ProgramFiles")]
        places.append(Path(os.environ.get("LOCALAPPDATA", "")) / "Programs/Inno Setup 6/ISCC.exe")
        compiler = next((place for place in places if place.is_file()), None)
        if not compiler:
            raise SystemExit("Inno Setup 6 is not installed (winget install JRSoftware.InnoSetup).")
        if not (PROJECT / "Builds/Windows/Ephemera.exe").is_file():
            raise SystemExit("No packaged game in Builds/Windows: run the package action first.")
        settings = configparser.ConfigParser(strict=False, interpolation=None)
        settings.optionxform = str
        settings.read(PROJECT / "Config/DefaultGame.ini")
        version = settings.get("/Script/EngineSettings.GeneralProjectSettings", "ProjectVersion", fallback="0.0.0")
        # How big the setup program will come out: the game's data goes in as it is, the programs
        # squeeze to about a quarter (measured: 2.4 GB of build made a 1.75 GB installer).
        build = PROJECT / "Builds/Windows"
        files = [f for f in build.rglob("*") if f.is_file() and "Saved" not in f.relative_to(build).parts]
        packed = sum(f.stat().st_size * (1.0 if "Paks" in f.parts else 0.3) for f in files)
        span = ["/DSpan"] if packed > 1.9e9 else []
        print(f"about {packed / 1e6:.0f} MB to pack{', in several files' if span else ''}")
        for old in (PROJECT / "Builds/Installer").glob("Ephemera-Setup-*"):
            old.unlink()
        run([compiler, f"/DAppVersion={version}", *span, "/Qp", PROJECT / "Tools/installer/Ephemera.iss"], env)
        print("Installer:", PROJECT / f"Builds/Installer/Ephemera-Setup-{version}.exe")
    else:
        run([editor, *common, f"-AbsLog={PROJECT}/Saved/Editor.log"], env)


if __name__ == "__main__":
    main()
