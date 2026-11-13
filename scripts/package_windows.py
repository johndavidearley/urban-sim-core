#!/usr/bin/env python3
"""Stage and audit a Windows x64 consumer candidate; never upload or publish it."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

SYSTEM_DLLS = set("kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll ole32.dll oleaut32.dll comdlg32.dll imm32.dll winmm.dll dwmapi.dll shcore.dll setupapi.dll version.dll ws2_32.dll bcrypt.dll crypt32.dll ntdll.dll normaliz.dll secur32.dll userenv.dll rpcrt4.dll wintrust.dll netapi32.dll iphlpapi.dll".split())


def pe_imports(path):
    """Read normal and RVA-based delay imports; fail closed on malformed/non-x64 PE."""
    data = Path(path).read_bytes()

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError(f"Truncated PE: {path}")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError(f"Not a Windows executable: {path}")
    pe = unpack("<I", 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, sections = unpack("<HH", pe + 4)
    optional_size, flags = unpack("<HH", pe + 20)
    optional = pe + 24
    if machine != 0x8664 or unpack("<H", optional)[0] != 0x20B or not flags & 2 or flags & 0x2000:
        raise ValueError("Consumer binaries must be executable x64 PE32+ files")
    if not 1 <= sections <= 96 or optional_size < 128:
        raise ValueError("Invalid PE header sizes")
    unpack(f"<{optional_size}s", optional)
    directory_count = min(unpack("<I", optional + 108)[0], (optional_size - 112) // 8)
    headers = unpack("<I", optional + 60)[0]
    table = optional + optional_size
    ranges = []
    for i in range(sections):
        start = table + i * 40
        unpack("<40s", start)
        virtual_size, rva, raw_size, raw = unpack("<IIII", start + 8)
        if raw + raw_size > len(data):
            raise ValueError("Truncated PE section")
        ranges.append((rva, max(virtual_size, raw_size), raw, raw_size))

    def offset(rva, size=1):
        if rva < headers and rva + size <= min(headers, len(data)):
            return rva
        for base, span, raw, raw_size in ranges:
            if base <= rva < base + span and rva - base + size <= raw_size:
                return raw + rva - base
        raise ValueError("Unmapped PE import RVA")

    imports = set()
    for directory, stride, name_offset in ((1, 20, 12), (13, 32, 4)):
        if directory_count <= directory:
            continue
        rva, size = unpack("<II", optional + 112 + directory * 8)
        if not rva and not size:
            continue
        if not rva or size < stride or size > stride * 4096:
            raise ValueError("Invalid PE import directory")
        terminated = False
        for index in range(size // stride):
            descriptor = offset(rva + index * stride, stride)
            if not any(data[descriptor:descriptor + stride]):
                terminated = True
                break
            if directory == 13 and unpack("<I", descriptor)[0] != 1:
                raise ValueError("Only RVA-based delay imports are supported")
            name_rva = unpack("<I", descriptor + name_offset)[0]
            name = bytearray()
            for i in range(256):
                value = data[offset(name_rva + i)]
                if value == 0:
                    break
                name.append(value)
            else:
                raise ValueError("Unterminated PE DLL name")
            dll = name.decode("ascii").lower()
            if not re.fullmatch(r"[a-z0-9_.-]+\.dll", dll):
                raise ValueError("Unsafe PE DLL name")
            imports.add(dll)
        if not terminated:
            raise ValueError("Unterminated PE import directory")
    return sorted(imports)


def audit_binary(path, renderer=False):
    imports = pe_imports(path)
    for dll in imports:
        system_api = dll.startswith(("api-ms-win-core-", "ext-ms-win-"))
        if dll not in SYSTEM_DLLS and not system_api and not (renderer and dll == "vulkan-1.dll"):
            raise ValueError(f"Unapproved runtime dependency {dll} in {path}; use static CRT/GLFW")
    if renderer and "vulkan-1.dll" not in imports:
        raise ValueError("Renderer must use the system Vulkan loader")
    return imports


def read_cache(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            result[key.split(":", 1)[0]] = value
    return result


def stage(build, output, version, source, allow_dirty=False, project_notice=None):
    build, output, source = Path(build).resolve(), Path(output).resolve(), Path(source).resolve()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9][A-Za-z0-9.-]*)?", version):
        raise ValueError("Use a version such as 0.1.0-candidate.1")
    archive = Path(str(output) + ".zip")
    if output.exists() or archive.exists():
        raise ValueError("Output directory and archive must be new")
    cache = read_cache(build / "CMakeCache.txt")
    if cache.get("URBAN_SIM_WINDOWS_RELEASE") != "ON":
        raise ValueError("Configure URBAN_SIM_WINDOWS_RELEASE=ON with MSVC x64 first")
    if cache.get("CMAKE_BUILD_TYPE", "") not in ("", "Release"):
        raise ValueError("Only Release consumer builds can be staged")
    for flag in ("URBAN_SIM_ENABLE_ASAN_UBSAN", "URBAN_SIM_ENABLE_TSAN"):
        if cache.get(flag) == "ON":
            raise ValueError("Sanitizer binaries are not consumer candidates")
    binaries = build / "bin" / "Release"
    if not binaries.is_dir():
        binaries = build / "bin"
    inputs = {"UrbanSimCore.exe": binaries / "UrbanSimCore.exe",
              "bin/UrbanSimCore-visualizer.exe": binaries / "UrbanSimCore-visualizer.exe"}
    dependencies = {name: audit_binary(path, name.startswith("bin/")) for name, path in inputs.items()}
    glfw_source = cache.get("GLFW_SOURCE_DIR") or cache.get("glfw_SOURCE_DIR")
    json_source = cache.get("nlohmann_json_SOURCE_DIR")
    if not glfw_source or not json_source:
        raise ValueError("Missing dependency source/license paths; use the controlled Windows release configuration")
    licenses = {
        "GLFW.txt": Path(glfw_source) / "LICENSE.md",
        "nlohmann-json.txt": Path(json_source) / "LICENSE.MIT",
        "Roboto-Condensed-OFL.txt": source / "assets/fonts/OFL.txt",
        "Roboto-Condensed-README.md": source / "assets/fonts/README.md",
    }
    for path in licenses.values():
        if not path.is_file():
            raise ValueError(f"Missing dependency license: {path}")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=source, text=True))
    if dirty and not allow_dirty:
        raise ValueError("Candidate source is dirty; commit it or explicitly use --allow-dirty for local review")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="windows-stage-", dir=output.parent) as temporary:
        root = Path(temporary)
        for relative, path in inputs.items():
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
        (root / "licenses").mkdir()
        for name, path in licenses.items():
            shutil.copyfile(path, root / "licenses" / name)
        (root / "THIRD_PARTY_NOTICES.txt").write_text(
            "Third-party components in this candidate:\n"
            "GLFW 3.4 (static): zlib/libpng license, licenses/GLFW.txt\n"
            "nlohmann/json 3.11.3 (header-only): MIT, licenses/nlohmann-json.txt\n"
            "Roboto Condensed (embedded font atlas): SIL OFL 1.1, licenses/Roboto-Condensed-OFL.txt and licenses/Roboto-Condensed-README.md\n"
            "Vulkan loader and GPU driver: external system components, not bundled.\n"
            "GoogleTest, Python/MCP tools, shader compiler and Vulkan SDK: build/test tools, not bundled.\n"
            "Publisher review of project/dependency distribution rights is still required.\n")
        if project_notice:
            shutil.copyfile(project_notice, root / "PROJECT_NOTICE.txt")
        else:
            (root / "PROJECT_NOTICE.txt").write_text("Internal evaluation candidate only. Project ownership, distribution authorization and end-user terms have not been supplied. Not approved for public distribution.\n")
        shutil.copyfile(source / "docs/SAVE_COMPATIBILITY.md", root / "SAVE_COMPATIBILITY.md")
        (root / "README.txt").write_text(f"UrbanSimCore {version} - Windows x64 evaluation candidate\n\nLaunch UrbanSimCore.exe. Keep bin and licenses alongside it.\nA Vulkan-capable graphics driver is required; no Vulkan SDK, compiler or Python is required to play.\nF11 opens named cities/backups. F5 quicksaves; F9 quickloads the active city.\nSaves: %LOCALAPPDATA%/UrbanSimCore/Saves (outside the installation).\nUpdate the game files without deleting your save folder.\nThis candidate has not passed clean-PC/Steam acceptance or publisher release approval.\nSee PROJECT_NOTICE.txt and SAVE_COMPATIBILITY.md.\n")
        files = {str(p.relative_to(root)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted(root.rglob("*")) if p.is_file()}
        manifest = {"schema": 1, "version": version, "platform": "windows", "architecture": "x64",
                    "source_revision": revision, "dirty": dirty, "configuration": "Release", "launch_executable": "UrbanSimCore.exe",
                    "imports": dependencies, "runtime_requirements": ["Vulkan-capable GPU driver including the system vulkan-1.dll"],
                    "project_notice_supplied": bool(project_notice), "public_distribution_approved": False, "files": files}
        (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        shutil.copytree(root, output)
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as zip_file:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                zip_file.write(path, path.relative_to(output.parent))
    return {"directory": str(output), "archive": str(archive), "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--project-notice", type=Path)
    args = parser.parse_args()
    print(json.dumps(stage(args.build, args.output, args.version, args.source, args.allow_dirty, args.project_notice), indent=2))
