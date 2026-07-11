# Implementation Status

> Historical development journal. Several "current" and "next" sections below
> reflect the point in time when they were written. See [STATUS.md](STATUS.md)
> for the authoritative current state and priorities.

Last updated: June 27, 2026

## Overview

| Phase | Status | Progress |
|-------|--------|----------|
| **Phase 1: Headless Foundation** | ✅ Complete | 100% |
| **Phase 2: World Model** | ✅ Complete | 100% |
| **Phase 3: Road System** | ✅ Complete | 100% |
| **Phase 4: Zoning & Parcels** | ✅ Complete | 100% |
| **Phase 5+: Advanced Systems** | ✅ Complete | 100% |

---

## Phase 1: Headless Foundation

### Milestone 1: Core Engine Scaffolding

| Task | Status | Notes |
|------|--------|-------|
| Project structure | ✅ Done | All directories created |
| CMake build system | ✅ Done | Top-level + src + tests CMakeLists.txt |
| Core types | ✅ Done | EntityId, SimulationTime, Random, Tile, CityMap |
| Unit test framework | ✅ Done | Google Test integrated |
| Basic CLI entry point | ✅ Done | UrbanSimCore-cli compiling and running |

**Status:** ✅ Complete

---

## Phase 2: World Model

### Milestone 1: CityMap & Inspection

| Task | Status | Notes |
|------|--------|-------|
| CityMap and Tile | ✅ Done | Grid storage, bounds checking, tile defaults |
| CLI map printing | ✅ Done | `--print-map` command |
| CLI tile inspection | ✅ Done | `--print-tile X Y` command |
| World tests | ✅ Done | 21 CityMap tests + Tile tests |

**Status:** ✅ Complete

---

## Phase 3: Road System

### Milestone 1: Road Network Graph & Pathfinding

| Task | Status | Notes |
|------|--------|-------|
| Road graph | ✅ Done | Undirected edges + adjacency lists |
| Connectivity tracking | ✅ Done | BFS over road component |
| Congestion tracking | ✅ Done | Per-edge load/capacity modeling |
| Pathfinding | ✅ Done | Dijkstra + congestion-aware variant |
| CLI road commands | ✅ Done | `--place-road`, `--connectivity-map`, `--find-path` |
| Network tests | ✅ Done | 19 tests covering road/path behavior |

**Status:** ✅ Complete

---

## Phase 4: Zoning & Parcels

### Milestone 1: Zoning Workflow & Entity Basics

| Task | Status | Notes |
|------|--------|-------|
| Zoning module | ✅ Done | `src/world/Zoning.hpp/.cpp` with zone parsing and rect application |
| Zone map CLI | ✅ Done | `--zone-rect X1 Y1 X2 Y2 TYPE`, `--print-zones` |
| Demand stub | ✅ Done | `--print-demand` deterministic demand output from seed |
| Land value by zone | ✅ Done | Zone defaults applied during zoning |
| Building entity | ✅ Done | `Building` + `EntityStore` with creation/retrieval |
| Slice 4 tests | ✅ Done | Zoning + entity store tests |

**Status:** ✅ Complete

---

## Build and Test Status

✅ **Build successful**

✅ **292 tests passing** (28 test suites)

### Test Suite Breakdown

CTest discovers each GoogleTest case individually. Use `ctest --test-dir
build --show-only` for the current authoritative list; this avoids maintaining
a second, quickly stale per-suite inventory here.

---

---

## Performance Optimization Pass (June 2026)

A dedicated multithreading and algorithmic optimization pass targeting the autonomous simulation loop at 200k+ population. Baseline before this work: ~5.53 ms/tick. Result after: ~1.96 ms/tick (estimated ~2.8× overall speedup).

### Thread Pool (`src/core/ThreadPool.hpp`)
- Added fixed-size `ThreadPool` class with a work queue, `std::packaged_task`, and `std::condition_variable`
- Pool is created once in `CitySimulator` before the tick loop using `hardware_concurrency - 1` threads
- All parallel paths have minimum work-size thresholds to avoid submission overhead at small city sizes

