# Phase 11: surface water and early waterworks

M37 addresses the missing surface-water sources observed after M36. Existing
flat saves are retained; their terrain is not regenerated beneath a built city.

## M37: lakes, rivers, intakes and untreated outlets

New City defaults to lakes/river terrain. T or the terrain button selects an
explicit flat map instead. Seeded terrain keeps the requested water/rough-land
counts while producing one connected, meandering river and a river-fed lake.
At normal map sizes/default fractions the river connects the top and bottom edges.
Semantic test fixtures still request flat terrain unless they opt in.

Natural new maps default Water/Sanitation tools to shore mode. S cycles service
types; V switches Water/Sanitation between shoreline and legacy inland facilities.
Intakes and raw sewage outlets occupy empty land with orthogonally adjacent water
and road access. Plans revalidate the shoreline before spending money.

| Facility | Build | Upkeep per tick |
|---|---:|---:|
| Shore water intake | $3,000 | $12 |
| Raw sewage outlet | $2,000 | $8 |
| Existing inland water | $8,000 | $28 |
| Existing inland sanitation / treated option | $7,000 | $26 |

A raw outlet provides sanitation coverage but reduces usable intake capacity
nearby on the same water body. Exposure follows up to 12 orthogonal water-tile
hops, falls off with distance and takes the strongest source. Maximum exposure
reduces usable intake capacity by 90%; beyond 12 hops or across separate water
bodies there is no effect. Unfunded/unconnected outlets do not contribute. Intake
previews and inspectors show source cleanliness. Replacing an outlet with the
existing inland sanitation option removes that raw-discharge source.

This is a bounded placement/contamination model. It does not introduce river
currents, advected pollutant mass, hydraulic pipe networks or a treatment unlock
tree. Source cleanliness is separate from the existing industrial land-pollution
field. Legacy facilities retain their inland behavior and costs; the optional
surface-water flag persists with new facilities and invalid shore saves are
rejected before replacing the live city.

## Acceptance

- Connected seeded water bodies, exact terrain counts and usable shorelines on
  32/64/96 maps.
- Native New City default, road construction, V mode selection, paid intake and
  outlet placement, cleanliness feedback, F5/F9 and identical replay coverage.
- Correct operating costs, separate-water-body isolation, funding effects,
  shoreline revalidation and atomic rejection of invalid imported shore sites.
- Legacy saves and the explicit flat-map M36 acceptance remain supported.
- Relevant strict/local checks, hosted cross-platform CI and an installed current
  host candidate with launch/save instructions.

Local strict checks passed all 576 C++ tests, seven Python unit tests and 33
rendered acceptance tests. The rendered suite completed in two retained chunks
(26 passed before a storage interruption; the remaining seven passed after
compression). The shoreline test verifies native default terrain, purchased road,
intake and outlet sites, source cleanliness, mode switching, F5/F9 and two identical
replays. Top-down and minimum-window isometric captures were inspected. A clipped
placement warning exposed by this check was fixed, with geometry coverage for
both text sizes. Evidence is retained under `playtest-artifacts/m37-water/`.

Hosted CI, the current-host package smoke and the full explicit-flat M36 rerun
remain pending at implementation commit time.
