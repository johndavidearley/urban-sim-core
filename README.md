# UrbanSimCore

A high-performance simulation engine for modeling living cities. Designed to simulate population dynamics, urban growth, economics, traffic, and services at scale.

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
./build/UrbanSimCore-cli --help
```

### Optional Live Visualizer (SDL2)

If SDL2 is installed, CMake adds an extra executable target named `UrbanSimCore-visualizer`.

```bash
cd build
cmake ..
cmake --build . --config Release
./UrbanSimCore-visualizer
```

Controls:
- Arrow keys: pan viewport
- `+` / `-`: zoom in/out
- `1`: zone overlay
- `2`: land value overlay
- `3`: pollution overlay
- `4`: service coverage overlay
- `5`: traffic congestion overlay
- `6`: demand overlay
- `7`: happiness overlay
- `8`: route heatmap overlay
- `O`: cycle route heatmap origin filter (residential anchors)
- `D`: cycle route heatmap destination filter (job anchors)
- `C`: clear route heatmap filters
- `Space`: pause/resume live deterministic ticks
- `.` or `N`: single simulation tick while paused
- `H`: show/hide in-window legend panel
- `Esc`: quit

### Run Tests

```bash
cd build
ctest --verbose
```

### Procedural Terrain

```bash
./build/UrbanSimCore-cli --size 40 --seed 7 --generate-terrain --print-map
./build/UrbanSimCore-cli --size 40 --seed 7 --terrain-water 0.30 --render-map city.ppm
```

`--generate-terrain` stamps water and rough terrain onto the map from a seeded,
smoothed height field — deterministic for a given `--seed`. Water tiles cannot
be zoned or built on, so growth routes around lakes. Tune water coverage with
`--terrain-water FRAC` (0.0–1.0, implies `--generate-terrain`). Generation runs
only on a fresh map; loading a snapshot restores its saved terrain instead.

### Autonomous Demand-Driven Simulation

```bash
./build/UrbanSimCore-cli --size 64 --seed 7 --simulate 80
./build/UrbanSimCore-cli --size 64 --seed 7 --generate-terrain --simulate 80 --simulate-report run.csv
./build/UrbanSimCore-cli --size 96 --seed 7 --simulate 50 --simulate-no-traffic
./build/UrbanSimCore-cli --size 64 --seed 7 --simulate 80 --simulate-inflation-rate 0.02
./build/UrbanSimCore-cli --size 32 --seed 5 --zone-rect 10 10 15 15 OFFICE --place-road 10 16 15 16 --run-growth 20 --print-budget-summary
./build/UrbanSimCore-cli --size 64 --seed 7 --simulate 100
./build/UrbanSimCore-cli --size 64 --seed 7 --simulate 100 --simulate-no-transit
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

It prints an evolution table (RCI demand, population, building counts, roads,
budget) plus a per-phase timing breakdown, so the same run doubles as an
end-to-end performance profile. Combine with `--generate-terrain` to grow around
water; add `--simulate-report FILE` for a per-tick CSV (including pollution and
land value), or `--simulate-no-traffic` to skip the (dominant) commute phase.

### Traffic Micro-Simulation (Phase 5, M11 — complete)

```bash
./build/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30
./build/UrbanSimCore-cli --size 64 --seed 7 --micro-traffic 40 --micro-traffic-steps 400
./build/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-incidents 5
./build/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-lanes 4
./build/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-following-gap 0.25
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
./build/UrbanSimCore-cli --size 96 --benchmark-phase5 24
./build/UrbanSimCore-cli --size 96 --benchmark-phase5 24 --benchmark-phase5-focus TRAFFIC
./build/UrbanSimCore-cli --size 96 --benchmark-phase5 24 --benchmark-phase5-focus SERVICE
```

Use `--benchmark-phase5-focus` with one of `ALL`, `GROWTH`, `POPULATION`, `TRAFFIC`, `ECONOMY`, or `SERVICE` to isolate timing for a single subsystem while still executing the full simulation pipeline.

### Traffic Route Diagnostics

```bash
./build/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5
./build/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-origin 10 10
./build/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-destination 15 10
./build/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-origin 10 10 --traffic-destination 15 10
```

Use `--traffic-origin X Y` and/or `--traffic-destination X Y` with `--print-top-edges N` to inspect congestion hotspots for specific commute route subsets.

### District Management

```bash
./build/UrbanSimCore-cli --size 64 --seed 42 --create-district Downtown 5 5 25 25 --create-district Industrial 35 35 55 55 --list-districts
./build/UrbanSimCore-cli --size 64 --seed 42 --run-growth 10 --seed-population 2000 --create-district Downtown 5 5 25 25 --print-district-summary 1
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
