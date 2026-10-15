# MCP desktop playtesting

The optional `UrbanSimCore-playtest` executable exposes the same Vulkan/GLFW
application through private process pipes. The Python `urban-sim-mcp` server
owns one game at a time and speaks MCP over stdio. Windows, Linux, and macOS use
the same game-side handlers. Normal GUI launches have no control endpoint.

## Build and connect

Install the graphics prerequisites described in `GRAPHICS.md`, then build:

```sh
cmake -S . -B build-playtest -DURBAN_SIM_WARNINGS_AS_ERRORS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-playtest --config Debug --parallel 4
python -m venv .venv
# POSIX: source .venv/bin/activate
# Windows PowerShell: .venv\Scripts\Activate.ps1
python -m pip install ./playtest
urban-sim-mcp --executable /absolute/path/to/UrbanSimCore-playtest --artifacts /absolute/path/to/playtest-artifacts
```

For a repository checkout, `python -m pip install -e ./playtest` keeps the
server in sync with source edits. After updating the package or executable,
restart the MCP client connection and launch a new game; running processes
keep their loaded code. MCP package 0.3.0 exposes inspection version 3
(population flow, neighborhood rates, freight, road classes and service/supply metrics). Check
`status.server_version` and `urban-sim://capabilities` to identify your server
and executable. The bridge protocol remains version 1.

Python 3.10+ is required. The package pins MCP SDK 1.26.0 and Pillow 11.3.0.
Python is not a CMake build dependency; `URBAN_SIM_BUILD_VISUALIZER=OFF` still
requires neither graphics dependencies nor Python. Windows multi-configuration
executables are under `bin/Debug` or `bin/Release`. `cmake --install` includes the executable and Python source package under
`share/urban-sim/playtest`; install that directory with pip when using an installed
layout. Use a Debug build with the
Khronos validation layer installed for renderer validation.

A generic stdio MCP client configuration:

```json
{
  "mcpServers": {
    "urban-sim": {
      "command": "/absolute/path/to/.venv/bin/urban-sim-mcp",
      "args": ["--executable", "/absolute/path/to/UrbanSimCore-playtest", "--artifacts", "/absolute/path/to/playtest-artifacts"]
    }
  }
}
```

On Windows use absolute paths to `.venv\\Scripts\\urban-sim-mcp.exe` and
`UrbanSimCore-playtest.exe`. The executable and server must run on the same OS:
a Windows game should use Windows Python, including when launched from WSL.
On Linux, pass the display-session environment (`DISPLAY`, or
`WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR`) through the MCP client to the server.
The client launches the server; launching the game is a separate `launch` tool.
The server never modifies client configuration automatically.

SDK copy-only Windows installations can expose layers with `VK_ADD_LAYER_PATH`
pointing to the SDK `Bin` directory. Explicit process-local `VK_LAYER_PATH`
is also supported. Stale third-party implicit-layer registrations can produce
loader errors; tests may set `VK_IMPLICIT_LAYER_PATH` to an empty directory for
an isolated renderer check. This is a test override, not a registry repair.

## Tools and coordinates

| Tool | Use |
|---|---|
| `launch`, `status`, `stop` | Own a fresh start-screen session, inspect lifecycle, save/discard and exit. |
| `input` | Ordered key, pointer, button, scroll, focus-loss and close events. |
| `window` | Native resize/minimize/restore/close; simulated focus loss. |
| `city` | New city (32/64/96), terrain, save/load, optional external save import. |
| `time` | Explicit 1–100 ticks, pause, speed, deterministic/realtime mode. |
| `tool` | Select/apply road, zoning, demolition, services, districts; district archetype/budget policies and deletion. |
| `view` | Nine overlays, both projections, camera, focus, HUD and gameplay toggles. |
| `inspect` | Summary/UI, <=512 tiles, or paginated entities/facilities/districts/routes/roads. |
| `coordinates` | Tile-center to screen and screen to tile under the active projection. |
| `screenshot` | Next completed Vulkan frame as PNG image content plus metadata. |
| `diagnostics` | Last 1–500 log lines and renderer validation/memory/frame data. |
| `record`, `replay`, `assert_state` | Retain scenarios, replay into fresh processes, assert inspection fields. |

