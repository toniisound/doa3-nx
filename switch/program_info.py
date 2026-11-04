# SPDX-License-Identifier: GPL-3.0-or-later
"""Prints 'manifest ebp forwards jp' for a lifted program folder, checked
against the versions tools/build_game.py validates."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_game import VERSIONS, program_manifest  # noqa: E402

generated = Path(sys.argv[1])
manifest, ebp = program_manifest(generated)
for version in VERSIONS.values():
    if (manifest, ebp) == (version["program"], version["ebp"]):
        print(manifest, ebp, version["forwards"], "ON" if version["jp"] else "OFF", version["name"].replace(" ", "_"))
        sys.exit(0)
sys.exit(f"{generated}: program {manifest} is not one the build validated; run ./build_game.sh again")
