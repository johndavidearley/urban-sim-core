# UrbanSimCore Development Roadmap

For the authoritative current validation baseline and active priorities, see
[STATUS.md](STATUS.md). This document retains detailed milestone history and
future ideas.

Last updated: October 18, 2026

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
- Automated validation at the M16 close-out: 294 tests passing; regular,
  strict, ASan/UBSan, and TSan configurations verified. Live counts are in
  STATUS.md.
- Phase 6 (M17–M22): implemented; local M22 verification is recorded in STATUS.md.
- Phases 7–11 (M23–M37) and waste-to-energy incinerators: complete. See STATUS.md.
- Phase 12 (M38–M44): complete; hosted CI verified on fd554fd.
- Phase 13 (M45–M48): M45–M46 complete locally. M47 is next.

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
- [x] Utility coverage (power, water) - upgraded from stub to a real,
      opt-in system; see "Next Targets" item 12

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
- [x] Integration path for GLFW/Vulkan (optional CMake target `UrbanSimCore-visualizer`)
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
- [x] Office demand - a genuine 4th RCI-like category, distinct from Commercial. `BuildingType::Office` and `ZoneType::Office` (symbol `O`) join the existing three; `ZoneDemand` gains an `office` field computed in `CitySimulator::evaluateDemand` from population and how established the city's commercial base already is (office space follows retail/downtown formation rather than leading it - an empty or purely-residential city generates zero office demand). Autonomous zoning (`autoZone`) splits new tiles four ways by demand, assigning office space to clean land alongside residential (it commands the highest base land value: 150 vs. commercial's 140). `GrowthSystem` builds/demolishes/redevelops real `BuildingType::Office` buildings under the same rules as the other three types. Offices are a genuine third job type in `PopulationSystem`'s income-band job matching (high income skews office-and-commercial heavy, low income skews industrial, roughly proxying blue- vs. white-collar employment) and count toward job-access land value, commute traffic, and district metrics. Economically, offices pay their own tax rate (9%, the highest - premium commercial-grade property) and maintenance, but sit outside the industrial/commercial goods-trade model entirely (white-collar work doesn't produce or consume physical goods). Persistence, save/load validation, CLI zoning (`--zone-rect ... OFFICE`), CSV/summary reporting, and the GLFW/Vulkan visualizer/PPM renderer all recognize the new type; old snapshots remain valid (the enum value is purely additive).
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
- [x] Disease/health - a new `HealthSystem` (`HealthSystem::evaluate`) is a pure read-out, exactly like `CrimeSystem`: no side effects on the map or entity store. It derives a single city-wide `HealthSummary::illnessRate` from housing crowding (population relative to residential capacity, a density/contagion proxy), residential-weighted pollution, uncovered sewage (`1 - ServiceCoverageSummary::sanitationCoverage`), and hospital coverage (`ServiceCoverageSummary::healthCoverage`, already computed by `ServiceSystem` every tick), which mitigates the combined rate by up to `healthCoverageReduction`. Runs unconditionally every tick (default-on, same rationale as crime) and - one-tick lagged - feeds migration desirability: a sicker city is less attractive to move into. Deliberately does not kill residents directly (that fidelity, and the "sudden mass loss" flavor of an epidemic, belongs to the opt-in destructive systems below) - disease here is an ambient quality-of-life drag, the same category as crime or pollution.
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

## Phase 6: A City With Consequences (M17–M22)

Defined October 6, 2026. M17, M18, M19, M20, and M21 are complete. Implement the rest in order. Each milestone
keeps the suite deterministic and names whether a default `--simulate` run
changes. The struck-through list under "Next Targets" is finished work.

These stay as they are through the whole phase:

- Player mutations stay the gameplay tools. A command/query façade waits
  for a second interactive frontend.
- Tick scheduling stays the interval fields on `SimOptions`. `SimulationTime`
  can label a HUD clock. It does not become a day/week/month scheduler.
- Citizens stay `PopulationGroup` aggregates. Power and water stay
  `ServiceSystem` coverage over the road graph, plus a load check.
- Freight stays in `TrafficSystem`'s commute-batch model. `TrafficMicroSim`
  stays the separate higher-fidelity demo.
- Sanitation stays an illness modifier, not a third construction gate.

### Milestone 17: Utility shortage — complete

Power already reports load. `ServiceCoverageSummary` has `powerDemandMW`
(occupancy × 0.002 residential, × 0.004 otherwise), `powerGenerationMW`
(sum of `ServiceFacility::powerCapacityMW`), and `powerSupplyRatio`. Nothing
reads the ratio. `Tile::connectedToPower` is still "a plant's BFS reached
this road anchor." Water has coverage and no demand. A fully wired city
can be short of power and keep growing.

- [x] When `enableUtilities` is on, a supply ratio below 1 sheds load.
      Deterministic: buildings ordered by stable id, farthest from the
      nearest plant first, lose power until remaining demand fits
      generation. Shed buildings read as not connected for the growth
      gate, the tile inspector, and a production penalty on commercial
      and industrial occupancy.
- [x] Water gets the same shape. Each Water facility has a fixed supply
      (a capacity field, defaulting so today's single starter plant still
      covers a small city). Demand is occupancy-based. Shortage sheds the
      farthest watered buildings.
- [x] `powerSupplyRatio` and a new water supply ratio show on the
      `--simulate` summary and the visualizer HUD. A shortage is visible
      without reading the console.
- [x] Coverage BFS is unchanged. A building outside the graph is
      uncovered; a building inside the graph can still be shed.
- [x] `enableUtilities` stays off by default, so a normal `--simulate` run
      is unchanged. With the flag on, growth can stall when plants are
      too small, which is the milestone.

**Deliverable:** A utilities run that outgrows its plants stops connecting
new buildings until another plant is placed. Tests lock full supply, a
forced shortfall, and shed order.

### Milestone 18: Schools and the labor market — complete

`educationCoverage` feeds the four-service average and district budget
weights. It does not change who lives in the city. `PopulationSystem`
splits housed population `{50, 35, 15}` low/middle/high and employed
population `{30, 40, 30}`, then matches bands to jobs with a fixed weight
table (low income cannot take office work). Schools are auto-placed, so
the coverage number is real and unused.

- [x] Zero education coverage keeps today's weights bit-for-bit. That is
      the uneducated baseline.
- [x] Lagged `educationCoverage` (one tick, same pattern as crime) moves
      the housed mix toward high income and opens office work to the
      middle band. Full coverage targets housed `{30, 40, 30}`, employed
      `{20, 40, 40}`, and job weights `{commercial, industrial, office}`
      of low `{30, 70, 0}`, middle `{25, 30, 45}`, high `{20, 5, 75}`.
      Weights lerp. There is no cliff at 100%.
- [x] Office buildings fill only to the educated share. Spare office
      capacity stays empty instead of being silently given to the low
      band. Industrial work absorbs the workers office cannot take.
- [x] The shift runs in `CitySimulator` and `playableCityTick`. The HUD
      and city summary show the three band counts, which already exist
      on `PopulationSummary`.
