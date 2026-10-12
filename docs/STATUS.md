# Project Status

Last verified: October 12, 2026

Priorities updated October 10, 2026. The work queue is Phase 6 (M17–M22)
in `ROADMAP.md`. Strict `ctest` was re-run that day. Sanitizer jobs were
not.

This is the authoritative source for the repository's current implementation
and validation status. `ROADMAP.md` describes milestone history and future
ideas; `IMPLEMENTATION_STATUS.md` and `NEXT_STEPS.md` are retained as historical
development logs and should not be used to determine current priorities.

## Current State

The headless simulation engine and optional visualization stack are functional.
Implemented systems include world generation, zoning and growth, population,
traffic and commute routing, economy and trade, services and utilities
(including power generation capacity/source mix, water, and sanitation coverage),
district policy, public transit, disasters, crime and health, persistence,
replay verification, micro-traffic, metrics, CLI reporting, PPM rendering, and
the optional GLFW/Vulkan visualizer.

Phase 1 through Phase 5 milestone work recorded in the roadmap is complete.
Phase 6 (M17–M22) is the active queue. M17 (utility shortage), M18
(schools and the labor market), and M19 (density and land value) are
implemented. Redevelopment chance follows land value, civic coverage,
and road congestion. The map draws capacity tier. Playable migration now
uses the same desirability calculation as the autonomous simulator, removing
the shrinking fixed population target found during the hour-long playtest.
M20 (neighborhood crime and illness) is implemented. Next is M21, freight
and player road upgrades.

### Shared full-sim path (CLI + visualizer)

- Default CLI `--ticks N` (with only size/seed/terrain options) now runs the
  same autonomous `CitySimulator` engine as `--simulate`, instead of an empty
  clock loop.
- Playable (player-built) cities use `PlayableCityTick` in `urban_sim_core`
  (growth, population, traffic + transit offload, services, pollution, land
  value, health, crime, waste, deathcare, economy, treasury); the GLFW/Vulkan
  visualizer calls this shared step each live tick.
- `CitySimulator` also runs waste + deathcare each tick, auto-places Garbage/
  Cemetery with civic facilities, and records waste/deathcare on
  `SimTickMetrics` / `--simulate-report` CSV.
- Visualizer **G** toggles autonomous growth via the same
  `city_sim::expandConstruction` helper as `CitySimulator` (roads, pollution,
  zoning, empty-zoned pacing, civic facilities) then the playable tick stack;
  session save/load persists the G-mode flag and developed extent.
- HUD shows treasury cash (`$`) beside economy balance (`BAL $`, same metric as
  CLI `budgetBalance`). Unpaid playable-tick shortfall becomes municipal debt
  (`DEBT $n`) with a small per-tick interest remainder; surplus cash repays it.
  Session save/load persists principal and interest remainder (older saves load
  as debt 0). `CitySimulator` still uses cash-only treasury accounting.

### Playable migration correction (October 12, 2026)

- Housing headroom, employment and lagged city conditions determine arrivals
  and departures. Utility coverage and supply shortages also affect playable
  desirability. Deaths are counted once after migration.
- The full and compact HUD show arrivals, departures, deaths and net change
  for the last completed tick; MCP exposes the same ledger and desirability.
- Saves persist migration conditions, education lag and district growth
  modifiers. Older version-1 sessions still load; their old population target
  is no longer a cap. Loading refreshes summaries without changing saved
  road loads or land values.
- Road route ties and edge aggregation now use stable coordinate order,
  preventing construction/load insertion order from changing continuation.
  This can change equally short commute routes in existing CLI scenarios;
  the extracted CLI migration formula itself is unchanged.

### Neighborhood read-outs (M20, October 12, 2026)

`NeighborhoodSystem` reuses cached, radius-bounded service distance fields;
strength falls with road distance. Residential crime uses city unemployment
and poverty plus local police strength. Illness uses city housing crowding,
local hospital/sewage strength and local pollution. Without health or sewage
facilities, pollution retains its legacy city input, preserving the old
no-services formula. Empty buildings get local read-outs but do not weight
the city mean; an empty city reports zero.

Cached rates are saved on buildings (old snapshots default to zero), included
in replay checksums and consumed on the next land-value refresh. The happiness
overlay and tile inspector expose the rates; MCP tile/entity inspection uses
`crime_rate` and `illness_rate`. No new mortality mechanism is added.

### MCP inspection update (October 12, 2026)

MCP package 0.2.0 adds `urban-sim://inspection` with field units and timing,
`status.server_version`, and executable inspection version 2 capability flags.
Migration flow, local neighborhood rates, service/utility totals and power/water
facility details are available through inspection. The bridge protocol remains
version 1. The local `.venv` installation is editable; existing client
connections need a restart to load updated server code. SDK smoke tests pass
against rebuilt Debug and Release executables on macOS, including real field
reads, invalid-section rejection, PNG responses and disconnect cleanup.

