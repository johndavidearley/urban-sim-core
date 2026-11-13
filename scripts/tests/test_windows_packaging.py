"""Offline packaging tests use synthetic PE headers, never claimed game builds."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package_windows as windows
import prepare_steam_depot as steam


def executable(imports, machine=0x8664, delay=False):
    data = bytearray(8192)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, machine, 1)
    struct.pack_into("<HH", data, 0x94, 240, 2)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<I", data, optional + 60, 0x400)
    struct.pack_into("<I", data, optional + 108, 16)
    section = optional + 240
    struct.pack_into("<IIII", data, section + 8, 0x1C00, 0x1000, 0x1C00, 0x400)
    stride, directory = (32, 13) if delay else (20, 1)
    struct.pack_into("<II", data, optional + 112 + directory * 8, 0x1000, (len(imports) + 1) * stride)
    for i, dll in enumerate(imports):
        name = 0x800 + i * 256
        raw = dll.encode("ascii") + b"\0"
        data[name:name + len(raw)] = raw
        descriptor = 0x400 + i * stride
        if delay:
            struct.pack_into("<II", data, descriptor, 1, 0x1000 + name - 0x400)
        else:
            struct.pack_into("<I", data, descriptor + 12, 0x1000 + name - 0x400)
    return bytes(data)


class WindowsPackaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.source = self.root / "source"
        self.build.mkdir()
        (self.build / "bin/Release").mkdir(parents=True)
        for name, imports in (("UrbanSimCore.exe", ["KERNEL32.dll", "USER32.dll"]),
                              ("UrbanSimCore-visualizer.exe", ["KERNEL32.dll", "vulkan-1.dll"])):
            (self.build / "bin/Release" / name).write_bytes(executable(imports))
        for path in ("assets/fonts/OFL.txt", "assets/fonts/README.md", "docs/SAVE_COMPATIBILITY.md"):
            p = self.source / path
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text("Fixture notice only, not an actual third-party license.\n")
        for folder, filename in (("glfw", "LICENSE.md"), ("json", "LICENSE.MIT")):
            (self.root / folder).mkdir()
            (self.root / folder / filename).write_text("Fixture license\n")
        (self.build / "CMakeCache.txt").write_text(
            "URBAN_SIM_WINDOWS_RELEASE:BOOL=ON\nCMAKE_BUILD_TYPE:STRING=\n"
            f"GLFW_SOURCE_DIR:STATIC={self.root / 'glfw'}\n"
            f"nlohmann_json_SOURCE_DIR:STATIC={self.root / 'json'}\n")
        self.git = patch.object(windows.subprocess, "check_output", side_effect=lambda args, **kwargs: "123456\n" if "rev-parse" in args else "")
        self.git.start()
        self.addCleanup(self.git.stop)

    def package(self):
        output = self.root / "UrbanSimCore-0.1.0-candidate.1"
        result = windows.stage(self.build, output, "0.1.0-candidate.1", self.source)
        return output, result

    def test_consumer_package_is_versioned_audited_and_preview_only(self):
        output, result = self.package()
        self.assertEqual(Path(result["archive"]).name, output.name + ".zip")
        manifest = steam.verify(output)
        self.assertFalse(manifest["public_distribution_approved"])
        self.assertFalse(manifest["project_notice_supplied"])
        self.assertNotIn("UrbanSimCore-cli.exe", manifest["files"])
        staging = self.root / "steam"
        settings = steam.prepare(output, staging, 12345, 12346)
        text = (staging / "scripts/app_build_12345.vdf").read_text()
        self.assertIn('"Preview" "1"', text)
        self.assertNotIn("SetLive", text)
        self.assertTrue((staging / "scripts/depot_build_12346.vdf").read_text().startswith('"DepotBuild"'))
        self.assertEqual(settings["executable"], "UrbanSimCore.exe")
        self.assertTrue((staging / "content/bin/UrbanSimCore-visualizer.exe").is_file())

    def test_wrong_architecture_and_malformed_pe_reject(self):
        p = self.root / "bad.exe"
        for data in (b"", b"MZ", executable(["kernel32.dll"], machine=0x14C), executable(["kernel32.dll"])[:500]):
            p.write_bytes(data)
            with self.assertRaises(ValueError):
                windows.pe_imports(p)

    def test_missing_runtime_and_debug_dependencies_reject(self):
        p = self.root / "bad.exe"
        for dll in ("glfw3.dll", "VCRUNTIME140.dll", "ucrtbased.dll", "unknown.dll", "vulkan-1.dll"):
            p.write_bytes(executable(["kernel32.dll", dll]))
            with self.assertRaises(ValueError):
                windows.audit_binary(p)
        p.write_bytes(executable(["kernel32.dll"]))
        with self.assertRaisesRegex(ValueError, "system Vulkan"):
            windows.audit_binary(p, renderer=True)

    def test_delay_imports_are_audited_too(self):
        p = self.root / "delay.exe"
        p.write_bytes(executable(["VCRUNTIME140.dll"], delay=True))
        self.assertEqual(windows.pe_imports(p), ["vcruntime140.dll"])
        with self.assertRaises(ValueError):
            windows.audit_binary(p)
        data = bytearray(p.read_bytes())
        struct.pack_into("<I", data, 0x400, 0)
        p.write_bytes(data)
        with self.assertRaises(ValueError):
            windows.pe_imports(p)

    def test_changed_and_extra_depot_files_reject(self):
        output, _ = self.package()
        p = output / "README.txt"
        original = p.read_bytes()
        p.write_bytes(b"modified")
        with self.assertRaises(ValueError):
            steam.verify(output)
        p.write_bytes(original)
        extra = output / "bin/UrbanSimCore-playtest.exe"
        extra.write_bytes(executable(["kernel32.dll"]))
        with self.assertRaises(ValueError):
            steam.verify(output)
        manifest = json.loads((output / "manifest.json").read_text())
        manifest["files"]["bin/UrbanSimCore-playtest.exe"] = hashlib.sha256(extra.read_bytes()).hexdigest()
        (output / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaises(ValueError):
            steam.verify(output)

    def test_unsafe_manifest_paths_and_versions_reject(self):
        output, _ = self.package()
        path = output / "manifest.json"
        manifest = json.loads(path.read_text())
        for version in ('0.1.0"\n"SetLive" "default', "../other", ""):
            broken = dict(manifest, version=version)
            path.write_text(json.dumps(broken))
            with self.assertRaises(ValueError):
                steam.verify(output)
        manifest["files"]["../outside.txt"] = "0" * 64
        path.write_text(json.dumps(manifest))
        with self.assertRaises(ValueError):
            steam.verify(output)

    def test_actual_executable_dependencies_override_forged_manifest_claims(self):
        output, _ = self.package()
        path = output / "bin/UrbanSimCore-visualizer.exe"
        path.write_bytes(executable(["vulkan-1.dll", "glfw3.dll"]))
        manifest = json.loads((output / "manifest.json").read_text())
        manifest["files"]["bin/UrbanSimCore-visualizer.exe"] = hashlib.sha256(path.read_bytes()).hexdigest()
        manifest["imports"]["bin/UrbanSimCore-visualizer.exe"] = ["vulkan-1.dll"]
        (output / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "glfw3"):
            steam.verify(output)

    def test_existing_output_and_bad_ids_are_not_overwritten(self):
        output, _ = self.package()
        original = (output / "manifest.json").read_bytes()
        with self.assertRaises(ValueError):
            windows.stage(self.build, output, "0.1.0-candidate.1", self.source)
        self.assertEqual((output / "manifest.json").read_bytes(), original)
        for app, depot in ((0, 12), (12, -1), (True, 13), (12, 2**32)):
            with self.assertRaises(ValueError):
                steam.prepare(output, self.root / "steam", app, depot)
        self.assertFalse((self.root / "steam").exists())

    def test_dirty_source_debug_builds_and_missing_notices_fail_closed(self):
        with patch.object(windows.subprocess, "check_output", side_effect=lambda args, **kwargs: "123456\n" if "rev-parse" in args else " M file\n"):
            with self.assertRaisesRegex(ValueError, "dirty"):
                self.package()
        cache = self.build / "CMakeCache.txt"
        original = cache.read_text()
        for change in (original.replace("URBAN_SIM_WINDOWS_RELEASE:BOOL=ON", "URBAN_SIM_WINDOWS_RELEASE:BOOL=OFF"),
                       original.replace("CMAKE_BUILD_TYPE:STRING=", "CMAKE_BUILD_TYPE:STRING=Debug"),
                       original + "URBAN_SIM_ENABLE_ASAN_UBSAN:BOOL=ON\n"):
            cache.write_text(change)
            with self.assertRaises(ValueError):
                self.package()
        cache.write_text(original)
        (self.root / "glfw/LICENSE.md").unlink()
        with self.assertRaisesRegex(ValueError, "license"):
            self.package()
        self.assertFalse((self.root / "UrbanSimCore-0.1.0-candidate.1").exists())


if __name__ == "__main__":
    unittest.main()
