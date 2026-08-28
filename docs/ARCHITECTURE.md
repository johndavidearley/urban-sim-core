# UrbanSimCore Architecture

This document describes the **implemented** engine. It is the source of truth
for system boundaries and data flow. `STATUS.md` tracks validation and
priorities; `ROADMAP.md` and `MVP_SPEC.md` retain milestone history and the
original product spec.

If code and this document disagree, fix the document or the code in the same
change. Do not leave intended-but-unbuilt designs in this file.

---

## Design Principles

1. **Simulation-first.** `urban_sim_core` compiles and runs without SDL. CLI,
   tests, and the optional visualizer are hosts of the same library.
2. **Deterministic ticks.** Integer tick counters and seeded RNG. Same seed +
   same mutations → same snapshot checksum.
3. **Aggregate-first.** Buildings and population groups are the unit of
   simulation. Individual vehicle agents exist only in the optional
   `TrafficMicroSim` sidecar.
4. **Static systems over a shared world.** Subsystems are classes of static
   methods. They take `CityMap`, `RoadNetwork`, `EntityStore`, and
   `PopulationStore` by reference. There is no `SimulationState` object and
   no `SimulationSystem` interface.
5. **Inspectable.** Metrics, overlays, PPM export, and CLI printers read the
   same stores the tick mutates.

The engine **does not depend on SDL**. Headless PPM rendering
(`MapRenderer`) and isometric math (`IsometricProjection`) live in the
library so CLI and tests can render without a window. The live visualizer
is a separate executable that calls systems and gameplay tools directly;
there is no command/query façade yet.

---

## Targets

```
┌─────────────────────────────────────────────────────────────┐
│  urban_sim_core  (static library)                           │
│                                                             │
│  Stores:  CityMap  RoadNetwork  EntityStore  PopulationStore│
│  Systems: Growth Population Traffic Transit Services …      │
│  Orchestration: CitySimulator::run  |  playableCityTick     │
│  Helpers: city_sim::*  (roads, zoning, facilities, transit) │
│  Gameplay: RoadTool ZoneTool BulldozeTool ServiceTool       │
│  Persistence: SaveLoadSystem  GameplaySessionSystem         │
│  Headless viz: MapRenderer  IsometricProjection             │
└───────────────┬─────────────────────────────┬───────────────┘
                │                             │
     UrbanSimCore-cli              UrbanSimCore-visualizer
     parse → dispatch →            SDL loop → tools +
     CitySimulator or inspect      playableCityTick
```

CMake (`src/CMakeLists.txt`):

| Target | Kind | Role |
|--------|------|------|
| `urban_sim_core` | static lib | World, systems, tools, persistence, metrics, PPM/iso |
| `UrbanSimCore-cli` | executable | Flag-driven host (`src/main.cpp` + `src/cli/`) |
| `UrbanSimCore-visualizer` | optional executable | SDL2 live builder (`URBAN_SIM_BUILD_VISUALIZER`) |

Visualizer sources are **not** in `urban_sim_core`. SDL is linked only to the
visualizer target.

---

## Shared Stores

There is no single world object. Hosts and orchestrators pass four stores
plus caller-owned extras (facilities, transit routes, funds, caches).

### CityMap (`src/world/`)

Dense row-major grid. A tile **is** the parcel; there is no `Parcel` type.

```cpp
struct Tile {
  Coord position;
  int type;                 // 0=empty, 1=terrain, 2=water
  int zone;                 // 0=none, 1=R, 2=C, 3=I; Office is ZoneType::Office (5)
  bool hasRoad = false;
  bool connectedToRoad = false;
  bool connectedToPower = true;   // live when utilities are enabled
  bool connectedToWater = true;
  uint32_t buildingId = 0;        // 0 = empty; spatial index into EntityStore
};
```

`landValue` and `pollution` are parallel `std::vector<float>` on `CityMap`,
not fields on `Tile`. Hot per-tick numeric loops go through
`CityMap::pollution` / `CityMap::landValue`.