### CI transit fixture correction (October 12, 2026)

Run `38261093054` failed the same rail-placement assertion in Linux strict,
Windows strict, ASan/UBSan and TSan; macOS passed. The old fixture assumed
seed 7 would grow past rail eligibility in 180 default ticks. The integration
fixture now starts with 4,800 housed residents and connected jobs, tests both
modes immediately, and checks route totals over ten ticks. A separate case
asserts no rail at 3,999 residents and one rail at 4,000. The rail threshold
and production simulation are unchanged by this correction.

All 391 individually discovered macOS CTest cases passed. Linux local checks
could not finish because exhausted host disk space left Docker storage
read-only; Linux/Windows and sanitizer hosted checks need a run of the fix.

### Recent performance work (post-MVP)

A multi-batch hot-path pass landed on `main` development:

- O(1) service result-cache validity (`EntityStore` mutation version)
- Chunked parallel pathfinding; A* over the road graph
- Lazy road nodes (no full-map node table)
- EntityStore type indices + O(1) capacity/count aggregates
- Zoning candidate list (no set→vector copy); incremental empty-zoned counter
- Spatial job sampling; multi-source service BFS by (type, radius)
- Dense land-value job distance field; active-region land-value averages
- Economy/population/service walks via type indices

## Validation Baseline

- 391 GoogleTest cases pass in the strict macOS Debug build on October 12, 2026.
  Five rendered MCP acceptance tests and seven Python unit tests pass locally.
  Headless Release passes 388 tests with Vulkan/GLFW discovery disabled;
  repeated 100-tick CLI reports match byte-for-byte.
- Tests are discovered individually by CTest.
- Regular and warnings-as-errors builds pass.
- Full ASan/UBSan and ThreadSanitizer runs pass.
- GitHub Actions runs strict Linux, macOS, and Windows jobs plus Linux
  ASan/UBSan and TSan jobs.
- CMake presets provide matching `regular`, `strict`, `asan`, and `tsan`
  configure/build/test workflows.

The authoritative live test list is produced by:

```bash
ctest --test-dir build --show-only
```

Quick performance smoke (headless):

```bash
./build/bin/UrbanSimCore-cli --benchmark-phase5 50
```

## Current Priorities

Phase 6 in `ROADMAP.md` is the queue. Implement in order.

1. **M17 Utility shortage.** Done. `powerSupplyRatio` and `waterSupplyRatio`
   shed the farthest wired buildings when `enableUtilities` is on (the
   playable tick always sheds). Default `--simulate` is unchanged.
2. **M18 Schools and the labor market.** Done. Lagged `educationCoverage`
   moves the housed mix toward high income and fills office seats from
   the educated share. Zero coverage keeps today's `{50, 35, 15}` split.
   Default `--simulate` changes once schools exist.
3. **M19 Density follows land value.** Done. Redevelopment chance scales
   with normalized land value and local civic coverage, and falls with
   road congestion. The 2× step and 64× cap are unchanged. Isometric and
   top-down views draw the capacity tier. Playable treasury bills a
   redevelopment more than a new low building.
4. **M20 Neighborhood crime and illness.** Done. Residential rates reuse
   service distance fields; nearer police, hospitals and sewage coverage
   improve the neighborhood. Resident-weighted means feed migration and
   the HUD. Lagged local rates lower land value; happiness and tile
   inspection show the local effects.
5. **M21 Freight and road class.** Goods are an accounting line. Freight
   loads the road graph. Arterials are a player upgrade; the autonomous
   grid stays capacity 10.
6. **M22 Parks and one city on both hosts.** `ZoneType::Park` is
   rejected by the zone tool. Disasters run only in `CitySimulator`.
   Debt exists only on the playable treasury.

Still constraints, not milestones:

- No command/query façade until a second interactive frontend exists.
- Benchmark large maps after hot-path changes (`--benchmark-phase5`,
  multi-trial, release builds).
- Split `GrowthPressureReport` and `CityPrinters` only if build times
  hurt.
- MSVC release packaging only if Windows shipping is required. CI already
  builds Windows.

## Document Roles

- `STATUS.md`: current implementation, validation baseline, and priorities.
- `ARCHITECTURE.md`: implemented system boundaries and data flow (keep in
  sync with code).
- `ROADMAP.md`: milestone definitions, completed work, and future ideas.
- `MVP_SPEC.md`: original product scope and success criteria.
- `IMPLEMENTATION_STATUS.md`: historical implementation journal.
- `NEXT_STEPS.md`: historical backlog sequence.