Read `urban-sim://controls` for key names, overlay ordering and action sources.
`urban-sim://inspection` documents field units, weighting and tick timing.
`urban-sim://capabilities`, `urban-sim://state`, `urban-sim://artifacts`, and
`urban-sim://artifact/{relative}` expose read-only evidence. Artifact names in
the resource template are URL-encoded and restricted to the current run.
Prompts `exploratory_playtest` and `regression_investigation` provide workflows.

`inspect(section="summary")` includes `population_change` with `arrivals`,
`departures`, `deaths`, `net`, and `desirability`. These describe the **last
completed tick**, not the sum of a multi-tick step. Net change equals arrivals
minus departures minus deaths. Migration uses the previous completed tick's
conditions; edits to utilities take effect in those conditions after a tick.
Metrics also expose sewage, garbage, recycling, cemetery and crematorium
coverage, utility capacities/demand/supply ratios, and deathcare backlog.
Older saves initialize the new lagged inputs to defaults, then update them
on the first tick; their stored population target does not limit growth.

M21 adds `inspect(section="roads")`, with endpoint tiles, `road_class`,
`capacity`, combined `load`, truck-only `freight_load` and `congestion`.
`tool(action="apply", name="road", road_class="arterial", x1=..., y1=...,
x2=..., y2=...)` upgrades existing local segments or builds new arterials
through normal costs. A toggles class while the road tool is active; input
and semantic actions share the same plan/build path.

`state.metrics` exposes `freight_trucks`, `freight_routes`,
`freight_domestic_goods`, `freight_imported_goods`, `freight_exported_goods`,
`freight_unrouted_goods`, and `freight_gate` (`x`, `y`, `connected`). These
measure the last traffic pass, not a batch's summed ticks. Freight batches
32 goods per truck, preserves existing commuter loads, and uses the west
edge gate `(0, height/2)`. Connect it by road to route external trade.
Industrial-only cities export surplus; commercial-only cities import goods.

Facility inspection includes power source, MW capacity and emissions for
power plants, and `water_supply` capacity for water facilities.

Residential tile and entity inspection expose `crime_rate` and `illness_rate`
(0–1). HUD/migration use the population-weighted means; empty homes do not
weight them. Local service strength decreases with road distance within each
facility's coverage radius. Local read-outs persist in snapshots and affect
land value on its next refresh. The Happiness overlay includes both rates.

Input coordinates are **logical window pixels**, not framebuffer pixels.
Screenshot metadata includes both dimensions for DPI scaling. Placement tool
endpoints are tile coordinates. Use `inspect(section="ui")` for visible control
rectangles and `coordinates` instead of hardcoding positions or assuming a
28-pixel tile scale. Unsupported or off-screen coordinate conversions fail
clearly; valid tile centers can lie outside the current camera view.

For example, a click is a `down` and `up` with the same x/y; a drag is `down`,
`move`, `up`. Keys use names such as `Enter`, `R`, `F3`, `Num1`, `Period`, and
`Escape`. Scrolling includes x/y and delta so zoom anchors at the intended cursor.
These events traverse player handlers, but they are synthetic application
inputs: they do not test OS keyboard delivery or window-manager focus behavior.

Semantic `tool(action="apply", name="road", x1=4, y1=12, x2=10, y2=12)`
uses the same plan/build/refresh handler as a road drag, including costs and
placement errors. Zone, service and district names use the existing parsers.
City edits have no arbitrary state-write or cost-bypass facility. Traces label
input as `ui`, window operations as `window`, and other commands as `semantic`.

## Recording and evidence

Default deterministic sessions run simulation only through explicit ticks
(including the player step key while paused), with controlled animation time.
Realtime sessions retain normal wall-clock pause/speed behavior. Terrain and
simulation seeds default to 42 and 98765; custom seeds are unsigned 32-bit values.
The simulation seed offsets the existing playable tick seed and derived refresh
seed. Normal GUI simulation keeps its existing seed behavior.