`Tile::buildingId` is the only position → building index. Growth, fire,
disasters, and `BulldozeTool` must keep it in sync with `EntityStore`.

### RoadNetwork (`src/networks/`)

Undirected graph keyed by tile coordinates. Nodes are **lazy**: only tiles
that currently touch a road edge exist in the node table.

- `buildRoad` / `removeRoad` mutate edges and denormalize `Tile::hasRoad`
  (via `const_cast` on the stored `const CityMap&`).
- `topologyVersion` increments only when an edge is actually added or
  removed. Relaying an existing segment is a no-op and does not invalidate
  caches.
- `resolveRoadAnchor` maps a building tile to the road node it paths from
  (self if adjacent to an edge, else first orthogonal neighbor).

`Pathfinding` is a separate stateless A* (Manhattan heuristic, unit edge
costs; optional congestion weight). It is not a method on `RoadNetwork`.

There is no power/water/transit graph in this layer. Utilities are service
facilities; transit is `TransitSystem` routes over the road graph.

### EntityStore (`src/entities/`)

Buildings only.

```cpp
struct Building {
  EntityId id;
  BuildingType type;   // Residential, Commercial, Industrial, Office
  Coord position;
  int capacity;
  int occupancy;
};
```

Secondary indices maintained on create/remove/upsert:

- ID-sorted vectors per `BuildingType`, plus combined `jobIds()`
- O(1) `countOfType` / `capacityOfType`
- `mutationVersion` — bumped on structural APIs (`create` / `remove` /
  `upsert` / non-empty `clear`). Occupancy writes through `getBuilding()`
  do **not** bump it. Service result caches rely on this.

Not thread-safe. Mutation stays on the tick's sequential phases.

IDs come from a process-global `EntityIdUtils::generateEntityId()`.
`clear()` does not reset the generator. Buildings and population groups
share the ID space.

### PopulationStore (`src/entities/`)

```cpp
struct PopulationGroup {
  EntityId id;
  IncomeBand band;     // Low, Middle, High
  uint32_t size;
  uint32_t employed;
};
```

Groups are not bound to a home tile or district. `PopulationSystem::allocate`
writes `Building::occupancy`. Totals are scanned, not cached.
`applyDeaths` sorts IDs then reduces proportionally (determinism).

### Caller-owned extras (not in the four stores)

| Extra | Typical owner | Notes |
|-------|---------------|--------|
| `std::vector<ServiceFacility>` | `CitySimulator::run` locals, or gameplay session | Civic buildings are not `EntityStore` entries |
| `std::vector<TransitRoute>` + `TransitCoverageCache` | same | Routes only grow in current placement |
| `int64_t funds` | visualizer session / `playableCityTick` | Construction treasury |
| `DistrictSystem` | CLI host | AABB policy; autonomous loop reads it |
| `ZoningCandidateIndex`, `TrafficRouteCache`, `ServiceCoverageCache` | orchestrator locals | See Caching |

---

## Orchestration: two tick pipelines

Construction ownership is the split. Shared subsystems are the same static
`*System` calls. `city_sim::*` (`src/systems/CitySimSupport.hpp`) holds
road-grid, zoning-candidate, pollution, facility, and transit placement
helpers used by the autonomous path (and, today, copied in part by
visualizer G-mode).

There is no calendar. `SimulationTime` (`ticksPerDay` / `isMonthBoundary`)
exists and is unused by either tick. Visualizer “speed” is wall-clock delay
between ticks (`tickIntervalMs`).

### Autonomous — `CitySimulator::run`

Host: CLI `--simulate`, default `--ticks N`, policy sweeps, micro-traffic
warmup. The simulator **builds** the city: road grid, demand zoning, civic
facilities, transit.

```cpp
static SimResult run(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  uint32_t seed,
  int ticks,
  const SimOptions& options = {},
  const DistrictSystem* districts = nullptr);
```

`ticks < 0` runs until `options.tickCallback` returns false (infinite /
SIGINT mode). Cross-tick state (caches, burning tiles, deathcare remainder,
lag scalars) lives as locals inside `run`, not on a world object.

