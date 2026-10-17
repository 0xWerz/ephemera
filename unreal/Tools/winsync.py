#!/usr/bin/env python3
"""Keeps the Windows PC's copy of the project in step with this one, and runs things there.

The game is written on the Mac and built, rendered and played on the PC (RTX 3060, the real
target). Code and tools go Mac -> PC; the content the PC's editor builds comes back PC -> Mac when
the Mac needs it.

    winsync.py push [--content]     send what changed here since the last push: Source, Config, Tools,
                                    Ephemera.uproject and the small source assets; with --content
                                    also Content (only after content was built on the Mac)
    winsync.py pull                 fetch the Content files that changed on the PC since the last pull
    winsync.py run <command...>     run a command on the PC, in the project folder
    winsync.py ue <unreal.py args>  run Tools/unreal.py on the PC (build, slice, shot, shots ...)
    winsync.py fetch <remote> <local>   copy one file or folder back (paths relative to the project)
    winsync.py logs                 what the game last did on the PC: its newest log and crash report are
                                    fetched to .scratch/ue/pc/logs/ and summed up (errors, warnings, slow
                                    frames, hangs, how it ended)

Only changed files travel (by size and modification time, remembered in .scratch/winsync/).
"""
import json
import os
import pathlib
import subprocess
import sys

HOST = "user@windows-pc"
REMOTE = "F:\\Ephemera"
ENGINE = "F:\\UE_5.8"
PROJECT = pathlib.Path(__file__).resolve().parents[1]
STATE = PROJECT.parent / ".scratch/winsync"
CODE = ["Ephemera.uproject", "Source", "Config", "Tools", "SourceAssets/terrain", "SourceAssets/water", "SourceAssets/audio", "Content/Splash", "Build/Windows/Application.ico"]
SKIP = ("__pycache__", ".DS_Store", "node_modules", ".profile")
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", HOST]


def local_files(roots):
    found = {}
    for root in roots:
        path = PROJECT / root
        paths = [path] if path.is_file() else sorted(p for p in path.rglob("*") if p.is_file())
        for p in paths:
            if any(part in SKIP for part in p.parts) or p.name.startswith("._"):
                continue
            stat = p.stat()
            found[p.relative_to(PROJECT).as_posix()] = [stat.st_size, stat.st_mtime_ns]
    return found


def load(name):
    path = STATE / name
    return json.loads(path.read_text()) if path.exists() else {}


def save(name, value):
    STATE.mkdir(parents=True, exist_ok=True)
    (STATE / name).write_text(json.dumps(value))


def remote(command, **kwargs):
    """Runs one cmd.exe command line on the PC, in the project folder."""
    return subprocess.run([*SSH, f'cd /d {REMOTE} && {command}'], **kwargs)


def push(with_content, assume_sent=False):
    roots = CODE + (["Content"] if with_content else [])
    now = local_files(roots)
    before = load("pushed.json")
    changed = sorted(path for path, stamp in now.items() if before.get(path) != stamp)
    gone = sorted(path for path in before if path not in now and any(path == r or path.startswith(r + "/") for r in roots))
    if assume_sent:
        changed, gone = [], []
    if changed:
        listing = STATE / "push.txt"
        STATE.mkdir(parents=True, exist_ok=True)
        listing.write_text("\n".join(changed) + "\n")
        size = sum(now[path][0] for path in changed)
        print(f"sending {len(changed)} files, {size / 1e6:.1f} MB", flush=True)
        tar = subprocess.Popen(["tar", "--no-xattrs", "--no-mac-metadata", "-cf", "-", "-C", str(PROJECT), "-T", str(listing)],
                               stdout=subprocess.PIPE, env={**os.environ, "COPYFILE_DISABLE": "1"})
        done = subprocess.run([*SSH, f'(if not exist {REMOTE} mkdir {REMOTE}) & tar -xf - -C {REMOTE}'], stdin=tar.stdout)
        tar.stdout.close()
        if tar.wait() != 0 or done.returncode != 0:
            raise SystemExit("push failed: nothing recorded as sent")
    for path in gone:
        remote(f'del /q "{path.replace("/", chr(92))}"', check=False)
    before.update(now)
    for path in gone:
        before.pop(path, None)
    save("pushed.json", before)
    print(f"pushed: {len(changed)} changed, {len(gone)} removed" if not assume_sent else f"recorded {len(now)} files as already on the PC")


