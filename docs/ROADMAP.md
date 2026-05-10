# UrbanSimCore Development Roadmap

Last updated: May 10, 2026

## Current Status Snapshot

- Backlog Slices 1-10: complete
- Headless core simulation loop: complete
- Population, traffic, economy, metrics, and save/load scaffolding: complete
- Automated validation: 108 tests passing

---

## Phase 1: Headless Foundation (M1–M3)

### Milestone 1: Core Engine Scaffolding
- [x] Project structure
- [x] CMake build system
- [x] Core types (EntityId, SimulationTime, Random)
- [x] Unit test framework
- [x] Basic CLI entry point

**Deliverable:** Executable that creates a 128×128 map and runs 100 empty ticks.

### Milestone 2: World Model and Road System
- [x] CityMap and Tile data structures
- [x] Road network graph
- [x] Pathfinding (A* or Dijkstra)
- [x] Road building/removal commands
- [x] Connectivity computation

**Deliverable:** Can place roads; connectivity reported for each tile.

### Milestone 3: Zoning and Building Growth
- [x] Parcel data structure
- [x] Zoning commands (ZONE_RESIDENTIAL, ZONE_COMMERCIAL, etc.)
- [x] Building entity creation
- [x] Demand calculation (stub)
- [x] Growth rules (checks for demand, road access, etc.)

**Deliverable:** Buildings spawn when zoning + roads + demand exist.

---

## Phase 2: Population and Economy (M4–M6)

### Milestone 4: Population System
- [x] PopulationGroup entity
- [x] Population distribution to buildings
- [x] Basic migration logic (attracted by housing)
- [x] Unemployment calculation
- [x] Population growth over time (seeded deterministic allocation)

**Deliverable:** Population grows and fills residential buildings; unemployment tracked.

### Milestone 5: Traffic and Commute
- [x] Commute calculation from home to job
- [x] Road congestion based on commuter load
- [x] Travel time calculation (base + congestion)
- [x] Commute burden on happiness

**Deliverable:** Realistic commute times that affect city metrics.

### Milestone 6: Economy and Budget
- [x] City budget tracking (revenue, expenses, debt)
- [x] Tax calculation and collection
- [x] Service and maintenance costs
- [x] Monthly budget summary

**Deliverable:** Balanced or deficit budget; can adjust tax rates.

---

## Phase 3: Services and Persistence (M7–M8)

### Milestone 7: Services and Utilities
- [x] Service building types (fire, police, schools, hospitals)
- [x] Coverage calculation (graph-based, not Euclidean)
- [x] Service satisfaction affecting happiness
- [x] Utility stubs (power, water coverage)

**Deliverable:** Service coverage affects city happiness and growth.

### Milestone 8: Save/Load and Determinism
- [x] JSON serialization scaffold for city state
- [x] Save game command (`--save-city FILE`)
- [x] Load game command (`--load-city FILE`)
- [x] Deterministic RNG replay parity checks (`--verify-replay N`)
- [x] Save/load validation tests

**Deliverable:** Can save, load, and replay a city deterministically.

---

## Phase 4: Visualization (M9–M10)

### Milestone 9: 2D Map Visualization
- [x] Integration path for SDL2 (optional CMake target `UrbanSimCore-visualizer`)
- [x] Isometric or top-down renderer (PPM top-down export scaffold)
- [x] Tile, road, building, zone rendering
- [x] Zoom and pan controls (tile scale + viewport render window)
- [x] Real-time metrics display (live HUD in visualizer title)

**Deliverable:** 2D visualization of city with buildings, roads, zones.

### Milestone 10: Debug Overlays
- [x] Land value overlay
- [ ] Demand overlay
- [x] Service coverage overlay
- [x] Congestion/traffic overlay
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

**MVP Baseline (headless through save/load scaffold):** complete

---

## Next Targets (Post-Backlog)

1. Add demand, happiness, and route heatmap overlays
2. Add pause/step controls for deterministic tick stepping in live mode
3. Save schema versioning and migration guards
4. Performance pass for larger maps and higher population counts
5. District-level service policies and budget controls

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
- Deterministic replay working with checksum parity verification

By end of Phase 4:
- Visual representation working
- Debug overlays available

By end of Phase 5:
- Advanced systems add depth without sacrificing performance
- Engine handles 100k+ population with advanced features