Per-tick order:

```
evaluateDemand(store, population)
  → expand road grid if demand high and empty zoned land is scarce
  → updatePollution (active AABB)
  → extendZoningCandidates → partial_sort nearest batch → autoZone
  → GrowthSystem::runStep          [district modifiers from previous tick]
  → desirability (jobs, lagged congestion/services/crime/illness, pollution)
  → PopulationSystem::allocate     [if populationInterval]
  → placeFacilitiesIfNeeded + ServiceSystem cache
  → pool: service evaluateFromCache  ∥  main: transit + TrafficSystem
  → FireSystem + NaturalDisasterSystem   [if enableDisasters]
  → LandValueSystem                    [if landValueInterval]
  → EconomySystem
  → CrimeSystem / HealthSystem         [lag into next tick's desirability]
  → WasteSystem / DeathcareSystem
  → DistrictSystem → GrowthChanceModifier for next tick
  → SimTickMetrics row
```

One-tick lag is intentional: this tick's growth and migration see last
tick's congestion, service satisfaction, crime, illness, and district
pressure.

`SimOptions` interval fields (`trafficInterval`, `serviceInterval`,
`populationInterval`, `landValueInterval`, `districtInterval`) skip
expensive passes. Traffic, service, and transit summaries are reused from
the previous computed tick so crime, health, waste, deathcare, and the
metrics row do not see zero coverage on a skipped pass. Land values
already persist on the map between recomputes.

Demand is `CitySimulator::evaluateDemand` (stock vs population).
`Zoning::calculateDemand(seed)` is an RNG stub used only as a playable
empty-map fallback.

### Playable — `playableCityTick`

Host: SDL visualizer (and any host that places roads/zones itself).
Construction stays with the host. Same subsystem names, narrower pipeline,
no thread pool, no interval gating.

```cpp
void playableCityTick(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  PlayableCityTickState& state,
  int64_t& funds,
  const PlayableCityTickOptions& options = {});
```

Order (a subset of the autonomous phase list): demand → pollution emitters
→ utility connectivity → growth → allocate `populationTarget` → transit +
traffic → services → land value → economy → `HealthSystem` → waste →
deathcare → `TreasurySystem`.

Playable does **not** run `CrimeSystem`, districts, or disasters.
Population is a session target, not migration desirability. Pollution
emitters and land value default on (`PlayableCityTickOptions`) so overlays
and economy see the same environment model as `--simulate`.

### Visualizer G-mode

`LiveSimulationState::autonomousGrowth` runs `runAutonomousGrowthStep`,
which calls `city_sim::expandConstruction` (the same road/zone/pollution
helper as `CitySimulator::run`) then `playableCityTick`. G-mode opts into
facility placement (utilities + waste/deathcare) during construction;
transit is left to `playableCityTick` so routes are not placed twice.
Empty-zoned pacing and pollution-before-zone match the autonomous path.
Districts are still CLI-only (`ConstructionOptions::districts` is null in
the visualizer). Session save persists the G-mode flag and developed
extent.

---

## Systems

Subsystems are `class FooSystem { public: static … };` with no base class
and no registration. `DistrictSystem` is the only stateful instance.
Adding a system means editing both tick functions (and often G-mode).

| System | Mutates | Cadence (autonomous) | In playable tick? |
|--------|---------|----------------------|-------------------|
| `GrowthSystem` | map, store | every tick | yes |
| `PopulationSystem` | occupancy, groups | `populationInterval` | yes (target, not migration) |
| `TrafficSystem` | edge load | `trafficInterval` | yes |
| `TransitSystem` | cache; offload during traffic | with traffic if `enableTransit` | yes |
| `ServiceSystem` | cache | `serviceInterval` | yes |
| `LandValueSystem` | `map.landValue` | `landValueInterval` | yes (optional flag, default on) |
| `EconomySystem` | none (pure) | every tick | yes |
| `WasteSystem` | map pollution if uncollected | every tick | yes |
| `DeathcareSystem` | population, `DeathcareState` | every tick | yes |
| `HealthSystem` | none | every tick | yes |
| `CrimeSystem` | none | every tick | yes |
| `MetricsSystem` | none | after `CitySimulator::run`; CLI `--print-city-summary` | via `collectFromPlayable` |
| `FireSystem` / `NaturalDisasterSystem` | map, store | `enableDisasters` | no |
| `DistrictSystem` | none during tick (host-owned defs) | `districtInterval` | no |
| `TrafficMicroSim` | local vehicles | CLI `--micro-traffic` after a growth run | no |
| `TreasurySystem` | `funds` | playable only | yes |

