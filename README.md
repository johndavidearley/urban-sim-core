# UrbanSimCore

A high-performance simulation engine for modeling living cities. Designed to simulate population dynamics, urban growth, economics, traffic, and services at scale.

Current implementation and validation details are maintained in
[docs/STATUS.md](docs/STATUS.md). The detailed [roadmap](docs/ROADMAP.md) is a
milestone history and idea backlog rather than the active status source.

Power facilities can be generic coverage utilities or typed generators with
capacity and lifecycle emissions:

```bash
./build/bin/UrbanSimCore-cli --size 32 --add-power-source SOLAR 10 10 12 25 --run-service-evaluation --print-service-summary
```

## Quick Start

### Build

```bash
git clone --recursive <repo-url>
# or, if already cloned without --recursive:
git submodule update --init --recursive

mkdir -p build && cd build
cmake ..
cmake --build . --config Release
```

The googletest submodule is required to build the test suite; without it, test
targets are skipped. nlohmann/json is required for snapshot persistence and is
fetched automatically at configure time if not installed locally.

### Run Headless Simulation

```bash
./build/bin/UrbanSimCore-cli --help
```

### Optional Live Visualizer (SDL2)

If SDL2 is installed, CMake adds an extra executable target named `UrbanSimCore-visualizer`.

```bash
cd build
cmake ..
cmake --build . --config Release
./bin/UrbanSimCore-visualizer
```

Controls:
- On launch, choose a 32, 64, or 96 tile map, toggle procedural terrain,
  start a new city, or load the existing session
- `F1`: show or hide the new-city guide
- `F2`: toggle large or compact UI text (large is the default)
- Click the bottom tool palette to select Roads, Zoning, Bulldoze, or Services;
  clicking an active Zoning or Service button cycles its subtype
- Click Play/Pause or `1X`/`2X`/`3X` in the HUD to control simulation time
- Click Save/Load in the HUD, or press `F5`/`F9`, to persist the playable session
- `R`: toggle the road construction tool
- `Z`: activate the zoning tool; press repeatedly to cycle Residential,
  Commercial, Industrial, and Office
- `B`: toggle the bulldozer tool
- `S`: activate the service tool; press repeatedly to cycle Fire, Police,
  Health, Education, Power, Water, Sanitation, Garbage, Recycling, Cemetery,
  and Crematorium
- Left-drag: preview and build an x-then-y road while the road tool is active
- Right-click: cancel the current road drag
- Arrow keys: pan viewport
- Middle-button drag: pan the map
- Mouse wheel: zoom while keeping the tile beneath the cursor anchored
- `+` / `-`: zoom in/out
- `1`: zone overlay
- `2`: land value overlay
- `3`: pollution overlay
- `4`: service coverage overlay
- `5`: traffic congestion overlay
- `6`: demand overlay
- `7`: happiness overlay
- `8`: route heatmap overlay
- Click buttons `1`–`8` in the legend to select the matching overlay directly
- `O`: cycle route heatmap origin filter (residential anchors)
- `D`: cycle route heatmap destination filter (job anchors)
- `C`: clear route heatmap filters
- `Space`: pause/resume live deterministic ticks
- `.` or `N`: single simulation tick while paused
- `H`: show/hide in-window legend panel
- `Esc`: cancel the active tool/drag, or quit when no tool is active

The playable-builder road tool starts with a $50,000 construction fund and
charges $100 for each new road segment. Green previews are buildable; red
previews cross blocked terrain/buildings or exceed available funds. Existing
segments included in a dragged route are not charged again.

Zoning costs $25 per changed tile and uses rectangular drag selection. It
cannot cover water or roads, and occupied tiles cannot be changed to another
zone type. Reapplying the same zone is free and does not consume funds.

The bulldozer uses rectangular drag selection and removes buildings, zoning,
and every road segment touching the selection. Demolition costs $200 per
building, $20 per road segment, and $5 per zoned tile. Orange previews are
valid; empty or unaffordable selections appear red.

