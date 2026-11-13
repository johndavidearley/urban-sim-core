# M56 — Save protection, reliability and performance

Status: **in progress** (November 13, 2026). M55's Windows display acceptance
remains open; this work starts the independent save-protection portion of M56.
No Windows performance or packaged-build soak result is claimed.

## Player saves and recovery

Normal play now saves under the OS user-data directory, rather than the process
working directory:

- Windows: `%LOCALAPPDATA%/UrbanSimCore/Saves`
- macOS: `~/Library/Application Support/UrbanSimCore/Saves`
- Linux: `$XDG_DATA_HOME/urban-sim/saves`, otherwise `~/.local/share/urban-sim/saves`

F5 writes the active named city (initially `City`); F9 validates it and tries
its three retained backups on failure. The start screen's Quick Load starts
with `City`; use Saved Cities to select another name or a particular backup. Recovery is reported
in the game. A separate `Autosave` slot is written every 200 completed simulation
ticks, retaining its own three previous generations. If no manual generation can
load, Load tries the autosave and its backups. A successful autosave does not
clear the unsaved-manual-progress flag or satisfy the guide's manual-save step.
A legacy working-directory city can still be loaded when no managed City or
Autosave slot exists; F5 then publishes it into the user-data store.

**F11** or the **Saves** button opens the player save browser. It lists named
cities, the separate Autosave slot, and up to three backups per slot, including
unavailable generations. Rows show tick and map size, with seven rows per page.
Select a row and Load Selected to load that exact generation. Unavailable rows
stay visible and cannot replace the current city. Loading over unsaved progress
requires a second click; replacing an existing named save does too.

Click the name field or Clear to type a new name; Backspace edits it. Names use
up to 48 ASCII letters, digits, spaces, underscores and hyphens, without leading
or trailing spaces. Autosave is reserved, as are Windows device names. Case-only
variants match existing browser names instead of creating another slot. F5 and
save-and-quit continue using the named city selected or saved in the browser.
The browser captures keyboard/mouse input and pauses automatic ticking and
autosaves until closed; Escape closes it without saving.

The start screen also has **Saved Cities / F11**, with load-only controls.
It constructs the destination map from the chosen city, allowing named 32×32
and 96×96 cities to coexist. In-game loading requires the running map size;
for another map size, save/quit and use the start-screen browser.

Every save writes an immutable generation containing a gameplay session and its
core city snapshot. It flushes both files, validates the pair through the normal
loader, then atomically replaces the slot manifest. The core filename is relative
to its session, so moving the complete store preserves references. Old generations
remain reachable through backup manifests. Damaged primaries cannot displace a
valid backup. Successful commits reclaim unreferenced generation directories;
failed cleanup does not turn a successful save into a failed publication.

On POSIX, files and containing directories are synchronized before publication;
on Windows, file flushes and `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` publish
the manifest. This protects interrupted application writes, but does not promise
recovery from failing storage hardware. Writes are serialized within one process;
simultaneous writers from separate game processes are not supported by this slice.

The raw import/export API and default MCP fixture path remain unchanged.
`URBAN_SIM_PROTECTED_SAVES=1` opts a playtest process into the production store in
its isolated `player-saves/` directory, permitting native recovery acceptance
without touching the user's real save folder.

## November 12 save-store foundation evidence

- Strict local Release build and all **762 C++ tests** pass, including nine
  new save-store cases. Existing version-0 migration, future-schema rejection
  and legacy/default-field tests are included. Seven Python unit tests pass.
  Validation preserves the live entity allocator; inspecting older backups
  cannot rewind it and cause subsequent construction to reuse an existing ID.
- Native production-store acceptance passes on the earned M55 10k city. The
  final process resumes at tick 17,312 with 10,271 residents. Truncated primary
  city data recovers from backup; damaging the remaining manual session recovers
  from Autosave. Save-and-quit exits normally, and a new process loads/resumes
  after relocating the complete store. Corrupting every slot manifest fails
  clearly without changing the live tick, funds, population or building count.
- Native inputs, fault list, exact runner, result and screenshots are retained in
  `playtest-artifacts/m56-qualified-save-acceptance/20261112-153628-8ea21393/` and
  `20261112-153711-ddf58681/`. Both processes report zero Vulkan validation errors.
  The final invalid-load screenshot was visually inspected: the complete short
  message fits and the earned city remains visible.
- Candidate SHA-256:
  `4261a349743d92e77b4026348aa0b4be2fa852fab2d83efe6318dbf0a1291a09`.
  Build identity, strict-build log and test logs are under
  `playtest-artifacts/m56-validation/`.
- Three targeted native regressions pass on this candidate: M55 guide/settings,
  pipe placement/backups/continuation, and legacy import/crash recovery. Logs:
  `playtest-artifacts/m56-validation/native-regressions.log`. The full 57-case
  rendered suite was not repeated for M56; its earlier M55 evidence retains
  that candidate's scope.
- Fault tests simulate interrupted/incomplete writes and publication failure;
  actual power-loss, platform packaging and long-session qualification remain
  outside this local slice. There is no minimum-Windows-machine performance
  result or current hosted CI/sanitizer result yet.

