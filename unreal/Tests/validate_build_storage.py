#!/usr/bin/env python3
"""Check that external build locks exclude other processes and release on exit."""
import json
from pathlib import Path
import subprocess
import sys
import uuid

WORKSPACE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(WORKSPACE / "unreal/Tools"))
from unreal import confined, environment
from external_build import prepare


def main():
    env = environment()
    engine = WORKSPACE / ".unreal/UE_5.8"
    build = prepare(engine, env, confined, WORKSPACE)
    dotnet, ubt = build
    sdk = dotnet.parent
    refs = sorted(sdk.glob("packs/Microsoft.NETCore.App.Ref/*/ref/net10.0"))[-1]
    csc = sorted(sdk.glob("sdk/*/Roslyn/bincore/csc.dll"))[-1]
    directory = WORKSPACE / "unreal/Saved/Local/BuildLockTest"
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / "Probe.cs"
    source.write_text('''using System;
using System.IO;
using System.Threading;
using EpicGames.Core;
class Probe {
    static void Main(string[] args) {
        string support = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        if (!Path.GetFullPath(support).StartsWith(args[2] + "/", StringComparison.Ordinal))
            throw new Exception("ApplicationData escaped workspace: " + support);
        if (args[0] == "hold") {
            using var mutex = SingleInstanceMutex.Acquire(args[1]);
            Console.WriteLine("held"); Console.Out.Flush(); Console.ReadLine();
        } else if (args[0] == "try") {
            bool acquired = SingleInstanceMutex.TryAcquire(args[1], out var mutex);
            mutex?.Dispose(); Console.WriteLine(acquired ? "acquired" : "busy");
        } else if (args[0] == "cancel") {
            using var cancellation = new CancellationTokenSource(150);
            try {
                using var mutex = SingleInstanceMutex.AcquireAsync(args[1], cancellation.Token).GetAwaiter().GetResult();
                throw new Exception("Acquired a held lock");
            } catch (OperationCanceledException) { Console.WriteLine("cancelled"); }
        }
    }
}''')
    core = ubt.parent / "EpicGames.Core.dll"
    subprocess.run(confined([str(dotnet), str(csc), "-nologo", "-target:exe",
                   f"-out:{directory}/Probe.dll", *[f"-r:{p}" for p in refs.glob("*.dll")],
                   f"-r:{core}", str(source)]), env=env, check=True)
    target = directory / "EpicGames.Core.dll"
    if target.is_symlink():
        target.unlink()
    target.symlink_to(core)
    (directory / "Probe.runtimeconfig.json").write_text(json.dumps({"runtimeOptions": {
        "tfm": "net10.0", "framework": {"name": "Microsoft.NETCore.App", "version": "10.0.0"}}}))
    lock = "EphemeraStorageTest_" + uuid.uuid4().hex

    def command(mode):
        return confined([str(dotnet), str(directory / "Probe.dll"), mode, lock, str(WORKSPACE)])

    def probe(mode):
        result = subprocess.run(command(mode), env=env, capture_output=True, text=True, timeout=10)
        if result.returncode:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    holder = subprocess.Popen(command("hold"), env=env, stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, text=True)
    try:
        assert holder.stdout.readline().strip() == "held"
        assert probe("try") == "busy", "A second process acquired a held lock"
        assert probe("cancel") == "cancelled", "Lock wait did not cancel"
        holder.communicate("\n", timeout=10)
        assert holder.returncode == 0
        assert probe("try") == "acquired", "A released lock could not be acquired"
        holder = subprocess.Popen(command("hold"), env=env, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True)
        assert holder.stdout.readline().strip() == "held"
        holder.kill()
        holder.communicate(timeout=10)
        assert probe("try") == "acquired", "A crashed process left a stale lock"
    finally:
        if holder.poll() is None:
            holder.kill()
            holder.communicate(timeout=10)
    print("Passed: external settings, process exclusion, cancellation, normal release, crash release")


if __name__ == "__main__":
    main()