Service buildings use single-click placement and require an empty, non-road
tile adjacent to the road network. Fire costs $5,000, Police $4,500, Health
$6,500, Education $5,500, Power $12,000, Water $8,000, and Sanitation $7,000.
The placement preview and map marker use a distinct color for each service,
and coverage refreshes immediately.

Garbage facilities cost $9,000 and provide 400 units of collection capacity;
Recycling facilities cost $11,000 and divert up to 35% of generated waste with
200 units of processing capacity. Uncollected waste raises pollution on zoned
land and lowers the Happiness overlay. The HUD reports the current waste
collection percentage, while both facility types add operating costs and can
be removed with the bulldozer.

Population mortality accumulates deterministically from a baseline rate plus
illness and pollution risk. Deaths reduce population and create a deceased
backlog. Cemeteries cost $10,000 and process 50 per tick; crematoriums cost
$14,000 and process 100 per tick, subject to road coverage. An unprocessed
backlog reduces happiness and raises health pressure. Mortality remainder,
population target, and backlog persist in saved sessions.

New buildings require both road-reachable power and water coverage. Sanitation
adds sewage-service coverage and operating cost but is not a construction
gate. The tile inspector reports current Power and Water connectivity, and the
new-city guide walks through placing both utilities before a civic service.

The in-window HUD remains visible independently of the debug legend and shows
simulation state, tick, population, building count, construction funds, and
the selected tool. Keyboard shortcuts remain available alongside the clickable
palette.

Four live demand bars in the HUD show residential, commercial, industrial,
and office demand using the same colors as their zoning tools. The speed
buttons run ticks at approximately 700 ms, 350 ms, or 120 ms intervals.

Invalid placement reasons are displayed beside the cursor while previewing a
tool. Every attempted road, zone, demolition, or service action also produces
a short-lived in-window success or error notification, including the amount
spent for successful actions.

Hovering over the map outlines the inspected tile and opens an information
panel with coordinates, terrain, zone, land value, pollution, road congestion,
building type and occupancy, and civic-service type and range. Inspection is
suppressed while the pointer is over HUD panels or the tool palette.

Clickable controls brighten on hover, while selected tools, overlays, map
sizes, terrain mode, and simulation speed retain their stronger active-state
highlight. This feedback applies to both the start screen and in-game UI.

Compact labels in the built-in bitmap font render at twice their original size
by default for readability on Retina and other high-density displays. `F2`
restores the legacy compact size when more panel space is preferred.

Hovering over tool-palette, overlay, playback, speed, save, and load buttons
also displays a descriptive tooltip. Tooltips explain interactions, current
subtypes, construction costs, simulation timing, and overlay meaning.

Quitting routes through an in-window confirmation dialog with Save & Quit,
Quit Without Saving, and Cancel choices. Active tools are still cancelled by
the first Escape press; a subsequent Escape opens the dialog. Simulation time
stops while confirmation is open, and closing the OS window uses the same flow.

The default save slot is `urban_sim_session.json`, with the validated core city
snapshot stored beside it as `urban_sim_session.json.city.json`. A session
restores the map, roads, buildings, population, civic facilities, funds, tick,
pause state, simulation speed, and current demand values.

The launch screen can also be controlled with Enter/`N` for a new city, `L`
to load, `T` to toggle terrain, and Escape to quit. Loading inspects the saved
city dimensions before constructing the simulation map.

New cities now begin paused and genuinely empty rather than loading a seeded
demonstration layout. The in-window guide walks through constructing the first
road, zoning land, starting simulation time, and placing a civic service.
Loaded sessions bypass the guide.

The construction fund is also the live city treasury. Each simulation tick
applies one percent of the economy system's tax/export revenue and
maintenance/import expenses to match the visualizer's short tick cadence.
The HUD shows the latest scaled income, expenses, and net treasury change;
these values are preserved by session saves.

Each placed service also has a recurring per-tick operating cost: Fire $25,
Police $22, Health $35, and Education $30. These costs join the HUD's `OUT`
figure without the economy scaling step because they are already expressed in
playable tick units. The HUD warns below $5,000 and reports an unfunded deficit
when expenses would push an empty treasury below zero; recovery is announced
when the balance becomes healthy again.

