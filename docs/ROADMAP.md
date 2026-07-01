# UrbanSimCore Development Roadmap

Last updated: June 27, 2026

## Current Status Snapshot

- Backlog Slices 1-14: complete
- Headless core simulation loop: complete
- Population, traffic, economy, metrics, save/load, districts, and visualization: complete
- Multithreaded simulation loop with thread pool: complete (~2.8× speedup at 200k pop)
- Phase 5, Milestone 11 (Traffic Micro-Simulation): complete
- Automated validation: 160 tests passing

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
- [x] Demand overlay
- [x] Service coverage overlay
- [x] Congestion/traffic overlay
- [x] Happiness/desirability overlay
- [x] Route heatmap

**Deliverable:** Multiple debug views toggle-able in real-time.

---

## Phase 5: Advanced Systems (M11+)

### Milestone 11: Traffic Micro-Simulation — complete
- [x] Individual vehicle agents (not one-per-person; one vehicle per commute batch)
- [x] Vehicle routing and pathfinding (shortest path per vehicle, road-node anchored)
- [x] Emergent congestion (edge speed falls once vehicle count exceeds lane capacity; `--micro-traffic`)
- [x] Intersection signal logic (junctions detected by degree; vehicles queue on red)
- [x] Traffic signals (basic; alternating axis green, coordinate-offset phases)
- [x] Emergency vehicle routing (dispatched from ServiceFacility sites to random incidents; ignores congestion and red signals; `--micro-traffic-incidents`)
- [x] Multi-lane road capacity with explicit per-lane occupancy and lane-changing (each vehicle tracks a lane index on its current edge; picks the least-loaded lane on entry and switches lanes mid-edge to overtake a congested one; `--micro-traffic-lanes`, default 2)
- [x] Car-following within a lane (a follower's speed is capped so it keeps at least `minFollowingGap` behind whichever vehicle ahead of it is still on the same edge, so a slow or stopped leader visibly backs up traffic behind it; `--micro-traffic-following-gap`, default 0.15)

Note on fidelity: each lane is tracked as its own independent single-file channel (an explicit `(edge, lane)` occupancy count, not just an aggregate per-edge total), so vehicles genuinely spread across lanes and change lanes to escape a crowded one — verified by `VehiclesSpreadAcrossLanesRatherThanBunching`. Within a lane, a follower's progress is capped against its immediate leader's actual (post-signal, post-arrival) position each step, so vehicles hold distinct positions with real spacing instead of overlapping — verified by `FollowersHoldDistinctPositionsRatherThanOverlapping` (with zero gap, all vehicles in a forced single-lane queue arrive on the identical step, a degenerate "everyone at the same progress" signature; with a real gap, arrivals spread out over a range of steps instead). What remains unmodeled: there is no literal 2D position within a lane (a following vehicle's exact placement is a single float along the edge, not an x/y offset), and lane width/vehicle size are not simulated - "gap" is a fraction of edge length, not a physical distance. That level of physical detail is out of scope at this milestone's level of detail.

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
- [x] Profiling and performance tuning (thread pool pass: ~2.8× at 200k pop)
- [x] Parallel pathfinding (Dijkstra fan-out across pool workers)
- [x] Lazy evaluation (service result cache; BFS cache across ticks)
- [ ] Spatial hashing for zoning candidate queries
- [ ] SIMD optimizations where beneficial
- [ ] Traffic route reuse across ticks (invalidate on road topology change)
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

1. District-level service policies and budget controls
2. Add persistence integrity report command for batch snapshot auditing
3. Extend benchmark reports with percentile timings over repeated runs
4. Add route diagnostics export mode for offline analysis
5. Add commute demand-shaping policy experiments for scenario balancing

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