`TrafficMicroSim` is a second traffic model (per-vehicle lanes, car-following,
signals). It does not replace `TrafficSystem` and is not composed into either
tick.

`MetricsSystem` / `CityMetrics` is the CLI snapshot schema. Autonomous
time series remain `SimTickMetrics` (one row per tick). After a run,
`SimResult::finalMetrics` is collected with the same `MetricsSystem`
path as `--print-city-summary`. The HUD reads `LiveSimulationState` (treasury, waste, deathcare, crime,
illness) rather than `CityMetrics`.

---

## Gameplay tools

Player mutations are **plan-then-apply tools**, not a `Command` hierarchy
and not a replay log.

| Tool | Plan type | Used by |
|------|-----------|---------|
| `RoadTool` | `RoadPlan` | visualizer; CLI `--place-road` |
| `ZoneTool` | `ZonePlan` | visualizer; CLI `--zone-rect` (R/C/I/Office) |
| `BulldozeTool` | `BulldozePlan` | visualizer |
| `ServiceTool` | `ServicePlan` | visualizer; CLI `--add-service` / `--add-power-source`; `operatingCostPerTick` in `playableCityTick` |

`apply` / `build` re-runs `plan` and refuses if the world diverged. Funds
are `int64_t&` owned by the session.

CLI inspect/setup has **no treasury**. It still calls the tools so water,
occupied tiles, and (for roads/zones) geometry match the visualizer; it
passes `INT64_MAX` so construction cost is waived. `--add-service` and
`--add-power-source` are coverage fixtures: they set
`ServicePlacementOptions::requireRoadAccess = false` and an optional
distance override so `--run-service-evaluation` works on an empty map.
`--zone-rect PARK|NONE` still uses `Zoning::applyZoneRect` because those
types are not player paint tools. District ordinances constrain
autonomous `autoZone` only; manual `--zone-rect` does not apply them.

---

## Hosts

### CLI

```
main
  parseCliArgs          // CliParse — help / validation may exit
  runCliApp             // try/catch
    tryEarlyCliDispatch // --simulate, --benchmark-phase5, --micro-traffic,
                        // snapshot inspect/audit, pressure CSV tools
    runCityCliWorkflow  // load/inspect/mutate a city, or default --ticks N
```

Default `--ticks N` (size/seed/terrain only) calls the same
`CitySimulator` path as `--simulate`. Inspect flags (`--run-growth`,
`--seed-population`, `--run-commute-simulation`, `--run-economy-calculation`,
`--run-service-evaluation`) are **one-shot subsystem probes** on purpose:
they do not advance `playableCityTick`. Economy inspect passes the map so
land value matches the playable/autonomous model.

### Visualizer

File split, not a façade: `VisualizerTypes`, `Overlay`, `Render`, `Hud`,
`Session`, and `VisualizerSDL.cpp` (`main`, event loop, tool dispatch).
After a tool apply or session load the visualizer calls
`refreshLiveDerivedState` → `refreshDerivedCityState` (utilities, services,
land value, economy, health, crime, optional occupancy/traffic) instead of
invoking those systems ad-hoc. The HUD reads those fields from
`LiveSimulationState`. Overlays may still call `TrafficSystem` diagnostics
for route heat.

---

## Persistence

Snapshots, not command logs.

