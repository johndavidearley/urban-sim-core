# M56 — Local candidate qualification

November 13, 2026. The source is the existing dirty working tree; this is local
qualification, not a hosted CI run or a Windows shipping approval. No gameplay
source changes were required; all checks below completed successfully.

## Candidate and evidence

- Native executable SHA-256:
  `06d26a29a503207c48b86020d86f1eacbc970ff494668390c4d4f6fefab10669`.
- Evidence root: `playtest-artifacts/m56-qualification/`.
- `build-identity.json` records platform/compiler, executable/sanitizer binary
  hashes and sanitizer scope. `source-hashes.json` records 287 source/build-input
  files. Earlier browser/recovery acceptance on this same native candidate is
  linked from [M56_RELIABILITY.md](M56_RELIABILITY.md).
- Final outcomes belong in `verification-summary.json`; full logs are retained
  separately for rendered tests, ASan/UBSan and TSan.

## Results

| Check | Local result | Scope |
| --- | --- | --- |
| Strict Release and C++ tests | 764/764 pass (retained browser candidate evidence) | Includes two GLFW key-translation tests. |
| Python unit tests | 7/7 pass (retained browser candidate evidence) | MCP protocol and server checks. |
| Full native rendered suite | 57/57 pass in one invocation | 113 retained process directories; zero Vulkan validation errors. |
| AddressSanitizer + UBSan | 762/762 pass | Debug/headless, halt on first finding; leak detection disabled on this Mac. |
| ThreadSanitizer | 762/762 pass | Debug/headless, halt on first finding; no race reports. |

The headless sanitizer builds retain core simulation, persistence, utility and
CPU geometry tests. They omit the two GLFW key-translation cases and do not
instrument the Vulkan driver or exercise the native renderer. The separate
rendered run covers the live Vulkan/GLFW application. Passing these local tests
does not establish minimum-hardware performance or packaged-build reliability.

On this macOS host, ASan uses `detect_leaks=0`, matching the repository's local
preset. No leak-check pass is claimed. The Linux CI configuration requests leak
detection; a fresh candidate CI result is still required.

## Reproduction

Activate the playtest Python environment and put Ninja/CMake on PATH. The build
uses the repository's pinned dependencies; the local run reused the already
available nlohmann-json source instead of downloading another copy.

```sh
cmake --preset asan -G Ninja
cmake --build --preset asan --parallel 2
ctest --preset asan --parallel 2

cmake --preset tsan -G Ninja
cmake --build --preset tsan --parallel 2
ctest --preset tsan --parallel 2
```

The retained local binaries are under `build-m56-asan/` and `build-m56-tsan/`;
the presets above use `build/presets/`. Equivalent direct test commands:

```sh
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-m56-asan --parallel 2 --output-on-failure

TSAN_OPTIONS=halt_on_error=1:history_size=7 \
ctest --test-dir build-m56-tsan --parallel 2 --output-on-failure

PYTHONPATH=playtest \
URBAN_SIM_PLAYTEST_EXECUTABLE="$PWD/build-strict/bin/UrbanSimCore-playtest" \
URBAN_SIM_PLAYTEST_ARTIFACTS="$PWD/playtest-artifacts/m56-full-rendered" \
VK_DRIVER_FILES=/opt/local/share/vulkan/icd.d/MoltenVK_icd.json \
.venv/bin/python -m unittest discover -s playtest/tests -p test_rendered.py -v
```

The Vulkan driver override shown is specific to this MacPorts macOS setup.
Use the native driver configuration for the machine under test.

## Remaining release work

M56 stays open. The full-suite/sanitizer release item also requires fresh hosted
CI tied to the candidate's source revision; the uncommitted local tree has not
been published. Proposed-minimum Windows 1080p performance, Windows/platform
qualification, leak qualification and three two-hour packaged-build sessions
remain open. M55's full 1080-high and Windows 150% display checks also remain open.
