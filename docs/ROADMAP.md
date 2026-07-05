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
- Phase 5, Milestone 15 (Disasters and Challenges): complete - fire spread, earthquakes, and floods (opt-in via --simulate-disasters, coverage-modulated emergency response for fire), and crime/disease simulation (always-on, feed migration desirability)
- Phase 5, Milestone 16 (City Optimization): complete - incremental spatial index for zoning candidates, cross-tick traffic route cache, and (the big one) a default Release build type that was previously missing entirely; 500×500 map now sustains 127k population at 5.75 ms/tick, well under the 60 FPS budget
- Automated validation: 259 tests passing

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

Update: both gaps called out above are now addressed, in a way deliberately scoped to stay additive. (1) `TrafficMicroSim::vehicleWorldPosition(vehicle, lanesPerRoad)` is a new pure query - not read by `simulate()` itself, so it cannot affect determinism or existing dynamics - that turns a vehicle's existing state (route/segment/progress/lane) into a literal fractional-tile (x, y): progress interpolates along the edge, and the lane index becomes a perpendicular offset of `kLaneWidthMeters` (3.5m, centered so lanes straddle the edge's centerline), giving a renderer something to actually draw side-by-side lanes from. (2) `Options::vehicleLengthMeters` (0 by default) is a genuine physical quantity - unlike `minFollowingGap`, an opaque fraction of an edge - that adds to the car-following distance via `minFollowingGap + vehicleLengthMeters / kMetersPerTile`; a small custom `glm::vec2` (float vector, with the arithmetic operators this needed) was added alongside this project's existing minimal `glm::ivec2` shim, since none existed. Default `vehicleLengthMeters = 0.0f` matches every existing caller's behavior exactly (verified: explicit 0 and the implicit default produce bit-identical output); a dedicated test (`VehicleLengthAloneCreatesFollowingDistanceEvenWithZeroGap`) isolates vehicle length from `minFollowingGap` (holding the latter at 0) to prove it's a genuine, independent contributor to following distance, not folded into the old fraction. `--micro-traffic-vehicle-length F` exposes it on the CLI.

### Milestone 12: Advanced Economy — complete
- [x] Commercial and industrial supply chains / imports-exports - `EconomyState` gains `goodsProduced` (industrial occupancy × `TradeRates::goodsPerIndustrialWorker`), `goodsConsumed` (commercial occupancy × `goodsPerCommercialWorker`), and `tradeBalance` (the difference). A surplus earns `exportRevenue`; a shortfall costs `importCost` at a strictly higher per-unit rate than exporting earns (importing is a genuine economic penalty, not a wash) - both flow into `totalRevenue`/`totalExpenses`/`balance`, so a city with plentiful jobs but no local industry visibly pays for it. No new call-site wiring needed (computed entirely from data `calculateEconomy` already receives); `TradeRates` defaults apply automatically everywhere.
- [x] Land value dynamics (distance to jobs, services, pollution) - `LandValueSystem` recomputes `Tile::landValue` per tick from a zone base plus three factors: a multi-source BFS from all commercial/industrial buildings (distance-capped at `jobAccessRadius`) for job proximity, the existing service-coverage BFS cache for facility proximity, and the existing per-tile pollution field. `EconomySystem::calculateEconomy` takes an optional `CityMap*` and reports the real mean over zoned tiles when provided (falls back to the old building-count placeholder otherwise, so district-scoped sub-economies and other map-less callers are unaffected). Interval-gated like services/traffic (`--simulate-land-value-interval`, default 1) since the job-access BFS is the costliest pass in the tick at city scale.
- [x] Office demand - a genuine 4th RCI-like category, distinct from Commercial. `BuildingType::Office` and `ZoneType::Office` (symbol `O`) join the existing three; `ZoneDemand` gains an `office` field computed in `CitySimulator::evaluateDemand` from population and how established the city's commercial base already is (office space follows retail/downtown formation rather than leading it - an empty or purely-residential city generates zero office demand). Autonomous zoning (`autoZone`) splits new tiles four ways by demand, assigning office space to clean land alongside residential (it commands the highest base land value: 150 vs. commercial's 140). `GrowthSystem` builds/demolishes/redevelops real `BuildingType::Office` buildings under the same rules as the other three types. Offices are a genuine third job type in `PopulationSystem`'s income-band job matching (high income skews office-and-commercial heavy, low income skews industrial, roughly proxying blue- vs. white-collar employment) and count toward job-access land value, commute traffic, and district metrics. Economically, offices pay their own tax rate (9%, the highest - premium commercial-grade property) and maintenance, but sit outside the industrial/commercial goods-trade model entirely (white-collar work doesn't produce or consume physical goods). Persistence, save/load validation, CLI zoning (`--zone-rect ... OFFICE`), CSV/summary reporting, and the SDL visualizer/PPM renderer all recognize the new type; old snapshots remain valid (the enum value is purely additive).
- [x] Inflation - `EconomyState`/`calculateEconomy` gain an `inflationMultiplier` (default 1.0, matching prior behavior for every existing caller) applied to maintenance costs and trade prices (export revenue/import cost) but deliberately NOT to tax revenue: a percentage of the city's own building stock, which only grows when the city actually builds more. The asymmetry creates real budget erosion for a stagnant city - the same pressure a real municipality that stops growing faces - without netting to zero in `economicHealth`'s ratio-based scoring the way a uniform scale-up-both-sides model would. `CitySimulator` computes a compounding per-tick multiplier from `SimOptions::inflationRatePerTick` (default 0 = no inflation, so existing runs/tests/replays are bit-for-bit unaffected unless a caller opts in via `--simulate-inflation-rate`).