The save browser/custom naming slice is implemented; its new candidate evidence
is recorded below. M56 remains open for candidate migration/packaging evidence
(policy: [SAVE_COMPATIBILITY.md](SAVE_COMPATIBILITY.md)),
current hosted CI/sanitizers, measured minimum-Windows-machine
1080p performance, and three two-hour packaged-build sessions. The full release
gate is in [EARLY_ACCESS.md](EARLY_ACCESS.md).

## Reproduce native save recovery

Build the playtest executable and use an earned checkpoint with its companion
city snapshot. For example, the M55 sustainable-ten-thousand checkpoint:

```sh
PYTHONPATH=playtest VK_DRIVER_FILES=/opt/local/share/vulkan/icd.d/MoltenVK_icd.json \
  .venv/bin/python playtest/examples/m56_save_recovery.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --session playtest-artifacts/m55-journey-acceptance/20261112-141703-c828a453/sustainable-ten-thousand.json
```

The runner enables the protected store only in its isolated process directories.
It loads the earned city unchanged, writes two native quicksaves, advances 201
ticks to create an autosave, damages each manual generation, and verifies native
backup/autosave recovery. It then saves/quits, relocates the complete managed
store into a fresh process, loads/resumes, damages all slot manifests, and checks
that a failed load preserves tick, funds, population and buildings. Screenshots,
inputs, file damage and result/build hash remain under `playtest-artifacts/`.
The scenario recording alone does not reproduce filesystem fault injection;
use the runner and retained store files for that check.

## November 13 named save browser evidence

The named-save/browser candidate passes the strict Release build, all **764 C++
tests**, and seven Python unit tests. The two new store tests cover validated
listing of named/backup/unavailable rows without changing the live entity
allocator, and portable Windows-safe names.

Native browser acceptance passes in
`playtest-artifacts/m56-browser-final/20261113-084525-7a8c9ea2/` (result, exact
runner and inputs), with five process directories listed in
`save-browser-result.json`. Both the 32×32 city and the earned 96×96/10k city
load from the start-screen browser and continue saving to their selected names.
The 1280×720 and 800×640 captures and start-screen capture were visually inspected;
columns align and the complete controls fit. All five processes report zero
Vulkan validation errors and stop cleanly. This does not establish Windows DPI
or full 1080-high desktop validation.

The save-protection runner also passes on this candidate in
`playtest-artifacts/m56-browser-recovery/20261113-084711-37bd4759/` and
`20261113-084758-f58c6a68/`. It explicitly selects Autosave in the browser before
exercising corrupted-primary/backup recovery, actual save-and-quit/restart,
relocation, and preservation of the live city when every slot manifest is bad.
The resumed city has 10,271 residents at tick 17,312.

Candidate SHA-256:
`06d26a29a503207c48b86020d86f1eacbc970ff494668390c4d4f6fefab10669`.
Build identity, test logs and `verification-summary.json` are under
`playtest-artifacts/m56-browser-validation/`. Three focused native regressions
also pass on this candidate: M55 guide/settings, pipe continuation and legacy
import/crash recovery. The subsequent local qualification results are tracked in
[M56_QUALIFICATION.md](M56_QUALIFICATION.md); hosted CI remains a separate gate.
The November 12 results above retain their original candidate scope.

Reproduce the browser
check (optional `--session` exercises the earned 96×96 city alongside named
32×32 saves):

```sh
PYTHONPATH=playtest VK_DRIVER_FILES=/opt/local/share/vulkan/icd.d/MoltenVK_icd.json \
  .venv/bin/python playtest/examples/m56_save_browser.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --session playtest-artifacts/m55-journey-acceptance/20261112-141703-c828a453/sustainable-ten-thousand.json
```

The check covers native name entry/backspace, reserved names, active-name
quicksave, independent named slots, overwrite confirmation, unsaved-load
confirmation, explicit backup selection, unavailable rows, pagination, 1280×720
and 800×640 captures, start-screen selection of both map sizes, and realtime
input/ticking isolation. It archives its exact runner and executable hash.
Fault injection is retained in the runner, not reproduced by scenario input
recordings alone. MCP text injection uses the same character-event handler as
GLFW native typing; expanded text batches are bounded and reject atomically.

## November 13 full-suite and sanitizer qualification

The same native candidate passes the **full 57-case rendered suite in one
invocation**, with 113 retained process directories and zero Vulkan validation
errors. Both headless Debug sanitizer builds pass **762/762 tests**:
ASan/UBSan with halt-on-error, and ThreadSanitizer with halt-on-error. No test
failures or sanitizer findings required gameplay source changes. The two GLFW
key-translation tests remain in the 764-case strict build, outside the headless
sanitizer builds. ASan leak detection was disabled on this Mac; no leak-check
pass is claimed.

Full configuration, linked runtimes, source/executable hashes, test logs and the
verification summary are retained in `playtest-artifacts/m56-qualification/`.
See [M56_QUALIFICATION.md](M56_QUALIFICATION.md) for commands and scope. Hosted CI
on a published candidate revision, Windows/platform qualification, leak
qualification, measured minimum-Windows-machine performance and the packaged
soaks remain open. This closes the local full-suite/sanitizer work, not M56.
