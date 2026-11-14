# M55 — Player journey and onboarding

Status: **verified locally on Windows** (November 14, 2026). Player-journey,
display/DPI and restart acceptance pass. This is not Steam release approval;
M56 reliability/performance and M57 consumer packaging remain separate gates.

## Implemented

- The city guide checks actual zoned-parcel power, water and sewage connections,
  source facilities and waste collection. It starts time and reviews the budget
  before recommending further service spending, and waits for residents before
  advancing. A placed water source or earned population unlock cannot bypass
  missing pipes or failed drainage.
- Guide buttons select the relevant construction tool, open the budget, save
  the city, or open help. Water shortages and sewage backups return to actionable
  recovery steps. Completion also requires a real manual save.
- F10 and the Settings button open a modal display/controls panel. Windowed and
  borderless desktop modes, three window-size presets, readable/compact text,
  defaults and saved preferences are available. Pending edits apply explicitly;
  gameplay input and automatic ticking wait while the panel is open.
- The controls guide explains the basic city loop, utilities, shortage recovery,
  camera controls, budgets and saves. Preferences live separately from city saves
  in the OS application-config directory; playtests use isolated run directories.
- A parcel sewer lateral drops into a trunk buried two metres below terrain.
  This allows an adjacent, slightly lower parcel to drain into its sewer without
  relaxing pipe-to-pipe gravity or creating pressure along an uphill trunk.

No audio system is shipped by this slice, so there are no audio volume controls.

## Launch-map journey

The provisional starter is a **96×96 flat map**, terrain seed 42, simulation
seed 98765. `playtest/examples/m55_native_journey.py` records native construction,
policy, time, and save/load actions using normal starting money and earned
catalog unlocks. Read-only inspections locate controls and reserved service sites.

The runner follows the M54 itinerary with paid combined pipes under streets,
a powered lift for the initial uphill collector, and sanitation sites on the
south/downhill side of later streets. Zoning skips actual reserved service sites.
It removes and restores the initial lift to exercise recovery. The original M54
runner and balance report remain unchanged.

The M55 gate is 10,000 residents, nonnegative operating flow for 100 consecutive
ticks, continued survival, and save/load continuation. Historical M54 money-curve
bounds and unique-building counts are not M55 acceptance requirements.

```sh
PYTHONPATH=playtest VK_DRIVER_FILES=/opt/local/share/vulkan/icd.d/MoltenVK_icd.json \
  .venv/bin/python playtest/examples/m55_native_journey.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --artifacts playtest-artifacts/m55-journey
```

Use `--opening-only` for the initial fixed stage, including any normal-money
purchase waits. This is not a claim that the stage ends at exactly tick 2,400.
The pilot ended at tick 3,000 with 361 residents, $95,528, no debt, and revenue
241 / expenses 182. Evidence:
`playtest-artifacts/m55-journey/20261112-135140-156d2eb9/`.

The first full probe is retained under
`playtest-artifacts/m55-journey-full/20261112-135302-6568f613/`. It was stopped
intentionally after exposing repeated per-parcel sewer diagnostic searches.
Recent 100-tick batches took about 40 seconds around 3,500 residents. This is
an incomplete performance probe, not a passed journey.

Sewer reachability now qualifies each powered lift once per outlet and expands
its gravity catchment backward. Actual residual-capacity flow routing is
unchanged. A regression compares the shared reach result with actual pressure
routing for every parcel in a chained-lift test map.

The fresh acceptance run is
`playtest-artifacts/m55-journey-acceptance/20261112-141703-c828a453/`. Its
`build-identity.json` records executable and runner SHA-256 values. It begins
from an empty city, omits optional landmark purchases, and retains native inputs,
paid construction decisions, checkpoints, samples and `money.csv`.
`opening-parity.json` verifies the first 3,000 ticks' sampled population, funds,
revenue, expenses, buildings and utility supply exactly match the pre-optimization
opening. `trajectory-parity.json` also records exact sampled parity through
10,710 ticks after the sewer diagnostic optimization.

