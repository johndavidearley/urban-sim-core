# Phase 9 plan: regional connections, trade and tourism

Started October 15, 2026. M31 connection/access/allocation, transaction and
persistence foundations are locally verified; M32 freight routing is in progress
and M33–M35 remain planned. Milestone checklists and acceptance criteria
are in [ROADMAP.md](ROADMAP.md). The M30 hosted CI entry gate passed on `7902ddf`
in run 38777408741.

## Player outcome

Build a cargo port or passenger terminal, build an airport, attract visitors and
provide hotels. Understand whether external access, local transport, attraction
quality or beds limit activity, then see the cost and result of an adjustment.
Sea and air access serve the city economy rather than acting as decorative assets.

## Current baseline

- Roads support commuters and aggregate truck freight. External freight uses a
  fixed west-edge gateway; economy calculations currently price net imports and
  exports independently of successful truck routing.
- Bus and aggregate rail share road-stop coverage and resident commute capacity.
  They do not represent intercity passenger arrivals.
- There are no ports, airports, visitors or hotels. Parks and city-quality
  measurements exist and can provide bounded tourism inputs.
- The native renderer, treasury ledger, history, alerts, diagnostics and MCP
  bridge provide integration points. Both playable hosts must use the same rules.

## Delivery order

| Milestone | Result | Depends on |
| --- | --- | --- |
| M31 | Connection records, access validation, accounting contracts and reports | M30 |
| M32 | Cargo ports and passenger terminals | M31 |
| M33 | Passenger airports | M31–M32 shared connection behavior |
| M34 | Visitor demand, stays, hotels and local visitor travel | M32–M33 |
| M35 | Integrated management and demonstrated balance/stability | M31–M34 |

Each facility milestone includes its usable placement tool, recognizable native
rendering, costs, inspection and tests. M35 completes the shared management view;
it must not be the first time a player can use an earlier facility.

## Connection and placement rules

Use a separate regional-connection model, not additional descriptive modes in
the existing bus/rail commute model. Share road anchors and diagnostics where
appropriate. Road connections, cargo ports, passenger terminals and airports
expose stable IDs, location/footprint, enabled state, configured capacity, cost,
access status and last completed-tick usage. Cargo and passenger capacities have
different units and cannot substitute for each other.

M31 must define footprint reservations and atomic placement/removal: reject an
overlap or invalid site before charging funds or changing tiles. A terminal's
declared road entrance must connect to the relevant city destination; finding
any nearby isolated road is insufficient. Reuse delivered utility requirements
and make every inactive reason visible. Capacity edits affect subsequent ticks;
they must not rewrite a completed report.

A sea site needs shore contact and a water component that reaches a map edge.
An inland pond does not provide regional sea access. Cache this reachability
against terrain changes instead of flooding the map for every shipment. No
canal or terrain-editing feature is required. Airports need a clear land footprint;
their external air connection is aggregate, without air lanes or aircraft agents.
No-water maps remain playable through roads and airports.

## Flow and treasury contracts

Before M32, specify allocation order, capacity units per simulation tick, costs,
fees and rejection rules in tests. Use stable tie-breaking rather than unordered
container order. Requested flow splits into admitted and unmet flow; admitted
cargo splits into locally delivered and undelivered flow. Retries across modes
must never credit the same unit twice. Start with no persistent freight backlog;
unserved cargo is reported for that tick rather than silently accumulating.

Existing saves with no regional facilities retain current road-gateway trade
behavior. New maritime logistics is an explicit opt-in when placing the first
cargo port, with a visible explanation and persisted mode. In that mode,
only fulfilled imports/exports receive the corresponding trade expense/revenue;
account for any resulting goods-availability effect once. Removing the last
cargo port does not silently switch back to unlimited legacy trade. Passenger
terminals and airports do not activate this freight mode. Capacity of the
remaining road gateway stays visible. M31 must lock this transition and its
save/load behavior before the first port is implemented.

Port freight replaces the external leg's gateway endpoint; the city-side truck
leg still contributes road load. Do not add a second truck trip or trade credit
for goods already served by the road gateway. Airport cargo is deferred.

Construction is a deliberate tool purchase under existing treasury semantics.
Operation, terminal fees and later municipal visitor receipts belong in explicit
reconciling ledger categories. Private hotel/retail spending may be displayed as
economic activity but is not automatically municipal revenue. Avoid crediting
the same visitor payment as both a fee and tax revenue on its full amount.

## Visitors and hotels

