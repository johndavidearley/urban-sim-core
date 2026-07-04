# UrbanSimCore Development Roadmap

Last updated: July 4, 2026

## Current Status Snapshot

- Backlog Slices 1-14: complete
- Headless core simulation loop: complete
- Population, traffic, economy, metrics, save/load, districts, and visualization: complete
- Multithreaded simulation loop with thread pool: complete (~2.8× speedup at 200k pop)
- Phase 5, Milestone 11 (Traffic Micro-Simulation): complete
- Phase 5, Milestone 12 (Advanced Economy): complete - dynamic land value, commercial/industrial supply chains and imports/exports, inflation, office demand
- Phase 5, Milestone 13 (Public Transit): complete - bus routes, train/subway networks, transit demand/capacity, modal split
- Phase 5, Milestone 14 (Districts and Policies): complete - district-level management, zoning ordinances, growth incentives, service budgets by district, and special districts (industrial/tech hub archetypes) all wired into the autonomous simulation loop
- Phase 5, Milestone 15 (Disasters and Challenges): started - fire spread and coverage-modulated emergency response (opt-in via --simulate-disasters), and crime simulation (always-on, feeds migration desirability)
- Automated validation: 238 tests passing

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

### Milestone 12: Advanced Economy — complete
- [x] Commercial and industrial supply chains / imports-exports - `EconomyState` gains `goodsProduced` (industrial occupancy × `TradeRates::goodsPerIndustrialWorker`), `goodsConsumed` (commercial occupancy × `goodsPerCommercialWorker`), and `tradeBalance` (the difference). A surplus earns `exportRevenue`; a shortfall costs `importCost` at a strictly higher per-unit rate than exporting earns (importing is a genuine economic penalty, not a wash) - both flow into `totalRevenue`/`totalExpenses`/`balance`, so a city with plentiful jobs but no local industry visibly pays for it. No new call-site wiring needed (computed entirely from data `calculateEconomy` already receives); `TradeRates` defaults apply automatically everywhere.
- [x] Land value dynamics (distance to jobs, services, pollution) - `LandValueSystem` recomputes `Tile::landValue` per tick from a zone base plus three factors: a multi-source BFS from all commercial/industrial buildings (distance-capped at `jobAccessRadius`) for job proximity, the existing service-coverage BFS cache for facility proximity, and the existing per-tile pollution field. `EconomySystem::calculateEconomy` takes an optional `CityMap*` and reports the real mean over zoned tiles when provided (falls back to the old building-count placeholder otherwise, so district-scoped sub-economies and other map-less callers are unaffected). Interval-gated like services/traffic (`--simulate-land-value-interval`, default 1) since the job-access BFS is the costliest pass in the tick at city scale.
- [x] Office demand - a genuine 4th RCI-like category, distinct from Commercial. `BuildingType::Office` and `ZoneType::Office` (symbol `O`) join the existing three; `ZoneDemand` gains an `office` field computed in `CitySimulator::evaluateDemand` from population and how established the city's commercial base already is (office space follows retail/downtown formation rather than leading it - an empty or purely-residential city generates zero office demand). Autonomous zoning (`autoZone`) splits new tiles four ways by demand, assigning office space to clean land alongside residential (it commands the highest base land value: 150 vs. commercial's 140). `GrowthSystem` builds/demolishes/redevelops real `BuildingType::Office` buildings under the same rules as the other three types. Offices are a genuine third job type in `PopulationSystem`'s income-band job matching (high income skews office-and-commercial heavy, low income skews industrial, roughly proxying blue- vs. white-collar employment) and count toward job-access land value, commute traffic, and district metrics. Economically, offices pay their own tax rate (9%, the highest - premium commercial-grade property) and maintenance, but sit outside the industrial/commercial goods-trade model entirely (white-collar work doesn't produce or consume physical goods). Persistence, save/load validation, CLI zoning (`--zone-rect ... OFFICE`), CSV/summary reporting, and the SDL visualizer/PPM renderer all recognize the new type; old snapshots remain valid (the enum value is purely additive).
- [x] Inflation - `EconomyState`/`calculateEconomy` gain an `inflationMultiplier` (default 1.0, matching prior behavior for every existing caller) applied to maintenance costs and trade prices (export revenue/import cost) but deliberately NOT to tax revenue: a percentage of the city's own building stock, which only grows when the city actually builds more. The asymmetry creates real budget erosion for a stagnant city - the same pressure a real municipality that stops growing faces - without netting to zero in `economicHealth`'s ratio-based scoring the way a uniform scale-up-both-sides model would. `CitySimulator` computes a compounding per-tick multiplier from `SimOptions::inflationRatePerTick` (default 0 = no inflation, so existing runs/tests/replays are bit-for-bit unaffected unless a caller opts in via `--simulate-inflation-rate`).