Bulldozer selections include civic facilities as first-class demolition
targets. Removing one costs $100, clears its map marker and coverage, and
immediately removes its recurring upkeep from the projected `OUT` figure.

### Run Tests

```bash
cd build
ctest --verbose
```

### Strict and Sanitizer Builds

With CMake 3.21 or newer, presets provide the shortest path to each maintained
configuration:

```bash
cmake --preset strict && cmake --build --preset strict && ctest --preset strict
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

The normal build remains unchanged. The equivalent explicit strict-build
commands are:

```bash
cmake -S . -B build-strict -DURBAN_SIM_WARNINGS_AS_ERRORS=ON
cmake --build build-strict -j
ctest --test-dir build-strict --output-on-failure
```

Clang and GCC builds can also enable AddressSanitizer plus UBSan, or
ThreadSanitizer. Use separate directories because TSan cannot be combined with
ASan/UBSan:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DURBAN_SIM_ENABLE_ASAN_UBSAN=ON
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DURBAN_SIM_ENABLE_TSAN=ON
```

On macOS, AppleClang's ASan runtime may not support LeakSanitizer; run tests
with `ASAN_OPTIONS=detect_leaks=0` there. Address and undefined-behavior checks
remain enabled.

GitHub Actions runs strict RelWithDebInfo builds on Linux and macOS, plus Linux
Debug ASan/UBSan with leak detection and Debug ThreadSanitizer, for every pull
request and push to `main`.

### Procedural Terrain

```bash
./build/bin/UrbanSimCore-cli --size 40 --seed 7 --generate-terrain --print-map
./build/bin/UrbanSimCore-cli --size 40 --seed 7 --terrain-water 0.30 --render-map city.ppm
```

`--generate-terrain` stamps water and rough terrain onto the map from a seeded,
smoothed height field — deterministic for a given `--seed`. Water tiles cannot
be zoned or built on, so growth routes around lakes. Tune water coverage with
`--terrain-water FRAC` (0.0–1.0, implies `--generate-terrain`). Generation runs
only on a fresh map; loading a snapshot restores its saved terrain instead.

### Autonomous Demand-Driven Simulation

```bash
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 80
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --generate-terrain --simulate 80 --simulate-report run.csv
./build/bin/UrbanSimCore-cli --size 96 --seed 7 --simulate 50 --simulate-no-traffic
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 80 --simulate-inflation-rate 0.02
./build/bin/UrbanSimCore-cli --size 32 --seed 5 --zone-rect 10 10 15 15 OFFICE --place-road 10 16 15 16 --run-growth 20 --print-budget-summary
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 100
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 100 --simulate-no-transit
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 60 --simulate-district Downtown 20 20 44 44 --simulate-district Suburbs 46 20 63 44
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 60 --simulate-district Factory 20 20 44 44 --simulate-district-archetype Factory INDUSTRIAL --simulate-district Innovation 46 20 63 44 --simulate-district-archetype Innovation TECHHUB
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 100 --simulate-disasters --simulate-fire-risk 3
```

`--simulate N` grows a city autonomously from a near-empty map for N ticks. Each
tick it derives residential/commercial/industrial demand from the city's own
state (housing, jobs, employment), extends a road grid outward, zones land in
proportion to demand, lets the growth system build, repopulates housing and
jobs, and records a metrics row. The result is an emergent city: residential
demand seeds the first homes, residents create demand for jobs (industrial, then
commercial), and the loop compounds until land runs out.

Residents migrate in gradually rather than instantly filling new housing, at a
rate set by the city's desirability — jobs being plentiful, traffic not too
congested, pollution low, and public services well-covered. Industry emits
pollution, and zoning steers housing onto the cleanest land and industry onto
the dirtiest, so districts self-segregate over time. The city also builds
fire/police/health/education facilities as it grows (spread across the
developed area); their coverage feeds desirability, and pollution and
under-coverage both feed back to slow migration.