### Population Allocation (`PopulationSystem`)
- Replaced O(people) round-robin hash-map loop with O(buildings) proportional fill
- Each building receives `floor(capped × capacity / totalCapacity)` residents in one pass; a small round-robin loop handles the remainder
- Result: ~86× speedup on the population phase at 200k pop

### Traffic — Parallel Dijkstra (`TrafficSystem`)
- Commute spec collection remains sequential (preserves RNG determinism)
- All Dijkstra path-finds are fanned out to pool workers after `resetCongestion` (all edges read-only at that point, safe for concurrent reads)
- Congestion accumulation stays sequential after all futures resolve
- Traffic runs on the main thread (not submitted to pool) so inner pool tasks cannot deadlock

### Services — Parallel Chunk Evaluation + BFS Cache (`ServiceSystem`)
- `ServiceCoverageCache` pre-builds one BFS distance field per facility; cache rebuilt only when `facilities.size()` changes
- `evaluateFromCache` partitions buildings across pool workers (threshold: `buildings × facilities >= 4096`)
- Service evaluation submitted to pool concurrently with traffic pathfinding on the main thread
- `RoadNetwork::nodes` (topology, read by services) and `edges` (congestion, written by traffic) are separate data structures — safe concurrent access
- Added result cache: `cachedBuildingCount + cachedResult` in `ServiceCoverageCache`. Evaluation is skipped entirely when neither building count nor facility count has changed since last tick. At 200k+ pop this skips evaluation on ~98%+ of ticks

### Pollution — LUT + Parallel Clear (`CitySimulator::updatePollution`)
- Precomputed 7×7 falloff weight lookup table eliminates `sqrt()` from the scatter hot loop
- Parallel clear phase fans out across pool workers (threshold: `nRows >= 32`); scatter stays sequential

### Zoning — Parallel Candidate Scan (`CitySimulator::zonableCandidates`)
- Candidate scan partitioned into row strips across pool workers (threshold: `nRows >= 16`)
- Each worker returns a partial `std::vector<Coord>`; main thread merges and sorts by distance

### Growth — Parallel Balance Scan + Combined Mutation Pass (`GrowthSystem`)
- **Pass 1 (zone balance count)**: Read-only scan fanned out across pool workers in row strips (threshold: `nRows >= 32`); partial `ZoneBalance` structs are reduced by summation
- **Passes 2–4 (demolition, redevelopment, build)**: Collapsed from three separate full-area scans into one combined pass. Each tile is read once and routed by occupancy: occupied tiles check demolition then redevelopment; empty tiles check for new construction. Reduces tile reads by 3×. RNG draw order is interleaved per-tile rather than per-phase (deterministic but not byte-identical to prior three-pass behavior)

### Measured Results (at tick 24830, population 201k, 527 buildings)
| Phase | Before | After (est.) |
|-------|--------|--------------|
| Traffic | 1.14 ms/tick | 1.14 ms/tick (already parallel) |
| Services | 0.69 ms/tick | ~0.01 ms/tick (result cache) |
| Growth | 0.54 ms/tick | ~0.18 ms/tick |
| Zoning | 0.34 ms/tick | ~0.34 ms/tick |
| Population | 0.12 ms/tick | ~0.01 ms/tick (proportional fill) |
| **Total** | **5.53 ms/tick** | **~1.96 ms/tick** |

---

## Current Focus

### Post-Backlog Phase 4: Visualization Iteration

Implemented so far:
- Dependency-free renderer module added: `MapRenderer` (PPM export)
- Optional SDL2 live visualizer target added: `UrbanSimCore-visualizer`
- Live visualizer renders map, roads, zones, and building types in real time
- Keyboard controls implemented: arrows pan, +/- zoom, ESC quit
- Real-time HUD metrics added through dynamic window-title updates
- In-window legend panel added with overlay scale hints and active mode indicators
- Debug overlay toggles added: zone (`1`), land value (`2`), pollution (`3`)
- Service coverage overlay added in live visualizer (`4`)
- Traffic congestion overlay added in live visualizer (`5`)
- Demand overlay added in live visualizer (`6`)
- Happiness overlay added in live visualizer (`7`)
- Route heatmap overlay added in live visualizer (`8`)
- Pause/resume and deterministic single-step tick controls added (`Space`, `.`/`N`)
- Route heatmap filter controls added in live visualizer (`O` origin cycle, `D` destination cycle, `C` clear)

