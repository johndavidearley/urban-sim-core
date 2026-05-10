# UrbanSimCore Development Roadmap

## Phase 1: Headless Foundation (M1–M3)

### Milestone 1: Core Engine Scaffolding
- [x] Project structure
- [ ] CMake build system
- [ ] Core types (EntityId, SimulationTime, Random)
- [ ] Unit test framework
- [ ] Basic CLI entry point

**Deliverable:** Executable that creates a 128×128 map and runs 100 empty ticks.

### Milestone 2: World Model and Road System
- [ ] CityMap and Tile data structures
- [ ] Road network graph
- [ ] Pathfinding (A* or Dijkstra)
- [ ] Road building/removal commands
- [ ] Connectivity computation

**Deliverable:** Can place roads; connectivity reported for each tile.

### Milestone 3: Zoning and Building Growth
- [ ] Parcel data structure
- [ ] Zoning commands (ZONE_RESIDENTIAL, ZONE_COMMERCIAL, etc.)
- [ ] Building entity creation
- [ ] Demand calculation (stub)
- [ ] Growth rules (checks for demand, road access, etc.)

**Deliverable:** Buildings spawn when zoning + roads + demand exist.

---

## Phase 2: Population and Economy (M4–M6)

### Milestone 4: Population System
- [ ] PopulationGroup entity
- [ ] Population distribution to buildings
- [ ] Basic migration logic (attracted by housing)
- [ ] Unemployment calculation
- [ ] Population growth over time

**Deliverable:** Population grows and fills residential buildings; unemployment tracked.

### Milestone 5: Traffic and Commute
- [ ] Commute calculation from home to job
- [ ] Road congestion based on commuter load
- [ ] Travel time calculation (base + congestion)
- [ ] Commute burden on happiness

**Deliverable:** Realistic commute times that affect city metrics.

### Milestone 6: Economy and Budget
- [ ] City budget tracking (revenue, expenses, debt)
- [ ] Tax calculation and collection
- [ ] Service and maintenance costs
- [ ] Monthly budget summary

**Deliverable:** Balanced or deficit budget; can adjust tax rates.

---

## Phase 3: Services and Persistence (M7–M8)

### Milestone 7: Services and Utilities
- [ ] Service building types (fire, police, schools, hospitals)
- [ ] Coverage calculation (graph-based, not Euclidean)
- [ ] Service satisfaction affecting happiness
- [ ] Utility stubs (power, water coverage)

**Deliverable:** Service coverage affects city happiness and growth.

### Milestone 8: Save/Load and Determinism
- [ ] JSON serialization of full state
- [ ] Save game command
- [ ] Load game command
- [ ] Deterministic RNG and replay
- [ ] Save/load validation tests

**Deliverable:** Can save, load, and replay a city deterministically.

---

## Phase 4: Visualization (M9–M10)

### Milestone 9: 2D Map Visualization
- [ ] Integration with SFML or SDL2
- [ ] Isometric or top-down renderer
- [ ] Tile, road, building, zone rendering
- [ ] Zoom and pan controls
- [ ] Real-time metrics display

**Deliverable:** 2D visualization of city with buildings, roads, zones.

### Milestone 10: Debug Overlays
- [ ] Land value overlay
- [ ] Demand overlay
- [ ] Service coverage overlay
- [ ] Congestion/traffic overlay
- [ ] Happiness/desirability overlay
- [ ] Route heatmap

**Deliverable:** Multiple debug views toggle-able in real-time.

---

## Phase 5: Advanced Systems (M11+)

### Milestone 11: Traffic Micro-Simulation
- [ ] Individual vehicle agents (not one-per-person)
- [ ] Vehicle routing and pathfinding
- [ ] Intersection and lane logic
- [ ] Traffic signals (basic)
- [ ] Emergency vehicle routing

### Milestone 12: Advanced Economy
- [ ] Commercial and industrial supply chains
- [ ] Land value dynamics (distance to jobs, services, pollution)
- [ ] Office demand
- [ ] Imports/exports
- [ ] Inflation

### Milestone 13: Public Transit
- [ ] Bus routes
- [ ] Train/subway networks
- [ ] Transit demand and capacity
- [ ] Modal split (cars vs transit)

### Milestone 14: Districts and Policies
- [ ] District-level management
- [ ] Zoning ordinances
- [ ] Growth incentives
- [ ] Service budgets by district
- [ ] Special districts (industrial zones, tech hubs)

### Milestone 15: Disasters and Challenges
- [ ] Crime simulation
- [ ] Fire spread
- [ ] Disease/health
- [ ] Natural disasters (earthquakes, floods)
- [ ] Emergency response

### Milestone 16: City Optimization
- [ ] Profiling and performance tuning
- [ ] Batch entity updates
- [ ] Spatial hashing for queries
- [ ] SIMD optimizations where beneficial
- [ ] Target: 60 FPS headless with 500×500 map, 100k population

---

## Timeline

| Phase | Milestones | Est. Duration |
|-------|-----------|---------------|
| 1 | M1–M3 | 4–6 weeks |
| 2 | M4–M6 | 6–8 weeks |
| 3 | M7–M8 | 3–4 weeks |
| 4 | M9–M10 | 4–6 weeks |
| 5 | M11+ | 12+ weeks |

**Total to MVP completion (through M8):** ~17–22 weeks

---

## Success Metrics

By end of Phase 1:
- Headless engine compiles and runs deterministically
- Road and zoning systems working
- Building growth occurs

By end of Phase 2:
- 10,000-citizen city simulates at 30 FPS headless
- Population, employment, traffic, budget all functional

By end of Phase 3:
- Game can be saved and loaded
- Deterministic replay working

By end of Phase 4:
- Visual representation working
- Debug overlays available

By end of Phase 5:
- Advanced systems add depth without sacrificing performance
- Engine handles 100k+ population with advanced features