Note on Office demand: unlike the other three M12 slices, this one is not behavior-neutral by default - `evaluateDemand` computing a nonzero `office` field changes the RCI zoning mix (and therefore building counts/population trajectory) for every `--simulate` run, since it's a genuine new growth-competing category rather than a pure read-out. Existing tests only assert qualitative bounds (population growth, R/C/I all nonzero, determinism-by-comparison), never exact counts, so none needed updating - but this is a deliberate exception to the "zero default behavior change" pattern used elsewhere in Phase 5, made because the milestone's whole point is diversifying what the city builds.

Note on performance: the job-access BFS is proportional to job-building count and its capped search radius, not to city size directly, but at moderate scale (tens of job buildings, hundreds of zoned tiles) it is still the single costliest phase per tick (see `--simulate` timing breakdown). Two mitigations exist today: the per-tile scan skips unzoned land (most of the active region) and the BFS is capped at `jobAccessRadius` (no benefit accrues past it). For very large simulations, `--simulate-land-value-interval N` throttles the recompute frequency, matching the existing service/traffic/population interval knobs; land value simply persists at its last computed value between recomputes, the same tradeoff already made for coverage and congestion.

Update: the *other* half of this phase's cost - `nearestServiceDistance`'s per-tile scan across every `ServiceCoverageCache::Entry` to find the nearest facility of any type - has since been fixed directly rather than just throttled. `ServiceCoverageCache` gained a `nearestAnyDistance` field: a single merged (min) distance-to-nearest-facility map built once in `ServiceSystem::buildCache` (whenever the facility list actually changes) by folding all entries' BFS fields together, turning what used to be an `O(tiles × facilities)` scan into `O(tiles)` lookups plus one `O(sum of facility BFS sizes)` merge per rebuild. Verified bit-identical final city state on the same seed; at 127 facilities (500×500 map, 1000 ticks, population 127k) the LandValue phase dropped from 2173 ms to 453 ms (~4.8×).

