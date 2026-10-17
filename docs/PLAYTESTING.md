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

## Fresh-city player acceptance

[Phase 10](PHASE10.md) defines the opt-in M36 journey from the native New City
screen to 10,000 residents. Run `playtest/examples/phase10_native_journey.py`
with the playtest Python environment. Unlike semantic fixture setup, every city
construction, policy change and tick in this journey comes from native input.
The runner retains a replay scenario and checks the normal budget, disabled
autonomous growth, successful purchases, utility supply and save continuation.

Paused means automatic ticking is stopped. N (or period) still advances one
complete tick; its toast reports that tick while automatic running remains paused.

## Surface-water controls

New City defaults to lakes/river terrain; T selects an explicit flat map. Old
saves retain their terrain. In the Water or Sanitation service tool, V toggles
inland/shore mode. Shore mode uses a water intake or untreated sewage outlet on
land beside water and a road. Intake previews/inspectors report clean-source
percentage; raw outlets nearby along the same water body reduce usable supply.
Semantic service requests may set `surface_water: true`; facility inspection
includes the flag, display name and intake `source_clean_fraction`.

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
12 integer percentages in 0..100, ordered Fire, Police, Health,
Education, Power, Water, Sanitation, Garbage, Recycling, Cemetery, Crematorium,
Incinerator. Legacy 11-entry requests and saves retain the new department at its
current/default funding.
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


### M31 regional configuration persistence

`inspect(section="regional_connections")` returns paginated configuration rows
sorted by connection ID: `id`, `kind` (`road_gateway`, `cargo_port`,
`passenger_terminal`, `airport`), `origin` [x,y], `width`, `height`, `entrance`
[x,y], `enabled`, `passengers_per_tick` and `goods_per_tick`. These capacities
are separate units; imports and exports share the goods capacity. Inspection
is read-only. These records are configuration foundations, not active terminals
or a completed-tick usage/delivery report. There is no placement/editing tool yet.

Optional gameplay-session `regionalConnections` stores the configuration using
`RegionalConnectionsJson.hpp`. Missing legacy data clears to an empty list.
Records have unique nonzero uint32 IDs, bounded footprints, adjacent entrances
(road gateways have an entrance inside the footprint at the map boundary),
boolean enabled flags and nonnegative uint32 capacities compatible with mode.
Up to 4,096 records are accepted. Overlapping footprints and entrances swallowed
by another footprint reject. Saved order is canonical by ID. Structural validation
happens before loading the core snapshot, and save validates before writing it.
Terrain, roads and utility readiness are derived access checks, not persisted
claims of operation. Configuration alone does not reserve tiles, charge funds,
produce goods, admit visitors or modify existing road-gateway trade.

The rendered regional-configuration case checks native loading, pagination,
read-only inspection, rejection without live-state mutation, save/load, full
session GUI/headless continuation, legacy clearing and two identical replays.


### M31 placement and settlement contracts

`RegionalConnectionTool` provides internal atomic registry placement/removal.
Placement checks current site/access, duplicate IDs, civic sites, footprints and
reserved entrances before reserving or charging the supplied construction price.
Sea reachability is rebuilt at execution. Funds must cover a nonnegative cost;
removal needs no working access, charges its explicit cost and creates no refund.
Registry reservations are the shared footprint data for the future terminal
integration; these APIs do not manufacture building IDs or edit terrain.

`regional::settle` finalizes an allocation at most once per strictly increasing
nonzero tick. Per-connection confirmed deliveries cannot exceed admitted flow,
and duplicate/unknown confirmations reject. Requested = admitted + unmet;
admitted = delivered + undelivered. Fees and export amounts use delivered units;
imports plus operation form expense. Prices are nonnegative integer treasury
dollars per delivered unit; operation is an explicit total. Overflow and invalid
reports reject without replacing the previous settlement. Facility milestones
supply pricing policy; no new default rates or city cash flow are enabled here.

`inspect(section="regional_report")` returns the last finalized report:
`valid`, `allocation` (tick, requested/admitted/unmet, connection rows),
`deliveries`, delivered/undelivered totals, prices, revenue and expenses.
These reconciled amounts are retained accounting data, not an extra cash deposit.
A valid report may precede the current city tick or reference a removed terminal;
it is historical data. Read-only inspection and loading do not finalize it again.

