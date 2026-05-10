# Next Steps: Immediate Implementation Tasks

## Completed Slices

### ✅ Backlog Slice 1: Build System & Core Types
- All tasks complete
- Project builds successfully
- 5 initial tests passing

### ✅ Backlog Slice 2: World Model Foundation
- ✅ CLI `--print-map` command with ASCII visualization
- ✅ CLI `--print-tile X Y` command for detailed tile inspection
- ✅ 21 new comprehensive tests for world model
- ✅ Error handling for bounds checking
- ✅ All 26 tests passing (7ms total)

**Example usage:**
```bash
$ ./bin/UrbanSimCore-cli --size 64 --print-map
Map (64x64):
  Legend: . = empty, # = has road, ~ = water, T = terrain
  [ASCII grid visualization]

$ ./bin/UrbanSimCore-cli --size 32 --print-tile 5 10
Tile at (5, 10):
  Type: Empty
  Zone: None
  Land Value: $100.00
  [tile details]
```

---

## Current: Backlog Slice 3: Road System
**Goal:** Build and query road networks

1. **Implement road network** (`src/networks/`)
   - `RoadNetwork.hpp` — Graph structure (nodes, edges)
   - Road building: add edges between adjacent tiles
   - Road removal: remove edges
   - Connectivity: BFS/DFS to mark connected tiles

2. **CLI commands for road testing**
   - `--place-road X1 Y1 X2 Y2` — Build a road segment
   - `--connectivity-map` — Print which tiles are connected to a starting point

3. **Implement basic pathfinding**
   - `Pathfinding.hpp` — Dijkstra or A* shortest path
   - Returns path as vector of coordinates
   - Handle unreachable destinations gracefully

4. **Add tests**
   - Road placement and removal
   - Connectivity checking
   - Pathfinding accuracy

**Deliverable:** Can build roads; connectivity and shortest paths computed.

## Remaining Slices

### Backlog Slice 4: Zoning and Parcels
**Goal:** Allow zoning and prepare for building growth

1. **Implement parcel system** (`src/world/`)
   - `Parcel.hpp` — Extend Tile with zone, land value, occupancy
   - Zone types: EMPTY, RESIDENTIAL, COMMERCIAL, INDUSTRIAL

2. **Zoning commands in CLI**
   - `--zone-rect X1 Y1 X2 Y2 TYPE` — Zone a rectangular area
   - `--print-zones` — Print zoning map

3. **Demand calculation (stub)**
   - Simple formula: demand = random(0, 1) per zone type
   - Update daily or monthly
   - Print demand summary in CLI output

4. **Building entity basics**
   - `Building.hpp` — Type, position, capacity, occupancy
   - EntityStore to hold buildings
   - Test creation and storage

**Deliverable:** Can zone land; demand is calculated; buildings can be spawned (not yet automatic).

---

### Backlog Slice 5: Building Growth
**Goal:** Automatic building spawning based on demand

1. **Growth rules**
   - Criteria: zoned, road connected, demand exists, random chance
   - Create building when criteria met
   - Update parcel to reference building

2. **ZoningSystem.cpp**
   - Loop over all empty parcels
   - Check growth criteria
   - Spawn buildings

3. **Tests for growth**
   - Demand causes growth
   - No growth without road access
   - No growth in wrong zones

**Deliverable:** Buildings grow when demand + zoning + roads exist.

---

### Backlog Slice 6: Population and Jobs
**Goal:** Model population and employment

1. **PopulationGroup entity**
   - `PopulationGroup.hpp` — Size, income, employment status

2. **PopulationSystem.cpp**
   - Distribute population to residential buildings
   - Calculate employment matching
   - Unemployment rate

3. **Metrics updates**
   - Total population
   - Available housing
   - Available jobs
   - Unemployment %

**Deliverable:** Population grows with housing; unemployment tracked.

---

### Backlog Slice 7: Traffic and Commute
**Goal:** Calculate commute times and impact

1. **TrafficSystem.cpp**
   - For each employed group: calculate commute path
   - Accumulate congestion on road edges
   - Compute travel time = base + congestion

2. **Congestion and happiness**
   - Long commutes reduce happiness
   - Congestion affects city metrics

3. **Tests**
   - Commute calculation
   - Congestion from multiple groups

**Deliverable:** Realistic commute times affecting city state.

---

### Backlog Slice 8: City Budget
**Goal:** Track revenue and expenses

1. **EconomySystem.cpp**
   - Revenue = population × tax rate
   - Expenses = fixed costs per service/road
   - Monthly update

2. **Tax command**
   - `--set-tax-rate CATEGORY RATE` — Adjust taxes

3. **Budget display**
   - Revenue, expenses, surplus/deficit

**Deliverable:** Balanced budget; taxes adjustable by player.

---

### Backlog Slice 9: Metrics and Summary
**Goal:** Comprehensive city status reporting

1. **CityMetrics.hpp & MetricsSystem.cpp**
   - Aggregate all key metrics
   - Recalculate on demand

2. **Pretty-print summary**
   - CLI command `--status` or default output each tick
   - Show population, jobs, happiness, budget, etc.

**Deliverable:** Full status display with all key metrics.

---

### Backlog Slice 10: Save and Load
**Goal:** Persistence and deterministic replay

1. **SaveGame.hpp**
   - JSON serialization of all state
   - Command history

2. **CLI commands**
   - `--save FILE` — Save current state
   - `--load FILE` — Load and run from saved state

3. **Determinism tests**
   - Load → run 100 ticks → compare metrics vs original

**Deliverable:** Games are saveable, loadable, and reproducible.

---

## Timeline & Progress

| Slice | Goal | Status |
|-------|------|--------|
| 1 | Core types & build | ✅ Complete |
| 2 | World model | ✅ Complete |
| 3 | Road system | ⏳ **Ready to start** |
| 4 | Zoning & parcels | ⏹️ Pending |
| 5 | Building growth | ⏹️ Pending |
| 6 | Population | ⏹️ Pending |
| 7 | Traffic | ⏹️ Pending |
| 8 | Economy | ⏹️ Pending |
| 9 | Metrics | ⏹️ Pending |
| 10 | Save/Load | ⏹️ Pending |

**MVP complete target:** ~15-20 days of focused development

---

## How to Proceed

1. **Say "continue"** to start Slice 3: Road System
2. **Implement all tasks** in the slice
3. **Write tests** as you go
4. **Update docs** to mark tasks complete
5. **Commit and push**
6. **Move to next slice**

Each slice is designed to be independent and builds on the previous ones.