Next implementation target:
- Performance pass and advanced route diagnostics

---

## Phase 5 Kickoff: Performance Baseline

Implemented so far:
- Added CLI benchmark command: `--benchmark-phase5 N`
- Added benchmark focus toggle: `--benchmark-phase5-focus ALL|GROWTH|POPULATION|TRAFFIC|ECONOMY|SERVICE`
- Benchmark run seeds a representative zoned/road-connected scenario on configurable map size
- Reports per-subsystem timing totals and average-per-tick:
	- Growth
	- Population allocation
	- Traffic simulation
	- Economy calculation
	- Service coverage evaluation
- Service coverage optimization pass completed:
	- Replaced per-building/per-facility shortest-path searches with cached per-facility BFS distance fields
	- Benchmark smoke reduction at `--size 96 --benchmark-phase5 24`:
		- Service time: ~1644 ms -> ~35 ms total
		- Avg tick time: ~77.7 ms -> ~10.9 ms
- Traffic simulation optimization pass completed:
	- Removed per-commute distribution construction and repeated map lookups in inner loops
	- Reused precomputed residential/job building pointer sets for deterministic selection
	- Added early non-road-endpoint rejection before pathfinding calls
	- Benchmark smoke runs at `--size 96 --benchmark-phase5 24` now show traffic near ~172-178 ms total (from prior ~185 ms range)
- Benchmark focus profiling examples validated:
	- `--benchmark-phase5 24 --benchmark-phase5-focus TRAFFIC` isolates traffic timing while keeping full pipeline execution for stable state evolution
	- `--benchmark-phase5 24 --benchmark-phase5-focus SERVICE` isolates service timing with the same scenario progression
- Route diagnostics filters added for targeted analysis:
	- `--print-top-edges N --traffic-origin X Y` limits diagnostics to commutes from a specific origin
	- `--print-top-edges N --traffic-destination X Y` limits diagnostics to commutes ending at a specific destination
	- combined origin + destination filters are supported for OD-pair hotspot inspection
- Visualization route filters added for live route heatmap exploration:
	- route heatmap now supports interactive origin/destination filter cycling from seeded building anchors
	- HUD now includes current route filter state for quick context while profiling

**Slice 5** - District system foundation (infrastructure + CLI):
- New `DistrictSystem` module created with core data model:
	- `District` struct: rectangular bounds, tax rates per type, service allocation percentage
	- `DistrictMetrics` struct: aggregated district population, buildings, budget, coverage
	- Static factory API for district CRUD: create, get, delete, list, update rates/allocation
- District metrics evaluation implemented:
	- Per-district population count by income band (via PopulationStore)
	- Building type breakdown (residential/commercial/industrial) filtered by district bounds
	- Average land value calculation from tile grid within bounds
	- Budget calculations using district tax rates via EconomySystem
	- Service coverage and happiness stubs (placeholders for future service subsystem integration)
- CLI commands added for district management:
	- `--create-district NAME X1 Y1 X2 Y2` - create district with rectangular bounds; supports multiple invocations
	- `--list-districts` - print all districts with bounds, area, tax rates, and service allocation
	- `--print-district-summary DIST_ID` - print aggregated metrics for a specific district
- Unit tests added: 12 tests covering creation, deletion, tax/allocation setters, metrics evaluation, boundary checks
- Test count increased: 114 -> 126 tests (all passing)