The full journey **passes locally**. It reached 10,036 residents at tick 17,010
with $3,870,100 and no debt. Ticks 17,011–17,110 all had nonnegative operating
cash flow; the smallest surplus was $518. The sustainable checkpoint has
10,066 residents, and the final continuation has 10,207 at tick 17,210.
Power and water supply remained 100% at these checkpoints. The recovery trace
records sewage backlog rising to 3,170.89 with the lift removed and returning
to zero after rebuilding it. Native reload and CLI continuation match the
uninterrupted city exactly for 100 ticks (`continuation.json`).

A separate native save-and-quit check exited normally, launched a new process,
loaded the saved city and advanced it to tick 17,112 with 10,067 residents.
Evidence: `playtest-artifacts/m55-restart/20261112-143459-a75d5946/restart-result.json`
and the resumed screenshot in `20261112-143500-3c6bbb11/captures/`.
Use `playtest/examples/m55_restart_check.py --session <checkpoint>` to repeat it.
The accepted executable SHA-256 is
`b4070bf49c022f9b4c56433bf226d795a2d2cf1e43be8e8c84e22026edf59149`.
The run retains its exact executed `runner.py`, build identity, ordinary-money
construction decisions, native scenario, saves and per-tick budget history.

## Verification and remaining gates

- Strict local Release and all 753 C++ tests pass, including the final guide
  sequence and the parcel-lateral/trunk-gravity distinction.
- Seven Python unit tests pass.
- All 57 rendered cases have local passing evidence: 54 passed the full rerun,
  then the airport unlock, migration guide and river-treatment cases passed
  targeted rechecks after their fixture updates. This is aggregate evidence,
  not a single all-green full invocation. The candidate executable is unchanged.
  Summary: `playtest-artifacts/m55-rendered-updated/verification-summary.json`;
  logs and per-process actions are retained in that directory,
  `playtest-artifacts/m55-rendered-final-fixes/` and
  `playtest-artifacts/m55-treatment-acceptance/`.
- Legacy feature fixtures now explicitly pay for pipes instead of assuming
  road-only water coverage. Migration verifies that unserved outer parcels still
  trigger power recovery after population unlocks. River treatment exercises
  real upstream sewage discharge, 98% removal of new contaminant, persistence
  of existing river mass, unfunded-outlet shutdown and exact continuation.
- The final native guide/settings test passes in
  `playtest-artifacts/m55-ui-final/20261112-141041-ebf04a86/`. It covers
  actionable guide steps, resident arrival, budget-before-spending, manual
  saving, input isolation, preference persistence and desktop-limited sizing.
- The requested 1920×1080 window was constrained by macOS to 1920×779 logical
  pixels with a 3840×1558 framebuffer. The actual dimensions are retained in
  `display-validation.json`; this is **not** an actual 1080-high layout pass.
- The earned 10k city was also captured at 1280×720 in both HUD text modes,
  with settings, controls, budget and borderless transitions in
  `playtest-artifacts/m55-display/20261112-144726-e5a77556/`. Settings, controls
  and budget captures were visually inspected locally. All 13 captures report
  zero Vulkan validation errors. The strict size result correctly remains false:
  the desktop constrained the second preset to 1920×777; borderless was 1440×900.
- Actual 1920×1080 and Windows 150% display-scaling readability remain open.
- The 10k checkpoint, 100-tick operating window, exact continuation and actual
  save/quit/relaunch/load/resume check pass locally.

The historical display gaps above are closed by the November 14 evidence below. M56 reliability
soaks/performance and M57 consumer Steam packaging remain separate gates.

## Display acceptance on the target desktop

### November 14 Windows baseline

Native MSVC x64 Release display capture reaches exact 1280×720 and
1920×1080 client/framebuffer sizes. Borderless mode reaches 2560×1440.
All 13 captures report zero Vulkan validation errors. Evidence is retained in
`build-windows/m55-display-baseline/20261114-082706-ee91ee8d/`, including
`display-result.json`, screenshots and recorded inputs. The executable SHA-256
is `6800557d0568e3a4caa60299024c2934ddfde80439e73f785b30d9763de347e9`.

The baseline game window reports **96 DPI**, so this is 100% scaling evidence; the
requested 150% check correctly fails. Settings, controls, budgets, both HUD text
modes and borderless captures were visually reviewed. Modal panels and their
controls fit at both requested resolutions. Review found that Transit/Alerts
buttons obscure service statistics in the detailed HUD; the final build moves
those readouts into unused space above the income totals.