M34 introduces one bounded citywide visitor demand, allocated across road, sea
and air capacity once. Airports and passenger terminals expose readiness in
M32/M33; they do not fabricate visitor demand before this model exists. Retain a
road-only tourism path so a city does not require an airport to receive visitors.
Admission also requires reachable local destinations, not just terminal capacity.

Use aggregate day visits and overnight cohorts, separate from PopulationStore
resident totals and migration. Define stay duration in simulation ticks and
document its time interpretation. Track arrivals, stays and departures with
conservation rules. Avoid an unconstrained visitor count or a multiplier that
creates residents. Attractions should start from accessible parks and bounded
city-quality inputs, with a visible breakdown; bespoke attractions can follow
after the baseline is measured.

Hotels are deliberate placeable buildings with finite beds, construction/upkeep
and job capacity counted once through the existing employment model. They are
not a new zoning category. Overnight admissions require available beds for the
stay; insufficient beds remain unmet overnight demand rather than silently
becoming day visitors. Hotel closure/removal must release affected cohorts and
report early departures without leaving negative occupancy or ghost stays.

Visitor local trips use aggregate road/transit loads. Resident commuters get
first use of existing transit capacity; visitors can use the remainder. Report
resident and visitor usage separately and enforce their combined capacity.
Visitors never enter resident job matching or resident commute counts.

## Integration and verification

Add bounded optional save records with strict IDs, counts, ranges, footprints and
cross-reference validation before mutation. Legacy saves load without visitors
or fabricated facilities. Persist overnight cohorts, logistics mode and anything
needed for exact continuation; rebuild derived reachability caches. Paused
inspection must not generate arrivals, charge operation or change history.

Expose configured policy separately from completed-tick measurements in native
UI, MCP, CLI reports and history. Alerts should identify measured access/capacity
problems and lead to the affected facility. Preserve keyboard navigation and
minimum-window readability. Keep management terminology focused on decisions.

Retain the following scenarios and compare deterministic replays and GUI/CLI
continuation, including after removal, load and recovery:

| Scenario | Required observation |
| --- | --- |
| Legacy city without facilities | Existing trade, residents and commute behavior preserved |
| Coast versus inland pond | Only edge-connected water permits maritime operation |
| Broken entrance/utility or disabled terminal | Explicit reason; no admitted flow or free benefit |
| Road and cargo port sharing demand | Capacity reconciles; goods and trade credits counted once |
| Airport without attractions, then with demand | Empty capacity creates no automatic visitors |
| Attractive road-only city, then new access | New capacity helps only when access is limiting |
| Full hotels and full resident transit | Bounded unmet stays/trips; residents retain priority |
| Hotel/connection demolition during stays | Cohorts, departures, reservations and costs remain valid |
| Mixed access under stress and recovery | Flows, traffic, alerts and treasury reconcile over time |

Measure tick cost, routing/cache work, visitor counts, capacities and ledger
effects at startup and mature scale. Choose numerical caps/rates from those
fixtures; do not promise a universal FPS target from one machine. M35 requires
rendered interaction/layout checks and strict/sanitizer hosted CI on the exact
reviewed commit, as M30 does.

## Deferred scope

Physical ships/aircraft, waterway editing, flight schedules, named destinations,
simulated neighboring cities, air cargo, individual tourist itineraries,
seasonal events, marketing policies and a renderer replacement are outside
Phase 9. Distinct native facility visuals are included; vehicle animation is not
a prerequisite for measured sea/air transport or tourism.


## M31 implementation progress

The foundation APIs now include connection/access/allocation types, atomic
registry placement/removal with explicit costs, confirmed-delivery accounting,
validated optional persistence and native-backed MCP configuration/report
inspection. Both playable hosts preserve the state, and saved finalization ticks
prevent duplicate settlement after reload. M31 is locally complete.

The APIs are internal foundations. M32 must integrate physical reservations with
world editing/growth, cargo delivery and the existing treasury exactly once before
exposing port tools. M33 provides airports and M34 supplies visitor demand. No
active terminals, new trade credits or tourists are created by M31 configuration
or historical report loading. Native facility placement/rendering follows with
each facility milestone, as specified above.


## M32 implementation progress

The freight backend now supports explicit external gateways, each with a stable
ID, road-side position, enabled flag and shared import/export capacity per tick.
Actual deliveries consume capacity and add only their city-side truck legs.
Unreachable candidates can fall back to another gateway without double-counting
traffic or undelivered goods. Per-gateway actual imports/exports are reported in
stable ID order. Missing overrides preserve the old single road gateway;
explicit empty or exhausted access never restores unlimited legacy routing.