✅ **Slice 12** - District service policies completed (infrastructure + CLI):
- Extended `District` struct with `ServicePriority` weights (fire, police, health, education)
- Extended `District` struct with `assignedFacilityIds` vector for facility assignment
- New service policy API methods:
	- `setDistrictServicePriorities()` - set per-district service weights
	- `assignFacilityToDistrict()` - assign facility to district
	- `unassignFacilityFromDistrict()` - remove facility from district
	- `getFacilitiesForDistrict()` - list facilities assigned to district
- CLI commands for district policy management:
	- `--set-district-tax DIST_ID TYPE RATE` - adjust per-type tax rate for district
	- `--set-district-service DIST_ID FIRE POLICE HEALTH EDUCATION` - set service priority weights
	- `--assign-facility DIST_ID FACILITY_ID` - assign facility to district
- All commands execute before list/summary commands for correct state
- Test count remains: 126 tests (all passing, no regressions)

✅ **Slice 13** - District policy integration into economy/service completed:
- District metrics now evaluate district-scoped economy inputs instead of full-city totals:
	- district-local building set used for revenue/expense calculations
	- district tax rates now materially affect district budget output
	- district population/employment is estimated from residential-capacity share to avoid full-city overcounting
- District metrics now evaluate service coverage from actual service simulation data when available:
	- supports district-assigned facility IDs (1-based IDs from service add order)
	- falls back to facilities physically inside district bounds when no assignments exist
	- applies district service priority weights (fire/police/health/education) to weighted coverage
	- applies district service allocation percentage as coverage scaling factor
- District happiness is now derived from combined economy health + service coverage signal (instead of static stub values)
- CLI district outputs improved:
	- service facility add now prints deterministic facility IDs for district assignment
	- district listing now shows priority weights and assigned facility IDs
	- district summary now passes roads/facilities for policy-aware service metrics
- District tests expanded with regression coverage for:
	- district tax policy effect on district-local revenue
	- assigned-facility + priority-weight impact on district service coverage/happiness

✅ **Slice 14** - District budget allocation controls completed:
- Extended district policy model with explicit service budget cap support (`serviceBudgetCap`)
- Added district metrics budget fields for policy diagnostics:
	- service budget target
	- service budget allocated
	- cap-applied indicator
- District service effectiveness now scales by budget fulfillment ratio (`allocated/target`)
- Added CLI policy tuning commands:
	- `--set-district-allocation DIST_ID PERCENT`
	- `--set-district-budget-cap DIST_ID AMOUNT`
- District list/summary output now surfaces budget allocation/cap state for inspection
- Added district tests for budget-cap setter and cap impact on service coverage

✅ **Slice 15** - Citywide district shared-budget balancing completed:
- Added shared budget balancing in `DistrictSystem::evaluateAllDistricts(..., sharedServiceBudgetPool)`
- Implemented proportional allocation under constrained pool with cap-aware redistribution
- Added rebalance recomputation of district service outcomes:
	- allocated budget and cap-applied flag
	- budget-fulfillment-scaled service coverage
	- happiness recomputed from economic health + balanced service coverage
- Added CLI diagnostics command:
	- `--print-district-balancing POOL`
	- prints per-district target/allocated/coverage/cap state and total allocated
- Added district tests for shared-pool competition and cap-driven redistribution behavior

✅ **Slice 16** - District budget pressure tied to growth demand shaping:
- Added growth chance modifier infrastructure in `GrowthSystem` (`GrowthChanceModifier` regions)
- Added CLI pressure-mode option for growth:
	- `--district-pressure-pool POOL`
	- During `--run-growth N`, district shared-budget fulfillment now scales per-district growth chance
- Added cumulative CLI request handling fixes:
	- multiple `--zone-rect` invocations are now applied in one run
	- multiple `--place-road` invocations are now applied in one run
	- axis-aligned `--place-road` ranges are expanded into adjacent edge segments
- District mutation commands are now applied before growth execution so pressure mode uses current district policy state in the same invocation
- Added growth test coverage for region-level chance suppression via modifiers

✅ **Slice 17** - Pressure tuning and diagnostics pass completed:
- Added tuned district growth pressure multiplier model in `DistrictSystem::computeGrowthPressureMultiplier(...)`:
	- budget-fulfillment factor
	- district sparsity boost
	- cap-pressure penalty
	- bounded multiplier range to reduce runaway concentration
