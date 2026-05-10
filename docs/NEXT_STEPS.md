# Next Steps: Immediate Implementation Tasks

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
- Add multi-group demographics and income bands
- Add explicit commute/job matching constraints beyond capacity-only assignment
- Add CLI command to print grouped population composition

---

## Remaining Slices

### Backlog Slice 6: Population and Jobs
### Backlog Slice 7: Traffic and Commute
### Backlog Slice 8: City Budget
### Backlog Slice 9: Metrics and Summary
### Backlog Slice 10: Save and Load

---

## Timeline & Progress

| Slice | Goal | Status |
|-------|------|--------|
| 1 | Core types & build | ✅ Complete |
| 2 | World model | ✅ Complete |
| 3 | Road system | ✅ Complete |
| 4 | Zoning & parcels | ✅ Complete |
| 5 | Building growth | ✅ Complete |
| 6 | Population | ⏳ Current |
| 7 | Traffic | ⏹️ Pending |
| 8 | Economy | ⏹️ Pending |
| 9 | Metrics | ⏹️ Pending |
| 10 | Save/Load | ⏹️ Pending |