**City snapshot** (`SaveLoadSystem` / `CitySnapshot`): versioned JSON of
map, buildings, population groups, roads. No facilities, treasury, tick,
transit, or G-mode. CLI `--save-city` / `--load-city`. Current schema
version is 1 (0→1 migration exists).

**Gameplay session** (`GameplaySessionSystem`): visualizer default
`urban_sim_session.json` plus sidecar `*.city.json`. Session JSON holds
funds, tick, pause, speed, demand, treasury, population target, deathcare
remainder, facilities, G-mode flag/extent/empty-zoned count, and transit
routes. Coverage caches are rebuilt on load. Older sessions without
transit/empty-zoned fields still load (routes empty, next tick can place).

**Replay** (`ReplayVerifier`): run a **scripted** zone/road/growth/pop/
commute/economy scenario twice and compare a snapshot checksum. This
proves determinism of that script. It is not player-command replay.

---

## Determinism

- `DeterministicRandom` wraps seeded `std::mt19937`.
- Hash-map iteration is unordered; systems that consume RNG sort by
  `EntityId` (or use `EntityStore` type indices, which are kept ID-sorted).
- Traffic commute specs are collected sequentially (RNG), then pathfinding
  may run in parallel (pure topology reads after `resetCongestion`).
- `ReplayVerifier` and sanitizer CI (ASan/UBSan, TSan) are the safety net.

`EntityIdUtils::generateEntityId` is not atomic. Entity creation must stay
off pool workers.

---

## Concurrency and caching

The autonomous tick creates one `ThreadPool` (`hardware_concurrency - 1`
workers) and reuses it every tick. Playable ticks are serial.

| Pass | Parallel | Sequential |
|------|----------|------------|
| Traffic | A* chunks across workers | Spec collection (RNG), congestion accumulation |
| Services | Building coverage slices; whole eval can overlap traffic | Facility placement, BFS cache rebuild |
| Pollution | Clear pass (row strips) | Scatter onto the shared field |
| Zoning candidates | Row-strip scan | Tile mutation, partial_sort, `autoZone` |
| Growth | Read-only zone-balance scan | Spawn/demolish (`EntityStore` is not thread-safe) |

**Deadlock rule:** only the main thread submits work that waits for pool
results. Pool tasks never wait on inner pool tasks. Traffic therefore runs
on the main thread (it waits for Dijkstra chunks) while service evaluation
occupies a worker. `evaluateFromCache(..., &pool)` from that worker would
deadlock a one-worker pool; the overlapping service task passes `nullptr`.

Work-size gates skip pool submit on small maps (e.g. row strips below a
minimum row count).

### Caches

| Cache | Invalidation |
|-------|----------------|
| `ServiceCoverageCache` | Facility signature + road `topologyVersion`; result cache uses `EntityStore::mutationVersion` |
| `TransitCoverageCache` | `routes.size() != builtForRouteCount` (routes only grow) |
| `TrafficRouteCache` | `RoadNetwork::getTopologyVersion()` — **autonomous only** |
| `ZoningCandidateIndex` | Incremental ring as extent grows; erase+compact after a zone batch |
| `emptyZonedCount` | Spawn/demolish/disaster deltas — **autonomous only** |
| Land-value dense distance field | Generation stamp (avoid full-map clears) |

### Measured throughput (reference)

At 200k+ population on an 8-core machine (7 pool workers), a prior
instrumented run:

```
Total: ~1.96 ms/tick  (~2.8× vs. single-threaded ~5.53 ms/tick)
  Traffic:     1.14 ms  (parallel A*; dominant)
  Services:   ~0.01 ms  (result cache hit on stable ticks)
  Growth:     ~0.18 ms
  Zoning:     ~0.34 ms
  Population: ~0.01 ms
```

Re-measure with `./build/bin/UrbanSimCore-cli --benchmark-phase5 50` after
hot-path changes. Release builds matter: the default configure type is
Release for this reason.

Remaining autonomous bottlenecks: traffic (route reuse is implemented;
sample count is the next lever) and sequential `autoZone` mutation.

---

## Testing

