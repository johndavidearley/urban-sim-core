# Finite landfills (M39)

Garbage facilities start with 2,000,000 units of free volume (80,000 before M46). Each completed tick
recycles current waste first, burns current residual within incinerator capacity,
uses spare burn capacity on stored landfill waste, then deposits the remaining
reachable waste. A landfill accepts at most 400 units per tick at full funding,
further limited by free space. Overflow remains uncollected and applies the
existing pollution and happiness penalties. Full sites retain upkeep costs.

Incinerators burn at most 300 units per fully funded plant per tick, across
current and stored waste combined. Both sources generate 0.02 MW per unit and
use the existing bounded incinerator pollution increment. Stored waste can be
burned even when the city generates no new waste. Storage disposal is a citywide
accounting transfer; no separate waste routing or export system is introduced.

Facility inspection shows free volume and the net fill on the last completed
tick. A negative fill means stored waste was removed. The budget Services tab
shows total free volume and last net fill. F7 includes a LANDFILL FULL row naming
the first full site's coordinates. It activates after three completed bad ticks,
recovers after two healthy ticks, and repeats notices after 60 ticks. Additional
landfill space, recycling, and incineration are the available relief measures.

Saves persist `landfillCapacity`, `landfillRemaining` and `landfillLastFill` on
facilities. A facility without `landfillCapacity` is from before M46, when sites
held 80,000. It keeps its used volume and gains the new free space, so 123 free
units load as 1,920,123. Facilities without any volume fields load empty with
zero last fill. Invalid volumes are
rejected before applying a load. Read-only service-power previews, inspection,
and paused refreshes never consume space. Changing volume updates waste cache
metadata without rebuilding road-distance fields. Budget funding scales
throughput and reach, while physical volume remains on the original facility.

MCP facility inspection exposes `landfill_remaining` and `landfill_last_fill`.
Summary metrics and CLI CSV reports add `landfill_remaining`,
`landfill_fill_rate`, and `waste_stored_incinerated`. Total incineration includes
stored waste; mass conservation therefore compares current generation plus
stored waste removed against recycling, total burning, landfilling, and overflow.

## Lifetime (M46)

A city makes 0.25 units of waste per resident per tick, and a site takes at most
400 per tick, so one site serves about 1,600 residents at full intake. At
2,000,000 units a site at full intake lasts about 5,000 ticks. In the M36
journey the first three sites open near tick 5,500 and the first fills at
tick 10,400.

Garbage coverage in the budget Services tab, the budget coverage list and MCP
`garbage_coverage` is the share of waste recycled, burned or deposited, capped
at road reach. Full sites lower it. MCP `garbage_reach` keeps road reach, which
still decides how much waste is collectible.

## Local validation — October 18, 2026

Windows MSVC warnings-as-errors Debug and Release CLI builds passed. All 598
C++ tests and seven Python unit tests passed. New C++ coverage checks finite
volume, overflow penalties, stock burning and mass balance, short-run parity,
funding, cache reuse, preview purity, one mutation per tick, legacy saves,
invalid-volume load atomicity, and alert debounce/recovery. Two 64×64, 400-tick
Release CLI runs with seed 7 produced byte-identical CSV reports.

All 35 rendered playtests passed. Rendered acceptance exercises an almost-full landfill, native F7 and budget
panels, facility inspection, save/load, and recovery after incinerator placement.
Local evidence is retained under `build/m39-acceptance/`.
The budget-line layout was corrected after screenshot review; the final
landfill scenario is also checked at 800×640 under `build/m39-final-acceptance/`.
Three 500×500, 1,000-tick Release benchmark trials reported a median of
5.665 ms/tick (range 5.617–6.079). This is a performance smoke measurement,
without a before/after comparison.
Hosted CI, sanitizer jobs, and distribution packaging have not been run for M39.
