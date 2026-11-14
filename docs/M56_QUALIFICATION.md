# M56 — Local candidate qualification

## November 14 Windows work

M56 remains **in progress**. M55's Windows display/DPI work is now locally
verified; the historical open-display note at the end of this document is
superseded. Current Windows evidence is retained under `build-windows/`.

- Fixed bridge cancellation on Python 3.11 by keeping I/O in the requesting
  task under AnyIO timeout scopes. All seven Python tests pass and the original
  cancellation regression passes 30 consecutive runs.
- Added optional `URBAN_SIM_PERFORMANCE_CSV` capture to the actual application,
  recording buffered present-to-present wall times, simulation time and
  scheduler lateness. Disabled by default; no GPU-timestamp claim is made.
- The two-minute realtime probe uses the earned 96×96/10k city at actual
  1920×1080, native 1X (700 ms), after a ten-second warmup. It makes no bridge
  status requests during the sample. On Ryzen 5 2600, GTX 1660 Ti,
  approximately 16 GiB RAM, driver `32.0.16.2124`, mean throughput is 47.2 FPS,
  p95 frame time **37.3 ms**, p99 216.7 ms, and p95 simulation tick 219.2 ms.
  The **33.3 ms p95 gate fails**. Of 171.4 expected normal-speed ticks, 169
  complete in 120 seconds. No minimum-hardware acceptance or accumulating-
  backlog pass is claimed. Raw rows/result:
  `build-windows/m56-performance/20261114-085435-frames.csv` and
  `20261114-085435-16c9d0f3/performance-result.json` in that directory.
  Hardware JSON files and `m56-performance.log` sit in `build-windows/`.
- During qualification the user requested neighborhood outpatient clinics and
  a central hospital. This is an explicitly requested gameplay change, described
  in [HEALTHCARE.md](HEALTHCARE.md). The performance baseline above predates
  that change. The full **58-case rendered suite passes in one invocation**
  (`m56-full-rendered.log`, 404.4 seconds) on clinic executable
  `c7315b6aef66381f268f9492c1401e72f9b540f6c8c1dc9f8a4eaea5591fd9fb`.
- Fixed realtime schedule drift by advancing the previous deadline rather than
  resetting it to a late frame. Paused/modal wall time is discarded and catchup
  remains bounded to one simulation tick per frame. Three unit regressions cover
  cadence, pause/catchup and uint32 millisecond rollover.
- The phase-corrected clinic build reports **171 completed ticks vs 171.4
  expected** in 120 seconds, with maximum scheduler lateness 28 ms. Mean
  throughput is 47.4 FPS, p95 frame time **37.15 ms**, p99 211.9 ms and p95 tick
  time 213.2 ms. Scheduling cadence improves, but the frame-time gate still
  **fails**. Evidence: `build-windows/m56-performance-final/20261114-093005-frames.csv`
  and `20261114-093005-42cd2e43/performance-result.json` under that root.
  Executable SHA-256 is
  `26cf954982f0a8756475f2a9450aa168f037b2ff601cc51cd6d88111e4842936`.
- Protected clinic-city corruption/backup/autosave/restart acceptance passes in
  `build-windows/m56-clinic-recovery/20261114-092835-33657d42/` and
  `20261114-092929-a3c12140/`. Named-save/browser, exact backup selection,
  overwrite/unsaved-load confirmation, map-size selection and realtime modal
  isolation pass in `build-windows/m56-clinic-browser/20261114-092933-79043f7e/`.
  These runs use the phase-corrected executable above.
- Final marker-only build also fixes the top-down generic medical cross on
  nonmedical services. Strict Release, **772 C++ tests**, seven Python tests,
  nine packaging tests and all **40 graphics probes** pass. Final healthcare
  paid continuation, subtype save/load and ten-tick native/CLI exact continuation
  pass; [healthcare evidence](HEALTHCARE.md) records the final hash. The full
  58-case run above predates only scheduling and marker changes; final targeted
  regressions and realtime probes retain their own logs/identities.
- A real static-runtime local review package is staged as
  `build-windows/UrbanSimCore-0.1.0-m56-outpatient.1-windows-x64.zip` (SHA-256
  `d46f2e04291e92e9397e02ece9f3ccd86e1312800726624478a2e1efd72b82ca`).
  PE audit and packaged launcher `--verify-package` pass. It is explicitly a
  dirty-source internal review candidate; public distribution remains false.
  No clean-consumer-PC, Steam or two-hour packaged-soak pass is claimed.

Fresh hosted CI/leak qualification, the measured performance gate and the three
two-hour packaged sessions remain open. Logs are local ignored build artifacts
and must be archived before cleaning the build directory. The November 13 Mac
results below retain their own candidate identity and sanitizer scope.

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