- Unit tests per system with constructed `CityMap` / stores (GoogleTest).
- Integration: `CitySimulator` runs, growth, traffic, services.
- Determinism: `ReplayVerifier` plus tests that parallel pathfinding
  matches serial output.
- Performance: `--benchmark-phase5` (optional `--benchmark-phase5-focus`).
- CI: strict RelWithDebInfo on Linux/macOS/Windows; Linux ASan/UBSan and
  TSan. See `STATUS.md` for the live test count.

---

## Source map

| Directory | Contents |
|-----------|----------|
| `src/core/` | `EntityId`, `DeterministicRandom`, `SimulationTime` (unused by ticks), `ThreadPool`, `TileScale` |
| `src/world/` | `CityMap`, `Tile`, `Zoning`, `TerrainGenerator` |
| `src/entities/` | `EntityStore`, `Building`, `PopulationStore`, `BuildingPartitions` |
| `src/networks/` | `RoadNetwork`, `Pathfinding` |
| `src/systems/` | Subsystems, `CitySimulator`, `PlayableCityTick`, `CitySimSupport` |
| `src/gameplay/` | Plan/apply tools, `TreasurySystem` |
| `src/metrics/` | `CityMetrics`, `GrowthMetrics` |
| `src/persistence/` | Snapshot, session, replay checksum |
| `src/cli/` | Parse, dispatch, workflow, printers, sweeps (CLI exe only) |
| `src/visualization/` | `MapRenderer` + iso in the lib; `Visualizer*` in the SDL exe |

Districts live in `DistrictSystem`, not `src/world/`.

---

## Intentional gaps (do not “fix” by inventing the old design)

These appeared in earlier drafts of this document and **are not** the
implementation. Do not add them unless a concrete host needs them:

- Virtual `Command` / `commandQueue` / command-history replay. Tools +
  snapshots are the player API. Record tools only if true session replay
  is required.
- `SimulationSystem` + `UpdateScheduler` + day/week/month frequencies.
  Interval fields on `SimOptions` and a hardcoded phase list are the
  scheduler.
- A mega `SimulationState` that copies the four stores. Pass references.
  A thin holder type is fine; a bag of copies is not.
- Vehicles, citizens, or facilities inside `EntityStore`.
- Power/water as separate graph types. Coverage is `ServiceSystem` BFS
  over the road graph.

---

## Next architecture work

Keep the four stores and static systems. Close the host fork:

1. ~~**One construction helper**~~ done: `city_sim::expandConstruction`
   is shared by `CitySimulator::run` and visualizer G-mode.
2. ~~**Playable as a subset of autonomous**~~ done: `playableCityTick`
   runs the same environment/civic phases (pollution, services, land
   value, health, crime, waste, deathcare, economy) with host-owned
   construction and treasury. Interval-skip reuses last traffic/service
   summaries instead of zeroing coverage. Districts and disasters stay
   autonomous/opt-in.
3. ~~**Keep tools as the player mutation API.**~~ done: CLI
   `--place-road` / `--zone-rect` / `--add-service` / `--add-power-source`
   call the gameplay tools. Cost is waived (no CLI treasury); service
   flags skip the road gate because they are coverage fixtures.
4. **Do not add a command/query façade** until a second interactive
   frontend (WASM, editor, network) exists. File-level splits of CLI and
   visualizer are enough at this size.
5. ~~**Unify metrics**~~ done for the CLI snapshot: `MetricsSystem`
   collects `CityMetrics` from live stores (`PopulationSystem::summarize`)
   or `PlayableCityTickState`. `SimResult::finalMetrics` is that snapshot
   after `CitySimulator::run`; `--simulate` and `--print-city-summary`
   print the same report. `SimTickMetrics` remains the per-tick time
   series (CSV / evolution table); HUD still reads playable fields
   directly.

Compile isolation of large reporters (`CityPrinters`,
`GrowthPressureReport`, `VisualizerSDL.cpp`) is a build-time concern, not
an architecture change. See `STATUS.md` for current priorities.