Land value (Phase 5, M12) is recomputed every tick from a zone base value plus
three location factors: distance to the nearest job (commercial/industrial
building, via a road-network BFS), distance to the nearest covered service
facility, and local pollution — replacing what used to be a value fixed once
at zoning time and never updated. It drives the reported average land value
and, through it, the economy's health score. The job-access BFS is the
costliest phase in the tick breakdown at city scale; throttle it with
`--simulate-land-value-interval N` (like the existing
`--simulate-service-interval`/`--simulate-traffic-interval` knobs) on large
simulations — land value simply persists at its last computed value between
recomputes.

The economy also models a supply chain (Phase 5, M12): industrial occupancy
produces goods, commercial occupancy consumes them, and the city trades the
net difference with the outside world — a surplus earns export revenue, a
shortfall costs import expense at a deliberately higher rate (importing is a
real economic penalty, not a wash). A city with commerce but no local
industry visibly pays for its imports in the budget summary; a heavily
industrial city with modest commerce earns export revenue instead. This
requires no extra setup — it's computed automatically from existing building
occupancy data every time the economy is calculated.

Inflation (Phase 5, M12) is opt-in via `--simulate-inflation-rate RATE` (default
0 = disabled, leaving every existing run bit-for-bit unaffected). It's a
compounding per-tick price-level index applied to maintenance costs and trade
prices — both are external, world-market costs — but deliberately not to tax
revenue, which is a percentage of the city's own building stock and only grows
when the city actually builds more. That asymmetry is the point: a city that
stops growing sees its costs climb while its revenue holds flat, the same
budget pressure a real municipality that stagnates faces. The current
multiplier is reported per tick and in the final budget summary.

Office demand (Phase 5, M12) adds a fourth RCI-like category alongside
residential/commercial/industrial: `BuildingType::Office` and `ZoneType::Office`
(zoned with `--zone-rect ... OFFICE`, symbol `O`). Office space follows an
established commercial base rather than leading it - an empty or purely
residential city generates no office demand - and once it ramps in,
autonomous zoning assigns it to the cleanest available land (it commands the
highest base land value of any zone). Office jobs are a genuine third
employment track: high-income residents skew toward office-and-commercial
work while low-income residents skew industrial, and offices pay their own
(highest) tax rate but sit outside the industrial/commercial goods-trade
model entirely - white-collar work neither produces nor consumes physical
goods in this simulation. This is the one M12 mechanic that isn't
behavior-neutral by default: because it's a genuine new zoning category
competing for land, it changes the city's growth mix (not just reported
metrics) for every `--simulate` run, the same way adding an RCI category
naturally would.

Public transit (Phase 5, M13 — complete) auto-places bus routes and, once a
city is large enough, rail lines, as it grows: each route is a fixed path of
stops along the road network (static infrastructure, like a service facility,
not a literal moving vehicle — `TrafficMicroSim` already covers agent-level
simulation for cars) with a per-tick rider capacity. If a route's stop
coverage reaches both the home and work ends of a commute, some of those
commuters ride transit instead of driving, taking that load off the road
network — modal split in action. Capacity constrains ridership (a small
route visibly caps out below demand, reported separately), and routes are on
by default (`--simulate-no-transit` to compare against the pre-transit
baseline). Because commute pairs are sampled uniformly at random each tick
(the same simplification `TrafficSystem` has always used, not something
transit changes), ridership shows up reliably over the course of a run
rather than on any single tick.

Rail (`TransitMode::Rail`) is a second transit mode sharing bus's exact
coverage/offload machinery — only its placement and parameters differ. Bus
routes connect to the *nearest* job (short local hops, denser, modest
capacity); rail connects to the *farthest* reachable job (long trunk lines
spanning the city, sparser, far higher capacity, and a wider walk-to-station
radius). Rail only starts appearing once a city is substantially larger
(roughly one line per 4,000 residents, capped at 3), and its reach noticeably
lifts modal share once it does — commonly 10-20% in a ~7,000-population test
city, versus under 1% from buses alone in the same city.