def remote_content():
    """Content files on the PC as {path: [size, ticks]}."""
    script = ("Get-ChildItem -Recurse -File Content | ForEach-Object { $_.FullName.Substring((Get-Location).Path.Length + 1).Replace([char]92, '/') "
              "+ '|' + $_.Length + '|' + $_.LastWriteTimeUtc.Ticks }")
    out = remote(f'powershell -NoProfile -Command "{script}"', capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.strip().split("|")
        if len(parts) == 3:
            found[parts[0]] = [int(parts[1]), int(parts[2])]
    return found


def pull(assume_same=False):
    now = remote_content()
    before = load("pulled.json")
    changed = sorted(path for path, stamp in now.items() if before.get(path) != stamp)
    if assume_same:
        changed = []
    if changed:
        listing = STATE / "pull.txt"
        STATE.mkdir(parents=True, exist_ok=True)
        listing.write_text("\r\n".join(path.replace("/", "\\") for path in changed) + "\r\n")
        subprocess.run(["scp", "-q", "-o", "BatchMode=yes", str(listing), f"{HOST}:{REMOTE.replace(chr(92), '/')}/Saved/pull.txt"], check=True)
        print(f"fetching {len(changed)} files, {sum(now[p][0] for p in changed) / 1e6:.1f} MB", flush=True)
        send = subprocess.Popen([*SSH, f'cd /d {REMOTE} && tar -cf - -T Saved\\pull.txt'], stdout=subprocess.PIPE)
        take = subprocess.run(["tar", "-xf", "-", "-C", str(PROJECT)], stdin=send.stdout)
        send.stdout.close()
        if send.wait() != 0 or take.returncode != 0:
            raise SystemExit("pull failed: nothing recorded as fetched")
        # What came from the PC is by definition what the PC has: do not send it straight back.
        pushed = load("pushed.json")
        pushed.update(local_files(["Content"]))
        save("pushed.json", pushed)
    save("pulled.json", now)
    print(f"pulled: {len(changed)} changed" if not assume_same else f"recorded {len(now)} content files as the same on both")


def logs():
    """Fetches the newest game log (packaged game first, then the editor-run game) and any crash report, and digests them."""
    script = (
        "$places = @((Join-Path $env:LOCALAPPDATA 'Ephemera\\Saved'), (Join-Path (Get-Location) 'Builds\\Windows\\Ephemera\\Saved'), (Join-Path (Get-Location) 'Saved'));"
        "$logs = foreach ($p in $places) { Get-ChildItem (Join-Path $p 'Logs\\*.log'), (Join-Path $p 'Play*.log') -ErrorAction SilentlyContinue };"
        "$log = $logs | Sort-Object LastWriteTime -Descending | Select-Object -First 1;"
        "$crash = foreach ($p in $places) { Get-ChildItem (Join-Path $p 'Crashes') -Directory -ErrorAction SilentlyContinue };"
        "$crash = $crash | Sort-Object LastWriteTime -Descending | Select-Object -First 1;"
        "if ($log) { 'LOG|' + $log.FullName + '|' + $log.LastWriteTime.ToString('s') };"
        "if ($crash) { 'CRASH|' + $crash.FullName + '|' + $crash.LastWriteTime.ToString('s') }")
    out = remote(f'powershell -NoProfile -Command "{script}"', capture_output=True, text=True).stdout
    found = {line.split("|")[0]: line.strip().split("|")[1:] for line in out.splitlines() if "|" in line}
    target = PROJECT.parent / ".scratch/ue/pc/logs"
    target.mkdir(parents=True, exist_ok=True)
    if "LOG" not in found:
        raise SystemExit("no game log found on the PC")
    path, when = found["LOG"]
    local = target / "last.log"
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", f"{HOST}:{path.replace(chr(92), '/')}", str(local)], check=True)
    text = local.read_text(errors="replace").splitlines()
    print(f"log: {path} (written {when}), {len(text)} lines")
    import collections
    import re
    kinds = collections.Counter()
    for line in text:
        match = re.search(r"\]([A-Za-z0-9_]+): (Error|Warning|Fatal)", line)
        if match and "UnifiedError" not in line and "LogEditorDataStorage" not in line:
            kinds[(match.group(2), match.group(1), re.sub(r"[0-9]+(\.[0-9]+)?", "N", line.split(match.group(2) + ":", 1)[-1].strip())[:110])] += 1
    for (level, category, sample), count in kinds.most_common(25):
        print(f"  {count:4d} x {level:7s} {category}: {sample}")
    for needle in ("Ephemera: ready to play", "Ephemera frames:", "low-latency frame pacing", "Ephemera sound:", "Ephemera: a frame took", "Hang detected", "Hitch detected", "Fatal error", "Unhandled Exception",
                   "DXGI_ERROR", "Out of video memory", "Assertion failed", "Ensure condition failed"):
        hits = [line for line in text if needle in line]
        if hits:
            print(f"  {len(hits)} x '{needle}', last: {hits[-1][-170:]}")
    ended = "closed normally" if any("Log file closed" in line for line in text[-5:]) else "DID NOT close its log (killed, frozen or crashed)"
    print(f"  end of session: {ended}")
    print("  last lines:")
    for line in text[-6:]:
        print("    " + line[-200:])
    if "CRASH" in found:
        path, when = found["CRASH"]
        print(f"crash report: {path} ({when})")
        subprocess.run(["scp", "-q", "-r", "-o", "BatchMode=yes", f"{HOST}:{path.replace(chr(92), '/')}", str(target / "crash")])
        for file in (target / "crash").rglob("*.log"):
            lines = file.read_text(errors="replace").splitlines()
            start = next((i for i, line in enumerate(lines) if "Unhandled Exception" in line or "Fatal error" in line or "Assertion failed" in line), None)
            if start is not None:
                print("\n".join("    " + line[-200:] for line in lines[start:start + 24]))
                break


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    action, rest = sys.argv[1], sys.argv[2:]
    if action == "push":
        push("--content" in rest, assume_sent="--assume-sent" in rest)
    elif action == "pull":
        pull(assume_same="--assume-same" in rest)
    elif action == "run":
        raise SystemExit(remote(" ".join(rest)).returncode)
    elif action == "ue":
        quoted = " ".join(f'"{a}"' if (" " in a or "|" in a or "&" in a) else a for a in rest)
        raise SystemExit(remote(f'python -B Tools\\unreal.py --engine {ENGINE} {quoted}').returncode)
    elif action == "logs":
        logs()
    elif action == "fetch":
        source, target = rest
        remote_path = f"{HOST}:{REMOTE.replace(chr(92), '/')}/{source}"
        pathlib.Path(target).parent.mkdir(parents=True, exist_ok=True)
        raise SystemExit(subprocess.run(["scp", "-q", "-r", "-o", "BatchMode=yes", remote_path, target]).returncode)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