Note on Office demand: unlike the other three M12 slices, this one is not behavior-neutral by default - `evaluateDemand` computing a nonzero `office` field changes the RCI zoning mix (and therefore building counts/population trajectory) for every `--simulate` run, since it's a genuine new growth-competing category rather than a pure read-out. Existing tests only assert qualitative bounds (population growth, R/C/I all nonzero, determinism-by-comparison), never exact counts, so none needed updating - but this is a deliberate exception to the "zero default behavior change" pattern used elsewhere in Phase 5, made because the milestone's whole point is diversifying what the city builds.

Note on performance: the job-access BFS is proportional to job-building count and its capped search radius, not to city size directly, but at moderate scale (tens of job buildings, hundreds of zoned tiles) it is still the single costliest phase per tick (see `--simulate` timing breakdown). Two mitigations exist today: the per-tile scan skips unzoned land (most of the active region) and the BFS is capped at `jobAccessRadius` (no benefit accrues past it). For very large simulations, `--simulate-land-value-interval N` throttles the recompute frequency, matching the existing service/traffic/population interval knobs; land value simply persists at its last computed value between recomputes, the same tradeoff already made for coverage and congestion.

### Milestone 13: Public Transit — complete
- [x] Bus routes - a new `TransitSystem` models fixed bus routes (`TransitRoute`: an ordered path of road-network stops, a vehicle count, and per-vehicle capacity) as static infrastructure, the same way `ServiceFacility` models fire/police/etc - not literal moving agents (that fidelity belongs to `TrafficMicroSim`, which is a separate, heavier system). `CitySimulator` auto-places routes as the city grows (roughly one per 1,200 residents, capped at 16), connecting the residential building farthest from existing route coverage to its *nearest* job building via the road network (short local hops), with a fallback search over alternate residential/job candidates so one disconnected building can't strand placement for the whole city.
- [x] Train/subway networks - a second `TransitMode::Rail` sharing the exact same `TransitRoute`/coverage/offload machinery as bus (the mode field is descriptive only, not a branch in any matching logic). Rail lines connect to the *farthest* reachable job instead of the nearest - long trunk lines spanning the city rather than short local hops - and are sparser (~1 per 4,000 residents, capped at 3), higher-capacity (6 vehicles × 150 riders vs. bus's 2 × 30), and wider-reaching (2× the walk-to-stop radius, representing that riders travel farther to reach a station). `TransitSummary` reports `busRoutes`/`railRoutes` separately alongside the combined totals.
- [x] Transit demand and capacity - each route has a per-tick capacity (`vehicleCount x capacityPerVehicle`); `TransitCoverageCache` pre-builds a multi-source BFS distance field per route (mirroring `ServiceCoverageCache`) so "is this building within walking distance of a stop" is a cheap lookup, reused across ticks like every other coverage cache in this codebase.
- [x] Modal split (cars vs transit) - `TransitOffload` is consulted once per commute batch inside `TrafficSystem::simulateCommutes` (a new optional trailing parameter, default `nullptr` = exact prior behavior for every existing caller): if a route's coverage reaches both the home and work ends of a commute, some of that batch's workers ride transit instead of driving, reducing the load that accumulates onto road congestion while the full worker count still counts toward `commutingPopulation`/commute-burden stats. `TransitSummary` reports `ridership` (actually carried) separately from `demand` (would have ridden, capacity permitting) - a capacity-constrained route visibly caps ridership below demand.

Note on default behavior: like Office demand in M12, this is not behavior-neutral by default (`SimOptions::enableTransit = true`) - reduced congestion feeds back into desirability/migration the same way traffic congestion always has, so it can shift a city's growth trajectory. `--simulate-no-transit` opts back out for comparison/testing. Unlike car routing (which only succeeds between buildings that land on literal road-node tiles - a pre-existing `TrafficSystem` simplification, not something this milestone changes), transit ridership is consequently sparse and driven by the same random uniform commute sampling `TrafficSystem` already uses, not a realistic nearest-job model - it shows up reliably over many ticks, not necessarily on any single tick. In practice, rail's much wider coverage and capacity substantially raise modal share once a city is large enough to grow one (commonly 10-20% in a ~7,000-population test city, versus under 1% with bus alone).

### Milestone 14: Districts and Policies - complete
- [x] District-level management - `DistrictSystem` (districts, per-district tax rates/service allocation/budget caps, `evaluateAllDistricts`) already existed as a fully-featured but standalone, manually-driven CLI system (`--create-district` etc.) with no path into the autonomous `--simulate` loop. `CitySimulator::run` now takes an optional `const DistrictSystem*`; when provided, it's genuinely read each tick rather than just being inspectable after the fact. `--simulate-district NAME X1 Y1 X2 Y2` (repeatable) defines districts for a `--simulate` run; a "District Summary" table prints after the run using the same metrics the standalone commands already computed.
- [x] Zoning ordinances - `District::bannedZoneTypes` (set via `DistrictSystem::setDistrictZoningOrdinance`) restricts which zone types autonomous growth (`CitySimulator`'s `autoZone`) may assign within a district's bounds; manual `--zone-rect` is intentionally unaffected (a sandbox tool, not a government). When demand's preferred type is banned for a tile, `autoZone` falls back to the first still-allowed type in a fixed order rather than leaving the tile stranded - without this, a district that happens to cover the city's growth origin and bans the type startup demand wants most (e.g. Residential) would deadlock the *entire* city's bootstrap, not just that district. Verified directly: an `Industrial`-archetype district placed over the city center still grows (as Commercial/Industrial) while the whole city's population keeps climbing around it.
- [x] Growth incentives - `GrowthSystem::runStep` already accepted a `chanceModifiers` list (`GrowthChanceModifier{minCorner, maxCorner, multiplier}`) that `CitySimulator` always passed as `nullptr`. Each tick (one-tick lag, the same pattern used for congestion/service-satisfaction feedback elsewhere in this loop), district metrics are re-evaluated and `DistrictSystem::computeGrowthPressureMultiplier` (already existed, already tested in isolation, never called from the simulation) turns each district's service-budget fulfillment and density into a per-tile build-chance multiplier for the *next* tick's growth step - a district starved of service budget visibly grows slower than a well-funded one of the same size and distance from the city center.
- [x] Service budgets by district (surfaced) - the existing budget-allocation/shared-pool-balancing logic in `DistrictSystem::evaluateAllDistricts` now runs against the live, evolving city each tick instead of only a manually-saved snapshot, and its output (`serviceBudgetTarget`/`Allocated`/`CapApplied`) both drives growth pressure above and prints in the post-run summary.
- [x] Special districts (industrial zones, tech hubs) - `DistrictArchetype` (`Industrial`, `TechHub`, plus `General` = no restriction) is a named preset bundling a zoning ordinance with service priorities, applied via `DistrictSystem::setDistrictArchetype` or `--simulate-district-archetype NAME ARCHETYPE`. `Industrial` bans Residential/Office and weights fire safety heavily; `TechHub` bans Industrial and weights education heavily. Presets are a starting point, not a lock-in - the general ordinance/priority setters still apply afterward for further customization.

Note on default behavior: `districts` defaults to `nullptr`, matching prior behavior exactly for every existing caller - passing a `DistrictSystem` (even an empty one) is required to activate any of this. Unlike Office demand/transit in M12/M13, this can't silently change existing `--simulate` runs since it requires an explicit new flag with no default districts.

### Milestone 15: Disasters and Challenges - started
- [x] Crime simulation - a new `CrimeSystem` (`CrimeSystem::evaluate`) is a pure read-out, unlike `FireSystem`: no side effects on the map or entity store. It derives a single city-wide `CrimeSummary::overallRate` from unemployment, an average-land-value poverty proxy (below `CrimeParams::referenceLandValue` raises crime), and police coverage (`ServiceCoverageSummary::policeCoverage`, already computed by `ServiceSystem` every tick, mitigates it by up to `policeCoverageReduction`). Runs unconditionally every tick (default-on, like Transit/Office in M12/M13, not opt-in like the destructive systems below) and - one-tick lagged, the same pattern used for congestion/service satisfaction elsewhere in this loop - feeds the same migration-desirability formula: a high-crime city is less attractive to move into, exactly the way high pollution or bad traffic already are.
- [x] Fire spread - a new `FireSystem` models fire as a per-tick stochastic process over the tile grid (not literal responding vehicles - `TrafficMicroSim`'s emergency-vehicle dispatch already covers that fidelity separately, as a standalone demo). Buildings can ignite each tick, weighted by type (industrial is far more fire-prone) and local pollution; an ignited building is destroyed immediately and its tile keeps burning for a few ticks, posing a spread risk to adjacent occupied tiles. Deterministic for a given seed (buildings/burning tiles processed in a fixed sorted order), matching every other subsystem in this codebase.
- [ ] Disease/health
- [ ] Natural disasters (earthquakes, floods)
- [x] Emergency response (coverage-modulated) - fire station coverage (`ServiceCoverageSummary::fireCoverage`, already computed by `ServiceSystem` every tick `CitySimulator` runs) is read as a single city-wide fraction and reduces ignition chance, spread chance, and burn duration alike - a proxy for faster emergency response, not a per-building distance lookup (a deliberate fidelity tradeoff: `ServiceSystem` already pays the per-tile BFS cost for this aggregate number, so `FireSystem` reuses it rather than paying a second BFS pass). Literal per-vehicle emergency dispatch remains `TrafficMicroSim`'s separate, higher-fidelity demo (`--micro-traffic-incidents`) - the two model emergency response at different levels of detail and aren't merged.

Note on default behavior: unlike the additive M12/M13/M14 systems, this is destructive, so `SimOptions::enableDisasters` defaults to `false` (the inflation-style opt-in pattern, not the office-demand/transit-style default-on one) - every existing `--simulate` call is completely unaffected unless `--simulate-disasters` is passed. `--simulate-fire-risk F` scales the base ignition chance for tuning.

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

