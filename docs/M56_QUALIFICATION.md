# M56 — Local candidate qualification

## November 18 macOS moving-activity diagnostic

The user-requested visual activity pass adds bounded cars, freight trucks and
sidewalk pedestrians without changing simulation rules or persistence.
Strict Release, all 780 C++ tests, ten Python tests and two targeted native
camera/picking and pipe save/CLI-continuation cases pass. The paced native
10k-city walkthrough verifies changed positions while running, exact pause,
inspection, construction and reload. Orthographic, perspective and top-down
captures were visually reviewed. Behavior and scope:
[moving activity](RENDERING_3D.md#moving-city-activity).
A synthetic same-tick reload regression also clears/restores pedestrians when
loading unoccupied/inhabited copies with unchanged roads and building count.
The qualified executable invalidates its activity cache on successful load.

A quiet, unprofiled 120-second native 1X run uses the earned 96×96/10k checkpoint,
a ten-second warmup, no bridge polling during measurement, and 1280×720 logical /
2560×1440 framebuffer pixels. No compilation or tests overlap the timed interval.

| Metric | Result |
| --- | ---: |
| Mean throughput | 50.71 FPS |
| p95 frame | 17.22 ms |
| p99 frame | 145.37 ms |
| Mean simulation tick | 133.91 ms |
| p95 simulation tick | 141.86 ms |
| Completed / expected ticks | 171 / 171.43 |
| Vulkan validation errors | 0 |

This single local diagnostic passes the probe's 33.3 ms p95 frame check. It is
not a controlled before/after comparison or a Windows minimum-hardware pass.
M56 remains open for its Windows 1080p, hosted CI/leak and packaged-soak gates.
The initial Nov 18 activity candidate had spatial sampling concentrated on
northern roads; the qualified candidate uses stable spatial sampling across the
city. Earlier exploratory traces retain their separate candidate identities.

Evidence: `playtest-artifacts/city-activity-qualification/verification-summary.json`,
`playtest-artifacts/organic-walkthrough/20261118-102408-e44c09ac/` and
`playtest-artifacts/city-activity-performance/20261118-102628-f7450da0/`, with its
adjacent frame CSV. Executable SHA-256:
`c395325361d5563775c81aa31ba603d0ed3a76bca376ca2ac5275c1085135b72`.
Logs and changed-source hashes are retained in the qualification directory.

## November 16 macOS connector-allocation follow-up

CPU sampling of the optimized earned 96×96/10k city still identifies utility
routing allocation overhead. Connector lookups now return fixed storage for the
parcel and its four neighbours instead of allocating a vector. Connector order,
search rules and flow allocation are unchanged.

One matching pair of 120-second native 1X probes uses the same checkpoint and
1280×720 logical / 2560×1440 framebuffer resolution. Each probe has a ten-second
warmup and a separate ten-second CPU sample before the timed interval; neither
timed interval overlaps compilation or test runs.

| Metric | Before | After |
| --- | ---: | ---: |
| Mean simulation tick | 150.71 ms | 143.33 ms |
| Median simulation tick | 150.17 ms | 142.91 ms |
| p95 simulation tick | 155.04 ms | 149.77 ms |
| p95 frame | 17.28 ms | 20.25 ms |
| p99 frame | 161.12 ms | 154.26 ms |
| Mean throughput | 49.38 FPS | 49.62 FPS |
| Completed / expected ticks | 172 / 171.43 | 171 / 171.43 |

Mean tick time improves 4.9%, p95 tick time 3.4%, and p99 frame time 4.3% in this
single comparison. P95 frame time worsens; no repeat-trial or universal frame-time
improvement is claimed. Both runs report zero Vulkan validation errors. Strict
Release and all 773 C++ tests pass. The entire city and session match the baseline
after 100 CLI ticks, apart from the output snapshot filename.
All three targeted native pipe, sewer-lift and water-booster regressions pass
(7.75 seconds), including power loss, repair and exact save continuation;
`rendered.log` and the `rendered/` directory retain their evidence.

Evidence is under `playtest-artifacts/m56-next-profile/`: baseline run
`20261116-083701-b065d432`, changed run `20261116-084346-56c8dbd0`, adjacent frame
CSVs, CPU samples, continuation outputs, `strict-tests.log` and
`verification-summary.json`. Baseline executable SHA-256:
`7d5dc5c100d9934a3165e8e7da301121bf9e1237214719a3fcc12fc93ab0720b`;
changed executable:
`7c0b89b21a23fe21dcbffa616bb9e53bc69b3456e2e2792946ffa445b8feecd9`.
The initial sandbox-blocked launch (`20261116-083620-87d3e9c5`) is excluded.
M56 remains in progress for Windows 1080p remeasurement, fresh hosted CI/leaks
and three two-hour packaged sessions. Earlier full-suite/sanitizer evidence below
belongs to its recorded candidate; those suites have not been repeated here.

## November 14 macOS performance follow-up

M56 remains **in progress**. CPU sampling of the earned 96×96/10k city
identified repeated pressure-route visited-state allocation as a simulation
hotspot. Water/sewer routing now uses an evaluation-local dense best-step
table, resetting only touched entries between searches. The table is capped at
1,048,576 entries (4 MiB); maps exceeding that bound retain the original sparse
search. Search order, pressure states, finite head and residual capacities are
unchanged. No routing data is cached across evaluations or ticks.

The performance probe now accepts `--resolution` for diagnostic comparisons
(1920×1080 remains the default), records window/framebuffer dimensions and
supports optional macOS `--cpu-profile` sampling before the timed measurement.
This Mac clamps a requested 1080p window to 777 pixels high, so the retained
comparison uses 1280×720 logical / 2560×1440 framebuffer pixels. It does **not**
qualify the minimum-Windows-machine 1080p gate.

Two unprofiled 120-second normal-speed runs start from the same sustainable
10k checkpoint, with a ten-second warmup and no status polling during the sample:

| Metric | Before | After |
| --- | ---: | ---: |
| Mean simulation tick | 184.42 ms | 160.21 ms |
| Median simulation tick | 182.86 ms | 154.37 ms |
| p95 simulation tick | 195.45 ms | 199.13 ms |
| p95 frame | 17.70 ms | 17.21 ms |
| p99 frame | 196.20 ms | 167.20 ms |
| Mean throughput | 46.43 FPS | 48.45 FPS |
| Completed / expected ticks | 171 / 171.43 | 171 / 171.43 |

Mean tick time improves 13.1% and p99 frame time improves 14.8% in this single
comparison; p95 tick time does not improve. Both runs end with 10,256 residents
and zero Vulkan validation errors. These are local diagnostic results, not
repeat-trial or Windows acceptance claims. The separate sampled run overlapped
compilation and is used only to identify CPU hotspots, not for timing comparison.

Evidence: `playtest-artifacts/m56-performance-mac-before/20261114-141528-04f2c346/`
and `playtest-artifacts/m56-performance-mac-after/20261114-142518-84852d22/`, with
their adjacent timestamped frame CSVs. The profile is under the before root at
`20261114-142051-8f9a3d47/cpu-profile.txt`. Baseline executable SHA-256:
`faf511042fa66e1290ccd1290056bd4e2dce0563408edffd18cbd0e99dc01ada`;
optimized executable:
`7d5dc5c100d9934a3165e8e7da301121bf9e1237214719a3fcc12fc93ab0720b`.

Strict Release and all **773 C++ tests** pass, including an exact dense/sparse
comparison with shared booster capacity, chained lifts and backlog. A 100-tick
CLI continuation matches the baseline's entire city and session exactly apart
from the output snapshot filename. All **58 rendered cases** pass in one
invocation (419.2 seconds). Seven
Python unit tests and all **771 headless ASan/UBSan cases** pass with no findings
(`detect_leaks=0`; the two GLFW cases are excluded). All **771 headless TSan
cases** also pass with no findings (`halt_on_error=1:history_size=7`). Logs,
continuation outputs, source/candidate hashes and `verification-summary.json`
are retained under `playtest-artifacts/m56-pressure-routing/`.
Hosted CI, Windows remeasurement, leak
qualification and three two-hour packaged sessions remain open.

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
remain open. M55's Windows display/DPI acceptance is verified as recorded above.