Optional `regionalSettlement` persistence reconstructs and validates the settlement,
including monetary totals, confirmation references and timestamps, before world
mutation. Legacy absence starts unmeasured. The saved tick guard survives reload,
so the same tick cannot be finalized again. Live terminal activation, enforcement
of registry reservations by world-edit/growth tools and one-time integration with
the existing trade/treasury loop belong to M32/M33, before player placement tools
are exposed. M31 itself introduces no new sea/air benefit.


### M32 external freight gateway backend

`FreightSystem::simulate` takes an optional final
`const std::vector<FreightGateway>*` argument. Null preserves the original fixed
west-edge gateway and unchanged restocking rules. An explicit list uses only its
gateways, including when it is empty. Each gateway has nonzero unique `id`, valid
map `position`, nonnegative `goodsPerTick` and `enabled`. Malformed lists reject
before traffic/cache mutation. Facilities will supply these derived routing
inputs after their access checks; this API does not validate a shore or build a port.

The backend sorts gateways by ID and spends shared import/export capacity only
on actual reachable delivery. Failed candidates add no truck load and no separate
unrouted count; remaining undelivered goods are counted once after alternatives.
Gateway usage resets every tick. `FreightSummary.gateways` reports each configured
gateway and actual imports/exports; legacy calls leave this new vector empty.
Existing `gate`/`gateConnected` remain diagnostics of the fixed road gate, so an
explicit gateway can deliver while `gateConnected` is false. Domestic supply and
producer-restocking policy retain their existing order and weighting.

Seven core cases verify capacity splits and per-edge loads, domestic-first exports,
capacity shared across producer restocking, disabled/unreachable fallback, empty
explicit access versus legacy defaults, stable ordering, changed capacity/topology
with cached paths and invalid configurations rejecting before traffic changes.
This routing slice does not activate facilities or reprice city trade.


### M32 constructed port reservations

The internal `PortTool` API builds fixed 2x1 maritime footprints on land with
current road/destination/utility and edge-connected shore access. Initial policy:
cargo construction $15,000 with 200 goods/tick; passenger construction $10,000
with 60 passengers/tick; demolition $500 with no implicit refund. These are
backend policies, not exposed player controls yet. Operation and traffic activation
remain pending M32 integration. Passenger demand still belongs to M34.

`regionalConnections` now saves optional boolean `constructed`, default false
for legacy/planned records. Only cargo/passenger 2x1 records may be constructed
in the current build. Constructed records are authoritative; tile
`regionalConnectionId` is a derived index, deliberately absent from core JSON.
Gameplay load checks conflicts against tile metadata, building records, road
edges and civic positions before changing the destination city, then rebuilds
ownership. Loading legacy data clears old ownership. A historical terminal may
be disconnected or lose access without invalidating its saved physical footprint.

MCP `regional_connections` rows add `constructed`; tile inspection adds
`regional_connection_id` (zero outside a physical reservation). Registry-only
placement/removal cannot bypass the physical tool lifecycle. Normal road, zoning,
service and bulldoze tools recheck ownership when applying stale plans. Direct
road/zoning operations and growth also protect the index.

Six core cases cover charges/capacities, stale plans and ordinary tools,
autonomous road/growth guards, failed/successful removal, reservation rebuilding
and legacy clearing, plus conflicting tile/edge/building records rejecting before
world mutation. A native constructed-port fixture checks edit rejection, saved
conflicts, exact GUI/headless session continuation, legacy clearing and identical
replays. The fixture represents an already-paid historical port; actual payment
and removal are verified through the C++ API. Native placement/rendering and
freight/treasury activation are not claimed by these tests.

### M32 operational site diagnostics

`inspect(section="regional_connections")` also returns `site_access`. This is a
read-only, current site check, not a completed-tick delivery measurement. It
checks physical ownership, the adjacent road entrance, delivered power and water,
and (for maritime terminals) shoreline contact with boundary-connected water.
Built terminals accept their own tile reservations; missing or foreign ownership
fails closed. Dormant configuration returns `not constructed` even if its proposed
site would otherwise be valid. Disabled terminals cannot admit operational flow.