- [x] Default `--simulate` changes once schools exist, same category as
      office demand in M12. A test freezes coverage at 0 and checks the
      old split.

**Deliverable:** A city with schools employs a higher office share than
the same seed with education facilities removed. Unemployment and band
counts explain the difference.

### Milestone 19: Density follows land value — complete

`GrowthSystem` already redevelops. On a saturated zone with demand, an
occupied building is removed and recreated at 2× capacity, capped at 64×
`defaultCapacity`. The chance now follows land value, civic coverage,
and road congestion. The map draws capacity tier.

- [x] Redevelopment chance scales with normalized land value and local
      civic coverage, and falls as the tile's road congestion rises.
      A fringe tile in a saturated zone stays low. A high-value, served,
      uncongested tile is the one that doubles.
- [x] The cap and the doubling step stay. This milestone changes where
      densification happens, not the capacity ladder.
- [x] The visualizer draws capacity tier (height or footprint) so a 2×
      and a 16× building read differently in isometric and top-down.
- [x] Both hosts use the same `GrowthSystem` rule. Playable construction
      funds charge more for a redevelopment than for a new low building.
- [x] Default growth changes. Determinism stays a same-seed comparison.
      Building counts can match an older run while capacities differ, so
      tests assert capacity, not only spawn counts.

**Deliverable:** On one seed, the highest-capacity buildings sit on the
highest land value, and the map shows that.

### Milestone 20: Neighborhood crime and illness

Completed October 12, 2026. `NeighborhoodSystem` reuses the scalar crime
and illness formulas with cached local service distances. Deathcare still
handles mortality; neighborhood rates are cached read-outs.

- [x] Each residential building gets a local crime rate and a local
      illness rate. Local police distance lowers crime. Local hospital
      and sanitation distance lower illness. Local pollution raises
      illness. The city rate becomes the population-weighted mean and
      remains the migration-desirability input.
- [x] Reuse `ServiceCoverageCache` distance fields. No second BFS.
- [x] `LandValueSystem` subtracts the previous completed local rates, so a covered
      neighborhood is worth more than an uncovered one with the same
      jobs and pollution.
- [x] The happiness overlay and the tile inspector show the local
      values. The HUD keeps the city mean.
- [x] Default-on, both hosts. A city with no police and no hospital
      retains the no-services formula when sewage facilities are also absent;
      pollution uses its legacy city input in that case.

**Deliverable:** Two neighborhoods in one city, one policed and one not,
show different crime, land value, and inspector text. The city mean still
moves migration.

### Milestone 21: Freight and road class

Completed October 12, 2026. `FreightSystem` adds batched truck loads to
commuters, and road edges persist their local/arterial class. The fixed trade
gate is the west map edge at `(0, height/2)` and requires road access.

- [x] Industrial occupancy generates freight batches to commercial
      buildings, weighted by capacity and distance the way job matching
      already is. Net exports and net imports add batches between
      industrial buildings and one deterministic map-edge gate. With no
      industrial base, imports route directly to retailers.
- [x] Freight adds to `Edge::currentLoad` beside commuters and is reported
      separately on `TrafficSummary` and `SimTickMetrics`.
- [x] A road class on the edge: local stays capacity 10, arterial is
      capacity 40. The autonomous grid lays local roads, so this milestone
      does not quietly raise capacity. The road tool can upgrade a
      dragged segment to arterial for a higher construction cost. A toggles
      class while the road tool is active: $100/local new segment,
      $400/arterial new segment, $300/existing local segment upgraded.
- [x] Freight is default-on in both hosts, with `--simulate-no-freight`
      for comparison. Arterials are player-built, so an untouched
      `--simulate` run changes only by the added truck load.
- [x] `TrafficMicroSim` is left alone.

**Deliverable:** An industrial city with no commercial base shows export
revenue and freight congestion at the gate. Upgrading one corridor to
arterial lowers congestion on that corridor in the overlay.

### Milestone 22: Parks and one city on both hosts

Implemented: park zoning and neighborhood benefits, opt-in playable disasters,
shared municipal debt accounting, saved-city continuation on the headless
host, and transit visualization. `docs/STATUS.md` records verification.

- [x] The zone tool accepts Park on empty, non-road, non-water tiles.
      Zoning a park removes no buildings; occupied tiles still require
      the bulldozer first. Parks have a small per-tile upkeep.
- [x] A park raises land value and the happiness overlay in a short
      radius. Growth still never builds on a park tile. The autonomous
      loop does not auto-zone parks.
- [x] `playableCityTick` can run `FireSystem` and `NaturalDisasterSystem`
      under the same `enableDisasters` default-off switch as
      `CitySimulator`. Burning tiles and disaster losses show on the HUD.
- [x] `CitySimulator` adopts `TreasurySystem`'s debt: unpaid shortfall
      becomes principal, interest accrues, surplus cash repays it.
      Session fields already persist this for the playable path. The
      autonomous report prints `DEBT` the way the HUD does. Default tax
      and spending can still clear, so a balanced run stays at debt 0.
- [x] The visualizer draws the transit routes `playableCityTick` already
      keeps on `PlayableCityTickState::transitRoutes`.

**Deliverable:** A player can zone a park and see the land around it
rise, can turn disasters on in the visualizer, and can load a city whose
debt is the same number in the HUD and in `--simulate` after the same
shortfall.

---

## Phase 7: Player fiscal controls and diagnostics (M23–M26)

### Milestone 23: Budget controls and treasury breakdown

City taxes, service funding, and treasury history are exposed in one budget
panel. Service policy uses the same rules on both hosts.

- [x] An F6 budget panel exposes citywide residential, commercial, industrial,
      office, and income tax rates. Integer percentages range from 0 to 20;
      defaults remain 5/8/6/9/10, with a reset action.
- [x] The next-tick estimate separates tax sources, exports, building/park
      upkeep, imports, and civic operation. Category rounding reconciles to
      the charged totals. It excludes future building growth and debt flows.
- [x] A separate last-tick ledger records actual revenue, expenses, building
      construction, borrowing, repayment, interest, and cash/debt changes.
      Paused policy edits leave that history and current cash untouched.
- [x] Policies and the last ledger survive save/load and headless playable
      continuation. Older saves retain the existing default tax rates.
      Invalid policies and inconsistent ledger imports are rejected.
- [x] Per-service funding controls change both operating expense and delivered
      coverage/quality, with explicit previews of the tradeoff.
- [x] Verify service-funding effects and persistence on both hosts before
      closing M23.

**Deliverable:** A player can explain last tick's cash movement,
change taxes and department funding while paused, see revenue, cost and service
coverage respond, and continue
the saved policy on either host. Tax effects on migration and demand are
covered by M24 below. Tool purchases are immediate
cash transactions outside the simulation-tick ledger.

## Milestone 24: Tax policy effects on growth and migration

- [x] Zone-specific construction demand responds to taxes relative to neutral defaults.
- [x] Residential and income taxes influence arrivals and gradual departures.
- [x] Budget panel explains relative attraction without promising population growth.
- [x] Verify defaults, bounded responses, persistence, replay and both hosts.

