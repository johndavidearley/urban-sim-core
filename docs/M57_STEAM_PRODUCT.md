# M57 — Windows consumer candidate and Steam preparation

Status: **in progress** (November 13, 2026). This slice prepares the Windows
build/package pipeline and offline Steam configuration. No Windows build,
consumer-PC acceptance, Steam login/upload, store submission or publication has
been performed from this macOS workspace. M55/M56 platform and reliability
requirements remain open.

## Implemented engineering slice

- `URBAN_SIM_WINDOWS_RELEASE=ON` requires native MSVC x64. It selects a static
  MSVC runtime and static GLFW, using the pinned source dependencies so their
  notices can be collected. Normal Mac/Linux/developer builds retain their
  existing defaults.
- A small Windows `UrbanSimCore.exe` bootstrap launches
  `bin/UrbanSimCore-visualizer.exe` and waits for it, returning its exit code.
  It has no Vulkan/GLFW link dependency, so a missing system Vulkan loader can
  produce a driver-install message before the renderer's imports execute.
  Missing game files and launch failures also produce visible messages.
  `--verify-package` checks child-file presence without a GUI or GPU check;
  it is **not** a launch/gameplay acceptance test.
- `scripts/package_windows.py` stages only the bootstrap, renderer, notices,
  save policy and README. Shaders/font data are already embedded. The CLI,
  playtest executable, Python, shader compiler and SDK are not copied.
- The packager reads normal and RVA-based delay imports from x64 PE files.
  Unapproved DLL dependencies, including shared/debug CRT and shared GLFW,
  fail the audit. Only the renderer may import the system `vulkan-1.dll`.
  The Windows graphics driver/runtime remains a consumer prerequisite.
- Each candidate gets a versioned directory/ZIP and a SHA-256 file manifest,
  source revision, dirty flag, audited import list and runtime requirements.
  Existing outputs are never overwritten. Dirty source requires an explicit
  `--allow-dirty` local-review override; it does not approve distribution.
- GLFW, nlohmann/json and embedded Roboto Condensed notices are copied from
  the build's source dependencies. `THIRD_PARTY_NOTICES.txt` maps the files.
  A publisher-supplied project notice may be passed with `--project-notice`.
  Without it, the package is explicitly marked internal evaluation only.
  `public_distribution_approved` stays false: tooling does not certify rights
  or release readiness.
- `scripts/prepare_steam_depot.py` verifies exact package contents, hashes and
  actual PE dependencies again, then stages `content/`, AppBuild/DepotBuild VDFs,
  and a manual launch-settings checklist for assigned AppID/DepotID values.
  The scripts use `Preview=1`, contain no `SetLive`, and never invoke SteamCMD.
- `.github/workflows/windows-candidate.yml` is a manual Windows build workflow:
  build-only SDK setup, Release compile/unit tests, package audit, launcher
  structure check, optional offline Steam preview, and internal artifact upload.
  It does not upload content to Steam or modify store/branch settings. This
  workflow is prepared but has not been run on the current uncommitted source.

## Reproduce on Windows

Use the Vulkan SDK and MSVC x64 on the **build machine**. They are build tools,
not files to place in the consumer depot. From a Developer PowerShell checkout:

```powershell
cmake -S . -B build-windows -A x64 `
  -DURBAN_SIM_WINDOWS_RELEASE=ON -DURBAN_SIM_WARNINGS_AS_ERRORS=ON
cmake --build build-windows --config Release --parallel 2 `
  --target UrbanSimCore UrbanSimCore-visualizer test_core
ctest --test-dir build-windows -C Release --parallel 4 --output-on-failure
python scripts/package_windows.py --build build-windows `
  --output build-windows/UrbanSimCore-0.1.0-candidate.1-windows-x64 `
  --version 0.1.0-candidate.1
```

The default requires a clean source checkout. `--allow-dirty` is reserved for
local review candidates. Do not treat synthetic packaging-test PE files as game
executables or consumer acceptance evidence.

After a genuine package is produced, prepare Steam preview scripts with the
publisher's actual assigned IDs (replace the placeholders):

```powershell
python scripts/prepare_steam_depot.py `
  --package build-windows/UrbanSimCore-0.1.0-candidate.1-windows-x64 `
  --output build-windows/steam-preview `
  --app-id YOUR_APP_ID --depot-id YOUR_WINDOWS_DEPOT_ID
```

`launch-settings.json` is a checklist for the Steamworks Installation tab, not
an API submission. Select Windows/64-bit and `UrbanSimCore.exe` at the depot root,
with no required arguments. Publisher review, the SDK's authenticated preview
build and consumer Steam testing are subsequent explicit actions.

Valve documents `Preview` as producing logs/manifests without uploading, and
`SetLive` as a branch assignment. ContentRoot/FileMapping define depot layout.
See [Valve's SteamPipe documentation](https://partner.steamgames.com/doc/sdk/uploading).
The system loader strategy follows the loader/driver model described by
[Khronos](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderApplicationInterface.md).

## Verification

- Nine packaging tests pass locally, covering x64/malformed PE rejection,
  normal/delay-import audits, missing notices, dirty/debug/sanitizer rejection,
  versioned archives, checksums, developer-file exclusion, forged dependency
  claims, unsafe versions/IDs and preservation of existing outputs.
- Default macOS strict Release build and all **764 C++ tests** pass. Seven MCP
  Python unit tests pass. Both workflow files pass local YAML syntax parsing.
- The consumer CMake profile correctly rejects this non-Windows host instead of
  labeling a Mac executable as a Windows candidate.
- Logs, source hashes and `verification-summary.json` are retained under
  `playtest-artifacts/m57-packaging/`. This is a dirty source checkout; the
  Windows workflow has not been run on a published candidate revision.
- The new Windows bootstrap is source-prepared, not Windows-compiled or
  executed here. No real Windows archive or Steam deployment is claimed.
The PE tests use synthetic header fixtures to exercise the audit and staging
logic; they are not Windows compilation or launch evidence.

## Remaining engineering and publisher gates

1. Build the real Windows candidate, retain its workflow/source/artifact identity,
   and inspect every audited dependency. Resolve Windows compiler/link failures
   or missing notices before accepting it.
2. On a clean standard-user consumer PC with a supported GPU driver and no SDK,
   compiler, Python or checkout, extract/install and launch the candidate.
   Verify clear missing-driver behavior, normal gameplay, named saves/autosaves,
   quit/relaunch, and that install-folder read-only permissions do not block saves.
3. Repeat through Steam: correct assigned app/depot IDs, preview mapping and
   manual launch settings, install/update/relaunch, and retained save recovery.
   Keep M56's minimum-hardware and packaged-soak evidence separate.
4. Supply project ownership/distribution terms and confirm dependency/asset rights.
   The pending project notice is an internal-candidate marker, not an approved EULA.
5. Complete publisher Steamworks onboarding, app/pricing/surveys/disclosures,
   store/library art, real gameplay screenshots, support contact and Early Access
   text. Review implemented scope and known limits against the current build.
6. Obtain store approval and record Coming Soon/fee eligibility dates in the
   account. No dates, approval status or launch promises are invented here.

M57's checklist stays open until real Windows/Steam consumer evidence and the
publisher requirements are satisfied. See [EARLY_ACCESS.md](EARLY_ACCESS.md).