Site readiness checks the entrance itself. It does not require a road to the
legacy external gateway, and `ready` does not promise a road path to every local
customer. `regional::diagnoseOperation` can separately test a specific destination;
freight still confirms actual delivery before consuming gateway capacity. Site
inspection does not advance time, add traffic, charge upkeep or book trade.
`NotConstructed` is appended as access value 9; existing persisted status values
keep their meaning. Settlement validation allows it only with zero admitted flow.
Native port controls and live logistics activation remain pending.

### M32 shared live logistics

A completed gameplay tick containing a constructed cargo port opts into
`regionalLogisticsEnabled`. The boolean is saved independently of the registry,
so removing the last port does not restore unlimited legacy trade. Missing legacy
state defaults to false. Dormant configuration, passenger terminals, airports and
read-only refreshes do not activate this mode. Native cargo construction now sets this mode immediately, before any tick;
the shared tick also handles already-constructed older fixtures.

In the new mode, ready cargo ports supply their configured goods capacity; a
fixed west-edge road gateway supplies 100 goods/tick. Imports and exports share
each gateway's capacity. The road gateway uses the highest uint32 ID not in the
registry, avoiding collisions with historical configuration. Gateways route in
ascending ID order and consume capacity only on confirmed city-side delivery.
Disabled or blocked cargo ports carry no goods. Existing cities outside this mode
retain legacy routing and forecast trade pricing.

Enabled constructed cargo ports cost $20/tick and passenger terminals $10/tick,
including when temporarily disconnected; disabled terminals cost zero. Upkeep is
billed once in the civic/transit/ports expense category. Passenger terminals do not
create passengers or tourists. Fulfilled imports and exports replace forecast
trade in the shared economy, then treasury scales and rounds the combined amounts
once. Goods reaching retailers determine commercial tax receipts and economic
health; shortages also lower the next tick's migration attraction. No expense is
charged for an import that never arrived.

`state.metrics` adds `regional_logistics_enabled`, `port_operating_cost`,
`goods_availability` (0..1) and `regional_gateways`. Each gateway exposes `id`,
`entrance`, `goods_per_tick`, `enabled`, `imports` and `exports`. These metrics are
current derived projections; `regional_report` remains the last completed tick.
Its local routing confirms admission and delivery together. Unreachable flow is
unmet rather than capacity reserved at the wrong gateway. Allocation includes
zero-flow passenger/dormant rows and the finite road gateway (up to 4097 rows).

New measured reports attach `amountScale` (normally 0.01) for converting their raw
quote prices, revenue and expenses to city dollars, plus `operatingExpense` in
city dollars. Older reports omit these fields and retain scale 1 / zero separate
upkeep. Quote expenses exclude the separate `operatingExpense`. These amounts
explain the tick; they are never deposited a second time. The treasury ledger is
authoritative for final category rounding, debt and total cash flow. Paused tax,
utility and tool refreshes reprice forecasts without finalizing reports or adding
a second freight pass to live road loads.


### M32 native port controls

`V` or the inspectable `ports` button cycles Cargo, Passenger, Remove, On/Off and
Capacity. Left-click places/manages a port; right-click or Escape cancels. The
read-only preview chooses a road entrance with a reachable civic road destination
and validates the full 2x1 shore footprint. Execution rechecks the current map,
roads, utilities, reservations and funds. Clicking the informational panel does
not place anything behind it. Both projections render distinct cargo containers
and passenger sheds in tile depth order; reserved yards suppress vegetation.

Cargo placement pays $15,000, creates 200 goods/tick and immediately persists the
logistics transition. Passenger placement pays $10,000 and creates 60 passengers/
tick without activating cargo or inventing visitor demand. The panel explains
prices/upkeep, the delivered-trade transition and persistent 100-goods road limit
before cargo purchase. Removal releases the whole owned footprint for $500, with
no refund. On/Off toggles access and upkeep. Capacity cycles 50/100/200 goods or
15/30/60 passengers, bounded by design capacity. Upkeep is fixed for enabled ports;
reducing capacity alone does not suspend it. Hover a port to see current access,
capacity/upkeep and the last completed port flow plus citywide unmet external
trade. A new port has no completed measurement until its next simulation tick.

