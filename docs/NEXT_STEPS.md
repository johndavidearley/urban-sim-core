# Next Steps: Immediate Implementation Tasks

## Post-Backlog Focus (Phase 3 Continuation)

1. Deterministic replay parity checks
- ✅ Implemented `ReplayVerifier` checksum tooling
- ✅ Added CLI verification command: `--verify-replay N`

2. Services and utilities simulation
- ✅ Added service facility categories (fire/police/school/health)
- ✅ Added graph-based coverage scoring and happiness impact
- ✅ Added CLI service commands (`--add-service`, `--run-service-evaluation`, `--print-service-summary`)

3. Persistence hardening
- ⏭️ Add snapshot schema version migration guards
- ⏭️ Expand load validation for malformed/incompatible data

4. Visualization kickoff
- ✅ Added first visualization scaffold (`MapRenderer`) with PPM export
- ✅ Added render CLI commands (`--render-map`, `--render-scale`, `--render-view`)
- ✅ Introduced live renderer integration path (SDL2 optional target)
- ✅ Added real-time map/road/zone/building rendering in SDL mode
- ✅ Added real-time HUD metrics in window title (building mix, roads, zoom, viewport)
- ✅ Added debug overlay toggles in SDL mode (`1` zone, `2` land value, `3` pollution)

5. Visualization hardening
- ✅ Added in-window legend/controls HUD panel without external font dependencies
- ✅ Added service coverage and congestion overlays fed from simulation outputs
- ⏭️ Add pause/step controls and deterministic tick stepping in SDL mode
- ✅ Added demand and happiness overlays for debugging simulation balance
- ⏭️ Add route heatmap overlay for path-flow diagnostics

---

## Completed Slices

### ✅ Backlog Slice 1: Build System & Core Types
- Project structure and CMake build completed
- Core types implemented and tested

### ✅ Backlog Slice 2: World Model Foundation
- CityMap and Tile model implemented
- CLI inspection commands implemented
- World tests added and passing

### ✅ Backlog Slice 3: Road System
- RoadNetwork graph with connectivity tracking
- Dijkstra pathfinding (with congestion-aware option)
- CLI road commands implemented
- 19 network/pathfinding tests added

### ✅ Backlog Slice 4: Zoning and Parcels
- Zoning module added with rectangle application
- CLI zoning commands: `--zone-rect`, `--print-zones`
- Demand stub command: `--print-demand`
- Building entity basics: `Building`, `EntityStore`
- Zoning and entity tests added

---

## Current: Backlog Slice 5 - Building Growth

**Goal:** Automatic building spawning based on demand and road-connected zoning

1. **Growth rules system** (`src/systems/`)
- ✅ Implemented `GrowthSystem` with deterministic seed-based spawning
- ✅ Criteria: zoned tile, road-accessible, positive demand, random chance
- ✅ Building ID persisted back to tile and entity store

2. **Integrate zoning + roads + entities**
- Read zone type and road connectivity from map/network
- Use EntityStore for building creation
- Ensure deterministic behavior from seed

3. **CLI verification command(s)**
- ✅ `--run-growth N` to execute growth steps
- ✅ `--print-buildings` to inspect spawned building entities
- ✅ Commands can be chained with zoning/road commands for scenario testing

4. **Tests**
- ✅ Growth occurs when prerequisites are met
- ✅ No growth without road connectivity
- ✅ No growth for unzoned tiles
- ✅ Deterministic growth for fixed seed

**Deliverable:** Buildings spawn automatically from zoned, connected parcels according to demand. ✅

### Remaining Slice 5 work
- ✅ Improve growth balancing and zone demand shaping (coverage-pressure + demand-floor tuning)
- ✅ Add growth-oriented city metrics/summary output (`--print-growth-summary`)
- ✅ Add additional long-run simulation tests

**Slice 5 status:** ✅ Functionally complete for current scope.

---

## Current: Backlog Slice 6 - Population and Jobs

**Goal:** Model residents, employment, and housing/job allocation

1. **Population entities**
- ✅ Added `PopulationGroup` and `PopulationStore`

2. **Allocation system**
- ✅ Assign residents to residential capacity
- ✅ Assign workers to available jobs

3. **Metrics integration**
- ✅ Population total, housing capacity, available jobs, unemployment

4. **Tests**
- ✅ Deterministic population/job assignment behavior