Start `record(action="start")` immediately after `launch`, then create/load a
city, play, capture frames, and assert outcomes. Stop with a simple scenario name.
Assertions address dotted inspection fields, for example `state.tick eq 3`,
`state.funds ge 0`, or `items count 7`. Operators are `eq`, `count`, `lt`, `le`,
`gt`, `ge`; assertions never evaluate Python or C++ code.

`replay(path=..., repeats=2)` replays twice into fresh processes and compares a
canonical hash of the final saved gameplay session and city snapshot. State
comparison requires deterministic mode. Rendered pixel parity across GPUs is
not assumed. Scenarios retain configuration/build metadata, ordered actions,
delays, screenshot checkpoints, expected command failures, and assertions.
Imported saves are embedded in the recording so replay does not depend on the
original files. Traces retain actual results.

Each launch creates a distinct retained run directory containing `run.json`,
`actions.jsonl`, `game.log`, requested captures, run-local saves, and recorded
scenarios/replay reports. `city(new)` during gameplay creates a fresh process and
run directory; record reproducible scenarios from the start screen. External
loads copy the source session and referenced city snapshot into the run.
All generated writes stay in the run directory. Evidence is retained on success,
failure, timeout, cancellation, and disconnect.

Commands serialize on the server and execute on the game main thread. A bridge
failure or timeout invalidates the session and stops its process; a potentially
applied mutation is never automatically retried. MCP disconnect closes/reaps
the owned game after a bounded shutdown. Capture while minimized fails explicitly;
inspection, restore and close remain responsive. GLFW/Wayland cannot report
minimization or restore a minimized surface, so capabilities report
`window_minimize_restore=false` and those native operations fail explicitly;
use compositor controls for that hardware check. Initialization errors use the
bridge error response and stderr rather than a Windows modal dialog.

A ready-to-run example mixes player road dragging with semantic setup:

```sh
urban-sim-playtest playtest/examples/smoke.json --executable /absolute/path/to/UrbanSimCore-playtest --artifacts playtest-artifacts --repeats 2
```

## Tests

```sh
python -m unittest discover -s playtest/tests -p test_unit.py -v
# POSIX; use $env:NAME = 'value' in Windows PowerShell.
export URBAN_SIM_PLAYTEST_EXECUTABLE=/absolute/path/to/UrbanSimCore-playtest
export URBAN_SIM_PLAYTEST_ARTIFACTS=/absolute/path/to/playtest-artifacts
python -m unittest discover -s playtest/tests -p test_rendered.py -v
```

Rendered tests require a desktop/display and Vulkan driver. Linux CI runs these
under Xvfb and nested Wayland via `scripts/smoke_playtest_wayland.sh` and uploads
retained evidence. Run existing CTest and graphics capture checks as well.
Windows/macOS interactive and DPI/display acceptance remain hardware checks.
The existing rail-route simulation test failure is tracked in `GRAPHICS.md`;
MCP support does not change that simulation behavior.

## Verification — October 11, 2026

Windows/MSVC Debug with warnings treated as errors passed. On NVIDIA GeForce
GTX 1660 Ti, all seven Python unit tests and four rendered acceptance tests
passed. The SDK client exercised initialization, discovery, structured results,
PNG image responses, resources, prompts, invalid schemas, and disconnect cleanup.
Recorded city setup replayed twice with identical canonical saved state;
imported-save replay also passed. Validation layers were loaded and no Vulkan
validation errors were reported in the isolated layer environment.

The original graphics smoke passed all 16 capture probes. Existing CTest passed
373/374; `CitySimulatorTests.RailLinesAppearAlongsideBusesInALargeCity` remains
the sole failure, producing zero rail routes. Headless configuration succeeded
with Vulkan/GLFW package discovery disabled. The Windows installed layout and
shipped example CLI are included in acceptance checks.

Linux builds and local hardware runs were not available in that workspace.
Python unit checks and strict builds are configured on all desktop CI runners;
rendered X11/Wayland runs and retained evidence upload are configured on Linux.
Those CI executions remain to be verified. Wayland minimize/restore is
explicitly reported as unsupported by the GLFW backend rather than counted as
native coverage.

### macOS MCP verification