The existing `tool` MCP action accepts `name="port"`, uses `x1/y1`, and supports
`port_action="cargo"|"passenger"|"remove"|"toggle"|"capacity"`. The default is
cargo. For capacity, an optional integer `capacity` sets 0..200 goods or 0..60
passengers; omission cycles the native presets. Unsupported actions, noninteger
or out-of-range capacities, occupied sites and failed payments reject without
charging or consuming IDs. The native and semantic paths share the same action
closure. Inspection version remains 3 (additive fields); tool count remains 17.
Capabilities advertise `ports`.

Gameplay saves add `nextRegionalConnectionId` (1..4294967296, with the final value
meaning exhausted). It survives removal of every port. Old/stale counters are
normalized above all registry IDs on save/load. Invalid types/ranges reject before
world replacement. Successful construction advances it; failed placement does
not. IDs do not reuse a demolished port's ID under the saved counter. Construction
and removal invalidate the autonomous zoning candidate cache so reserved land is
excluded and released land becomes eligible again. Full session continuation and
recorded native replay retain IDs, settings, mode, money and reports.


### M33 airports and measured passenger readiness

`J` or the inspectable `airport` button selects airport placement. Airports use
a fixed 4x2 land footprint, a reachable civic road entrance, and delivered power
and water on every tile. The middle tiles need roads along both long sides for
utility delivery. No shore or boundary-connected water is required. Construction
costs $25,000 and starts at 120 passengers/tick, with no cargo capacity. The shared
terminal plan/build/remove implementation retains maritime-only PortTool wrappers
and revalidates execution. Reservations, growth/edit guards and derived ownership
rebuilding apply to all eight tiles.

Existing `V` terminal operating modes manage airports as well as ports. Airport
capacity cycles 30/60/120 or can be set explicitly to 0..120; enabled upkeep is
fixed at $30/tick, including access outages. Disabled airports have no upkeep or
availability. Whole-airport removal costs $1,000 with no refund, preserves the
regional ID counter and leaves cargo rules unchanged. Airports never activate
cargo mode or create cargo gateways. A rebuilt airport receives a fresh ID.

The existing `tool` accepts `name="airport"` (default placement), or
`name="port",port_action="airport"`. Remove/toggle/capacity actions accept either
name and target `x1/y1`. General terminal tools keep their existing 17-tool API;
capabilities add `airports` and native key input adds `J`. Inspection metrics add
`airport_operating_cost` and `regional_operating_cost`; `port_operating_cost`
continues to count only maritime terminals. The budget labels the combined
category `CIVIC/TRANSPORT` and bills each enabled facility once.

`regional_connections` inspection adds current `available_passengers_per_tick` and
`available_goods_per_tick` (zero unless physically ready and enabled). Completed
`regional_report` allocation rows now record `availablePassengers` and
`availableGoods`. They describe available per-tick capacity, not arrivals. Newly
measured admission cannot exceed those limits; imports and exports share goods
capacity. Blocked/disabled rows expose zero availability. Both fields must appear
together and use nonnegative uint32 integers. Legacy reports omit them and retain
unknown historical capacity; omission is not interpreted as zero.

A city with constructed passenger terminals/airports and no cargo mode now gets
a completed-tick passenger-readiness report. It excludes the legacy road freight
gateway, invents no passenger demand/arrivals/fees and does not replace legacy
freight pricing. Its separate `operatingExpense` records terminal upkeep; the
city treasury still bills services and trade normally. In cargo mode the same
regional report includes all offers and the finite road freight gateway. Paused
inspection/refresh keeps the last completed measurement and charges nothing.
The native hover panel shows passenger utilization as actual/available. Real
tourism demand and local attraction access are deferred to M34.


## M34 hotel placement controls

Use K or the inspectable `hotel` button to select placement. A hotel costs
$10,000 and needs empty unzoned land beside a road with both power and water.
The preview and panel explain invalid sites before a click. Each hotel is one
commercial entity with 12 worker slots and 32 visitor beds; standard commercial
upkeep applies once. H badges identify hotels in top-down and isometric views.
The panel explicitly marks visitor service inactive pending live tourism ticks.
Use B/bulldoze for the ordinary $200 building removal; Escape/right-click cancel.