The existing saved tax policy drives responses each tick. No extra hidden lag
state is introduced; paused edits do not advance population or construction.

## Milestone 25: Population and budget trends

- [x] Retain a bounded completed-tick history of population, migration and finances.
- [x] Add a Trends budget tab with independently labeled chart scales.
- [x] Mark ticks that first apply changed taxes or department funding.
- [x] Preserve history through save/load and headless playable continuation.
- [x] Verify retention, legacy saves, rejected imports, native UI and replay.

## Milestone 26: City alerts and diagnostics

- [x] Diagnose utility shortages, unemployment, essential-service gaps and deficits.
- [x] Show measurements and actionable checks in an F7 Alerts view.
- [x] Route alert actions to relevant overlays or the budget.
- [x] Persist debounce, recovery and cooldown state on both playable hosts.
- [x] Verify native actions, saved cooldowns, recovery, legacy saves and replay.

## Phase 8: City management and usability (M27–M30)

Make the existing simulation easier to understand and manage before expanding
its systems. M27–M30 are verified; hosted strict/sanitizer CI passed on
`7902ddf` (run 38777408741). Completion requires observable behavior and
validation, rather than a feature checklist alone.

### Milestone 27: Building and neighborhood diagnostics

- [x] Inspect local construction/redevelopment gates using the growth system's
      road, demand, density and location rules; distinguish blockers from influences.
- [x] Show delivered utilities, civic road reach and measured building vacancy.
- [x] Summarize a bounded nearby area with housing/jobs occupancy and correctly
      weighted health/crime/service measurements; label citywide joblessness separately.
- [x] Expose the same read-only diagnosis through MCP inspection.
- [x] Verify blocked and eligible sites, neighborhood boundaries/weights, native
      minimum-window layout, unchanged simulation state and regression replay.

**Acceptance:** A player can inspect a tile or building and identify local
blockers or measured factors to check next. No explanation claims a guaranteed
next-tick growth event or infers neighborhood unemployment from nearby job slots.
Diagnostics introduce no saved state or simulation mutation.
**Excluded:** individual citizen/job matching, historical causal attribution,
new growth rules, a new workforce model and neighborhood policy editing.

### Milestone 28: Transport management

- [x] Inspect routes, stops, assigned capacity, utilization and uncovered demand.
- [x] Add deliberate capacity controls with operating-cost feedback.
- [x] Link congestion diagnostics to relevant roads/routes and overlays.
- [x] Verify edits, save/load, deterministic replay and playable host parity.

**Acceptance:** A player can locate a measured transport bottleneck, change a
supported capacity setting and see the cost and subsequent utilization results.
**Excluded:** new rail systems, new vehicle classes, road geometry redesign and timetable simulation.

### Milestone 29: Management UI polish

- [x] Improve navigation between inspectors, alerts, overlays and budget views.
- [x] Add chart inspection and clearer policy-marker details.
- [x] Improve alert targeting and keyboard/readability behavior at minimum size.
- [x] Run native interaction and layout regression checks.

**Acceptance:** Common diagnosis-to-action flows work with mouse and keyboard,
with readable labels, visible focus and no overlapping controls at supported sizes.
**Excluded:** renderer replacement, a complete visual redesign and new simulation systems.

### Milestone 30: Stabilization and balance

- [x] Run sustained playtests across startup, growth, mature-city stress and recovery.
- [x] Record performance, tune demonstrated balance problems and retain scenarios.
- [x] Check older saves, continuation parity and deterministic replay.
- [x] Verify hosted strict/sanitizer CI for the reviewed commit.

**Acceptance:** Published scenarios reproduce expected outcomes, performance
limits and remaining issues are documented, compatible saves continue correctly,
and required hosted CI passes on the exact reviewed commit.
**Excluded:** speculative optimization, unmeasured balance changes and feature expansion.