### Remaining Slice 6 work
- ✅ Add multi-group demographics and income bands
- ✅ Add explicit commute/job matching constraints beyond capacity-only assignment
- ✅ Add CLI command to print grouped population composition

**Slice 6 status:** ✅ Functionally complete for current scope.

---

## Current: Backlog Slice 7 - Traffic and Commute

**Goal:** Introduce commute time and congestion impact from worker movement

1. **Commute modeling** ✅
- ✅ Assignment of employed groups to commute routes
- ✅ Deterministic seed-based commute distribution

2. **Traffic accumulation** ✅
- ✅ Commuter flows mapped onto road network edges
- ✅ Per-edge load tracking with congestion calculation

3. **Metrics impact** ✅
- ✅ Commute burden applied to city metrics
- ✅ Congestion affects happiness calculation
- ✅ Average commute time reported

4. **Tests** ✅
- ✅ 10 comprehensive TrafficSystemTests
- ✅ Deterministic commute and congestion behavior
- ✅ Edge cases (no buildings, no employment, etc.)

5. **CLI Commands** ✅
- ✅ `--run-commute-simulation` - Execute traffic simulation
- ✅ `--print-traffic-summary` - Display commute metrics
- ✅ `--print-top-edges N` - Show most congested roads

**Slice 7 status:** ✅ Functionally complete for current scope.

---

## Current: Backlog Slice 8 - Economy and Finances

**Goal:** Model city budget, taxation, and economic impact

1. **City budget system** ✅
- ✅ Implement taxation from population/buildings
- ✅ Track city revenue and expenses
- ✅ Calculate balance/debt

2. **Building maintenance costs** ✅
- ✅ Residential/commercial/industrial maintenance expenses
- ⏭️ Variable costs based on age/condition (future)

3. **Economic impact on growth** ✅
- ✅ City budget outputs now flow into `CityMetrics`
- ⏭️ Economic zones expansion/contraction remains future work

4. **CLI commands** ✅
- ✅ `--run-economy-calculation` - Execute economy pass
- ✅ `--print-budget-summary` - Show revenue/expenses/balance/health
- ⏭️ `--print-economic-zones` - future extension

**Slice 8 status:** ✅ Functionally complete for current scope.

---

## Current: Backlog Slice 9 - Metrics and Summary

**Goal:** Consolidate subsystem metrics into one citywide summary workflow

1. **Metrics consolidation system** ✅
- ✅ Added `MetricsSystem::collectCityMetrics` to compose population, traffic, and economy outputs

2. **Summary reporting** ✅
- ✅ Added `MetricsSystem::createCitySummaryReport` with budget balance/status lines

3. **CLI workflow** ✅
- ✅ Added `--print-city-summary` for consolidated reporting
- ✅ Added command-path reuse of cached traffic/economy summaries in a single run

4. **Tests** ✅
- ✅ Added `MetricsSystemTests` covering composition and report output

**Slice 9 status:** ✅ Functionally complete for current scope.

---

## Current: Backlog Slice 10 - Save and Load

**Goal:** Add persistence scaffolding for city state snapshots

1. **Persistence model** ✅
- ✅ Added `SaveLoadSystem` snapshot schema and JSON serialization/deserialization

2. **State restoration hooks** ✅
- ✅ Added store import/reset helpers for buildings and population groups
- ✅ Added snapshot apply flow for map tiles, roads, congestion, entities, and population

3. **CLI workflow** ✅
- ✅ Added `--save-city FILE`
- ✅ Added `--load-city FILE`
- ✅ Added loaded-state summary bootstrap so reporting commands can run immediately after load

4. **Tests** ✅
- ✅ Added `SaveLoadSystemTests` for round-trip persistence and failure handling

**Slice 10 status:** ✅ Functionally complete for current scope.

---

## Remaining Slices

No backlog slices remain in the current 10-slice plan.

---

## Timeline & Progress

| Slice | Goal | Status |
|-------|------|--------|
| 1 | Core types & build | ✅ Complete |
| 2 | World model | ✅ Complete |
| 3 | Road system | ✅ Complete |
| 4 | Zoning & parcels | ✅ Complete |
| 5 | Building growth | ✅ Complete |
| 6 | Population | ✅ Complete |
| 7 | Traffic | ✅ Complete |
| 8 | Economy | ✅ Complete |
| 9 | Metrics | ✅ Complete |
| 10 | Save/Load | ✅ Complete |