On macOS 15.8.1 with Intel Iris Plus 655 and MoltenVK, the strict Debug build
passed all 375 C++ tests and seven Python MCP unit tests. All four rendered
acceptance tests passed in both the build layout and installed layout. Installed
checks cleared `VK_DRIVER_FILES` and `VK_ICD_FILENAMES`, exercising bundled
loader/GLFW libraries and automatic bundled MoltenVK manifest discovery.
The installed visualizer also passed all 16 graphics capture probes, with
20 frames/20 draw calls and stable 4 MiB reusable geometry buffers.

The MCP SDK client verified discovery, image responses, resources, prompts,
invalid schemas, and disconnect cleanup. Rendered scenarios covered both
projections and all nine overlays, road/zoning/service/district actions,
save/load and imported saves, identical two-run replays, resize,
minimize/restore, close while minimized, focus-loss cancellation, insufficient
funds, and process-crash recovery.

Additional installed-bundle MCP checks verified start-screen button input,
coordinate-derived road dragging, logical 1280×800 to framebuffer 2560×1600
Retina scaling, cursor-anchored zoom, HUD pause/speed/save/load, step input,
large bitmap text, realtime ticks, and unsaved quit/cancel. Odd-height isometric
picking initially failed after zoom; inverse projection now uses the same
integer half extents as drawing, with CPU and rendered MCP regression coverage.

Retained evidence is under `playtest-artifacts/macos-build`,
`playtest-artifacts/macos-installed`, and
`playtest-artifacts/macos-installed-suite`; the supplemental SDK report is
`playtest-artifacts/macos-installed/macos-mcp-report.json`. These are local,
git-ignored artifacts. Logs and the supplemental SDK client script are retained
under `playtest-artifacts/macos-validation`. No Vulkan errors were reported,
but Khronos validation layers were unavailable on this Mac, so this run does not establish validation
layer coverage. Apple Silicon, cross-display scaling, and native OS input/focus
delivery remain separate hardware checks; MCP input is synthetic.

### Parks

The bridge advertises `features.parks=true`. Apply parks through
`tool(action="apply", name="zone", zone="park", x1=..., y1=..., x2=..., y2=...)`.
Summary state exposes `park_tiles` and `park_upkeep` (playable dollars/tick).
Tile inspection uses existing zone and land-value fields. Save/load and
record/replay retain parks as ordinary zone-4 tiles; no snapshot migration is
needed. Placement costs $100/tile, upkeep $2/tile/tick. Benefits taper over
four Manhattan tiles and use the nearest park rather than stacking.

Transit overlay (`view` overlay 8, keyboard 9): cyan bus lines, gold rail
lines, white stop markers, and network-wide ridership, demand, and capacity.
Lines connect consecutive stops using shortest connected road paths; they are
display guides, not a separate rail network or vehicle simulation.


M22 adds `features.disasters`, `view toggle` name `disasters`, and state
`disasters` with `enabled`, `active_fires`, `lost_fire`, `lost_disaster`,
`earthquake`, and `flood`. Detailed HUD inspection exposes a `disasters`
button. New sessions default off; disabling clears active flames and preserves
cumulative loss counts. Save/load retains active fires and the simulation seed.

The rendered M22 acceptance test continues one checkpoint with both the
visualizer and `--simulate-session`, compares treasury/disaster/population
state and semantic city snapshots, then reloads the CLI output in the renderer.
Entity IDs are process-local; the comparison resolves tile references to
building positions and compares population groups by contents.


M23 adds `features.budget_controls`, F6, and the `budget` command/tool:
`inspect`, `open`, `close`, `reset`, and `set_taxes`. `set_taxes` accepts
`tax_percent` as exactly five integers in 0..20, ordered residential,
commercial, industrial, office, income. Invalid requests are rejected before
mutation. Detailed HUD inspection exposes `budget`; an open panel exposes
`budget_close`, `budget_reset`, and `tax_0_minus`/`tax_0_plus` through index 4.

State `budget` contains `tax_percent`, `preview`, and `last_tick`.
Revenue arrays are ordered residential, commercial, industrial, office,
income, exports. Expense arrays are building upkeep, park upkeep, imports,
service operation, building growth. Preview growth is zero; only completed
ticks know actual new growth costs. `last_tick.valid` distinguishes old saves
or a fresh city from a recorded transaction. `ui.budget` is the panel state.
Opening or editing the panel does not advance time or alter recorded history.


