"""Run inside Unreal's Python commandlet to check the installed engine."""
import json
from pathlib import Path
import sys
import tempfile

import unreal

workspace = Path(__file__).resolve().parents[2]
selected = json.loads((workspace / ".unreal/Installer/selected-release.json").read_text())
version = unreal.SystemLibrary.get_engine_version()
assert version.startswith(selected["version"] + "-"), version
temporary = Path(tempfile.gettempdir()).resolve()
assert temporary.is_relative_to(workspace), str(temporary)
result = {"engine_version": version, "platform": sys.platform, "temporary_directory": str(temporary)}
(workspace / ".unreal/Installer/engine-smoke.json").write_text(json.dumps(result, indent=2) + "\n")
unreal.log("Ephemera engine startup check passed: " + version)
