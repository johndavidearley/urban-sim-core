# Project Status

Last verified: October 15, 2026

Phases 6–7 (M17–M26) are implemented and locally verified. Phase 8
(M27–M30) is underway: M27 diagnostics, M28 transport management and M29 UI
polish are locally verified. M30 local stabilization checks are complete;
hosted verification of a commit containing these changes remains pending. See `ROADMAP.md`.
The latest hosted strict and sanitizer run verified here passed on commit
`ecd7547` (M26). Hosted verification of the current working tree is pending.

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
5. **M21 Freight and road class.** Done. Batched goods routing adds
   trucks beside commuters. Player arterials have capacity 40; autonomous
   local roads stay at 10. Class and freight loads survive save/load.
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