- Added CLI growth-pressure diagnostics:
	- `--print-growth-pressure`
	- prints per-step district multiplier/fulfillment/cap-state while running growth under `--district-pressure-pool`
- Added district tests covering multiplier bounds/cap-awareness and fulfillment monotonicity

✅ **Slice 18** - Policy export/report tooling for offline calibration completed:
- Added growth pressure CSV export command:
	- `--export-growth-pressure FILE`
	- requires `--district-pressure-pool` + `--run-growth`
- Export includes per-step per-district calibration data:
	- multiplier
	- budget fulfillment
	- cap state
	- target vs allocated budget
	- district buildings/population snapshot fields
- CSV output validated with mixed district-cap scenario for offline comparison workflows

✅ **Slice 19** - Scenario diff tooling for pressure reports completed:
- Added report comparison CLI command:
	- `--compare-growth-pressure FILE_A FILE_B`
- Implemented CSV parser + schema validation for exported pressure reports
- Added district-level delta summary output for policy comparisons:
	- average multiplier delta
	- average fulfillment delta
	- cap-hit rate delta
	- allocation-share delta (`allocated/target`)
- Comparison validated against identical reports (all deltas expectedly zero)

✅ **Slice 20** - Batch comparison workflow support for policy sweep ranking completed:
- Added ranking command for pressure-report sweeps:
	- `--rank-growth-pressure BASE CANDIDATE` (repeatable for multiple candidates)
- Added baseline-relative score model combining:
	- fulfillment delta (positive)
	- allocation-share delta (positive)
	- multiplier delta (small positive)
	- cap-hit-rate delta (negative penalty)
- Added ranked output with per-candidate deltas and deterministic ordering
- Validated ranking behavior with stricter-cap candidate vs identical baseline report

✅ **Slice 21** - Automated policy sweep runner completed:
- Added end-to-end sweep command:
	- `--run-policy-sweep OUT_DIR`
	- `--sweep-district DIST_ID`
	- `--sweep-seeds A,B,C`
	- `--sweep-caps A,B,C`
	- `--sweep-allocations A,B,C`
- Added scenario orchestration that:
	- captures a baseline snapshot
	- runs per-scenario growth-pressure simulations from that baseline
	- exports per-scenario pressure CSV reports
	- ranks all generated candidates against baseline using existing score model
- Added sweep manifest export:
	- `OUT_DIR/sweep_manifest.csv`
	- includes scenario parameters, report paths, score, and summary deltas
- Enriched sweep manifest with per-district subscore breakdown columns for the swept district:
	- district sample count
	- district-level multiplier/fulfillment/cap-rate/allocation-share deltas
- Added optional all-district breakdown manifest mode:
	- `--sweep-manifest-all-districts`
	- emits `OUT_DIR/sweep_manifest_districts.csv` with one row per scenario per district
	- includes district name, district sample count, and district-level deltas
- Added district-level ranking view in CLI output:
	- prints top and bottom districts by average delta score across sweep candidates
	- includes fulfillment/cap-rate/allocation-share average deltas per district
- Refactored growth pressure execution into reusable helper to support both direct growth runs and sweep execution
- Validated with multi-dimensional smoke sweep (seeds/caps/allocations) producing reports, ranking output, and manifest

Next implementation target:
- Begin Slice 15 economy refinements (interest/loan scaffolding baseline)

✅ **Slice 13 (Phase Start)** - Traffic route caching baseline completed:
- Added per-tick origin/destination route caching in `TrafficSystem` commute simulation path
- Added route caching to route diagnostics path reconstruction for repeated OD requests
- Cache scope kept tick-local to preserve deterministic behavior and avoid cross-run stale routing
- Validated compile and traffic CLI smoke run (`--run-commute-simulation`, `--print-traffic-summary`, `--print-top-edges`)

✅ **Slice 13 (Phase 2)** - Adaptive congestion feedback routing completed:
- Added configurable congestion penalty pathfinding API:
	- `Pathfinding::findShortestPathWithCongestionWeight(...)`
