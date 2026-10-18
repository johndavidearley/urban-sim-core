#!/usr/bin/env python3
"""Install an existing macOS Release build and archive a local candidate."""
import argparse
import hashlib
import json
import platform
import subprocess
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-graphics-release"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--name", default="UrbanSimCore-local")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("Run on macOS with a native Release build")
    cache = (args.build / "CMakeCache.txt").read_text()
    if "CMAKE_BUILD_TYPE:STRING=Release\n" not in cache:
        parser.error("Package a Release build")
    name = f"{args.name}-macos-{platform.machine()}"
    root = args.output.resolve() / name
    root.mkdir(parents=True, exist_ok=False)
    subprocess.run(
        ["cmake", "--install", str(args.build), "--prefix", str(root)], check=True
    )
    launcher = root / "Launch UrbanSimCore.command"
    launcher.write_text(
        """#!/bin/sh
set -eu
package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
save_dir=${URBAN_SIM_SAVE_DIR:-"$HOME/Library/Application Support/UrbanSimCore"}
mkdir -p "$save_dir"
cd "$save_dir"
exec "$package_dir/bin/UrbanSimCore-visualizer" "$@"
"""
    )
    launcher.chmod(0o755)
    (root / "README.txt").write_text(
        f"""UrbanSimCore local candidate ({args.name}) — macOS {platform.machine()}

Extract the entire archive, then open Launch UrbanSimCore.command.
The Vulkan loader, MoltenVK and GLFW runtime are bundled under lib/urban-sim.
This candidate is ad-hoc signed, not notarized for public distribution.

Saves: ~/Library/Application Support/UrbanSimCore/urban_sim_session.json
Keep its .city.json companion alongside it. F5 saves; F9 loads.
URBAN_SIM_SAVE_DIR optionally chooses a different save directory.
F1 opens the city guide. F6 opens the budget. Space pauses; N steps one tick.
New City defaults to lakes/river terrain. T toggles flat maps on the start screen.
S cycles civic tools; with Water/Sanitation selected, V toggles shore/inland mode.
Shore intakes and raw sewage outlets need adjacent water and a road. Nearby
raw outlets reduce intake supply; separate them along the water body.
Incinerator (S, after Crematorium): $18,000, $60/tick; burns residual garbage
for up to 6 MW. F6 Services controls funding. Recycling takes priority.
With Power selected, V cycles generic, coal, gas, nuclear, solar, wind and
hydro plants, then a substation ($1,500, $4/tick) that extends reach only. With Sanitation on the shore, V also offers clean treatment.
Landfills fill up; schools and hospitals have seats and beds.
P selects a district; its HEIGHT button caps redevelopment at 1X to 64X.
F6 BONDS issues one municipal bond ($10K/$50K/$200K over 100/200/400 ticks).
See the in-game controls for road, zoning and civic construction tools.

The bin directory also contains the CLI and native playtest executable.
Python playtest sources are in share/urban-sim/playtest; their dependencies
are installed separately. No Python environment is required to play.
"""
    )
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], text=True))
    manifest = {
        "revision": revision,
        "dirty": dirty,
        "platform": platform.platform(),
        "architecture": platform.machine(),
        "files": {},
    }
    for path in sorted(root.rglob("*")):
        if path.is_file():
            manifest["files"][str(path.relative_to(root))] = hashlib.sha256(
                path.read_bytes()
            ).hexdigest()
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive = shutil.make_archive(str(root), "zip", root.parent, root.name)
    digest = hashlib.sha256(Path(archive).read_bytes()).hexdigest()
    Path(archive + ".sha256").write_text(f"{digest}  {Path(archive).name}\n")
    print(archive)


if __name__ == "__main__":
    main()