Districts and policy (Phase 5, M14 — complete) let `--simulate-district NAME
X1 Y1 X2 Y2` (repeatable) define administrative boundaries whose service
budget genuinely shapes growth, not just an after-the-fact report. Each
district's revenue funds a service budget (capped or uncapped, via the same
`DistrictSystem` the standalone `--create-district`/`--set-district-*`
commands already exposed); how well that budget is met, plus how sparse the
district still is, feeds a per-tile build-chance multiplier for its bounds
on the next tick — a district starved of service budget visibly grows slower
than an equally-sized, equally-central one that isn't. A "District Summary"
table prints after the run. This is opt-in only: no `--simulate-district`
flags means no districts, and the simulation behaves exactly as before.

Districts can also carry a zoning ordinance — a list of zone types
autonomous growth isn't allowed to assign within their bounds (manual
`--zone-rect` ignores it; that's a sandbox tool, not a government).
`--simulate-district-archetype NAME ARCHETYPE` applies a named preset:
`INDUSTRIAL` bans Residential/Office and prioritizes fire safety; `TECHHUB`
bans Industrial and prioritizes education; `GENERAL` (the default) applies
no restriction. If demand's preferred type is banned for a tile, growth
falls back to the first still-allowed type rather than leaving the land
stranded — a district that happens to cover the city's growth origin and
bans the type startup demand wants most still develops (just as something
else), instead of deadlocking the whole city.

Disasters (Phase 5, M15 — started) add fire as a per-tick stochastic hazard,
opt-in via `--simulate-disasters` (destructive, so - unlike every other M12-
M14 mechanic - it's off by default; every existing `--simulate` call is
completely unaffected unless you ask for it). Buildings can ignite each tick,
weighted by type (industrial is far more fire-prone) and local pollution; an
ignited building is destroyed immediately and its tile keeps burning for a
few ticks, posing a spread risk to adjacent occupied tiles. Fire station
coverage (the same `ServiceSystem` number that already feeds desirability)
cuts ignition chance, spread chance, and burn duration alike — a proxy for
faster emergency response. `--simulate-fire-risk F` scales the base ignition
chance for tuning; cumulative losses are reported per tick and in the final
summary.

It prints an evolution table (RCI demand, population, building counts, roads,
budget) plus a per-phase timing breakdown, so the same run doubles as an
end-to-end performance profile. Combine with `--generate-terrain` to grow around
water; add `--simulate-report FILE` for a per-tick CSV (including pollution and
land value), or `--simulate-no-traffic` to skip the (dominant) commute phase.

### Traffic Micro-Simulation (Phase 5, M11 — complete)

```bash
./build/bin/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --micro-traffic 40 --micro-traffic-steps 400
./build/bin/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-incidents 5
./build/bin/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-lanes 4
./build/bin/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-following-gap 0.25
```

`--micro-traffic N` grows a city for N ticks, then runs an individual
vehicle-agent traffic model on it: one vehicle per commute batch, each routed
home→job over the road graph and stepped along its path. Each road has
`--micro-traffic-lanes` lanes (default 2), and each lane is tracked as its own
independent single-file channel — a vehicle picks the least-loaded lane when
entering a road and switches lanes mid-edge to overtake a congested one, so
congestion is *emergent* per lane rather than a static batch load or an
aggregate edge-wide count. Within a lane, vehicles car-follow: a follower's
speed each step is capped so it keeps at least `--micro-traffic-following-gap`
(default 0.15, a fraction of an edge) behind whichever vehicle is immediately
ahead of it in the same lane, so a slow or stopped leader visibly backs up
traffic behind it instead of every vehicle in the lane sharing one progress
value. Road junctions with 3+ connections are signalized (alternating green by
axis, offset by coordinates for a rough green wave), and vehicles queue on red
rather than passing through. It reports agent-level stats (vehicles
spawned/arrived, mean trip length in steps, peak/average edge occupancy,
signalized junctions, mean signal wait). Tune the step budget with
`--micro-traffic-steps`. This runs alongside the aggregate `TrafficSystem`
(used elsewhere), which is unchanged.

