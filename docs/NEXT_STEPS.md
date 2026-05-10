# Next Steps: Immediate Implementation Tasks

## Current Backlog (Ordered by Priority)

### Backlog Slice 1: Build System & Core Types
**Goal:** Get the project compiling and running

1. **Create CMakeLists.txt**
   - Top-level CMake configuration
   - Source subdirectories
   - Google Test integration
   - Output directories (bin, lib)
   - Compiler flags (C++17, warnings)

2. **Implement core types** (`src/core/`)
   - `EntityId.hpp` — Type alias and utilities
   - `SimulationTime.hpp` — Time tracking, day/month boundaries
   - `Random.hpp` — Seeded deterministic RNG
   - `Types.hpp` — Common types (Coord, Rect, etc.)

3. **Create CLI entry point** (`src/main.cpp`)
   - Parse command-line arguments (map size, seed, ticks)
   - Create empty SimulationState
   - Run N ticks
   - Print tick summary to stdout
   - Basic error handling

4. **Set up Google Test**
   - `tests/CMakeLists.txt`
   - Example test: "EntityId increments correctly"
   - Run tests via `ctest`

**Deliverable:** Executable `./build/UrbanSimCore-cli` that:
```
$ ./build/UrbanSimCore-cli --size 64 --ticks 100 --seed 42
Initializing city (64x64)...
Running 100 ticks...
Tick 0: Time 0h (Day 0, Month 0)
...
Tick 99: Time 99h (Day 4, Month 0)
Simulation complete. No errors.
```

---

### Backlog Slice 2: World Model Foundation
**Goal:** Create map, tiles, parcels

1. **Implement world structures** (`src/world/`)
   - `Tile.hpp` — Position, terrain type, zone, building ref
   - `CityMap.hpp` — Grid storage, accessor methods, bounds checking

2. **Create CLI commands for map inspection**
   - `--print-map` — Print ASCII representation of map
   - `--print-tile X Y` — Print detailed tile info
   - Example: `./UrbanSimCore-cli --size 32 --seed 42 --print-map`

3. **Add basic tests**
   - `tests/WorldTests.cpp`
   - Test tile creation, map bounds, valid coordinates

**Deliverable:** Map creation and printing; can inspect individual tiles.

---

### Backlog Slice 3: Road System
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

---

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

## Current Priority

**Start with Backlog Slice 1** (Build System & Core Types).

This establishes:
- A working CMake build
- Core type definitions
- A running CLI executable
- Test framework

Once you complete Slice 1, update this document and move to Slice 2.

---

## How to Proceed

1. **Pick one backlog slice** above.
2. **Create the feature branch** (optional, depending on your workflow).
3. **Implement all tasks** in the slice.
4. **Write tests** as you go.
5. **Update IMPLEMENTATION_STATUS.md** to mark tasks complete.
6. **Commit and push.**
7. **Move to next slice.**

Each slice should take 1–3 days depending on complexity.

---

## Completed Slices

(None yet—mark as you finish!)