### Milestone 13: Public Transit — complete
- [x] Bus routes - a new `TransitSystem` models fixed bus routes (`TransitRoute`: an ordered path of road-network stops, a vehicle count, and per-vehicle capacity) as static infrastructure, the same way `ServiceFacility` models fire/police/etc - not literal moving agents (that fidelity belongs to `TrafficMicroSim`, which is a separate, heavier system). `CitySimulator` auto-places routes as the city grows (roughly one per 1,200 residents, capped at 16), connecting the residential building farthest from existing route coverage to its *nearest* job building via the road network (short local hops), with a fallback search over alternate residential/job candidates so one disconnected building can't strand placement for the whole city.
- [x] Train/subway networks - a second `TransitMode::Rail` sharing the exact same `TransitRoute`/coverage/offload machinery as bus (the mode field is descriptive only, not a branch in any matching logic). Rail lines connect to the *farthest* reachable job instead of the nearest - long trunk lines spanning the city rather than short local hops - and are sparser (~1 per 4,000 residents, capped at 3), higher-capacity (6 vehicles × 150 riders vs. bus's 2 × 30), and wider-reaching (2× the walk-to-stop radius, representing that riders travel farther to reach a station). `TransitSummary` reports `busRoutes`/`railRoutes` separately alongside the combined totals.
- [x] Transit demand and capacity - each route has a per-tick capacity (`vehicleCount x capacityPerVehicle`); `TransitCoverageCache` pre-builds a multi-source BFS distance field per route (mirroring `ServiceCoverageCache`) so "is this building within walking distance of a stop" is a cheap lookup, reused across ticks like every other coverage cache in this codebase.
- [x] Modal split (cars vs transit) - `TransitOffload` is consulted once per commute batch inside `TrafficSystem::simulateCommutes` (a new optional trailing parameter, default `nullptr` = exact prior behavior for every existing caller): if a route's coverage reaches both the home and work ends of a commute, some of that batch's workers ride transit instead of driving, reducing the load that accumulates onto road congestion while the full worker count still counts toward `commutingPopulation`/commute-burden stats. `TransitSummary` reports `ridership` (actually carried) separately from `demand` (would have ridden, capacity permitting) - a capacity-constrained route visibly caps ridership below demand.

Note on default behavior: like Office demand in M12, this is not behavior-neutral by default (`SimOptions::enableTransit = true`) - reduced congestion feeds back into desirability/migration the same way traffic congestion always has, so it can shift a city's growth trajectory. `--simulate-no-transit` opts back out for comparison/testing. Unlike car routing (which only succeeds between buildings that land on literal road-node tiles - a pre-existing `TrafficSystem` simplification, not something this milestone changes), transit ridership is consequently sparse and driven by the same random uniform commute sampling `TrafficSystem` already uses, not a realistic nearest-job model - it shows up reliably over many ticks, not necessarily on any single tick. In practice, rail's much wider coverage and capacity substantially raise modal share once a city is large enough to grow one (commonly 10-20% in a ~7,000-population test city, versus under 1% with bus alone).

Update: the "literal road-node tiles" simplification called out above has been fixed. `RoadNetwork` pre-registers every map tile as a graph node at construction (so `hasNode()` was never actually a useful filter - it was true for any in-bounds coordinate, road or not), and `TrafficSystem`/`CitySimulator`'s transit-route placement both used to path from a building's own tile directly. Since zoning only requires road access at a tile *or* one of its 4 neighbors (`hasRoadAccess`), any building placed where only a neighbor has the road (a normal, common case) silently failed to route at all - it counted toward `commutingPopulation` but contributed nothing to congestion, commute time, or transit ridership. `RoadNetwork` gained a shared `resolveRoadAnchor()` (replacing near-identical copies that already existed in `LandValueSystem`/`ServiceSystem`), and `TrafficSystem::collectCommuteSpecs`, its transit-offload lookup, and `CitySimulator`'s bus/rail route placement all now resolve to the nearest road-adjacent anchor before pathfinding instead of using the raw building position. This is a genuine default-behavior change (more buildings can now commute/ride transit than before), same category as Office demand in M12 - measured as a modest shift in a 500×500/1000-tick benchmark (population 127,009 -> 123,267, transit ridership 2,940 -> 2,340; still comfortably under the 60 FPS budget). Verified with dedicated tests placing buildings adjacent-to-but-not-on a road tile.

Update: the "not a realistic nearest-job model" gap called out above has also been addressed. `TrafficSystem::collectCommuteSpecs` used to pick a job building uniformly at random from every job building on the map, completely independent of distance from home - a resident could be assigned to any workplace anywhere with equal probability. Job selection is now weighted by `capacity / (1 + kDistanceDecay * manhattanDistance)` per commute batch, so nearer and larger job buildings are more likely (home selection is left uniform - only the job side of the pair was unrealistic). This is a genuine default-behavior change, verified with two statistical tests (200 trials each) proving the weighting favors nearer/higher-capacity jobs as intended. It turned out *not* to reliably raise transit modal share in isolation (measured slightly lower - 2.1% vs 2.5% - in one 500×500 benchmark, though many interacting feedback loops mean this isn't a clean before/after comparison); the fix's value is model realism (commuters now behave like commuters), not a guaranteed transit-ridership bump.

### Milestone 14: Districts and Policies - complete
- [x] District-level management - `DistrictSystem` (districts, per-district tax rates/service allocation/budget caps, `evaluateAllDistricts`) already existed as a fully-featured but standalone, manually-driven CLI system (`--create-district` etc.) with no path into the autonomous `--simulate` loop. `CitySimulator::run` now takes an optional `const DistrictSystem*`; when provided, it's genuinely read each tick rather than just being inspectable after the fact. `--simulate-district NAME X1 Y1 X2 Y2` (repeatable) defines districts for a `--simulate` run; a "District Summary" table prints after the run using the same metrics the standalone commands already computed.
- [x] Zoning ordinances - `District::bannedZoneTypes` (set via `DistrictSystem::setDistrictZoningOrdinance`) restricts which zone types autonomous growth (`CitySimulator`'s `autoZone`) may assign within a district's bounds; manual `--zone-rect` is intentionally unaffected (a sandbox tool, not a government). When demand's preferred type is banned for a tile, `autoZone` falls back to the first still-allowed type in a fixed order rather than leaving the tile stranded - without this, a district that happens to cover the city's growth origin and bans the type startup demand wants most (e.g. Residential) would deadlock the *entire* city's bootstrap, not just that district. Verified directly: an `Industrial`-archetype district placed over the city center still grows (as Commercial/Industrial) while the whole city's population keeps climbing around it.
- [x] Growth incentives - `GrowthSystem::runStep` already accepted a `chanceModifiers` list (`GrowthChanceModifier{minCorner, maxCorner, multiplier}`) that `CitySimulator` always passed as `nullptr`. Each tick (one-tick lag, the same pattern used for congestion/service-satisfaction feedback elsewhere in this loop), district metrics are re-evaluated and `DistrictSystem::computeGrowthPressureMultiplier` (already existed, already tested in isolation, never called from the simulation) turns each district's service-budget fulfillment and density into a per-tile build-chance multiplier for the *next* tick's growth step - a district starved of service budget visibly grows slower than a well-funded one of the same size and distance from the city center.
- [x] Service budgets by district (surfaced) - the existing budget-allocation/shared-pool-balancing logic in `DistrictSystem::evaluateAllDistricts` now runs against the live, evolving city each tick instead of only a manually-saved snapshot, and its output (`serviceBudgetTarget`/`Allocated`/`CapApplied`) both drives growth pressure above and prints in the post-run summary.
- [x] Special districts (industrial zones, tech hubs) - `DistrictArchetype` (`Industrial`, `TechHub`, plus `General` = no restriction) is a named preset bundling a zoning ordinance with service priorities, applied via `DistrictSystem::setDistrictArchetype` or `--simulate-district-archetype NAME ARCHETYPE`. `Industrial` bans Residential/Office and weights fire safety heavily; `TechHub` bans Industrial and weights education heavily. Presets are a starting point, not a lock-in - the general ordinance/priority setters still apply afterward for further customization.

Note on default behavior: `districts` defaults to `nullptr`, matching prior behavior exactly for every existing caller - passing a `DistrictSystem` (even an empty one) is required to activate any of this. Unlike Office demand/transit in M12/M13, this can't silently change existing `--simulate` runs since it requires an explicit new flag with no default districts.

### Milestone 15: Disasters and Challenges - complete
- [x] Crime simulation - a new `CrimeSystem` (`CrimeSystem::evaluate`) is a pure read-out, unlike `FireSystem`: no side effects on the map or entity store. It derives a single city-wide `CrimeSummary::overallRate` from unemployment, an average-land-value poverty proxy (below `CrimeParams::referenceLandValue` raises crime), and police coverage (`ServiceCoverageSummary::policeCoverage`, already computed by `ServiceSystem` every tick, mitigates it by up to `policeCoverageReduction`). Runs unconditionally every tick (default-on, like Transit/Office in M12/M13, not opt-in like the destructive systems below) and - one-tick lagged, the same pattern used for congestion/service satisfaction elsewhere in this loop - feeds the same migration-desirability formula: a high-crime city is less attractive to move into, exactly the way high pollution or bad traffic already are.
- [x] Fire spread - a new `FireSystem` models fire as a per-tick stochastic process over the tile grid (not literal responding vehicles - `TrafficMicroSim`'s emergency-vehicle dispatch already covers that fidelity separately, as a standalone demo). Buildings can ignite each tick, weighted by type (industrial is far more fire-prone) and local pollution; an ignited building is destroyed immediately and its tile keeps burning for a few ticks, posing a spread risk to adjacent occupied tiles. Deterministic for a given seed (buildings/burning tiles processed in a fixed sorted order), matching every other subsystem in this codebase.
- [x] Disease/health - a new `HealthSystem` (`HealthSystem::evaluate`) is a pure read-out, exactly like `CrimeSystem`: no side effects on the map or entity store. It derives a single city-wide `HealthSummary::illnessRate` from housing crowding (population relative to residential capacity, a density/contagion proxy), residential-weighted pollution, and hospital coverage (`ServiceCoverageSummary::healthCoverage`, already computed by `ServiceSystem` every tick), which mitigates it by up to `healthCoverageReduction`. Runs unconditionally every tick (default-on, same rationale as crime) and - one-tick lagged - feeds migration desirability: a sicker city is less attractive to move into. Deliberately does not kill residents directly (that fidelity, and the "sudden mass loss" flavor of an epidemic, belongs to the opt-in destructive systems below) - disease here is an ambient quality-of-life drag, the same category as crime or pollution.
- [x] Natural disasters (earthquakes, floods) - a new `NaturalDisasterSystem` (`NaturalDisasterSystem::step`) models one-off catastrophic events, unlike `FireSystem`'s persistent per-tile spread: each tick independently rolls a rare, uniform-risk earthquake (any building can be the epicenter) and a separate, water-restricted flood (only buildings within `floodProximity` of a water tile are eligible epicenters - a flood can't strike inland development). When one triggers, every building within its radius has a chance of being destroyed immediately that falls off linearly with distance from the epicenter - a single-tick blast radius, not a multi-tick spread. Unlike fire, neither event's chance or severity is mitigated by any service coverage (a fire department doesn't reduce whether the ground shakes or a river overflows) - `--simulate-earthquake-risk F` / `--simulate-flood-risk F` scale the two independently for tuning. Deterministic for a given seed (buildings processed in a fixed ID-sorted order).
- [x] Emergency response (coverage-modulated) - fire station coverage (`ServiceCoverageSummary::fireCoverage`, already computed by `ServiceSystem` every tick `CitySimulator` runs) is read as a single city-wide fraction and reduces ignition chance, spread chance, and burn duration alike - a proxy for faster emergency response, not a per-building distance lookup (a deliberate fidelity tradeoff: `ServiceSystem` already pays the per-tile BFS cost for this aggregate number, so `FireSystem` reuses it rather than paying a second BFS pass). Literal per-vehicle emergency dispatch remains `TrafficMicroSim`'s separate, higher-fidelity demo (`--micro-traffic-incidents`) - the two model emergency response at different levels of detail and aren't merged. Earthquakes/floods are deliberately exempt from this mitigation (see above) - only fire has a "response speed" lever in this model.

Note on default behavior: unlike the additive M12/M13/M14 systems, fire and natural disasters are destructive, so `SimOptions::enableDisasters` defaults to `false` (the inflation-style opt-in pattern, not the office-demand/transit-style default-on one) and gates both `FireSystem` and `NaturalDisasterSystem` together - every existing `--simulate` call is completely unaffected unless `--simulate-disasters` is passed. `--simulate-fire-risk F` / `--simulate-earthquake-risk F` / `--simulate-flood-risk F` scale each hazard's base chance independently for tuning. Crime and health, by contrast, are pure read-outs with no map/entity side effects, so - like Office demand/Transit in M12/M13 - they default on and need no opt-in flag.

### Milestone 16: City Optimization - complete
- [x] Profiling and performance tuning (thread pool pass: ~2.8× at 200k pop)
- [x] Parallel pathfinding (Dijkstra fan-out across pool workers)
- [x] Lazy evaluation (service result cache; BFS cache across ticks)
- [x] Spatial hashing for zoning candidate queries - `CitySimulator::run`'s zoning step used to rescan the *entire* active region (`O(extent^2)` tiles, each paying a 4-neighbor road-adjacency check) every single tick to find unzoned, road-accessible land. It now maintains a persistent `std::unordered_set<Coord, Vec2Hash>` candidate index across ticks (`extendZoningCandidates`), extended incrementally: since roads in this loop only ever grow outward in fixed `spacing` steps (`layRoadGrid`), any tile that just became road-accessible is provably confined to the new ring between the old and new extent (a "picture frame" decomposition into 4 rectangular bands, with a 1-tile buffer on the inner edge for neighbor-adjacency safety) - so each extent growth only rescans the new ring, not the whole developed area. Tiles are dropped from the index the instant `autoZone` actually zones them (checked directly by re-reading `tile.zone` after the call, so a tile left unzoned by a district ordinance ban stays a valid candidate for retry). `autoZone` still only ever consumes the nearest `zoneBatchPerTick` candidates, so a `std::partial_sort` (not a full sort) of the index extracts them. Verified bit-identical final city state (population/building counts/zone mix) against the old full-rescan behavior on the same seed; zoning phase time dropped ~32% on a 400-tick/160×160 benchmark, with the gap widening at larger map sizes since the old cost was quadratic in extent and the new cost is proportional to the ring/candidate-set size.
- [x] SIMD optimizations where beneficial - investigation found the actual per-tick hot spots (`TrafficSystem`'s Dijkstra/pathfinding, `LandValueSystem`'s multi-source BFS plus per-tile `unordered_map` lookups across every service facility) are graph- and hash-map-bound, not uniform numeric array loops, so hand-written SIMD intrinsics there would need an Array-of-Structs -> Struct-of-Arrays rewrite of `Tile`/the road graph to pay off - out of scope as a targeted change. The one change that actually mattered here: **`CMakeLists.txt` had no default `CMAKE_BUILD_TYPE`**, so every build (including every benchmark figure elsewhere in this document) was compiling at the implicit `-O0` - no inlining, no auto-vectorization, none of the SIMD codegen this item is about, at all. Now defaults to `Release` (`-O3 -DNDEBUG`) when the caller doesn't specify one (single-config generators only; Xcode/MSVC pick their configuration at build time and are left alone). Effect measured directly: the full test suite went from ~11-12s to ~3.3s, and a 400-tick/160×160 `--simulate` benchmark dropped from 6.1s to 1.5s (~4x) - Traffic ~3.7x, LandValue ~4.3x, Zoning ~3.6x - with zero code changes, just enabling the optimizer that was already being requested (`-Wall -Wextra -Wpedantic` were the only flags actually reaching the compiler before this).
- [x] Traffic route reuse across ticks (invalidate on road topology change) - `TrafficSystem::simulateCommutes`'s parallel pathfinding phase always runs immediately after `RoadNetwork::resetCongestion()` and strictly before any congestion is accumulated, so every path it computes depends only on road topology, never on congestion or which tick it is. A new caller-owned `TrafficRouteCache` (keyed by `RouteEndpointKey{origin, destination}`, mirroring the `ServiceCoverageCache`/`TransitCoverageCache` cross-tick pattern) lets `CitySimulator` skip re-running Dijkstra for any commute whose (home, job) pair was already resolved, submitting a pool task only on a cache miss. `RoadNetwork` gained `getTopologyVersion()`, bumped only when `buildRoad`/`removeRoad` actually adds or removes an edge (re-laying an already-present road, as the grid-expansion path does every tick, is correctly a no-op) - the cache clears itself the first time it's used against a network whose version has moved on. Default `nullptr` matches prior behavior exactly for every other caller (visualizer, benchmark, replay verifier, tests). Benefit is workload-dependent: it's largest for a city that has stopped growing (topology stable) with a small, frequently-revisited building population; a continuously-growing city re-invalidates the cache each time the road grid expands, and this codebase's random-uniform commute sampling means a fixed number of (home, job) pairs recur less often than one might expect on a large, still-growing map - measured as a real but modest (~1-9%) traffic-phase win in the growth benchmarks exercised here, never a regression (verified bit-identical results with/without the cache on the same seed).
- [x] Target: 60 FPS headless with 500×500 map, 100k population - met with headroom once Release optimizations were actually enabled (see above): a 500×500 map seeded to 1000 `--simulate` ticks reaches population 127,009 (523 buildings, 637 road tiles) averaging **5.75 ms/tick** total across every subsystem in the loop (roads/zoning/growth/population/traffic/economy/services/land value/transit/crime/health) - comfortably under the 16.67 ms/tick budget for 60 FPS, with `--simulate-land-value-interval`/`--simulate-service-interval` available to buy further headroom if a given scenario's facility/job-building count pushes those two BFS-heavy phases higher.

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

The following are deferred/out-of-scope items called out by name in this
document's "Note on ..." sections - each is a real gap, just one judged not
worth its cost at the time the note was written:

6. ~~Model literal 2D in-lane vehicle position and physical lane
   width/vehicle size in `TrafficMicroSim`~~ - done: see the M11 fidelity
   note update. `TrafficMicroSim::vehicleWorldPosition()` derives a literal
   (x, y) from a vehicle's existing state; `Options::vehicleLengthMeters`
   (0 by default) makes vehicle size a genuine physical contributor to
   car-following distance.
7. ~~Replace `LandValueSystem::nearestServiceDistance`'s per-tile scan across
   every `ServiceCoverageCache` entry with a single precomputed combined
   distance field~~ - done: see the M12 performance note update. `nearestServiceDistance`
   now does a single lookup into `ServiceCoverageCache::nearestAnyDistance`
   (built once in `ServiceSystem::buildCache`); LandValue phase measured
   ~4.8× faster at 127 facilities.
8. ~~Let commute/transit routing reach buildings that aren't exactly on a
   road-node tile~~ - done: see the M13 transit note update. `RoadNetwork`
   gained a shared `resolveRoadAnchor()`; `TrafficSystem` and
   `CitySimulator`'s transit-route placement both route from the resolved
   anchor instead of the raw building position now.
9. ~~Replace `TrafficSystem`'s random-uniform home/job commute sampling with a
   distance- or capacity-aware matching model~~ - done: see the M13 transit
   note update below. Job selection is now weighted by
   `capacity / (1 + kDistanceDecay * manhattanDistance)` per draw (home
   selection is unchanged/uniform - only *which job a given commuter
   targets* was unrealistic) - straight-line distance as a cheap proxy, the
   same tradeoff CitySimulator's transit-route placement already makes
   rather than a per-draw road BFS. Verified with two statistical tests
   (200 trials each) proving the weighting actually favors nearer and
   higher-capacity jobs; traffic phase cost unaffected (weight computation
   is O(job buildings) per draw, negligible next to Dijkstra).
10. ~~Rewrite `CityMap`'s `Tile` storage as struct-of-arrays~~ - done through
    Phase 4 (pollution/landValue pulled into parallel arrays; see the
    phased breakdown below). Phase 4's benchmark found no measurable perf
    win, so Phase 5 (explicit loop vectorization) was deliberately not
    pursued - the accessor-based API is kept for architectural reasons,
    not a performance win that didn't materialize.

    Investigated: `CityMap` is referenced by 39 files - nearly every system
    in the codebase (Growth, LandValue, Service, Traffic, Population,
    Economy, Zoning, TerrainGenerator, the visualizer, save/load
    persistence, District/Fire/NaturalDisaster, every CLI command). Of the
    current ~4000ms/1000-tick cost at 500x500, the phases that would
    actually benefit from SoA (Roads/Zoning/Growth - genuine tile-array
    scans) are only ~10% of it; the phases that dominate (Traffic,
    Economy, Services, LandValue) are graph/hashmap-bound and wouldn't
    speed up at all. A full rewrite in one shot is a 39-file blast radius
    for a best-case ~6-7% total-tick win, against a budget we're already
    3x under (~5ms/tick vs. the 16.67ms/60 FPS target). Rather than skip
    it outright or commit to the whole thing at once, it's broken into
    phases below - each independently small, verified by the existing test
    suite, and with a data-driven go/no-go checkpoint (Phase 4) before the
    genuinely risky step:

    - [x] **Phase 1 - accessor layer (no storage change) - done.** `CityMap`
      gained `float& pollution(Coord)` / `float pollution(Coord) const`,
      the same const/non-const pair for `landValue`, and `int zone(Coord)
      const` / `void setZone(Coord, int)` - thin wrappers over the
      existing `getTile(coord).field` access, verified (via a new test,
      `FieldAccessorsReadAndWriteTheSameStorageAsGetTile`) to read/write
      the exact same underlying storage `getTile()` does. No call site
      migrated yet (that's Phase 2) - this step is purely additive, and
      the full suite (271 -> 272 tests) confirms zero behavior change.
    - [x] **Phase 2 - migrate call sites, one system at a time - done.**
      Every genuine `CityMap::Tile.{pollution,landValue,zone}` access
      outside `CityMap.cpp` itself now goes through the Phase 1 accessors:
      `Zoning`, `GrowthMetrics`, `DistrictSystem`, `FireSystem`,
      `GrowthSystem`, `LandValueSystem`, `CitySimulator`, `MapRenderer`,
      `VisualizerSDL`, and `CityPrinters`' `printZones` - migrated file by
      file, full suite green after each. Verified behavior-preserving both
      by the test suite (272 tests, unchanged) and a direct before/after
      `--simulate` comparison (500x500/1000 ticks: identical final
      population/building counts).

      Deliberately left as direct `Tile&` access, not oversights:
      `CityMap.cpp` itself (the accessors' own implementation, plus the
      constructor's initialization); `SaveLoadSystem`/`ReplayVerifier`'s
      `SerializedTile` (a distinct persistence-layer type, not `CityMap`'s
      `Tile`, despite the similar field names); `EconomySystem`'s
      `metrics.pollution` (a `CityMetrics` field, unrelated); and
      `CityPrinters::printTile` plus `SaveLoadSystem`'s snapshot
      capture/apply loops, which read or write *every* field of a tile in
      one place - genuine full-struct dump/reconstruct operations that
      would need direct access regardless of storage layout, so migrating
      3 of their 8-9 fields to accessors wouldn't reduce Phase 3's
      eventual blast radius at all.
    - [x] **Phase 3 - the actual storage swap - done.** `Tile` no longer
      has `landValue`/`pollution` fields at all; `CityMap` now owns them as
      two parallel `std::vector<float>` (`pollutions`, `landValues`),
      indexed identically to `tiles`. This turned out to have a bigger
      blast radius than "contained to `CityMap.hpp/.cpp`" as originally
      described: removing the fields from `Tile` broke every place that
      read/wrote them via a bare `Tile`/`getTile()`, including 5 test files
      that used `map.getTile(coord).pollution = X` as a test-setup
      convenience (not just the 2 production exceptions - `CityPrinters::
      printTile` and `SaveLoadSystem`'s snapshot capture/apply - already
      known from Phase 2). All were migrated to the accessors first as
      prep, then the storage swap itself compiled with zero further errors
      - confirming the prep step had genuinely caught every caller. 273
      tests passing throughout; a direct before/after `--simulate`
      comparison (500x500/1000 ticks) shows byte-identical final city
      state.
    - [x] **Phase 4 - measure, then decide - done, decision: stop here.**
      Re-ran the 500x500/1000-tick benchmark 3x before and after Phase 3.
      Result: no measurable win. Roads/Zoning/Growth (the phases that
      would benefit from contiguous float arrays) swing by roughly as much
      from ordinary run-to-run machine noise (~20-30%) as any effect the
      storage change could plausibly produce - consistent with the
      original ~6-7% best-case estimate being too small to reliably
      observe at this measurement granularity. Determinism held throughout
      (identical final city state on every trial, both before and after).
      Phase 3 is being kept anyway - the accessor-based API and contiguous
      per-tile-field storage are worth having on architectural grounds
      (cleaner separation of concerns, and a foundation the *next* hot
      numeric field to get added won't have to redo) - but Phase 5 is not:
      it would add real complexity (rewriting loops to bypass the
      accessor call and operate on raw array pointers) on top of a change
      that hasn't shown a measurable win to build on.
    - Phase 5 (not pursued - see Phase 4's finding) - vectorize the hot
      loops explicitly by restructuring `updatePollution`'s clear/scatter
      phases and the zoning candidate scan to operate directly on the
      contiguous arrays instead of through a per-tile accessor call.
11. Make `TrafficRouteCache` resilient to a continuously-growing city - e.g.
    invalidate only the paths that cross a changed edge instead of clearing
    the whole cache on any topology change - to raise its measured-modest
    (~1-9%) benefit during active growth (M16 traffic-cache note).

    Investigated: this item's premise doesn't fully hold up. Re-tested with
    a *topology-stable* scenario (no growth at all, 1500 ticks on a
    saturated 40×40 city) and the cache still only helped ~1.4% - so
    topology churn isn't actually the dominant limiter on hit rate. The
    real cause is that commute sampling draws random (home, job) pairs from
    a combinatorial space far larger than the number of commutes resolved
    per tick, so repeats are rare regardless of whether the topology is
    changing. A partial-invalidation scheme would add real complexity (a
    fully correct version edges into incremental-shortest-path-algorithm
    territory - a new edge can shorten a path that never touches it) for
    a benefit this data doesn't support. Item 9 (realistic commute
    matching) is the one that would actually move this number.

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