Add `--micro-traffic-incidents N` to also dispatch N emergency vehicles, each
routed from the nearest of a few demo facility sites to a random building (the
"incident"). Emergency vehicles ignore both congestion slowing and red
signals and get a speed boost, so their response time is a direct, deterministic
comparison against ordinary commute trip times (e.g. ~12 steps for an
emergency dispatch versus ~34 for an average commute on the same city).

There is no literal 2D position within a lane (a vehicle's in-lane placement
is a single float along the edge, not an x/y offset), and lane width/vehicle
size are not simulated — the following gap is a fraction of edge length, not
a physical distance. That level of physical detail is out of scope at this
milestone's fidelity.

### Phase 5 Benchmarking

```bash
./build/bin/UrbanSimCore-cli --size 96 --benchmark-phase5 24
./build/bin/UrbanSimCore-cli --size 96 --benchmark-phase5 24 --benchmark-phase5-focus TRAFFIC
./build/bin/UrbanSimCore-cli --size 96 --benchmark-phase5 24 --benchmark-phase5-focus SERVICE
```

Use `--benchmark-phase5-focus` with one of `ALL`, `GROWTH`, `POPULATION`, `TRAFFIC`, `ECONOMY`, or `SERVICE` to isolate timing for a single subsystem while still executing the full simulation pipeline.

### Traffic Route Diagnostics

```bash
./build/bin/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5
./build/bin/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-origin 10 10
./build/bin/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-destination 15 10
./build/bin/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-origin 10 10 --traffic-destination 15 10
```

Use `--traffic-origin X Y` and/or `--traffic-destination X Y` with `--print-top-edges N` to inspect congestion hotspots for specific commute route subsets.

### District Management

```bash
./build/bin/UrbanSimCore-cli --size 64 --seed 42 --create-district Downtown 5 5 25 25 --create-district Industrial 35 35 55 55 --list-districts
./build/bin/UrbanSimCore-cli --size 64 --seed 42 --run-growth 10 --seed-population 2000 --create-district Downtown 5 5 25 25 --print-district-summary 1
```

Create districts with `--create-district NAME X1 Y1 X2 Y2` (rectangular bounds). List all districts with `--list-districts`. Print district metrics with `--print-district-summary DIST_ID`.

## Project Structure

- **src/core/** — Foundation (Time, Random, EntityId, Commands)
- **src/world/** — Map, Tiles, Parcels, Districts
- **src/entities/** — Building, PopulationGroup, Business, Vehicle
- **src/networks/** — RoadNetwork, UtilityNetwork, Pathfinding
- **src/systems/** — PopulationSystem, ZoningSystem, EconomySystem, DistrictSystem, etc.
- **src/metrics/** — CityMetrics aggregation
- **src/persistence/** — SaveGame, Snapshot, Replay
- **tests/** — Unit and integration tests
- **examples/** — Example simulations and configs
- **configs/** — Configuration files (JSON/YAML)
- **docs/** — Architecture, roadmap, design docs

## Current Status

✅ **Backlog Slices 1-10 Complete** — Core headless simulation, persistence, replay verification, and visualization scaffolding are implemented.

See [IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) for detailed progress.

## Technology Stack

- **Language:** C++17/20
- **Build:** CMake 3.16+
- **Testing:** Google Test
- **Serialization:** nlohmann/json
- **Optional:** SFML or SDL2 for future visualization

## Vision

> Simulate 10,000 citizens using aggregate population groups, grid-based land parcels, graph-based road connectivity, and monthly city budget updates.

From there, expand into traffic micro-simulation, utilities, services, and policies—without losing performance or clarity.

## Documentation

- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — Engine design, data models, systems
- [ROADMAP.md](docs/ROADMAP.md) — Development milestones
- [MVP_SPEC.md](docs/MVP_SPEC.md) — MVP features and simulation loop
- [NEXT_STEPS.md](docs/NEXT_STEPS.md) — What to build next
- [IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) — Progress tracking

## License

MIT