This first capture uses the checkout's small, 32-resident saved city, not the
historical earned 10k checkpoint, which is absent from this checkout. The fresh
native Windows journey and final earned-city captures below supersede this
baseline for acceptance.

### November 14 Windows acceptance

The fresh, normal-money native Windows journey passes in
`build-windows/m55-native-journey/20261114-082811-a7c373a4/`. It reaches
10,036 residents at tick 17,010 with $3,870,100 and no debt. All 100 operating
ticks from 17,011 through 17,110 have nonnegative cash flow; minimum surplus is
$518. The sustainable checkpoint has 10,066 residents and full power/water
supply. Sewage failure/recovery is exercised. Native reload and CLI continuation
match the uninterrupted city exactly for 100 ticks. The final city has 10,207
residents at tick 17,210. `build-identity.json`, `runner.py`, `money.csv`,
`decisions.json`, checkpoints, `continuation.json` and `result.json` retain the
inputs, source/build identity and results. This run uses the baseline executable
hash above; simulation code is unchanged by the subsequent HUD-only repair.

Final corrected playtest executable SHA-256:
`02fa8cc11a605641b3a747be956c5cc467af7c693cd4d023cf479ad9fab742c8`.
Strict MSVC x64 Release and all **764 C++ tests** pass. The two targeted native
guide/settings regressions pass at 150% desktop scaling, including preference
persistence and paused input isolation (`build-windows/m55-guide-final.log`).

After the user selected 150% desktop scaling, final display acceptance on the
earned 10k city passes in
`build-windows/m55-display-final/20261114-084714-c6dde338/`. Every one of the
13 captures reports **144 DPI through GetDpiForWindow**, exact 1280×720 and
1920×1080 client/framebuffer dimensions, or 2560×1440 for borderless mode.
All report zero Vulkan validation errors. Agent visual review of all 13 PNGs
finds readable settings/controls/budget panels, both text modes, usable controls
and no recurrence of the obscured service statistics. This records an agent
review of actual captures, not an invented human sign-off or outside-player test.

Native save-and-quit/relaunch/load/resume also passes on the corrected build:
`build-windows/m55-restart-final/20261114-084730-019f736e/restart-result.json`.
The first process exits with code 0; the new process preserves saved funds/tick
and advances to tick 17,112 with 10,067 residents. The unchanged checkpoint
SHA-256 is `3b6a5a754bea40f02aaefe7084efbea19a81318365c3d5aa43469e90e37d0708`.

M55 is locally verified on this Windows desktop. Hosted CI, the separately
observed Python 3.11 cancellation-unit-test failure, minimum-machine FPS,
packaged soaks, SDK-free consumer/Steam acceptance and outside-player testing
remain under M56–M58; none is claimed by this sign-off. Local evidence lives
under the ignored build directory and must be archived before cleaning builds.

`playtest/examples/m55_display_check.py` records the actual window/framebuffer
sizes, executable hash, Vulkan validation count, and screenshots of settings,
controls, compact/readable city HUDs, budgets and borderless mode. The default
run fails if either requested resolution is constrained. On Windows,
`--windows-150` additionally requires the game's own visible window to report
144 DPI through `GetDpiForWindow`; it does not infer scaling from a command-line
label or Python's DPI. Run on the intended monitor with Windows desktop scaling
set to 150%, and enough desktop space for a 1920×1080 client window.

```powershell
$env:PYTHONPATH = 'playtest'
python playtest/examples/m55_display_check.py `
  --executable build-ci/bin/Release/UrbanSimCore-playtest.exe `
  --session path/to/sustainable-ten-thousand.json --windows-150
```

The optional checkpoint must include its companion city snapshot. Omit
`--session` to capture an empty city. Review the retained PNGs for readable text,
clipping, overlap and usable controls before recording a human sign-off. Passing
size/DPI assertions alone does not establish readability. Use
`--allow-constrained` only for diagnostic capture; `display-result.json` still
reports failed resolution/DPI requirements and never closes the gate.