These are internal routing inputs. Paid cargo/passenger port APIs and physical
reservation protection are now implemented. M32 still needs native controls and
presentation, controller cache invalidation, explicit logistics activation,
operation costs and fulfilled-trade treasury integration. No active port traffic
or visitor simulation is enabled by these backend changes.


## M32 constructed-port reservation progress

`PortTool` creates 2x1 cargo/passenger terminal footprints through the shared
atomic preflight and charges fixed construction prices. It records constructed
ownership separately from building IDs. Dedicated removal checks ownership,
charges demolition and releases the footprint without a refund. Registry-only
operations reject constructed terminals to prevent orphaning their physical sites.

Gameplay records persist `constructed` (legacy default false). Derived tile
ownership is rebuilt on load, after validating against core tile, road-edge,
building and civic records. It is not duplicated in core snapshot JSON. Ordinary
editing tools, stale plans, low-level road/zoning writes and growth cannot replace
a reserved footprint. Full sessions retain terminals; standalone core snapshots
remain only core city data. Native fixtures verify both-host continuation and
legacy clearing. Player controls and active logistics still await integration.

## M32 operational site diagnostics

Built connections now have a separate read-only readiness check. It requires
matching ownership on every footprint tile rather than treating the facility's
own reservation as an obstruction. Missing/foreign ownership, loss of utilities,
invalid road entrances and loss of boundary-connected water block operation.
Dormant configurations report `not constructed` and admit no operational flow.
Site inspection checks the entrance; destination-specific checks test the actual
city-side road path without requiring a connection to the legacy road gateway.
MCP configuration inspection exposes this as `site_access`; measured settlement
reports remain separate. This does not yet activate logistics or charge upkeep.

## M32 shared live logistics integration

Both playable hosts now carry regional connections, the persisted logistics mode
and finalized reports through the same tick state. A constructed cargo port opts
in on a completed tick; dormant configuration and passenger terminals do not.
The saved mode persists after removal. New-mode routing combines ready ports with
a finite 100-goods road gateway and keeps imports/exports within shared capacity.
Only confirmed city-side delivery consumes it. The synthetic road ID avoids every
saved registry ID. Configuration limits remain 4096, operational reports allow
one additional fixed road row.

Fulfilled trade replaces forecast import costs/export receipts in the economy;
the existing treasury applies its scale and aggregate rounding once. Retailer
goods availability reduces commercial tax receipts/economic health and migration
attraction. Enabled cargo/passenger upkeep is $20/$10 per tick, including temporary
access outages; disabled facilities cost zero. The budget's civic/transit/ports
category includes it. Passenger facilities still create no visitor demand.

Completed flow reports include per-gateway actual delivery, unmet demand and
access reasons. New monetary metadata makes raw economy quote units explicit
(`amountScale`, normally 0.01) and records separate city-dollar `operatingExpense`.
The report is explanatory, never another treasury deposit. Paused refreshes retain
measured reports and cash; projections use a copy of road loads when traffic is
not being refreshed. Native port tools, presentation, cache invalidation and transition/capacity controls
are described in the following closeout section.


## M32 native controls and presentation (October 16)

`V` and the visible Ports button now expose paid cargo/passenger placement, whole-
port demolition, enable/disable and bounded operating capacity. Read-only planning
chooses a valid road entrance and actual reachable civic destination; construction
revalidates it. Cargo placement immediately persists the logistics mode and the
panel explains the fulfilled-trade transition and retained finite road limit.
Passenger terminals remain readiness/capacity only, with no visitor demand.

Per-tile cargo/container and passenger-shed geometry uses the normal depth order
in both projections. Port yards suppress vegetation. Preview, costs, access
reasons, current policy and last completed flows are visible. The informational
panel consumes clicks. Native and MCP actions share the controller; failed
placements/settings do not spend cash or consume IDs.

A validated persistent regional ID counter prevents reuse after demolition and
is preserved even with an empty registry. Older counters normalize above existing
IDs. Port edits clear autonomous zoning candidate caches, allowing released land
to become eligible again without zoning into occupied port footprints. The new
native acceptance case covers real button/key/site input, all five actions,
paused transition/removal, costs, ID nonreuse, save/load, exact host continuation,
replay and minimum-window layout. Final verification is recorded in STATUS.md.


M32 was closed locally on October 16 after 514 C++ tests, 21 rendered tests,
7 Python units, strict GCC and 512 headless ASan/UBSan tests passed. Evidence:
`playtest-artifacts/m32-controls-20261016/REPORT.md`. Hosted verification remains
part of the Phase 9/M35 gate. M33 airports is the next milestone.