Local evidence: `playtest-artifacts/m30-final-20261015/REPORT.md`. Two retained
1,720-tick scenarios cover fresh growth and an older mature city, utility stress,
recovery, exact GUI/headless continuation and two-run replay. An entity allocator
checkpoint bug was fixed; no speculative balance parameters changed. Hosted CI
passed on `7902ddf` in [run 38777408741](https://github.com/johndavidearley/urban-sim-core/actions/runs/38777408741).

---

## Phase 9: Regional connections, trade and tourism (M31–M35)

**Complete October 16, 2026; hosted CI verified on `bf8c036`.** Sea and air access
now produce measurable economic and visitor effects. Detailed rules and dependencies
are in [PHASE9.md](PHASE9.md). The M30 hosted CI gate passed on `7902ddf`.
Existing bus/rail commute behavior remains the baseline.

### Milestone 31: Regional connection foundations

- [x] Define road, sea and air connection records, explicit passenger/cargo units,
      deterministic allocation and completed-tick reports.
- [x] Add shared terminal footprint, placement, road/utility access and removal
      validation; expose precise inactive reasons.
- [x] Separate requested, admitted, delivered and unmet flows; establish treasury
      accounting and prevent duplicate trade/visitor credits.
- [x] Add optional validated persistence, inspection and host-parity fixtures;
      legacy cities without new facilities retain existing behavior.

**Progress (October 15):** M31 foundations are locally complete. Core records,
site/sea-access diagnostics, deterministic allocation, atomic registry placement
and removal, confirmed-delivery settlement, validated persistence and native-backed
MCP inspection are implemented. Verification passed 485 C++ tests, 19 rendered
tests, 7 Python tests, strict GCC checks and 483 headless ASan/UBSan tests.
Evidence: `playtest-artifacts/m31-transactions-20261015/REPORT.md`.
M32 must wire registry reservations into world edits/growth and book fulfilled
trade exactly once before exposing physical port placement. M31 exposes no
player terminal tools and creates no new sea/air benefit.

**Acceptance:** A disconnected or disabled connection admits no new traffic;
capacity is respected and reports reconcile. Save/load, replay and GUI/headless
continuation preserve connection state. No new sea/air benefit exists yet.
**Excluded:** simulated neighboring cities, individual travelers, ship/aircraft agents.

### Milestone 32: Ports and maritime trade

- [x] Place cargo ports and passenger terminals on valid shore sites connected
      by water to the map boundary and by road to the city.
- [x] Add explicit construction/upkeep and separate cargo/passenger capacities.
- [x] Allocate external freight across the existing road gateway and cargo ports;
      route only port-side truck legs over city roads and reconcile trade once.
- [x] Show terminal access, cargo use, passenger readiness, costs and blocked flows;
      verify inland water, disconnection, demolition and save compatibility.

**Completed locally (October 16):** Native `V`/Ports controls now place paid
cargo/passenger terminals, remove whole ports, toggle operation and adjust bounded
capacity. Shore, road, utility, occupancy and funds checks are shared with preview
and revalidated at execution. Port edits invalidate autonomous zoning caches.
Persistent regional IDs survive demolition and empty registries without reuse.

Both projections show distinct cargo/passenger yards in tile depth order. The
panel explains costs, upkeep and the immediate first-cargo logistics transition,
then shows current access and last completed flow/unmet trade. Cargo construction
persists the mode before any tick; removing the last port keeps it. Shared freight
and treasury routing uses ready ports and the finite 100-goods road gateway,
prices only fulfilled trade once and bills enabled cargo/passenger upkeep at
$20/$10 per tick. Passenger terminals remain readiness-only until M34.

Verified: 514 C++ tests, 21 rendered tests, 7 Python tests, strict GCC checks and
512 headless ASan/UBSan tests. Native coverage includes all five actions, prices,
paused activation/removal, ID nonreuse, save/load, exact GUI/headless continuation,
replay and minimum-window layout. Evidence:
`playtest-artifacts/m32-controls-20261016/REPORT.md`. Hosted Phase 9 CI remains
pending for the uncommitted working tree. M33 airports is next.

**Acceptance:** A valid cargo port carries measured external goods and changes
truck origins/destinations and its ledger entries. An isolated pond or disconnected
port carries none. Passenger capacity is inspectable but creates no visitors
until M34 supplies visitor demand.
**Excluded:** waterway construction, ship navigation, intercity ferries and timetables.

### Milestone 33: Airports and passenger access

- [x] Place an airport with a validated land footprint and road/utility access.
- [x] Add operating capacity controls with construction and upkeep feedback.
- [x] Feed passenger capacity into the shared connection report and expose
      inactive reasons, utilization and local access diagnostics.
- [x] Verify capacity limits, edits, demolition, persistence and host parity.

**Completed locally (October 16):** Airports use paid 4x2 land reservations,
validated civic road access and utilities on every tile. J/Airport button places
them; shared terminal modes handle operation, capacity and whole-site removal.
Policy is $25,000 construction, $30/tick enabled upkeep, 120 passengers/tick and
$1,000 demolition, with 30/60/120 capacity presets. Airports never enable cargo
mode or create cargo gateways. Both projections render runway/apron/terminal/tower.

Shared reports measure available passenger/goods capacity and enforce admission
limits; passenger-only cities get readiness reports without invented demand or
fees. Old reports retain unknown capacity. Disabled/disconnected airports expose
zero availability. Shared terminal specifications preserve maritime APIs and all
reservation/edit/growth guards, ID persistence and billing behavior.

Verified: 522 C++ tests, 22 rendered tests, 7 Python units, strict GCC and 520
headless ASan/UBSan tests. Native checks cover controls, prices, capacity, zero
arrivals/cargo activation, save/load, exact host continuation, replay, removal/
rebuild IDs and minimum-window layout. Evidence:
`playtest-artifacts/m33-20261016/REPORT.md`. Hosted Phase 9 CI remains pending.
M34 tourism and hotels is next.

**Acceptance:** A player can build and fund a valid airport and inspect its
available passenger capacity and cost. Invalid or disconnected facilities admit
no arrivals; actual tourism arrivals become available in M34.
**Excluded:** air cargo, runway/flight simulation, airline scheduling and noise contours.

### Milestone 34: Tourism, attractions and hotels

**Complete locally (October 16, 2026):** bounded park/quality demand shares road,
sea and air gateways; three-tick hotel cohorts and resident-first local travel
remain separate from residents. Native placement, per-hotel Open/Closed policy,
MCP controls and validated persistence are available. Explicit municipal fees
($2 per admitted day visitor, $4 per occupied bed/tick) and visitor services
($1 per day visitor/occupied bed/tick) reconcile to one treasury settlement.
Private spending is not credited as city income. Evidence:
`playtest-artifacts/m34-account-20261016/REPORT.md`.

- [x] Add bounded visitor demand driven by inspectable attraction and city-quality
      inputs, with road, sea and air access sharing one allocation.
- [x] Track day visitors and overnight cohorts separately from residents;
      add placeable hotels with finite beds, costs and existing-model job accounting.
- [x] Add aggregate visitor access/attraction trips using roads and available
      transit capacity after resident commuters; report both populations separately.
- [x] Show visitor demand, arrivals, stays, departures, bed occupancy, unmet demand
      and municipal tourism revenue/costs without counting private spending as city cash.
- [x] Verify cohort conservation, hotel/connection closures, saturation, treasury
      reconciliation, save/load, replay and host parity.

**Acceptance:** An accessible attractive city gains bounded visitors. Overnight
stays require beds, tourists never inflate resident population or migration, and
transport/hotel constraints produce explicit unmet demand. Port/airport capacity
can increase served visitors when access is the limiting factor.
**Excluded:** individual itineraries, events/seasons, a new hotel zoning type and city branding.

### Milestone 35: Regional management, integration and balance

- [x] Finish a unified Connections/Tourism management view, overlays, alerts,
      keyboard navigation, native assets and MCP inspection/editing.
- [x] Retain road-only, port, airport, mixed-access and closure/recovery scenarios;
      measure trade, treasury, visitors, resident travel and performance.
- [x] Tune only demonstrated capacity/cost problems; verify older saves and
      deterministic continuation across both hosts.
- [x] Run sustained rendered playtests and exact-commit hosted strict/sanitizer CI.

**Acceptance:** A player can diagnose an access, cargo or hotel bottleneck, change
its setting, and see the subsequent measured effect and cost. Published evidence
shows no double-counted flows or ledger income, no resident/transit regression,
and passing required CI on the reviewed commit.
**Excluded:** a renderer replacement, physical ship/aircraft simulation and further feature expansion.

Completed October 16, 2026. Local evidence includes 1,320 rendered mature-city
stress ticks, two matching replays, full GUI/CLI continuation and 6,000 profiled
ticks across startup/mature legacy/mixed fixtures. Hosted strict
Linux/macOS/Windows, ASan/UBSan, TSan and rendered/graphics checks passed on
`bf8c0366dda5dcef5882db2d7f3e6881a373a639` in
[run 38956044741](https://github.com/johndavidearley/urban-sim-core/actions/runs/38956044741). See
[PHASE9.md](PHASE9.md) and `playtest-artifacts/m35-scale-20261016/REPORT.md`.
M35 and Phase 9 are complete.


## Phase 10: End-to-end playability (M36)

**Complete.** See [PHASE10.md](PHASE10.md) for the native-control and evidence contract.

### Milestone 36: Fresh-city player journey and packaged candidate

- [x] Retain a fresh-city native-control playthrough to 10,000 residents using
      the normal budget, with autonomous building disabled.
- [x] Fix demonstrated discoverability, feedback, progression and layout issues.
- [x] Verify bounded acceptance, replay and save/continuation for that journey.
- [x] Produce and smoke-test a packaged build with launch/save instructions.
- [x] Pass relevant local checks and hosted CI on the reviewed implementation.

**Acceptance:** A player can start a city, understand and resolve its bottlenecks,
reach 10,000 residents and continue/save/load it through native controls. Evidence
retains economic/service conditions, not just a population counter. The packaged
candidate launches with documented runtime requirements and save location.
**Excluded:** new simulation domains and a renderer replacement.

---

## Phase 11: Surface water and early waterworks (M37)

**Complete.** See [PHASE11.md](PHASE11.md).

- [x] Default new maps to connected lakes/rivers with usable shorelines.
- [x] Add native shoreline intakes and cheaper untreated sewage outlets.
- [x] Model bounded same-water-body intake contamination and show cleanliness.
- [x] Verify persistence, native controls/replays, strict tests and hosted CI.
- [x] Package and smoke-test the current-host candidate.

---

## Phase 12: Scarcity the player can spend against (M38–M44)

Defined October 17, 2026. Complete October 18, 2026: hosted CI passed on
`fd554fd` (strict Windows/macOS/Linux, ASan/UBSan, TSan), and the packaged
macOS candidate passed native acceptance and launcher graphics probes. The
playtest after the phase found balance problems; Phase 13 corrects them.
Implement in order. Each milestone keeps the suite deterministic and says
whether a default `--simulate` run changes.

These stay as they are through the phase:

- Player mutations stay the gameplay tools. A command/query façade waits
  for a second interactive frontend.
- Tick scheduling stays the interval fields on `SimOptions`. A bond term is a
  tick count on the treasury, not a day/week/month calendar.
- Citizens stay `PopulationGroup` aggregates. School seats and hospital beds
  are city capacities, not enrolled people.
- Power and water stay `ServiceSystem` coverage over the road graph, plus a
  load check. Plant source is a field on the existing power facility.
- Freight stays in `TrafficSystem`'s commute-batch model. `TrafficMicroSim`
  stays the separate higher-fidelity demo.
- Sanitation stays an illness modifier, not a construction gate. Treatment
  changes shoreline contamination. It does not become a third growth check.

### Milestone 38: Power plants that differ

**Complete locally, October 17, 2026.** Strict `ctest` in `build-strict`
passed 590 tests. Playtest Python unit tests passed (7). Rendered playtests
and hosted CI were not re-run.

The service tool places a source on the existing Power facility. One Power
department slider still funds every plant. Sources are placement modes, in
the same way shoreline water is a mode of Water. Generic stays the autonomous
and legacy-save plant and adds no local tile pollution. Player-built sources
add road-tapered air pollution. Hydro must sit on land beside water and does
not become a water intake. Nuclear is high capacity and low air pollution only.

- [x] The service tool gains a power-source mode on the existing Power
      facility. One Power department slider still funds every plant. Sources
      are placement modes, in the same way shoreline water is a mode of Water.
- [x] Target catalog, full funding. Generic stays $12,000, $45/tick, 100 MW,
      400 kg/MWh, one land tile on a road. Coal is $10,000, $40/tick, 80 MW,
      1000 kg/MWh. Gas is $14,000, $36/tick, 60 MW, 450 kg/MWh. Nuclear is
      $80,000, $90/tick, 200 MW, 12 kg/MWh. Solar is $8,000, $8/tick, 15 MW,
      45 kg/MWh, on a 2×2 empty footprint. Wind is $6,000, $6/tick, 10 MW,
      11 kg/MWh. Hydro is $18,000, $18/tick, 40 MW, 24 kg/MWh, and must pass
      the M37 shoreline check.
- [x] Operating emissions add local air pollution on the existing pollution
      field, tapered by road distance from the plant. A higher kg/MWh plant
      raises nearby land pollution more. The citywide kg/MWh report stays.
- [x] Autonomous placement keeps today's generic 100 MW plant. An untouched
      `--simulate` run stays on that plant. Player-built sources are the new
      path, on both hosts. Legacy saves load as generic.
- [x] The inspector and HUD show source, MW, and local emissions. MCP placement
      accepts the source on the existing power service.

**Acceptance:** A coal plant and a hydro plant of the catalog MW produce
different nearby pollution, different upkeep, and different supply. Hydro
placement on inland land is rejected. A generic-only city matches the current
power and pollution totals.
**Excluded:** a separate power graph, weather, fuel markets, fuel stockpiles,
and a new radiation or meltdown system. Nuclear is high capacity and low air
pollution only.

### Milestone 39: Landfills that fill — implemented locally

Garbage facilities now retain occupied volume across ticks. Collection is
limited by remaining space, and spare incinerator capacity can recover space
by burning stored waste after current residual.

- [x] Each Garbage facility stores a remaining volume. New and auto-placed
      sites start at 80,000 units. Legacy saves start at that same volume.
      The volume is saved on the facility.
- [x] Recycle this tick's waste first, then burn residual up to incinerator
      capacity, then burn stored waste with any burn capacity left, then put
      the rest into landfill volume. A full site adds no collection capacity.
      Waste that no site can take stays `uncollected` and keeps today's
      pollution and happiness penalties. Burning stock uses the existing
      0.02 MW per unit and the incinerator air-pollution term.
- [x] The inspector, budget waste line, and an F7 row show remaining volume
      and the last completed fill rate. Reuse the M26 alert timing. Placing
      another landfill, or raising recycling or incineration, is the relief.
- [x] Default `--simulate` changes once a site fills. At 400 units a tick that
      is 200 ticks. Short runs that never fill keep today's waste totals. A
      test forces a small volume and locks the stop, the stock burn, and the
      uncollected penalty.

**Acceptance:** A depot with less volume than this tick's landfill flow stops
accepting, stored waste can be burned down, and the alert names the full site.
A run that never fills reports the same recycled, burned, and landfilled counts
as today.
**Excluded:** waste export, a second waste graph, and imported incinerator fuel.

Local validation (October 18, 2026): strict Windows builds, 598 C++ tests,
seven Python unit tests, and all 35 rendered playtests passed. See
[LANDFILLS.md](LANDFILLS.md) for behavior, evidence, and validation limits.

### Milestone 40: School seats and hospital beds — implemented locally

Education and health retain their road-reach fields, with derived city seats
and beds constraining education and local hospital mitigation. Population
groups remain citywide; there are no individual student or patient assignments.

- [x] A fully funded school has 800 student seats. A fully funded hospital has
      400 beds. Funding scales seats and beds the way it scales utility supply.
      Zero funding contributes none.
- [x] School-age demand is 20% of housed population. The labor lerp uses
      `min(education reach, seats / school-age demand)`. When seats cover
      demand, the housed and employed splits match today's reach-only result.
      Zero seats keeps the uneducated `{50, 35, 15}` baseline.
- [x] Patient demand is housed population times the city illness rate, with a
      floor of one whenever illness is above zero. Hospital mitigation, including
      the M20 local hospital-distance benefit, scales by
      `min(1, beds / patient demand)`. Sufficient beds leave M20 rates unchanged.
- [x] The HUD and the tile inspector show seats used, beds used, and which
      limit binds, reach or capacity. Both hosts share the formula. Saves do
      not need a new seat field; seats are derived from funded facilities.
- [x] Default `--simulate` changes when residents exceed seats or beds. A test
      with capacity forced above demand matches the current labor split and
      illness rate.

**Acceptance:** A city past its seats employs a lower office share than the
same city with enough seats. A hospital past its beds leaves a higher illness
rate than the same hospital with spare beds. Reach still limits a facility
that has spare capacity and a short road radius.
**Excluded:** per-student assignment, school catchment maps, and a new
education or hospital zone.

Local validation (October 18, 2026): strict Windows builds, 604 C++ tests,
seven Python unit tests and all 36 rendered playtests passed. See
[CIVIC_CAPACITY.md](CIVIC_CAPACITY.md) for formulas, evidence and limits.

### Milestone 41: Local fire response — implemented locally

Fire response now uses cached road distance per building. See
[LOCAL_FIRE_RESPONSE.md](LOCAL_FIRE_RESPONSE.md).

- [x] When disasters are on, a building's ignition, spread, and burn duration
      use the cached fire-station distance, with the same distance falloff
      NeighborhoodSystem uses for police. The citywide fraction stays on the
      HUD and remains the fallback when no station exists.
- [x] A station next to a building suppresses more than a station at the edge
      of the radius. Buildings outside every station radius keep the current
      no-coverage chance.
- [x] Both hosts. The inspector shows local fire reach. Disasters stay off by
      default, so a normal `--simulate` run is unchanged. With the flag on,
      where fires start and how long they burn follows station placement.
- [x] No second BFS. Read `ServiceCoverageCache`.

**Acceptance:** On one seed with disasters on, a block beside a fire station
loses fewer buildings than an otherwise similar block outside the radius. The
HUD still shows the city fire-coverage fraction. A run with disasters off
matches today's city.
**Excluded:** per-truck dispatch, which stays in `TrafficMicroSim`, fire boats,
and a separate fire graph.

### Milestone 42: Shoreline treatment — implemented locally

Shoreline sanitation now supports raw outlets and clean treatment. See
[SHORELINE_TREATMENT.md](SHORELINE_TREATMENT.md).

- [x] Sanitation gains a shoreline treatment mode. It uses the M37 shore,
      road, and utility checks. Construction is $9,000 and enabled upkeep is
      $34/tick, above the inland plant's $7,000 and $26. It provides
      sanitation coverage and adds no contamination.
- [x] Replacing a raw outlet with treatment removes that outlet's exposure.
      Intakes on that water body recover capacity on the next service pass.
      A different water body is unchanged. Zero funding closes the plant:
      no coverage and no discharge.
- [x] The cleanliness preview, inspector, and sanitation panel show treated
      versus raw. Legacy raw outlets and inland plants keep their costs and
      behavior. Old saves gain no treatment plant.
- [x] Default `--simulate` is unchanged until a treatment plant exists.

**Acceptance:** A raw outlet that cuts an intake, once replaced by a funded
treatment plant, no longer cuts it. An unfunded treatment plant covers
nothing and discharges nothing. Two water bodies still ignore each other.
**Excluded:** currents, advected pollutant mass, pipe networks, and a
multi-stage treatment ladder.

### Milestone 43: District height caps — implemented locally

Redevelopment doubles capacity up to 64× wherever land value, civic coverage,
and congestion allow. A district can stop that ladder. See
[DISTRICT_HEIGHT.md](DISTRICT_HEIGHT.md).

- [x] A district stores a maximum capacity multiple: uncapped, or one of
      1×, 2×, 4×, 8×, 16×, 32×, 64×. Uncapped is the default. Old districts
      load uncapped.
- [x] `GrowthSystem` will not redevelop a building past that multiple.
      Buildings already above a newly set cap stay until the player
      bulldozes them. The cap does not change demand, taxes, or zone bans.
- [x] The district tool and MCP edit the cap. The playable tick and
      `CitySimulator` both read it. The map's capacity-tier drawing is the
      check: a capped block stops climbing.
- [x] An uncapped district matches today's redevelopment chance and ladder.
      Default `--simulate` with no districts is unchanged.

**Acceptance:** Two saturated high-value blocks, one capped at 2× and one
uncapped, diverge in capacity. The capped block still accepts buildings at
or below 2×. Saving and loading keeps the cap.
**Excluded:** a new zone type per density, and automatic demolition of
buildings that already exceed a cap.

### Milestone 44: Municipal bonds — implemented locally

Unpaid ticks already become overdraft principal at 0.1% per tick, repaid from
later surplus. A bond lets the player borrow ahead of a project on a chosen
term. See [MUNICIPAL_BONDS.md](MUNICIPAL_BONDS.md).

- [x] The budget panel can issue one bond at a time: principal of $10,000,
      $50,000, or $200,000, repaid in equal principal parts over 100, 200, or
      400 ticks. Remaining principal accrues 0.15% per tick, separate from
      the 0.1% overdraft rate. Issuing adds the principal to cash immediately.
- [x] The tick pays the bond installment before ordinary expenses. Cash that
      cannot cover the installment rolls the missed amount into the existing
      overdraft and keeps the bond on its remaining schedule. Surplus does
      not prepay the bond unless the player pays it off from the panel.
- [x] The last-tick ledger and the next-tick estimate gain a bond line.
      Saves store principal, term, ticks elapsed, and interest remainder.
      Legacy saves have no bond. A malformed bond rejects before city mutation.
- [x] Default `--simulate` issues no bond. A city with no bond keeps today's
      cash and debt. Both playable hosts share the ledger.

**Acceptance:** Issuing a bond raises cash, then reduces it by the installment
plus interest on later ticks. A broke city rolls a missed installment into
overdraft once. Paying the bond off clears it. Two replays of the same issue
match.
**Excluded:** a bond market, a credit rating, more than one open bond, and a
calendar scheduler. The term is a tick count.

---

## Phase 13: Phase 12 balance correction (M45–M48) — draft

Drafted October 18, 2026 from the Phase 12 playtest
(`playtest-artifacts/phase12-playtest/REPORT.md`). M45–M46 are complete locally.
Implement in order. Phase 12's invariants still hold.

The unchanged M36 journey (`playtest/examples/phase10_native_journey.py`)
still reaches 10,000 residents with no debt, but it stalls. From tick 6,000 to
12,000 it holds 86 buildings and about 2,400–3,400 residents. M36 grew to 100
buildings and 4,700 residents over the same span. A throwaway build that cut
patient demand to 10% matched M36 within one resident at tick 10,000. A build
whose landfills never filled did not change the stall. The journey is the
shared acceptance driver for this phase.

### Milestone 45: Hospital demand a city can meet — implemented locally

**Complete locally, October 18, 2026.** The unchanged journey matched the M36
baseline within one resident at every 2,000-tick checkpoint and reached 10,379
residents at tick 16,100 with no debt. See STATUS.md.

Patient demand is housed population × illness rate. Illness is about 0.5 even
under full hospital coverage, so half the city always needs a bed. Hospitals
supply 25–50% of demand all game. At 10,000 residents that is 6,893 patients
against 1,600 beds.

- [x] Patients are housed population × illness rate × 0.2. At typical illness
      (about 0.65), one funded 400-bed hospital serves about 3,000 residents.
      The bisection solve and the sufficient-capacity fast path stay.
- [x] The HUD, tile inspector and MCP show patient demand beside beds used and
      supplied. The HUD bed line no longer clips under the disasters button.
- [x] A default `--simulate` run changes only where beds bind. Record the new
      400-tick CSV baseline.

**Acceptance:** The unchanged journey is within 5% of the M36 baseline
population at ticks 8,000 and 12,000. A 6,000-resident city with one hospital
still reports CAP.
**Excluded:** per-patient assignment, ambulances, and a new illness model.

### Milestone 46: Landfills that last — implemented locally

**Complete locally, October 18, 2026.** The journey criterion was amended
during implementation. At 2,000,000 units, the journey's three sites, built
near tick 5,500 and never added to, fill around tick 10,400. That is the
intended lifetime, so the criterion now bounds when sites fill instead of
forbidding the alert. See STATUS.md.

A landfill takes at most 400 units per tick and holds 80,000, so a busy site
fills in about 200 ticks. In the journey all four sites are full by the end,
and LANDFILL FULL is active from tick 6,000 onward. Garbage coverage still
reads 53–88% with every site full.

- [x] A landfill holds 2,000,000 units, so a site at full intake lasts about
      5,000 ticks. Saves record capacity per facility. Legacy facilities keep
      their used volume and gain the new free space.
- [x] Garbage coverage counts only waste that was recycled, burned or
      deposited. Full sites lower it.
- [x] The Services tab's sewage line no longer overlaps the divider.

**Acceptance:** In the journey, no LANDFILL FULL alert appears before tick
10,000, and no site fills within 4,000 ticks of opening. A site serving 10,000
residents with no recycling or incineration fills in 4,000–6,000 ticks. A city whose sites are all full shows garbage coverage
near zero.
**Excluded:** waste export, transfer stations and landfill closure costs.

### Milestone 47: Fires a city can contain

With disasters on, the 10,000-resident journey city (five stations, 61% fire
coverage) lost 223 buildings in 800 ticks. Population fell to 5,539 and did not
recover. Ignition destroys a building at once. Since M41, a building beyond
every station's radius gets no response.

- [ ] First, measure the same city for 1,000 ticks at the pre-M41 commit and at
      M44, and record both. This shows whether M41 made losses worse.
- [ ] Ignition marks a building as burning. It is destroyed only when its burn
      countdown ends. Local response shortens the countdown and can put the
      fire out before then.
- [ ] Disasters stay off by default. A default `--simulate` run is unchanged.

**Acceptance:** The journey city with its existing stations loses at most 5% of
its buildings over 1,000 ticks with disasters on. A district outside every
station's reach loses visibly more. Save/load mid-fire continues identically.
**Excluded:** fire trucks on roads, building materials and insurance.

### Milestone 48: Substations

Every power source reaches 22 road hops, and reach decides placement. The
journey city runs 400 MW of plants for 50 MW of demand. The capacity
differences between M38 sources rarely matter.

- [ ] Substation is a power source: $1,500, $4/tick, 0 MW, the same reach as a
      plant. It extends coverage and generates nothing.
- [ ] Plant toasts use the same capitalization as other toasts.
- [ ] Autonomous placement and legacy saves are unchanged.

**Acceptance:** The 10,000-resident journey city keeps 100% power coverage and
supply with one generating plant plus substations, at lower power upkeep. A
city of substations alone reports a power shortage.
**Excluded:** a separate power-line graph and transmission losses.

### Open questions, not yet milestones

- Money stops being scarce after about tick 4,000 ($17.8M at 16,300 in the
  journey). Bonds matter only early, when even small bonds outweigh revenue.
  Decide whether to scale late-game costs, or tie revenue to something players
  must keep investing in.
- Re-run this playtest after M45. Removing the hospital stall changes the
  money and growth curve that the other questions depend on.

---

## Timeline

| Phase | Milestones | Est. Duration |
|-------|-----------|---------------|
| 1 | M1–M3 | 4–6 weeks |
| 2 | M4–M6 | 6–8 weeks |
| 3 | M7–M8 | 3–4 weeks |
| 4 | M9–M10 | 4–6 weeks |
| 5 | M11+ | 12+ weeks |
| 6 | M17–M22 | complete |
| 7 | M23–M26 | complete |
| 8 | M27–M30 | complete; hosted CI verified on 7902ddf |
| 9 | M31–M35 | complete; hosted CI verified on bf8c036 |
| 10 | M36 | complete: end-to-end playability |
| 11 | M37 | complete: surface water and early waterworks |
| 12 | M38–M44 | complete; hosted CI verified on fd554fd |
| 13 | M45–M48 | M45–M46 complete locally |

**MVP Baseline (headless through save/load scaffold):** complete.
**Phase 7 (M23–M26):** implemented and locally verified.
**Phase 8 (M27–M30):** complete; local stabilization and hosted CI verified on `7902ddf`.
**Phase 9 (M31–M35):** complete; local stress/performance and hosted CI verified on `bf8c036`.

---

## Playtest-driven correction before M20

Completed October 12, 2026: player-built cities now share `MigrationSystem`
with the autonomous simulator. Population responds to housing, jobs, services,
utilities and quality of life instead of a fixed target that shrinks after
mortality. Last-tick arrivals/departures/deaths/net are visible in the HUD and
MCP. Session inputs survive save/load, and a rendered regression grows an
ordinary zoned city above 1,200 residents and unlocks bus transit. M20–M22 and the initial economy/road tuning are complete; M23 budget
controls are complete and locally verified.

## Next Targets (Post-Backlog)

Finished through Phase 9 (M35). This list is the
record of work that landed after M16.

1. ~~District-level service policies and budget controls~~ - already done,
   this entry predates and was never pruned after M14: `DistrictSystem`
   has `setDistrictTaxRates`/`setDistrictServiceAllocation`/
   `setDistrictServiceBudgetCap`/`setDistrictZoningOrdinance`, all wired
   into the autonomous `--simulate` loop via `--simulate-district`.
2. ~~Add persistence integrity report command for batch snapshot auditing~~ -
   done: `--audit-snapshots DIR` scans a directory for `*.json` snapshots,
   loads and validates each via the existing `SaveLoadSystem::
   loadSnapshotFromFile`, and prints a per-file OK/FAILED status line
   (with migration/tile/building/population counts on success) plus an
   aggregate summary - `--inspect-snapshot` already covered one file in
   detail, this covers many at once. Exit code reflects any failures
   found, for scripting/CI use.
3. ~~Extend benchmark reports with percentile timings over repeated runs~~ -
   done: `--simulate-benchmark-trials N` runs the same scenario (same seed
   every trial, so differences reflect measurement noise, not different
   simulated cities) N times and reports min/median/max per-phase timing
   instead of one run's numbers. Motivated directly by the Tile-storage
   Phase 4 checkpoint above, where a single run's noise (~20-30%) made it
   impossible to tell a real effect from measurement variance.
4. ~~Add route diagnostics export mode for offline analysis~~ - done:
   `--top-edges-export FILE` writes `--print-top-edges`' result set as CSV
   (rank, endpoints, congestion, commuters, commute time) alongside the
   existing console listing, for loading into a spreadsheet/plotting tool.
5. ~~Add commute demand-shaping policy experiments for scenario balancing~~ -
   done: `--commute-sweep DIR` mirrors `--run-policy-sweep`'s shape
   (baseline snapshot captured once, applied fresh per scenario, per-
   scenario CSV + ranked manifest) but for commute-side levers instead of
   district budget caps. New `SimOptions::transitCapacityMultiplier`
   (1.0 default, zero behavior change) scales auto-placed bus/rail
   vehicleCount and capacityPerVehicle; the sweep grids seeds x
   multipliers plus an automatic transit-disabled comparison per seed,
   ranking by mean traffic congestion and reporting mean transit modal
   share and mean unmet transit demand (`transitDemand - transitRidership`,
   a capacity-shortfall signal) as deltas vs. baseline. Validated with a
   real scenario: 0.25x transit capacity left 80 riders/tick capacity-
   constrained (unmet demand) while 4x fully absorbed demand and
   roughly doubled modal share - genuine, actionable signal, not just
   plumbing. Reuses `writeSimulationReportCSV` (newly exposed from
   `Simulate.cpp`, previously file-local) rather than duplicating the CSV
   format.

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
      `VisualizerGLFW`, and `CityPrinters`' `printZones` - migrated file by
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
    - **Post-Phase-4 correction:** the Phase 2 migration had introduced a
      real, quantifiable regression, found via the new `--simulate-
      benchmark-trials` tool (see "Next Targets" item 3): several call
      sites called `map.getTile(coord)` for one field (e.g. `.type`) and
      then a *separate* `map.zone(coord)`/`.landValue(coord)` for another
      field on the same coord, paying for a second index/bounds-check
      computation that a single reused `Tile&` would have avoided for
      free (zone never moved out of `Tile` - only pollution/landValue
      did). The worst offender: `LandValueSystem::averageLandValue`,
      called unconditionally every tick from `EconomySystem` (no interval
      gate, unlike `updateLandValues`) over the *entire* map, not just the
      active region - 250,000 tiles/tick at 500x500. Fixed there and five
      similar spots (`CitySimulator`'s `isZoningCandidate`/
      `emptyZonedTiles`, `GrowthMetrics::collect`, `GrowthSystem` x3,
      `MapRenderer`/`VisualizerGLFW`'s tile-color helpers). Measured with
      the new benchmark tool: Economy phase (which is where
      `averageLandValue`'s cost was attributed) dropped from ~2543ms to
      ~1881ms median (5 trials, 500x500/1000 ticks) - a ~26% reduction.
      Determinism verified unchanged.
    - Phase 5 (not pursued - see Phase 4's finding) - vectorize the hot
      loops explicitly by restructuring `updatePollution`'s clear/scatter
      phases and the zoning candidate scan to operate directly on the
      contiguous arrays instead of through a per-tile accessor call.
11. ~~Make `TrafficRouteCache` resilient to a continuously-growing city~~ -
    resolved without a code change, by item 9 (realistic commute matching).

    Investigated (first pass, before item 9): this item's premise didn't
    fully hold up. Re-tested with a *topology-stable* scenario (no growth
    at all, 1500 ticks on a saturated 40×40 city) and the cache still only
    helped ~1.4% - so topology churn wasn't the dominant limiter on hit
    rate. The real cause was that commute sampling drew random (home, job)
    pairs from a combinatorial space far larger than the number of
    commutes resolved per tick, so repeats were rare regardless of whether
    the topology was changing. Predicted at the time: "item 9 (realistic
    commute matching) is the one that would actually move this number."

    Re-verified (after item 9): same topology-stable 40×40/1500-tick
    scenario, same `--simulate-benchmark-trials 3` methodology for rigor.
    Traffic phase median dropped from 4404ms (cache disabled) to 3771ms
    (cache enabled) - a **~14.4% reduction**, roughly 10x the ~1.4% seen
    before item 9's fix. Distance/capacity-weighted job selection means
    commuters now repeatedly target the same nearby, high-capacity
    workplaces rather than uniformly-random distant ones, so the same
    (home, job) pairs - and thus cached paths - recur far more often.
    Confirms the original prediction; no further code change needed, a
    partial-invalidation scheme remains unwarranted (item 9 was the actual
    lever, not cache granularity).
12. ~~Build real power/water coverage (M7's stub)~~ - done: raised when
    auditing what was genuinely still missing from the project, this was
    the one concrete gap - `Tile::connectedToPower/connectedToWater`
    existed since M7 but were hardcoded `true` and read by nothing. Built
    as a real system reusing the existing `ServiceCoverageCache`/BFS
    infrastructure rather than parallel code: `ServiceType` gained
    `Power`/`Water` variants (indices 4-5), tracked through the same cache
    with type-filtered nearest-distance maps
    (`nearestPowerDistance`/`nearestWaterDistance`). `ServiceCoverageSummary`
    exposes `powerCoverage`/`waterCoverage` but deliberately excludes them
    from `overallCoverage`/`satisfaction`, so every existing caller of that
    formula is untouched.

    Fully opt-in via `SimOptions::enableUtilities` (`--simulate-utilities`
    on the CLI, off by default): when set, facilities auto-place Power/Water
    alongside Fire/Police/Health/Education (scaled so the original four
    types' density is unaffected), a new `updateUtilityConnectivity` pass
    writes real coverage into `Tile::connectedToPower/connectedToWater` each
    active tick, and `GrowthSystem::runStep` gains a `requireUtilities` gate
    that blocks construction on any tile not connected to both. With the
    flag off, connectivity is left untouched at its M7 default (`true`)
    forever - verified by a test that runs a full city and checks every
    tile.

    Found and fixed a real deadlock during testing: facility auto-placement
    only starts once population reaches 1000 (fine for Fire/Police/Health/
    Education, which are soft coverage stats that don't block growth), but
    with Power/Water now a hard growth gate, a cold-start city could never
    reach population 1000 in the first place - no buildings without
    utilities, no utilities without population, no population without
    buildings. Fixed by guaranteeing at least one Power and one Water
    facility exist from the very first placement pass, independent of
    population, while leaving the original four types' population-gated
    timing untouched. Confirmed via `--simulate-utilities` CLI runs: before
    the fix a city flatlined at 4 buildings/population ~6 for 80 ticks;
    after, it grows steadily (population 46+ and climbing by tick 52 on the
    same seed, before plateauing at the initial facilities' coverage
    radius - expected, since only 2 of the 6 facility types exist until
    population reaches 1000).
13. ~~Add sewage coverage as a third utility leg alongside power/water~~ -
    done as an illness-rate modifier, not a third construction gate.
    `ServiceType::Sanitation` coverage already existed (placement, cache,
    operating cost, HUD). `HealthSystem::evaluate` now takes
    `sanitationCoverage` and adds `HealthParams::sanitationWeight *
    (1 - coverage)` to the raw illness rate; hospital coverage still
    mitigates the combined rate. Default `--simulate` does not auto-place
    Sanitation (`enableUtilities` remains the placement opt-in), so
    uncovered sewage is the baseline until a player or utilities pass
    places it. Growth still requires only Power and Water.

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