- Added adaptive route-choice loop in `TrafficSystem::simulateCommutes(...)`:
	- dynamically raises congestion penalty weight during peak edge saturation
	- relaxes weight under lighter path congestion
	- uses epoch-based cache invalidation to keep routing responsive to evolving loads
- Preserved deterministic behavior with tick-local state and seeded commute selection
- Validated compile and traffic CLI smoke run after fixing cache invalidation ordering regression

✅ **Slice 14 (Phase Start)** - Multi-zone demand balancing baseline completed:
- Added growth chance balancing factor that aligns spawned building mix with relative zone demand shares
- Balancing model uses:
	- demand share vs current built share gap (primary)
	- mild zoned supply share gap adjustment (secondary)
	- bounded multiplier clamping to avoid runaway zone swings
- Added growth test coverage for high-demand zone bias across repeated mixed-zone growth steps
- Validated compile and mixed-zone CLI growth smoke run (`--print-growth-summary`, `--print-buildings`)

✅ **Slice 14 (Phase 2)** - Early aging/demolition scaffolding completed:
- Added `EntityStore::removeBuilding(...)` support for growth-side teardown operations
- Extended `GrowthStats` with demolition counters and surfaced demolition output in growth-step CLI logs
- Added low-demand overbuild demolition pass in `GrowthSystem::runStep(...)`:
	- eligible when zone demand is low and built coverage materially exceeds target coverage
	- deterministic seeded chance, bounded to avoid runaway teardown
	- skips unmanaged placeholder building IDs to avoid invalid snapshot/entity mismatches
- Added growth test coverage for demolition behavior under sustained low-demand overbuilt conditions
- Validated compile and growth CLI smoke runs showing live demolition events and stable summaries

---

## Persistence Hardening (Post-Backlog)

Implemented so far:
- Snapshot schema migration guard added for legacy version `0` snapshots (auto-migrated to current schema)
- Snapshot version gate added to reject unsupported future versions
- Structural/data validation added before apply/load:
	- dimensions and tile-count integrity
	- tile coordinate uniqueness and bounds checks
	- building reference integrity from tiles
	- occupancy/employment consistency checks
	- road adjacency and load sanity checks
- Save/load tests expanded to cover migration and malformed snapshot rejection
- CLI load flow now prints migration diagnostics (source version, target version, migration path)
- Added snapshot inspection command to print schema and structural snapshot summary without simulation run

Next implementation target:
- Performance pass and profiling for larger map/population workloads

---

## Phase 3 Continuation: Services and Utilities

Implemented so far:
- Service simulation module added: `ServiceSystem`
- Service facility types: Fire, Police, Health, Education
- Graph-distance road coverage evaluation (BFS over road graph)
- Service satisfaction and coverage metrics integrated into `CityMetrics`
- CLI service commands: `--add-service`, `--run-service-evaluation`, `--print-service-summary`
- City summary now includes service coverage and satisfaction
- Service system tests validate reachability logic and happiness impact

Next implementation target:
- Visualization milestone progression and schema-version hardening

---

## Phase 4 Progress: Visualization

Implemented so far:
- Added dependency-free top-down renderer module: `MapRenderer`
- Image export command added: `--render-map FILE`
- View controls added: `--render-scale N`, `--render-view X Y W H`
- Rendering includes tiles, roads, and building type color layers
- Optional SDL2 live visualizer target added: `UrbanSimCore-visualizer`
- Live visualizer includes keyboard pan/zoom controls (arrows, +/-)
- Live visualizer HUD metrics shown in window title (updated during runtime)
- Live visualizer in-window legend panel for overlay controls and scale bands
- Live visualizer debug overlays for zone, land value, pollution, service coverage, traffic congestion, demand, happiness, and route heatmap
- Live visualizer supports deterministic live tick progression and manual stepping
- Renderer tests validate PPM format and viewport clamping behavior

Next implementation target:
- Performance tuning and route diagnostics refinement
