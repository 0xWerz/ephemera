"""Prepare a local build-tool adapter for .NET's hardcoded /tmp mutex storage.

Uses Epic's installed C# sources and bundled compiler/dependencies. The original
engine files remain unchanged. Only SingleInstanceMutex is replaced, using flock
files within this workspace and keeping mutual exclusion/cancellation semantics.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def prepare(engine, env, confined, workspace):
    if sys.platform != "darwin":
        return
    tools = Path(__file__).resolve().parent
    engine_dir = engine / "Engine"
    shared = engine_dir / "Source/Programs/Shared"
    core = shared / "EpicGames.Core"
    binaries = engine_dir / "Binaries/DotNET/UnrealBuildTool"
    sdk = engine_dir / "Binaries/ThirdParty/DotNet/10.0/mac-arm64"
    csc = sorted(sdk.glob("sdk/*/Roslyn/bincore/csc.dll"))[-1]
    references = sorted(sdk.glob("packs/Microsoft.NETCore.App.Ref/*/ref/net10.0"))[-1]
    sources = sorted(p for p in core.rglob("*.cs") if "obj" not in p.parts and p.name != "SingleInstanceMutex.cs")
    sources += [shared / "MetaData.cs", tools / "BuildSupport/SingleInstanceMutex.cs"]
    digest = hashlib.sha256()
    for path in [*sources, Path(__file__), engine_dir / "Build/Build.version", binaries / "UnrealBuildTool.dll"]:
        digest.update(path.read_bytes())
    fingerprint = digest.hexdigest()
    # UBT requires its entry point below Engine/Binaries/DotNET to find the engine.
    destination = engine_dir / "Binaries/DotNET/EphemeraBuildSupport" / fingerprint[:16]
    destination.mkdir(parents=True, exist_ok=True)
    marker = destination / "complete.json"
    if not marker.exists():
        command = [str(sdk / "dotnet"), str(csc), "-nologo", "-target:library", "-unsafe",
                   "-nullable:enable", "-langversion:preview", "-optimize+", "-define:TRACE;NET;NET10_0;NETCOREAPP"]
        refs = [*references.glob("*.dll"), *(p for p in binaries.glob("*.dll")
                if p.name not in {"EpicGames.Core.dll", "UnrealBuildTool.dll"} and not (references / p.name).exists())]
        core_command = [*command, f"-out:{destination}/EpicGames.Core.dll", *[f"-r:{p}" for p in refs]]
        analyzer = references.parents[1] / "analyzers/dotnet/cs/System.Text.RegularExpressions.Generator.dll"
        core_command += [f"-analyzer:{analyzer}", *[str(p) for p in sources]]
        print("Preparing external-drive build locks using the installed Unreal sources...", flush=True)
        subprocess.run(confined(core_command), env=env, cwd=workspace, check=True)
        for source in binaries.iterdir():
            if source.name == "EpicGames.Core.dll":
                continue
            target = destination / source.name
            if source.name in {"UnrealBuildTool.dll", "UnrealBuildTool.deps.json", "UnrealBuildTool.runtimeconfig.json"}:
                shutil.copyfile(source, target)
            elif not target.exists():
                target.symlink_to(source, target_is_directory=source.is_dir())
        marker.write_text(json.dumps({"fingerprint": fingerprint, "source_engine": str(engine),
                                     "adapter": "SingleInstanceMutex via workspace flock"}, indent=2) + "\n")
    locks = workspace / "unreal/Saved/Local/BuildLocks"
    locks.mkdir(parents=True, exist_ok=True)
    env["EPHEMERA_MUTEX_DIR"] = str(locks)
    return [sdk / "dotnet", destination / "UnrealBuildTool.dll"]