The completed M23 budget API adds `set_funding` with `service_percent`:
exactly 11 integer percentages in 0..100, ordered Fire, Police, Health,
Education, Power, Water, Sanitation, Garbage, Recycling, Cemetery, Crematorium.
Defaults are all 100. `set_taxes` preserves funding; `set_funding` preserves
taxes. `reset` restores the whole policy, while `reset_funding` preserves taxes.
`services` and `taxes` open the appropriate budget tab; `open` keeps the
current tab. State adds `budget.service_percent`, `service_costs`, and
`service_coverage` (road-reachable building fractions), with `ui.budget_services`
for the selected tab. Funding is saved in `budgetServicePercent`; older saves
omit it and load all services at 100%.

Open-panel UI inspection adds `budget_taxes`, `budget_services`, and
`funding_0_minus`/`funding_0_plus` through index 10 on the Services tab.
The UI uses 25-point steps; semantic commands accept any whole percentage.
Operating costs are summed and rounded to dollars per department, using the
same function for preview and billing. Invalid arrays/rates are rejected
before policy or city mutation. Effective facilities are derived from original
ratings each refresh; raw facility inspection and saves retain installed ratings.


M24 adds `budget.tax_attraction`: four R/C/I/O multipliers relative to default
taxes. Residential combines residential and income tax deviations equally.
These are demand/arrival multipliers, not predicted population growth. Tax
edits remain paused-safe; effects occur on simulation ticks. Existing saved
`budgetTaxPercent` supplies all inputs, so no additional lag or save fields
are required. `populationChange.desirability` remains normalized to 0–1.


M25 adds `budget(action="trends")` and `ui.budget_trends`. UI inspection exposes
`budget_trends` on all budget tabs and hides tax/funding inputs in Trends.
`budget.history` contains up to 240 completed-tick rows with `tick`, `population`,
`arrivals`, `departures`, `deaths`, `net_migration` (including deaths), `revenue`,
`expenses`, `funds`, `debt`, `tax_percent`, `service_percent`, and the derived
`policy_changed` marker. Values are actual outcomes, not next-tick forecasts.
Saves store the same bounded rows under optional `cityHistory`; legacy saves
start empty. Bad lengths, ticks, numeric ranges, policy arrays and inconsistent
migration totals are rejected before city mutation. The first retained row is
a baseline, so its marker is false. Continuation appends identical samples in
GUI and headless playable modes.


M26 adds F7, native HUD `alerts`, budget tab `budget_alerts`, and
`budget(action="alerts")`. `budget(action="view_alert", alert_index=0..4)`
shares native actions (power, water, unemployment, services, budget), routing
to service overlay, demand overlay or Taxes. UI inspection names row buttons
`alert_view_0` through `alert_view_4`. `ui.budget_alerts` identifies the tab.
`budget.alerts` exposes `checked_tick`, measured `inputs`, and five `records`
with active state, bad/good tick counters, first-detected tick and last-notice
tick. Counter thresholds are 3 bad / 2 recovered ticks; cooldown is 60 ticks.
`cityAlerts` is optional saved state; missing means unchecked, and invalid
counts, ranges, timestamps and inconsistent states reject before city mutation.
Both playable hosts update alerts after the actual treasury and population
results, and GUI/headless saved alert state is identical. Paused refreshes and
save/load do not evaluate alerts or emit notices.


M27 adds `inspect(section="diagnostics", x, y)` for a single valid tile.
It returns `growth` local gate blockers/advice, road access, delivered utility
flags, last evaluated demand, civic reach, congestion and redevelopment location
factor; `building` occupancy/capacity; `services_mask` in ServiceType bit order;
and a radius-4 `neighborhood`. Neighborhood crime/illness are resident-weighted
(0 when there are no residents), service reach is building-weighted, and counts
are clipped to valid tiles. Local job occupancy is not local unemployment.
A clear blocker list does not promise growth: city balance, zone saturation,
district effects and seeded chance still matter. Read-only queries do not
advance ticks, spend cash, rewrite history or create saved diagnostic state.