MCP `tool` accepts `name: "hotel"`, `action: "select"` or `"apply"` and x1/y1.
Hotel placement rejects terminal-specific `port_action`/`capacity` arguments.
`inspect entities` adds `hotel_beds`; `capacity` and `occupancy` remain worker
quantities. `state.tools.hotel` reports the active hotel tool, and the capability
`hotel_placement` advertises this control separately from completed tourism.

`test_native_hotel_placement_cost_guards_persistence_and_replay` covers real
button/key/click paths, costs/guards, saved beds/jobs, demolition/rebuild IDs,
minimum-window layout and deterministic replays. It uses ordinary city funds.
The older hotel metadata test independently checks GUI/headless continuation.


## M34 live tourism cohorts

The visible `tourism` button toggles policy without changing the last completed
report. Building a hotel enables tourism; legacy saves default disabled. MCP
`budget` accepts `action: "set_tourism", tourism_enabled: true|false`.
`inspect` section `tourism` returns a nested `tourism` record containing current
policy, cohort `state` (including pending early departures) and completed `report`.
It does not overwrite the protocol's top-level UI state. Summary metrics expose
tourism_enabled, tourism_tick, visitor_day_arrivals, visitor_overnight_arrivals,
visitors_staying and visitor_pending_departures; population remains residents.

Completed demand reports include accessibleParks, qualityPercent, park/quality
contributions, requested day/overnight, arrivals/departures, staying, transport/bed
shortages and per-gateway mode/access/capacity/usage. Hotel panels show current
occupied beds and last completed totals; passenger terminal panels use completed
tourism allocation. CLI saved-session continuation prints day/night/stay counts.

Pausing, inspection and policy edits create no visitors, departures, fees or
history samples. Removing an occupied hotel queues early departures without
retaining invalid hotel references, so a paused save/load stays valid. The next
completed tick counts those departures exactly once. Disabling policy stops
admissions and releases surviving stays on that next tick. Local visitor road/
transit loads are described below; municipal tourism fees are still pending.

The live tourism acceptance test checks real policy-button input, bounded
arrivals, resident separation, no fabricated fees, paused inspection/save/load,
exact GUI/headless core/session continuation, active cohorts, paused hotel
removal, queued departures, policy shutdown, strict invalid import rejection
before mutation and two identical replays.

## M34 visitor road/transit travel

`inspect tourism` now includes completed `travel`: measured/tick,
requested/road/transit/walking/unrouted, aggregate legs by kind and route usage
with capacity, residentRiders, demand and ridership. Summary metrics expose
visitor_road_trips, visitor_transit_trips, visitor_walking_trips and
visitor_unrouted_trips. Road inspection adds visitor_load; total load includes
resident, freight and visitor components. Hotel panels display completed trip
mode counts in both projections.

Visitors use spare seats after residents, share them across legs/routes, and
fall back to roads. Shared road anchors count as walking; disconnected trips are
unrouted. Paused fleet/road edits reproject demand without duplicating loads or
changing the completed travel record. Historic capacity is validated against the
completed resident transit report, allowing fleet edits before saving.

The visitor travel acceptance case covers resident seat priority, spare fleet
capacity, zero-fleet road fallback, repeated paused save/load, GUI/CLI semantic
city and exact session continuation, both projections and two identical replays.
Newly grown entity IDs are process-local; semantic city comparison resolves tile
building references and compares all remaining fields. Municipal tourism fees
and hotel operating controls remain pending.

## M34 hotel operating policy

With K/Hotel selected, clicking an existing hotel toggles its visitor beds.
MCP budget action set_hotel takes hotel_id and hotel_open; inspect entities adds
hotel_open. Closing releases guests next tick and stops overnight admissions;
day visits continue. Paused policy edits leave reports and stays untouched.
Jobs and existing commercial maintenance continue while closed. The hotel panel
shows the current policy and occupied beds. Legacy hotels default open.

The hotel policy acceptance case checks closure with active guests, paused
save/load, one-time early departures, exact GUI/CLI continuation, native tile
reopening, invalid IDs and two identical replays. Placement acceptance also
checks native and MCP toggles without additional construction charges.

