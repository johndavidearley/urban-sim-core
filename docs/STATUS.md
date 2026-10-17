# Project Status

Last verified: October 17, 2026

Waste-to-energy incinerators are implemented: native construction, recycling-first
residual waste processing, fuel-dependent electricity (up to 6 MW per plant),
air pollution, service funding and save compatibility. Local strict validation
passed 582 C++ tests, seven Python unit tests and 34 rendered tests. See
[WASTE_TO_ENERGY.md](WASTE_TO_ENERGY.md) for costs, controls and model limits.
Hosted CI passed on implementation commit `5da5ee8` in
[run 38996056201](https://github.com/johndavidearley/urban-sim-core/actions/runs/38996056201),
including strict Windows/macOS/Linux, ASan/UBSan, TSan and Linux rendered
playtests. The packaged macOS Intel candidate passed installed graphics smoke
and native incinerator acceptance with save/load and two identical replays.
Evidence: `playtest-artifacts/incinerator/REPORT.md`.


Phases 6–8 (M17–M30) are implemented and verified. Hosted strict builds on
Linux/macOS/Windows, ASan/UBSan and TSan passed for `7902ddf` in
[run 38777408741](https://github.com/johndavidearley/urban-sim-core/actions/runs/38777408741).
Phase 9 (M31–M35) is complete. Regional road/sea/air access, fulfilled-trade
accounting, bounded tourism/hotels, resident-first visitor travel and unified
Connections / Tourism management are implemented and verified. Local mature-city
stress, performance, save/load and GUI/CLI continuation evidence is retained in
`playtest-artifacts/m35-scale-20261016/REPORT.md`.
Hosted CI passed on implementation commit `bf8c0366dda5dcef5882db2d7f3e6881a373a639`
in [run 38956044741](https://github.com/johndavidearley/urban-sim-core/actions/runs/38956044741): strict
Linux/macOS/Windows builds/tests, ASan/UBSan, TSan, Linux X11/Wayland graphics and
rendered playtests, installed macOS graphics smoke and install-layout checks.
M36 / Phase 10 is complete: native fresh-city growth to 10,093 residents, exact
save continuation to 10,379, two identical replays and a smoke-tested macOS Intel
candidate. Strict local checks passed 571 C++ tests, seven Python unit tests and
32 rendered tests. Hosted CI passed on `ae9ccb9e6433da7b15be9cbe1663f1b7d05ccb62`
in [run 38981010151](https://github.com/johndavidearley/urban-sim-core/actions/runs/38981010151),
including strict Windows/macOS/Linux, ASan/UBSan, TSan and X11/Wayland playtests.
See `PHASE10.md` for conditions, limitations, retained evidence and package details.
M37 / Phase 11 is complete: connected lakes/rivers by default, shoreline water
intakes and untreated sewage outlets, bounded source-contamination feedback and
legacy-save compatibility. Local checks passed 576 C++ tests, seven Python unit
tests and 33 rendered tests; the M36 journey retained its original canonical city
hash. The extracted macOS Intel package passed native shore/F5/F9/replay and
16 launcher graphics probes. Hosted CI passed on implementation commit
`e89a6012c9098add2c309308c63d9c6a31cb96ee` in
[run 38991588321](https://github.com/johndavidearley/urban-sim-core/actions/runs/38991588321).
See `PHASE11.md` for scope, controls, retained evidence and package details.
See `ROADMAP.md` and `PHASE9.md`. M38 differentiated power plants is complete
locally (October 17, 2026). The service tool places coal, gas, nuclear, solar,
wind, and hydro on the existing Power facility. One Power funding slider still
covers every plant. Autonomous placement and an untouched `--simulate` run stay
on the generic 100 MW plant, which adds no local tile pollution. Strict `ctest`
in `build-strict` passed 590 tests. Playtest Python unit tests passed (7).
Rendered playtests and hosted CI were not re-run. The active backlog is M39–M44
in `ROADMAP.md`: finite landfill volume, school seats and hospital beds, local
fire response, shoreline treatment, district height caps, and municipal bonds.
Earlier entries below preserve the implementation history and the gates that
were pending at each stage.

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
Phase 6 (M17–M22) is implemented locally. M17 (utility shortage), M18
(schools and the labor market), and M19 (density and land value) are
implemented. Redevelopment chance follows land value, civic coverage,
and road congestion. The map draws capacity tier. Playable migration now
uses the same desirability calculation as the autonomous simulator, removing
the shrinking fixed population target found during the hour-long playtest.
M20 (neighborhood crime and illness) and M21 (freight and player road
upgrades) are implemented. M22 adds parks, playable disasters, shared debt
accounting, saved-session headless continuation, and visible transit routes.

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
  as debt 0). `CitySimulator` now uses the same ledger, including principal,
  fractional interest, and automatic repayment. Saved playable sessions can
  continue under playable rules with `--simulate-session`.

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

All 391 individually discovered macOS CTest cases passed. Subsequent hosted
run `38269529496` on commit `c4bd14c` passed Linux/macOS/Windows strict,
ASan/UBSan and TSan. M21 has newer local checks below; it has not run in
hosted CI yet.

### Freight and arterial roads (M21, October 12, 2026)

`FreightSystem` batches 32 goods per truck using the economy's powered
industrial/commercial occupancy totals and rounding. Retail destination
preference uses capacity divided by Manhattan distance plus one. Net imports
restock industrial sites, weighted by capacity and gate proximity, before
local retail deliveries; a city without industry imports directly to shops.
Net exports leave industrial sites through the fixed west-edge gate at
`(0, map height/2)`. Disconnected shipments are reported as unrouted goods.
Trade prices still use the existing produced-minus-consumed accounting.

Freight is default-on on both hosts and contributes to combined congestion
and migration. `--simulate-no-freight` disables it; `--simulate-no-traffic`
skips commuters and freight. Summary/CSV fields report truck counts and
routed domestic/import/export plus unrouted goods. Paths use a bounded
128-entry cache invalidated by topology or building-stock changes.

Road edges are local (capacity 10) or arterial (40). A toggles class while
the road tool is active. Local new segments cost $100, arterial new segments
$400 and local-to-arterial upgrades $300. Local planning and autonomous
re-laying leave existing arterials intact. Classes and separate freight
loads persist in snapshots; old roads load as local with zero freight.
Both projections draw wider arterial roads; congestion overlays and the
tile inspector expose the effect. MCP 0.3.0 / inspection version 3 adds road
inspection, class selection, freight metrics and the A control.

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

- 402 GoogleTest cases pass in the strict macOS Debug build on October 12, 2026.
  Six rendered MCP acceptance tests and seven Python unit tests pass locally.
  Headless Release passes 400 tests with Vulkan/GLFW discovery disabled;
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

Phase 12 in `ROADMAP.md` is the queue. Implement in order. M17–M38 and
waste-to-energy incinerators are done. M38 is local only: strict `ctest`
passed 590 tests on October 17, 2026, and rendered playtests were not re-run.

1. **M39 Landfills that fill.** Garbage sites start with 80,000 units of
   volume. Full sites stop accepting. Incinerators can burn stored waste.
2. **M40 School seats and hospital beds.** Labor and illness keep today's
   results when seats and beds cover demand. Past that, reach is no longer
   the only limit.
3. **M41 Local fire response.** With disasters on, station distance changes
   ignition, spread, and burn duration. Disasters stay off by default.
4. **M42 Shoreline treatment.** A funded treatment plant covers sanitation
   and does not cut intake capacity. Raw outlets stay available.
5. **M43 District height caps.** A district can stop the redevelopment
   ladder at a chosen multiple. Uncapped districts match today.
6. **M44 Municipal bonds.** The budget panel can borrow a fixed principal
   and repay it over a tick term, separate from the automatic overdraft.

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

## Playable economy balance — October 13, 2026

Playable refreshes now opt into occupancy-based property taxation ($200 per
occupied unit), earned-income valuation ($200 per worker, weighted by income
band), and maintenance proportional to capacity divided by base capacity.
The headless simulator keeps its original default valuations. Construction,
service operating costs, treasury scaling, and saved balances retain their
existing rules. Existing savings are not reset; revised flows apply on ticks.

The continued hour-city finished with 5,345 residents and $866,899,979. Its
revenue dropped from $282,829 to $2,415 per tick; expenses were $1,283 after the
maintenance change. A new mixed-zone starter retained $6,078 after 200 ticks
from $27,500 following setup, with no debt; that lightly serviced small city
still runs a deficit and needs expansion or spending changes. The recorded
starter replayed twice with identical canonical saved states. Evidence is in
`playtest-artifacts/economy-balance-20261013`. This checks two scenarios, not
long-term balance across every city size.

### Multi-seed balance pass

Twelve rendered 600-tick runs cover three simulation seeds (98765, 7, 42) and
four layouts: tiny settlement, connected multi-block starter, mature imported
city, and a starter with redundant power. Occupied housing/workplaces are now
protected from low-demand demolition; vacant stock can still retire. This
shared growth rule applies to both playable and autonomous cities.

All multi-block starters retained $21,761–$22,691 with no debt and a $14–$16
last-tick surplus. Tiny settlements and redundant-power layouts entered debt,
preserving expansion and spending tradeoffs. Mature populations grew in all
three seeds. Four layout scenarios and an arterial/trade scenario each replayed
twice with identical saved states. Core tests passed 414/414.

A sequential mature-city transport check found congestion medians 0.971 on
local roads, 0.930 after arterial trunk upgrades, and 0.860 after trade-gate
connection; unrouted goods fell from 546 to zero. City evolution between phases
means those comparisons are not fully controlled experiments. Congestion is
still high; larger transport scenarios remain a tuning priority. Evidence and
limitations are in `playtest-artifacts/balance-pass-20261013/REPORT.md`.

### Controlled transport tuning

Shared aggregate road throughput is now local 100 / arterial 400 trips/trucks
per traffic pass. Commute batches cap at 32 workers; sampling follows occupied
homes/workplaces, with an all-empty fixture fallback. Route diagnostics use
actual road capacity. Frozen 178-, 5,345-, and 6,892-resident cities were
compared under three seeds, three road configurations, transit on/off, and
sequential/parallel routing. For the reference parallel case, median
congestion is 0.928 on locals, 0.599 on all arterials, and 0.499 on all
arterials with transit. Costs and the 4:1 class capacity ratio are unchanged.

417/417 C++ tests passed, including single-batch pooled-load parity. Six more rendered 600-tick growing/mature runs stayed
solvent; starter and mature scenarios each replayed twice identically. Larger
cities and parallel routing's topology-only path selection remain limits.
See `playtest-artifacts/transport-pass-20261013/REPORT.md` for controlled
results, CPU measurements, methodology, and raw evidence.

### Arterial route attraction and cache correctness

Congestion-aware commute paths now charge relative free-flow cost 1.25 per
local edge and 1.0 per arterial edge. Separate routing revisions invalidate
commute caches on genuine class changes without changing topology/coverage
cache revisions. Idempotent upgrades preserve caches; distance-only paths
retain unit edge costs. On the frozen reference city, arterial-trunk median
congestion improved from 0.860 to 0.717 (0.799 to 0.656 with transit).

418/418 C++ tests passed. Six rendered 600-tick growing/mature cases remained
solvent, with starter/mature double replays plus a double upgrade/trade replay.
Evidence and remaining parallel queue-routing limits are documented in
`playtest-artifacts/arterial-routing-20261013/REPORT.md`.

### M22 parks — implemented October 14, 2026

Players can zone parks on empty land for $100/tile, with $2/tile/tick upkeep.
Roads, water, buildings, and services are excluded. Parks cannot grow
buildings; automated zoning never places them. Nearest-park proximity tapers
over four Manhattan tiles, adding at most 30 land value and 0.08 happiness
overlay value. Benefits do not stack and are refreshed on placement/removal.
Both projections render landscaped paths, benches and trees. Existing zone
storage handles persistence; MCP adds park capability and upkeep inspection.

Local strict build and 422 C++ tests passed, along with eight rendered tests,
16 graphics capture checks, and identical two-run park replays. Evidence and
limitations are in `playtest-artifacts/parks-20261014/REPORT.md`. Hosted CI
verification of parks remains pending. Other M22 host-parity work is still
open; this does not add disasters or visible transit routes.


### M22 transit visualization — implemented October 14, 2026

Key 9 or the ninth legend button opens the transit overlay. Bus routes use
cyan strokes, rail routes gold strokes, with white stop markers in both
projections. The normal city remains visible; isometric routes are drawn in
the ground layer before buildings. Consecutive stops use shortest connected
road paths, cached until topology or stops change. Disconnected segments are
omitted. These are route guides for the existing road-based transit model,
not a separate rail network or per-route passenger simulation.

The overlay reports network bus/rail route counts, stops, ridership, eligible
demand, and total capacity. MCP accepts overlay 8 and Num9, and advertises
`transit_overlay`. No save format or simulation rules change.

Local warnings-as-errors build, 424 C++ tests, all eight rendered acceptance
tests (including identical two-run replay), a final Num9 input check, and
16 graphics capture checks passed. A 5,345-person saved city with four bus
routes and one rail route was inspected in both projections. Evidence is in
`playtest-artifacts/transit-mature-20261014/`. Hosted CI remains pending for
this change.


### M22 completion — October 14, 2026

All M22 roadmap items are implemented. Playable ticks now run the existing
fire and natural-disaster systems behind a default-off switch. The detailed
HUD exposes the switch, both HUDs show active fires and cumulative losses,
and map flames mark burning lots. Burning lots cannot redevelop. Disabling
extinguishes flames while keeping loss totals. Destroyed buildings trigger
population/derived-state refresh; displaced residents count as departures.
Saves retain the switch, burning tiles, losses, and simulation seed. Older
saves default to disabled. Malformed burning-state imports are rejected.

`CitySimulator` now applies `TreasurySystem` to construction/service costs
and its existing economy: unpaid expenses become principal, positive cash
repays debt, and remaining principal accrues fractional interest at 0.1%
per tick. CASH/DEBT and treasury CSV columns make that ledger inspectable.
`--simulate-session PATH --simulate N` continues a playable save with its
saved seed and rules; optional `--simulate-session-out PATH` writes the result.

Continuation reserves imported entity IDs so later construction cannot reuse
them. Transit coverage refreshes on road topology changes, which also keeps
continuous and loaded G-mode consistent.

Validation: strict local build and 431 C++ tests passed; all nine rendered
acceptance cases passed, including identical two-run replay. The first full
rendered run passed eight cases and hit a disk-full capture error in replay;
that case passed after redundant raw captures were removed while retaining
PNGs. Seven Python tests, GCC 16 strict syntax checks, and 16 graphics capture
checks passed. Saved-city treasury/disaster/population and semantic city
contents matched across hosts; a 5,345-person city also matched with G-mode
active. A default 50-tick autonomous run retained debt 0. Evidence and preview:
`playtest-artifacts/m22-20261014/REPORT.md`. Hosted CI has not run for these
working-tree changes.


### M23 first slice — budget controls and treasury breakdown

The first M23 slice added tax controls. F6 or the detailed HUD budget button opens a modal panel
with citywide residential/commercial/industrial/office/income tax controls.
Whole percentages range from 0 to 20; reset restores the unchanged defaults
5/8/6/9/10. Paused edits recompute the estimate without a tick, cash movement,
or changes to the last recorded transaction. Taxes currently affect revenue;
that first slice did not yet model tax responses (added in M24 below).

The next-tick estimate itemizes six revenue and five expense categories.
Rounding is apportioned cumulatively so displayed categories reconcile to
the treasury totals. The estimate excludes new building growth and debt
flows. A separate last-tick ledger records actual billings, growth cost,
borrowing, repayment, interest, and before/after cash/debt. Immediate tool
purchases remain outside simulation-tick totals and are labeled as such.

Policy and immutable ledger history persist in saves and headless playable
continuation. Legacy saves retain default rates and start history next tick.
Invalid tax policies and inconsistent ledger imports are rejected before
city mutation. MCP adds `budget` actions and structured preview/history.

The first slice left service funding for the completion work recorded below.

Validation for the M23 first slice: local strict build and 436 C++ tests
passed; all 11 rendered acceptance cases passed, including two identical
budget-policy replays, save/load, custom-policy CLI parity, and minimum-window
UI checks. The initial SDK test expected 16 tools and was updated for the
new seventeenth budget tool, then passed with real budget calls. Seven Python
tests, GCC 16 strict checks, and 16 graphics capture checks passed. Evidence:
`playtest-artifacts/m23-20261014/REPORT.md`. Hosted CI remains pending.


### M23 completion — service funding (October 14, 2026)

M23 is complete locally. F6 → Services exposes all eleven departments with
0–100% funding, per-tick operating cost and road-reachable building coverage.
Native controls use 25-point steps; MCP accepts integer percentages. Cuts
reduce reach, utility supply and processing quality; zero suspends operation
without removing facilities. Restoring funding recovers original ratings.
Taxes and service funding have independent reset controls.

Paused funding changes refresh derived service state without advancing time,
charging cash or rewriting the last-tick ledger. Raw installed facilities
remain unchanged. Both simulation hosts share funding rules; saves retain
policy and legacy saves default to 100%. Invalid funding imports are rejected
before city mutation. Coverage is road reach, distinct from utility capacity.

Validation: warnings-as-errors build, 440/440 C++ tests, 12/12 rendered
acceptance cases, 7/7 Python unit tests, GCC 16 strict syntax checks and
16 graphics capture checks passed. Rendered tests exercise native controls,
minimum-window layout, paused edits, persistence, replay and GUI/headless
continuation parity. Evidence: `playtest-artifacts/m23-funding-20261014/REPORT.md`.
Hosted CI has not been run for these uncommitted changes.


### M24 — tax effects on growth and migration (October 14, 2026)

Implemented and locally verified. Zone taxes now modify construction demand
relative to neutral defaults; household attraction averages residential and
income tax deviations and affects migration. Default policies preserve prior
behavior. Attraction is bounded to 40–140%; demand remains normalized.
Above-default household taxes add gradual departure pressure rather than
changing population during a policy edit. Budget → Taxes shows R/C/I/O
attraction, and MCP exposes `budget.tax_attraction`. These are relative factors,
not promised growth rates. Existing saved taxes drive identical rules in both
hosts without additional hidden or lagged policy state. Existing custom-tax
saves consequently acquire the new behavior when continued.

Validation: warnings-as-errors build; 444 C++ tests; all 12 existing rendered
cases plus a new low/high-tax branch and low-tax save/load case; 7 Python unit
tests; GCC 16 strict checks. The minimum-window budget screenshot was inspected
and its new attraction row moved to avoid the reset control. Low-tax
migration keeps saved desirability normalized to 0–1. Evidence is retained in
`playtest-artifacts/m24-20261014/REPORT.md`. Hosted CI remains unrun for these
working-tree changes.


### M25 — population and budget trends (October 14, 2026)

Implemented: F6 → Trends adds six charts for population, net population change,
revenue, expenses, cash and debt. The latest 240 completed ticks are retained;
new and legacy cities start empty. Each chart labels its own scale and latest
value. Last-tick arrivals, departures and deaths appear separately. Revenue
and expenses are actual billings, while cash includes prior tool purchases.
Gold markers compare the policy applied on a completed tick with the preceding
recorded tick. The first retained row is the baseline; paused edits do not
append samples or rewrite history. Markers show timing, not proven causation.

History survives saves and headless playable continuation, with bounded,
validated imports rejected before city mutation. MCP exposes history and a
`trends` budget action; native tab hit-testing shares the visible-control rules.
Local validation passed: warnings-as-errors build, 449 C++ tests, all 14
rendered playtests, 7 Python unit tests and GCC 16 strict checks. Normal, empty
and minimum-window Trends captures were inspected. Evidence is recorded in
`playtest-artifacts/m25-20261014/REPORT.md`. Hosted CI remains unrun for this
working tree.


### M26 — city alerts and diagnostics (October 14, 2026)

Implemented and locally verified. F7, the detailed HUD Alerts button, and the
budget Alerts tab show power/water shortages, unemployment, essential-service
gaps and budget deficits. Each row shows measured completed-tick inputs,
a suggested check and a View action for the service overlay, demand overlay
or Taxes tab. Unchecked cities show no claimed measurements. Three bad ticks
activate an alert; two recovered ticks clear it, with threshold deadbands.
Repeat notices, including recurrences, wait at least 60 simulation ticks.
Paused edits and load do not evaluate alerts or issue notices. Saved inputs,
state, counters and cooldowns continue identically in both playable hosts;
missing legacy state starts unchecked. Invalid imports reject before mutation.

Validation passed: warnings-as-errors build, 454 C++ tests, 15 rendered
playtests, 7 Python unit tests and GCC 16 strict checks. The alert case was
repeated after the final unchecked-state UI refinement. Native routing,
recovery, persistence, cooldown suppression, minimum-window layout and
GUI/headless equality passed. One intermediate test run overlapped relinking
and could not launch three tests; the final complete rerun passed 454/454.
Evidence: `playtest-artifacts/m26-20261014/REPORT.md`. Hosted CI remains unrun
for this working tree.


### Phase 8 / M27 — building and neighborhood diagnostics (October 15, 2026)

Phase 8 now defines M27 diagnostics, M28 transport management, M29 management UI
polish and M30 stabilization, each with acceptance criteria and exclusions.
M27 is implemented and locally verified; M28–M30 remain planned.

The hover inspector and MCP single-tile diagnostic query show measured local
growth gates, delivered utilities, civic road reach, demand, congestion and
building vacancy. New-construction and redevelopment rules are distinguished;
clear local gates do not guarantee growth. Neighborhood radius 4 is bounded
to at most 81 tiles, with occupancy totals, resident-weighted crime/illness and
building-weighted service reach. City joblessness is explicitly not local;
nearby job slots cannot establish which residents hold those jobs. Diagnostics
reuse growth helpers, read funded service caches, add no saved state and do
not advance time or mutate the city.

Validation passed: warnings-as-errors build, 458 C++ tests, all 16 rendered
playtests, 7 Python unit tests and GCC 16 strict checks. Save/load diagnostic
values match, queries leave tick/funds/population/history unchanged, and normal
and minimum-window inspector captures were inspected. The new test's protocol
metadata comparison and camera fixture were corrected, and the inspection
reference section ordering was synchronized with tool discovery. No failures
remain. Evidence: `playtest-artifacts/m27-20261015/REPORT.md`. Hosted CI remains
unrun for the working-tree changes.


### Phase 8 / M28 — transport management (October 15, 2026)

F8 / the Transit HUD button opens route management. Routes expose stops and
road-linked stop count, reach, per-vehicle and total capacity, current coverage
congestion, and separately labeled completed-tick usage. Bus fleets support
0–20 vehicles and cost $2 per assigned vehicle per tick. Zero retains routes
and coverage while suspending carrying capacity and operating expense. Rail
remains inspect-only with prior cost behavior. Existing saved bus fleets acquire
the new operating cost on continuation. Budget expense category 3 now includes
civic and bus operation; both hosts and paused previews use the same cost helper.

Per-route demand follows existing route-order selection without double-counting
or changing offload matching. Usage reports preserve sampled capacity separately
from paused policy edits and persist through optional transitReport save data.
Uncovered city commuters are separate from unmet assigned demand. View actions
open transit coverage or congestion near the selected route's covered roads.
Invalid numeric fleets, overflowing capacities, duplicate IDs and inconsistent
report imports are rejected before mutation. No time, cash or completed history
is changed by a paused fleet edit.


M28 validation passed: warnings-as-errors build, 464 C++ tests, all 17 rendered
playtests, 7 Python unit tests and GCC 16 strict checks. Transport coverage
includes native F8/fleet/bottleneck actions, zero-fleet demand retention, actual
billing, unchanged paused history/cash, save/load, GUI/headless equality and two
identical fleet-policy replays. Normal, empty and minimum-window panels were
inspected. The new geometry assertion was corrected to allow fullscreen dimmer
vertices on the window boundary. Evidence:
`playtest-artifacts/m28-20261015/REPORT.md`. Hosted CI remains unrun for the
working-tree changes. M29 UI polish is next planned in Phase 8.


### Phase 8 / M29 — management UI polish (October 15, 2026)

Budget and transport panels now support visible keyboard focus, enabled-control
navigation and Enter activation. Trends supports mouse and keyboard sample
inspection, a synchronized cursor and tax/funding policy details. Minimum-size
layout and F2 readability switching work inside management panels.

Inspectors can be pinned with I and released with I/Escape. Utility/service and
employment alert actions select a relevant measured building where available,
center the map and pin its inspector; budget alerts open Taxes. Employment
selection identifies unfilled job capacity, not inferred local unemployment.
Navigation and inspection introduce no saved state or simulation changes.

Validation passed: warnings-as-errors build, 466 C++ tests, 18 rendered tests,
7 Python unit tests, and strict GCC syntax checks for the changed HUD and
geometry tests. Native screenshots were reviewed at normal and minimum sizes.
Evidence: `playtest-artifacts/m29-20261015/REPORT.md`. Hosted strict/sanitizer
verification of these working-tree changes remains pending. M30 is next planned.


### Phase 8 / M30 — local stabilization (October 15, 2026)

Retained scenarios exercise fresh 32-tile growth and an imported 96-tile mature
city for 1,000 growth ticks, 200 zero-power-funding ticks, 500 recovery ticks and
20 continuation ticks each. Each scenario passes two identical recorded replays
and exact GUI/headless comparison of persisted session and city state. The
older source save and its funds were preserved; tests use run-local copies.

Stress reduced mature-city population from 6,277 to 82; restored funding recovered
it to 6,937 (6,941 after continuation). Power alerts cleared on recovery. The
small starter fixture recovered from zero to 26 residents but accumulated debt;
it is not evidence of a financially balanced starting layout. No rates were
tuned from this limited fixture. Median growth command time was about 0.58 ms
per tick for the starter and 6.04 ms for the mature city. These include IPC,
were measured under local workload, and do not establish renderer FPS or a
cross-platform performance guarantee.

The scenario uncovered ID drift after save/load: deleted entities left the GUI
allocator ahead of a fresh CLI process. Core snapshots now preserve the next
entity ID. Full-world restore validates and restores that counter; legacy saves
derive it from live entities. Regression tests cover removed IDs, serialization,
legacy fallback and invalid counters rejecting before mutation.

Warnings-as-errors build, 468 C++ tests, 18 rendered tests, 7 Python unit tests
and strict GCC checks passed. Local ASan/UBSan headless testing passed 466/466 tests (native tests excluded;
macOS leak detection disabled). All evidence is in
`playtest-artifacts/m30-final-20261015/REPORT.md`. Hosted run 38742990099 passed
Linux/macOS/Windows strict, ASan/UBSan and TSan for `ecd7547`, which does not
contain the uncommitted Phase 8 work. M30 and Phase 8 remain open until required
hosted CI passes on a commit containing the reviewed changes.


### Phase 9 — planning (October 15, 2026)

The planned sequence is M31 regional connection foundations, M32 cargo ports and
passenger terminals, M33 passenger airports, M34 tourism/hotels and M35 integrated
management and stabilization. `PHASE9.md` records placement/access rules, shared
flow accounting, legacy trade compatibility, visitor/resident separation,
capacity constraints, reproducible scenarios and exclusions. Milestone checklists
and acceptance criteria are in `ROADMAP.md`.

No Phase 9 code or simulation changes have been made. M30's required hosted CI
on the Phase 8 changes remains the implementation entry gate. Phase 9 uses
aggregate sea/air access initially; physical ships/aircraft, air cargo, neighboring
city simulation and individual visitor itineraries are deferred.


### Phase 9 / M31 — first foundation slice (October 15, 2026)

The M30 entry gate is now satisfied by hosted run 38777408741 on `7902ddf`,
including all three strict platforms and both sanitizer jobs. This supersedes
the pending-CI notes in earlier dated entries.

`RegionalConnectionSystem` introduces road-gateway, cargo-port, passenger-terminal
and airport records. Passenger and goods capacities use separate per-tick units;
imports and exports share cargo capacity. Allocation is stable by ascending ID,
with imports before exports, and reports requested/admitted/unmet counts.
Disabled or inaccessible offers admit nothing. Duplicate/null IDs, unsupported
mode/capacity combinations and invalid access enums reject. Admission itself
creates no treasury credit or assertion of local goods delivery.

Pure site preflight checks bounded unoccupied land footprints and reservations,
adjacent road entrances, a real road path to the requested destination, delivered
power/water and maritime shore access. A derived flood-fill mask distinguishes
map-edge-connected water from inland ponds and diagonal-only contacts; callers
must rebuild it after terrain changes. Preflight never charges funds or reserves
city tiles. No new simulation loop or player tool is enabled yet.

Warnings-as-errors build and all 474 C++ tests passed (six new regional tests).
Strict GCC checks and retained evidence are recorded in
`playtest-artifacts/m31-foundations-20261015/REPORT.md`. M31 remains in progress:
atomic placement/removal, costs/ledger contracts, validated persistence, native/MCP
inspection and GUI/headless integration are still required before M32 ports.


### Phase 9 / M31 — configuration persistence and inspection (October 15, 2026)

Optional `regionalConnections` configuration survives gameplay-session save,
native startup/in-session load and headless continuation. Missing legacy data
clears to an empty list. Parsing rejects malformed types, ranges, duplicate/null
IDs, incompatible mode/capacity pairs, invalid footprints/entrances and overlapping
reservations. Road-gateway entrances must be within their footprint at the map
edge; other entrances must be immediately outside their footprint. Another
connection cannot reserve a footprint over an entrance. Saved order is by ID.

Load validates before applying the core snapshot; save validates before writing
it. Native-backed MCP inspection adds paginated `regional_connections` rows with
separate configured passenger/goods capacities per tick. Inspection does not
advance ticks or modify policy, cash or history. Records remain configuration
only: loading them does not reserve tiles, activate terminals or change trade.

Validation passed: warnings-as-errors build, 477 C++ tests, 19 rendered tests,
7 Python unit tests and strict GCC checks. Native coverage verifies invalid
imports preserve live records, full-session GUI/headless continuation matches,
legacy loading clears records and two recorded replays have identical hashes.
Evidence: `playtest-artifacts/m31-persistence-20261015/REPORT.md`.

M31 remains open for atomic placement/removal, delivery/treasury contracts and
native management presentation. Hosted CI has not tested the uncommitted M31
changes; Phase 8's verified hosted baseline remains `7902ddf`.


### Phase 9 / M31 — transaction and accounting foundation complete (October 15, 2026)

M31 is locally complete. `RegionalConnectionTool` atomically registers/removes
footprints with explicit nonnegative construction/demolition prices and cash
checks. It rechecks roads, destination reachability, utilities, land/registry/civic
occupancy and entrances at execution; sea access is rebuilt to reject a site
whose coast became a pond after planning. Failed operations preserve funds and
reservations. Removal releases only its record, needs no functioning access and
has no implicit refund. Registry data is not a fake building or terrain mutation.

`regional::settle` separates requested, admitted, delivered, undelivered and unmet
flow. Only confirmed local deliveries produce fees/import/export amounts, with
operation shown separately. Duplicate or unknown confirmations, excess delivery,
inconsistent totals, negative prices, overflow and stale/duplicate ticks reject
without replacing the last settlement. Rates are explicit integer dollar inputs;
no new default tariffs or balance changes are introduced. These accounting
amounts are not automatically deposited in the city treasury.

Optional `regionalSettlement` saves the validated report and finalization tick;
reload preserves the once-per-tick guard. Historical reports may predate the
current city tick and survive terminal removal. Native-backed MCP
`regional_report` inspection is read-only; both hosts retain identical reports,
and malformed monetary totals reject before world mutation. Legacy absence
starts unmeasured. The native regression includes exact full-session continuation,
legacy clearing, unchanged cash on report load and identical recorded replays.

Passed: warnings-as-errors build, 485 C++ tests, 19 rendered tests, 7 Python tests,
strict GCC checks and 483 headless ASan/UBSan tests (two native tests excluded;
macOS leak detection disabled, address/undefined checks halt on error).
Evidence: `playtest-artifacts/m31-transactions-20261015/REPORT.md`.
Hosted CI for the uncommitted M31 work remains pending.

M32 ports is next. It must connect the internal reservation APIs to world editing
and growth, active freight delivery and the existing treasury exactly once before
exposing port construction and native facility presentation. M31 itself adds no
placeable terminals, active sea/air traffic or tourists. This supersedes earlier
M31 entries listing these foundation contracts as unfinished.


### Phase 9 / M32 — external freight gateway backend (October 15, 2026)

`FreightSystem::simulate` now accepts an optional external gateway list. Each
record has a unique nonzero ID, map position, enabled flag and nonnegative shared
import/export capacity per tick. A null override preserves the legacy fixed road
gateway; an explicit empty or exhausted list carries no external goods.

Configured gateways are considered in stable ID order. Only deliveries with a
valid local road route consume capacity and add truck loads. Unreachable candidates
can fall back without duplicate truck legs or separate failure counts. Remaining
undelivered goods are counted once after alternatives. Gateway usage resets each
simulation call and reports actual imports/exports by ID. Existing domestic supply
and producer-restocking order/weighting remain unchanged. Restocked goods are
forwarded to retailers without a second external import.

Seven new cases verify split capacities and per-edge loads, domestic priority,
shared producer-restocking limits, disabled/unreachable fallback, explicit closed
access, deterministic ordering and cache behavior after capacity/topology edits,
and invalid inputs rejecting before traffic/cache mutation. Passed: warnings-as-
errors build, all 492 C++ tests, all 19 rendered regressions and strict GCC checks.
Evidence: `playtest-artifacts/m32-gateways-20261015/REPORT.md`.

This is a backend slice; existing playable hosts still use their legacy call path.
M32 remains open for physical cargo/passenger terminals, world reservation
protection, explicit logistics activation, fulfilled-trade treasury integration
and native/MCP controls. No ports are placeable and no trade prices or new costs
were activated by this slice. Hosted CI/sanitizer checks for M32 remain pending.


### Phase 9 / M32 — physical port reservation backend (October 15, 2026)

Internal `PortTool` now builds paid 2x1 cargo/passenger footprints and removes them
with ownership checks, a demolition fee and no implicit refund. Initial policy
is $15,000/200 goods per tick for cargo and $10,000/60 passengers per tick for
passenger terminals; demolition is $500. Registry-only APIs reject constructed
records, preventing accidental removal that leaves orphaned tile ownership.

Optional `constructed` saves physical status; legacy records remain unconstructed.
Tile ownership is derived from those records rather than duplicated in core JSON.
Gameplay save/load checks occupied terrain, zones, road-edge and building records
and civic sites; load validates before replacing the world and rebuilds the index.
Legacy load clears old flags. The physical index protects normal editing, stale
plans, direct road/zoning writes and growth. Generic bulldozing cannot partially
remove a terminal. Full gameplay sessions retain the records; raw core snapshots
remain core city data only.

Passed: warnings-as-errors build, 498 C++ tests, 20 rendered tests, 7 Python tests,
strict GCC checks over changed paths and 496 headless ASan/UBSan tests (two native
tests excluded; macOS leak detection disabled). Six new core cases and one new
native case cover payment, ownership, edit protection, invalid snapshots, exact
continuation, legacy clearing and replay. Evidence:
`playtest-artifacts/m32-reservations-20261015/REPORT.md`.

M32 is not complete. Native placement/removal controls and presentation, controller
cache invalidation after edits, explicit logistics mode, port operation costs,
active freight routing and one-time trade/treasury booking remain. No player port
construction tool or active port traffic is exposed yet. Hosted CI for the
uncommitted Phase 9 changes remains pending.


### Phase 9 / M32 — operational site diagnostics (October 15, 2026)

Built ports now use a dedicated read-only readiness check: all footprint tiles
must carry their own matching ownership, with valid road access, delivered
utilities and boundary-connected shoreline. Configuration that has not been
constructed reports `not constructed` and admits no operational capacity. The
new access status is appended as value 9; historical values retain their meaning.
Settlement serialization validates the new status with zero admitted flow.

MCP `regional_connections` inspection exposes `site_access`. Site readiness checks
the entrance itself without requiring a road to the old external gateway; a
specific local destination needs its own reachable city-side road path. This
inspection adds no traffic, time, upkeep or treasury booking.

Passed: warnings-as-errors build, 502 C++ tests, both affected rendered tests,
7 Python tests, strict GCC checks and 500 headless ASan/UBSan tests (two native
tests excluded, macOS leak detection disabled). The affected native cases retain
exact GUI/headless continuation and replay coverage. The previous full rendered
suite passed 20 cases; this change reran the two affected cases. Evidence:
`playtest-artifacts/m32-readiness-20261015/REPORT.md`.

M32 remains open for native port controls/presentation, controller cache
invalidation after edits, persisted logistics activation, upkeep and live freight/
fulfilled-trade treasury integration. No active ports or tourists are enabled.


### Phase 9 / M32 — shared live logistics and treasury (October 15, 2026)

Constructed cargo ports now participate in both playable hosts through the shared
tick. A completed tick with a constructed cargo port opts into persisted
`regionalLogisticsEnabled`; removing the last port retains the finite logistics
rules. Passenger terminals and dormant configuration do not activate cargo.
New-mode routing combines ready cargo ports with a finite west-edge road gateway
(100 goods/tick), using actual city-side deliveries and shared import/export
capacity. Synthetic road IDs avoid collisions with all saved registry IDs.

Fulfilled imports/exports replace forecast trade in the economy. Existing treasury
scaling and aggregate rounding book them once. Retail goods shortages reduce
commercial receipts/economic health and migration attraction. Enabled cargo and
passenger terminals cost $20/$10 per tick; disabled terminals cost zero. Upkeep
is billed once in civic/transit/ports expenses, including access outages. Native
budget projections include it. Passenger facilities create no visitor demand.

Completed regional reports track actual gateway delivery, unmet demand and access.
New `amountScale` identifies raw quote units (normally 0.01); separate
`operatingExpense` records city-dollar upkeep. Reports explain the tick without
another cash deposit. Paused projections after utility/tool edits run against a
copy of road loads; tax edits retain fulfillment pricing. Neither finalizes a
report, charges money nor adds another freight pass to live roads. Native-backed
metrics expose effective gateways, mode, upkeep and retailer goods availability.

Passed: warnings-as-errors build, 510 C++ tests, 20 rendered tests, 7 Python
units, strict GCC checks and 508 headless ASan/UBSan tests (two native tests
excluded; macOS leak detection disabled). A native fixture delivered 30 imports
at tick 50 for raw expense 1800 at scale 0.01 ($18), plus $20 upkeep, and retained
exact GUI/headless session continuation and replay. Budget minimum-window layout
was inspected. Evidence and source hashes:
`playtest-artifacts/m32-logistics-20261015/REPORT.md`.

M32 still needs native placement/removal controls, port graphics, controller
cache invalidation after edits and visible transition/capacity controls. The
construction control must set/explain the mode immediately on first placement;
the shared tick currently also handles already-constructed historical fixtures.
Airports, tourism and hosted Phase 9 CI remain pending.


### Phase 9 / M32 — native port controls and local closeout (October 16, 2026)

M32 is complete locally. `V` and the visible Ports button cycle Cargo, Passenger,
Remove, On/Off and Capacity. Native and MCP actions share read-only placement
planning and the same execution closure. Placement chooses a real civic road
destination and adjacent entrance, checks shore/utility/reservation/funds rules,
and revalidates before charging. Ports occupy 2x1 owned sites. Whole-port removal
charges $500, releases ownership and never refunds construction. Edits invalidate
autonomous zoning candidates so released land becomes eligible again.

Cargo construction pays $15,000 and immediately persists the logistics mode,
even while paused; demolition before the first tick still retains the mode.
Passenger construction costs $10,000 and creates no visitor demand. Enabled
upkeep remains $20/$10 per tick; disabling suspends upkeep and flow. Native
capacity presets are 50/100/200 goods or 15/30/60 passengers; semantic controls
can set zero through the corresponding design limit. Capacity cuts alone do not
suspend fixed upkeep. Invalid settings and placement do not spend or consume IDs.

The new validated `nextRegionalConnectionId` survives demolition and an empty
registry; older counters normalize above current registry IDs. Both projections
render distinct cargo containers/passenger sheds in normal tile depth order,
without vegetation in their yards. The port panel explains prices and the
fulfilled-trade/finite-road transition before construction, and shows current
access, capacity/upkeep and last completed per-port/citywide external flow.
Its clicks cannot build behind it. Passenger service is labeled inactive.

Passed: warnings-as-errors build, 514 C++ tests, 21 rendered tests (133.271 sec),
7 Python units, strict GCC checks and 512 headless ASan/UBSan tests (37.64 sec;
two native tests excluded, macOS leak detection disabled). Four new core cases
cover planning/read-only behavior, stale execution, counter persistence/rejection
and bounded graphics. The new native case exercises actual button/key/site input
for all five actions, costs, paused activation/removal, ID nonreuse, save/load,
exact GUI/headless continuation and two identical replays. Minimum-window panel
layout and both-projection port captures were inspected. Evidence/source hashes:
`playtest-artifacts/m32-controls-20261016/REPORT.md`.

M33 passenger airports is next. Visitors/hotels remain M34; no ship or aircraft
agents were added. Hosted Phase 9 strict/sanitizer CI and M35 remain pending;
these changes are uncommitted.


### Phase 9 / M33 — passenger airports and local closeout (October 16, 2026)

M33 is complete locally. `J` and the Airport button place a 4x2 inland airport
for $25,000 with capacity 120 passengers/tick. The shared terminal implementation
selects/rechecks real civic road access, delivered power/water on every tile,
land/occupancy/reservation/funds checks and atomic ownership. Roads along both
long sides serve the footprint's middle tiles. Maritime PortTool wrappers retain
their existing contract. Derived eight-tile ownership is validated and rebuilt
on load and protected by ordinary editing/growth guards. Airport edits use the
shared persistent ID counter and zoning-cache invalidation.

Shared V terminal operating modes enable/disable airports, cycle 30/60/120
passenger capacity (or explicitly set 0..120), and remove the whole facility for
$1,000 without refund. Enabled upkeep is $30/tick, including access outages;
disabling suspends it and availability. The existing treasury bills this once
in CIVIC/TRANSPORT expenses. Airports do not activate cargo mode or add cargo
gateways. Runway, apron, terminal and tower render in both projections with
preview, access/cost feedback, operating settings and measured utilization.

Regional report rows now include optional measured available passenger/goods
capacity. Admission must fit measured limits and blocked rows expose zero.
Legacy reports without those fields keep unknown historical capacity and still
round-trip. Passenger-only cities now finalize readiness-only reports without
changing legacy freight pricing or fabricating arrivals/fees. Cargo-mode reports
include passenger capacity as well. Current inspection exposes effective capacity;
paused inspection/refresh retains the last completed report and charges nothing.
Visitor demand remains zero until M34.

Passed: warnings-as-errors build; 522 C++ tests (8.73 sec); 22 rendered tests
(148.007 sec); 7 Python units; strict GCC checks; 520 headless ASan/UBSan tests
(48.82 sec), excluding two native tests with macOS leak detection disabled.
Eight new core cases cover inland payment/ownership, bad sites/utilities/funds,
whole-site editing/removal guards, upkeep/readiness without cargo or visitors,
save validation, capacity metadata/legacy reports, geometry and access loss.
The new native case verifies button/key/site input, design limits, disable/enable,
zero arrivals, paused save/load, exact GUI/headless continuation, two replays,
demolition/rebuild IDs and minimum-window layout. Both projections and minimum
layout were inspected. Evidence/source hashes:
`playtest-artifacts/m33-20261016/REPORT.md`.

M34 tourism/hotels is next. Air cargo, flight/aircraft agents, schedules and noise
contours remain outside M33. Hosted Phase 9 CI and M35 are pending; changes remain
uncommitted.


### M34 tourism foundation (October 16, 2026; in progress)

Standalone bounded attraction demand, shared road/sea/air allocation, reachable
hotel beds and three-tick overnight cohorts are implemented. Day departures,
scheduled/early overnight departures and transport/bed shortages are explicit.
Ten new tests cover bounded demand, mode sharing, capacity saturation, hotel
reachability, conservation, closure/removal and invalid-state rejection.

Warnings-as-errors builds, all 532 C++ tests, strict GCC and 530 headless
ASan/UBSan tests passed. No live simulation or rendering path consumes this model
yet; rendered tests were not repeated for this slice. Hotel placement/jobs,
world-route measurements, persistence, local visitor traffic and municipal
accounting remain before M34 completion. Evidence:
`playtest-artifacts/m34-foundation-20261016/REPORT.md`.


### M34 hotel backend and world access (October 16, 2026; in progress)

Added paid hotel placement backend: one unzoned commercial entity with 12 jobs,
32 visitor beds and $10,000 construction cost. Existing commercial maintenance,
taxes and employment apply once. Occupied-site tools and autonomous growth
preserve the hotel; ordinary bulldozing removes its identity for $200. Optional
core snapshot metadata survives legacy-compatible save/load. Hotel IDs, beds,
footprints, real road edges and civic overlaps are validated before load mutation.

Tourism world access now measures real road components for parks, terminal
entrances and hotels. The west road gateway offers 100 passenger slots per tick;
dormant connection records remain inactive. Hotels lacking roads or utilities
provide no beds. Ports/airports need actual attraction paths, not just a ready
entrance. These measurements remain separate from live completed tourism ticks.

Validation: 544 C++ tests, 542 headless ASan/UBSan tests, strict GCC, 7 Python
units, the 22 existing rendered tests and one new hotel-session rendered test.
The new test verifies paused metadata persistence, exact GUI/headless session
and core continuation, and two identical replays. M34 still needs placement UI,
live cohort/session integration, visitor road/transit loads and municipal tourism
accounting. Evidence: `playtest-artifacts/m34-hotels-20261016/REPORT.md`.


### M34 native hotel controls (October 16, 2026; in progress)

K/Hotel now places paid hotels through the shared HotelTool backend, with a
site preview and price/beds/jobs/upkeep panel. H badges identify hotels in both
projections. B/bulldoze removes them. MCP accepts hotel placement, exposes the
Hotel button/key/capability and reports hotel_beds separately from worker counts.
No live visitor stays or tourism revenue are fabricated; the panel marks visitor
service inactive until completed-tick integration.

Passed warnings-as-errors builds, all 544 C++ tests, 542 headless ASan/UBSan
tests, 7 Python units, strict GCC on rendering/native/input/bridge code, and all
24 rendered tests (164.241 seconds). New acceptance covers real button/key/site
placement, costs/guards, save/load, demolition/rebuild IDs, both projections,
minimum-window layout and two identical replays. Existing hotel-session coverage
verifies exact GUI/headless continuation. Visual review confirmed readable
signage/panel at normal and minimum sizes. M34 remains open for live visitor
ticks, cohort persistence, visitor traffic, operating policy and municipal
accounting. Evidence: `playtest-artifacts/m34-hotel-controls-20261016/REPORT.md`.


### M34 live visitor ticks and cohort persistence (October 16, 2026; in progress)

The shared playable tick now allocates bounded road/sea/air visitor demand and
three-tick overnight stays. Tourism is legacy-default-off; hotel construction
opts in, and the visible Tourism button/MCP policy also enables road-only day
visits without hotels. Native panels show current occupied beds and completed
arrivals, stays, departures and unmet demand. CLI session continuation prints
visitor counts; MCP inspect tourism exposes the full nested policy/state/report.
Potential demand follows candidate gateway road components even for disabled or
zero-capacity service, so closure remains explicit unmet transport.

Optional session records validate bounded demand, integer counts, IDs, capacities,
flow totals, cohort clocks, hotel occupancy and incoming-core cross references
before mutation. Paused hotel demolition releases cohort IDs and queues early
departures, keeping save/load valid; the next completed tick books them once.
Paused inspection/policy edits preserve reports, cash and resident history.
Visitors do not enter resident/job/migration totals or create municipal revenue.

Passed warnings-as-errors builds, all 549 C++ tests, 547 headless ASan/UBSan
tests, 7 Python units and strict GCC. All 25 distinct rendered tests passed across
the full run and retry: disk exhaustion interrupted the last five cases; after
reclaiming only pixel-verified redundant captures, those five plus live tourism
acceptance passed in a six-case retry (55.450 seconds). Live acceptance verifies
real policy input, active stay save/load, exact GUI/headless session AND core
continuation, paused demolition/departures, invalid import preservation and two
identical replays. M34 remains open for resident-first visitor road/transit loads,
hotel operating policy and municipal tourism accounting. Evidence:
`playtest-artifacts/m34-live-20261016/REPORT.md`.

### M34 resident-first local visitor travel (October 16, 2026; in progress)

Visitor day visits, hotel admissions, hotel/park trips and departures now produce
aggregate local travel. Resident commuters reserve transit seats first; visitors
share remaining covering-route capacity and then use roads. Separate visitor
road loads contribute to congestion. Walking and broken-access/unrouted counts
remain explicit. Visitor trips do not inflate resident population or commute
reports. Native hotel panels and MCP expose separate mode/route totals.

Completed travel survives sessions with strict cohort and resident-capacity
validation. Core snapshots preserve visitor/freight/other load splits. Repeated
paused refreshes and save/load do not accumulate visitor traffic or rewrite the
completed report. Legacy saves without travel remain valid. M34 remains open for
hotel operating controls and municipal tourism accounting. Validation passed:
557 C++ tests, 555 ASan/UBSan tests, 26 rendered cases and seven Python unit tests,
plus strict GCC warnings-as-errors checks. Evidence:
`playtest-artifacts/m34-travel-20261016/REPORT.md`.

### M34 hotel operating controls (October 16, 2026; in progress)

Per-hotel Open/Closed visitor operation is available through native K/Hotel tile
clicks and MCP budget set_hotel with hotel_id/hotel_open. Panels show policy and
beds; closing releases guests once on the next tick and blocks overnight
admissions while day visits continue. Reopening restores bed eligibility.
Commercial jobs, taxes and maintenance continue. Paused edits cost nothing and
preserve completed reports/cohorts, including across save/load. Core/session
persistence defaults older hotels open and rejects invalid policy records before
mutation. Validation passed: 559 C++ tests, 557 ASan/UBSan tests, 27 rendered
cases, seven Python unit tests and strict GCC warnings-as-errors checks.
Municipal tourism accounting remains. Evidence:
`playtest-artifacts/m34-policy-20261016/REPORT.md`.

### M34 complete locally: municipal accounting (October 16, 2026)

Explicit $2 day-admission and $4 occupied-bed/tick fees, with $1 visitor services
per admitted day visitor/occupied bed/tick, settle once with existing treasury
revenue/expenses before debt repayment and interest. Private visitor spending is
not city income. Original tax/maintenance/terminal/transit categories remain
separate. Native budget panels, hotel rates, MCP accounting/ledger fields and CLI
reports expose the amounts; the next-tick estimate explicitly excludes visitors.
Strict optional save records reconcile completed demand and ledger categories;
legacy records add no retroactive charges. Paused actions never settle again.

Validation passed: 562 C++ tests, 560 ASan/UBSan tests, 28 rendered acceptance
cases, seven Python unit tests and strict GCC warnings-as-errors checks. Coverage
includes saturated beds, stay turnover, shutdown, cash/debt/category totals,
invalid imports before mutation, minimum-window readability, exact GUI/CLI
continuation and identical replays. Evidence:
`playtest-artifacts/m34-account-20261016/REPORT.md`.

M34 is complete locally. M35 unified regional/tourism management, integrated
balance/performance, sustained rendered playtests and hosted Phase 9 CI remain.


### M35 started: unified management, October 16, 2026

Added the first Connections / Tourism panel (U), combining current policy with
completed cargo/visitor/travel/accounting measurements and unmet-access/bed
counts. Native mouse and keyboard controls select constructed terminals, change
operation/capacity and toggle tourism; MCP exposes their bounds and focus.
Strict local build, 564 CTest cases and rendered default/minimum-window acceptance
passed. See `PHASE9.md` for scope and retained evidence. M35 remains in progress;
integrated hotel editing, map/alert/overlay navigation, stress/recovery and
performance scenarios, host parity and exact-commit hosted CI remain pending.


### M35 continued: hotel management and facility navigation

Connections / Tourism now browses hotels as well as terminals. Open/Closed
controls preserve measured guests while paused; closures release cohorts on the
next tick and retain commercial jobs/upkeep. Hotel beds and worker capacity are
shown separately from completed guest counts. Locate on Map pins a hotel or
terminal and switches to the service coverage overlay; the tile inspector labels
hotel policy/capacity. F6/F7/F8 switch to Budget/Alerts/Transit. MCP exposes the
new controls and tab/selection state. Loading resets management selection.

The native closure/recovery scenario fills a road-only hotel, closes it with
active guests, verifies early departures and ledger totals, reloads the paused
closure, reopens via keyboard, and checks twelve further ticks. Two deterministic
replays match. Strict build and 565 CTest cases pass; management layout and map
navigation were inspected at minimum window size. Evidence is retained in
`playtest-artifacts/m35-hotels-20261016/REPORT.md`. Mixed-access stress, actionable
regional alerts, scale/performance and exact-commit hosted CI remain pending.


### M35 continued: measured regional alerts and mixed-access recovery

The Connections button highlights measured passenger-access, bed, cargo-admission,
cargo-delivery or local-travel bottlenecks. Next Alert / Manage Alert select the
relevant terminal or hotel, or open Transit. Blocked passenger terminals take
priority over saturated ready terminals. Counts/ticks come from completed reports;
paused edits preserve warnings until a subsequent measurement. These derived
alerts add no persistence fields and leave city-alert debounce/cooldown unchanged.
F7 links to regional management with U; MCP `regional_alerts` and CLI playable
continuation expose the same diagnostic model.

A retained 32x32 mixed road/sea/air/cargo fixture has residents, a shared-capacity
bus and one hotel. It measures twenty ticks across baseline, airport closure,
reopening and recovery. Closing the airport reduces day admissions from 221 to
160; native Manage Alert selects that airport, and reopening restores 219 on the
next tick as demand evolves. Bus use remains bounded (12 residents + 18 visitors
at tick 24; 26 + 4 at tick 40). Cargo flows, visitor demand splits and treasury
categories reconcile each tick; twelve-tick GUI/CLI continuation matches, and
two fresh-process replays match. Strict build, 569 CTest cases and 7 Python tests
pass. Evidence: `playtest-artifacts/m35-alerts-20261016/REPORT.md`.

M35 remains in progress: this small mixed fixture does not establish mature-scale
performance or long-duration stability. Those measurements, broader closure/load
stress and exact-reviewed-commit hosted strict/sanitizer CI remain.


### M35 local gates verified: scale, endurance and recovery

The retained 96x96 legacy city (6,937 residents at input) continues without
fabricated visitors. A paid mixed-access extension then passes 1,320 rendered
stress/recovery ticks, including airport/shore-passenger closure, road-only
arrivals, hotel closure, cargo-port demolition, closure save/load and recovery.
Passenger admissions fall from about 266 to 160 and then 100 as modes close;
reopening restores them. Cargo delivery falls from 300 to 100 goods when the
port is demolished and returns to 300 after rebuilding; finite logistics stays
enabled. Block histories reconcile cash, and full twenty-tick GUI/CLI continuation
and two 90-step fresh-process replays match. Final rendered population is 7,455.

An optional strict-built regional benchmark and retained profile runner measure
6,000 ticks across small 32x32, mature legacy 96x96 and mature mixed 96x96 saves.
Warm/cold modes agree on final world checksums, funds, tourism and cargo. Mean
retained-cache tick costs are 0.915, 7.353 and 7.483 ms respectively on this
x86_64 macOS/Clang 17 RelWithDebInfo host. File/render/inspection time is excluded;
raw distributions and routing/cache sizes are retained. This is bounded fixture
evidence, not a universal FPS or maximum-city claim. No rates/costs were tuned.

Validation: 569 normal CTest cases, 567 headless ASan/UBSan CTest cases, seven
Python unit tests and a 100-tick mature mixed sanitizer benchmark pass. Local
macOS sanitizer runs disable leak detection. Tools and reproduction instructions
are in `tools/REGIONAL_BENCHMARK.md` and `docs/PLAYTESTING.md`; raw evidence is in
`playtest-artifacts/m35-scale-20261016/REPORT.md`.

M35's first three roadmap checklists are locally verified. The milestone remains
open solely for exact-reviewed-commit hosted strict Linux/macOS/Windows,
ASan/UBSan and TSan validation. Working-tree changes are still uncommitted.


### M35 / Phase 9 complete — October 16, 2026

[Hosted CI run 38956044741](https://github.com/johndavidearley/urban-sim-core/actions/runs/38956044741) passed on
`bf8c0366dda5dcef5882db2d7f3e6881a373a639`, the pushed implementation commit.
All five jobs passed: strict Windows, macOS and Linux, ASan/UBSan and TSan.
Linux X11/Wayland rendered playtests and graphics/control smoke, installed macOS
graphics smoke and all platform install checks passed. This closes M35's final
exact-commit gate and completes Phase 9. The prior local evidence remains valid;
this closeout changes documentation only and does not claim the implementation
CI run tested later documentation edits.