M28 adds F8, HUD `transport`, `budget(action="transport")`, and
`budget(action="set_route_vehicles", route_id, vehicles=0..20)` for existing bus
routes. Native transport controls are `transport_close`, `route_prev`,
`route_next`, `vehicles_minus`, `vehicles_plus`, `route_coverage` and
`route_bottleneck`. `ui.transport` identifies the modal. `inspect(routes)` adds
vehicle_count, capacity_per_vehicle, operating_cost and coverage_peak_congestion.
`budget.transit_cost` is the next-tick total bus operating cost, and
`budget.transit_report` is completed-tick state: valid, tick, commuters, and
per-route id/capacity/demand/ridership. Utilization uses sampled capacity, not
a subsequently edited fleet. Covered demand is attributed once by existing
route order; commuters minus summed assigned demand is outside route reach.
Civic expense category 3 equals civic operation plus bus operation ($2 per
assigned vehicle). Zero vehicles retains stops/cache/reach and costs zero.

Fleet configuration uses existing saved transitRoutes fields; optional
transitReport retains usage independently from paused tool refreshes. Invalid
fleet numbers, overflowed capacities, duplicate route IDs or inconsistent
report totals reject before city mutation. Report absence starts unmeasured.
Replay and headless continuation use the same policy, billing and report rules.


### M29: management navigation and inspection

In Budget (F6/F7) and Transport (F8), use **Tab/Down** to advance focus,
**Up** to go back and **Enter** to activate. A white outline marks focus;
unavailable controls are skipped. **F2** switches readable/compact labels
inside either panel. Transport also supports **Left/Right** route browsing.

In Trends, hover or click a plot to inspect a retained tick across all six
charts. **Left/Right** steps samples, **Home** selects the oldest and **End**
follows the latest. Readouts show that sample's values, migration and deaths.
Policy details compare it with the previous retained sample: tax changes list
old/new values; funding shows the first changed department plus a count of
additional changes. The oldest row is a baseline when no earlier sample remains.

On the map, **I** pins the hovered tile's inspector; **I/Escape** releases it.
Alert View actions center and pin a relevant building when available: missing
power/water, missing civic reach, or unfilled job capacity. These are measured
selection hints, not historical causal claims. Budget alerts open Taxes.
Inspection and navigation do not advance the simulation or change saved policy.

MCP UI inspection exposes `budget_focus`, `transport_focus`, effective
`trends_tick` and `pinned_tile` ([-1,-1] when unpinned). Management controls
include `enabled` and `focused`; Trends adds six plot rectangles in `charts`.
Native Tab, Home, End and I keys are accepted by the playtest bridge.


M30 sustained deterministic scenarios are retained in
`playtest/examples/phase8_stabilization.py`. For example:

```sh
VK_DRIVER_FILES=/usr/local/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json \
.venv/bin/python playtest/examples/phase8_stabilization.py \
  --executable build/bin/UrbanSimCore-playtest \
  --legacy-save playtest-artifacts/hour-continuation-20261012/city-save/urban_sim_session.json \
  --artifacts playtest-artifacts/m30-repeat
```

Use the Vulkan driver configuration appropriate to your platform. Each scenario
runs 1,000 growth ticks, 200 ticks with power funding disabled, 500 recovery
ticks and 20 continuation ticks. The older save is imported into run-local files;
source files and funds are preserved. Exact GUI/headless continuation includes
the session, city tiles, buildings, population and allocator counter, with
unordered entity arrays compared by ID. Each recorded scenario is replayed twice.
Measurements include command/IPC time per completed tick; they are neither
renderer FPS nor wall-clock endurance results. These scenarios keep the normal
window size; the separate rendered regression suite still checks minimum size.

Core snapshots now optionally contain `nextEntityId` (1 through UINT32_MAX+1).
New saves preserve consumed/deleted IDs for continuation parity. Missing legacy
counters derive from live entity IDs; malformed, overflowing or colliding
counters reject before city mutation. Loading a complete city replaces the
process-global allocator state, just as it replaces that active city's entities.