## M34 municipal tourism accounting

`inspect tourism` includes `accounting` with measured/tick/dayFees/stayFees/services.
Fees are $2 per admitted day visitor and $4 per occupied bed each tick; services
cost $1 per admitted day visitor/occupied bed. Departures and unmet demand incur
no fee. Metrics add tourism_fees/tourism_services; budget last_tick adds the same
fields. Totals reconcile as sum(revenue)+tourism_fees and
sum(expenses)+tourism_services. Existing six revenue/five expense arrays retain
their categories. The native budget shows completed fees/services/net; CLI
continuation prints fees/services. Next-tick estimates exclude visitor amounts.

The accounting acceptance case verifies a full stay cycle, bed saturation,
separate fees/costs, treasury totals and cash, paused save/load, strict tampered
import rejection before mutation, minimum-window budget readability, shutdown
with zero charges and two identical replays. C++ checks include debt repayment,
legacy absence without retroactive charges and report/ledger reconciliation.


The **U** shortcut or **Connections** button opens the regional/tourism management
panel. `inspect(section="ui")` exposes `regional_close`, `regional_prev`,
`regional_next`, `regional_operation`, `regional_capacity`, `regional_tourism`, `regional_tab` and `regional_locate`
controls with bounds, enabled state and focus. State `ui.regional` indicates the
panel is open. Tab/Up/Down move focus, Enter activates, Left/Right browse
constructed terminals, Escape closes, Space pauses and N steps while paused.
Policy edits preserve completed-tick reports until the next simulation tick.

The panel's **View Hotels / View Terminals** button switches the browsed asset
type. Hotel operation uses the same Open/Closed policy as MCP `budget.set_hotel`;
beds and worker capacity remain fixed. Hotel guest counts use the last completed
tourism state, including guests awaiting departure after a paused closure.
**Locate on Map** pins the selected facility and opens the service coverage
overlay. F6/F7/F8 switch directly to Budget/Alerts/Transit. State
`ui.regional_hotel_tab` and `ui.regional_selected_hotel` expose the hotel tab and
selection index. Load resets management selection and closes the panel.

`inspect(section="regional_alerts")` returns at most five derived completed-report
bottlenecks: visitor access, hotel beds, cargo access/capacity, local cargo
delivery and local visitor travel. Each item includes `problem`, `tick`, `unmet`,
`target_id` and `advice`. `target_id=0` means a citywide problem. Alert quantities
come from saved reports and do not clear merely because policy was edited while
paused. These diagnostics do not change city-alert debounce/cooldown or add save
records. New saves and legacy saves without measurements produce no fabricated
alerts. CLI playable continuation prints the same diagnostics.

Management controls `regional_alert_next` and `regional_alert_manage` cycle and
act on the measured bottleneck. Manage selects a terminal or hotel, or opens
Transit for local visitor travel. `ui.regional_alert_count` and
`ui.regional_selected_alert` expose the current list size and selected index.
For passenger access, a measured blocked terminal takes priority over a full
ready terminal, then the lowest ID breaks ties. Cargo admission shortfalls remain
citywide; an undelivered admitted cargo row can identify a connection. A removed
facility falls back to the relevant management tab rather than a stale map pin.


For longer M35 evidence, `playtest/examples/phase9_stabilization.py` accepts
`--executable`, `--legacy-save` (the retained mature 96x96 M30 checkpoint) and
`--artifacts`. It first verifies legacy continuation without fabricated visitors,
then constructs a connected shore/air/hotel extension through semantic tools on
an explicit coast fixture. It retains 1,320 rendered stress/recovery ticks after
setup, checks complete per-batch treasury history and flow/travel constraints,
loads during a closure, compares full GUI/CLI continuation and replays twice.
Native alert editing remains covered by `test_regional_*` acceptance tests.
The script never overwrites its source save. `measurements.json` timestamps are
step-request latency (including graphics/IPC), not engine tick CPU time.

The optional `UrbanSimCore-regional-benchmark` target profiles the shared tick
path without file/render time, with retained/warm and reconstructed/cold host
caches and per-tick invariants. See `tools/REGIONAL_BENCHMARK.md` for commands,
measurement boundaries and cache-count interpretation.
