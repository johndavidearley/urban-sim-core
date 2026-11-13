#!/usr/bin/env python3
"""Validate a consumer candidate and prepare SteamPipe PREVIEW scripts offline.

No login, upload, branch assignment or store changes are performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def verify(package):
    package = Path(package).resolve()
    if any(p.is_symlink() for p in package.rglob("*")):
        raise ValueError("Package symlinks are not supported")
    manifest = json.loads((package / "manifest.json").read_text())
    if manifest.get("schema") != 1 or manifest.get("platform") != "windows" or manifest.get("architecture") != "x64":
        raise ValueError("Expected a Windows x64 candidate manifest")
    if manifest.get("launch_executable") != "UrbanSimCore.exe":
        raise ValueError("Unexpected launch executable")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9][A-Za-z0-9.-]*)?", manifest.get("version", "")):
        raise ValueError("Unsafe package version")
    expected = manifest["files"]
    required = {"UrbanSimCore.exe", "bin/UrbanSimCore-visualizer.exe", "PROJECT_NOTICE.txt",
                "SAVE_COMPATIBILITY.md", "README.txt", "THIRD_PARTY_NOTICES.txt", "licenses/GLFW.txt", "licenses/nlohmann-json.txt", "licenses/Roboto-Condensed-OFL.txt", "licenses/Roboto-Condensed-README.md"}
    if set(expected) != required:
        raise ValueError("Incomplete consumer package")
    actual = {str(p.relative_to(package)).replace("\\", "/") for p in package.rglob("*") if p.is_file()}
    if actual != set(expected) | {"manifest.json"}:
        raise ValueError("Unmanifested or missing package files")
    for name, sha in expected.items():
        if not re.fullmatch(r"[A-Za-z0-9_./-]+", name) or name.startswith("/") or ".." in name.split("/"):
            raise ValueError("Unsafe manifest path")
        lower = name.lower()
        if lower.endswith((".dll", ".pdb", ".py", ".spv")) or "playtest" in lower or "steam_appid" in lower:
            raise ValueError("Development/SDK runtime files must not enter the consumer depot")
        if hashlib.sha256((package / name).read_bytes()).hexdigest() != sha:
            raise ValueError(f"Package checksum mismatch: {name}")
    # Audit the actual executables, not only the manifest's dependency claims.
    from package_windows import audit_binary
    audit_binary(package / "UrbanSimCore.exe")
    audit_binary(package / "bin/UrbanSimCore-visualizer.exe", renderer=True)
    return manifest


def prepare(package, output, app_id, depot_id):
    if type(app_id) is not int or type(depot_id) is not int or not 0 < app_id <= 0xFFFFFFFF or not 0 < depot_id <= 0xFFFFFFFF:
        raise ValueError("Supply positive assigned AppID and Windows DepotID values")
    manifest = verify(package)
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("Steam staging output must be a new directory")
    if output == Path(package).resolve() or Path(package).resolve() in output.parents:
        raise ValueError("Steam staging must be outside the package")
    output.mkdir(parents=True)
    shutil.copytree(package, output / "content")
    scripts = output / "scripts"
    scripts.mkdir()
    (output / "output").mkdir()
    depot = f"depot_build_{depot_id}.vdf"
    (scripts / f"app_build_{app_id}.vdf").write_text(f'''"AppBuild"
{{
  "AppID" "{app_id}"
  "Desc" "UrbanSimCore {manifest['version']} internal candidate preview"
  "ContentRoot" "../content"
  "BuildOutput" "../output"
  "Preview" "1"
  "Depots"
  {{
    "{depot_id}" "{depot}"
  }}
}}
''')
    (scripts / depot).write_text(f'''"DepotBuild"
{{
  "DepotID" "{depot_id}"
  "FileMapping"
  {{
    "LocalPath" "*"
    "DepotPath" "."
    "recursive" "1"
  }}
}}
''')
    settings = {"purpose": "Manual Steamworks Installation-tab checklist; this JSON is not uploaded by SteamPipe",
                "app_id": app_id, "depot_id": depot_id, "executable": "UrbanSimCore.exe",
                "arguments": "", "working_directory": "", "os": "Windows", "architecture": "64-bit",
                "preview_only": True, "branch_assignment": None, "store_submission_performed": False}
    (output / "launch-settings.json").write_text(json.dumps(settings, indent=2) + "\n")
    (output / "README.txt").write_text("Offline SteamPipe preview staging only. Review assigned IDs and launch-settings.json in Steamworks. The AppBuild script uses Preview=1 and has no SetLive key. No Steam commands have been run. Publisher rights, store setup, consumer-PC acceptance and release approval are still required.\n")
    return settings


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app-id", type=int, required=True)
    parser.add_argument("--depot-id", type=int, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.package, args.output, args.app_id, args.depot_id), indent=2))
