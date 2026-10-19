# Fire, police and sanitation capacity (M50)

Player-city departments allocate service to occupied homes and workplaces. A
fully funded Fire station serves 2,000 occupants, Police 2,500 and Sanitation
3,000. Facility quality and funding multiply these limits. Visitors and empty
building capacity add no demand. The autonomous default simulation retains its
previous rules.

Allocation uses each site's existing road reach. Building/site pairs are ordered
by road distance, then building ID, then site position. Each pair receives the
smaller of remaining building demand and remaining site capacity. Overlapping
sites can fill the rest of a partially served building without counting its
occupants twice. Disconnected sites contribute no usable capacity. Spare capacity
does not extend reach. Site distance fields are rebuilt with topology/facility
changes; occupant assignments refresh when population changes.

A building's served fraction scales fire coverage and local fire response,
police coverage and local crime protection, and sanitation coverage and local
illness protection. Growth's civic score uses the reduced fire/police coverage
and a sanitation multiplier from 0.5 to 1. Health and education keep their own
capacity rules. Empty sites consume no capacity. A covered city of 399 residents
and 300 workers with one of each department has exactly the prior coverage,
response, neighborhood rates and growth score in the regression test.

The Services tab shows coverage followed by served/unserved occupant counts.
The tile inspector shows the three local assignments. MCP `metrics` and `budget`
include `department_capacity`, keyed by `fire`, `police`, `sanitation`, with
`capacity`, `demand`, `reachable`, `served`, `unserved`. Diagnostics and entity
rows expose `department_served` and `department_unserved` arrays in that order.
Facility inspection exposes `occupant_capacity`. Coverage percentages remain
building weighted; occupant counters measure people, so they need not match.

These values are derived, with no new save fields. Legacy sessions load through
the shared playable-state refresh; CLI `--simulate-session` and the visualizer
use the same allocation. Funding edits refresh paused cities. No vehicles,
dispatch or response-time simulation is introduced.

## Retained journey experiment

`playtest/examples/phase14_capacity.py` loads the retained Phase 13 journey at
10,324 residents and 17,684 occupants. Its five Fire stations have 10,000 total
capacity: 10,382 occupants are within reach, 6,560 are served and 11,124 unserved.
Normal placement of 13 Fire, 10 Police and 17 Sanitation sites restores all
17,684 occupants in each department. The greedy placement follows the largest
remaining shortage; these counts are not a claim of optimal placement.

The experiment freezes that recovered city and a copy retaining half of each
department's sites, with equal starting funds and the same population, roads,
utilities, policies and buildings. Over 2,000 ticks, the full city reaches
14,432 residents (growth 4,108), versus 13,815 (growth 3,491): 15.0% less growth
with half the sites. Mean population is 13,101.9 versus 12,312.8. Reach and
capacity both matter in this comparison. Other city systems continue operating;
this is a controlled department experiment, not the revised M54 economy journey.

Evidence is under
`playtest-artifacts/m50/journey/20261019-111109-87974d2c/`, including full/half
checkpoints, per-tick CSVs, the capacity report and minimum-window Services PNG.

Reproduce from the repository root with the playtest Python environment:

```sh
PYTHONPATH=playtest .venv/bin/python playtest/examples/phase14_capacity.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --source "$PWD/playtest-artifacts/phase13-playtest/journey/20261019-084536-79ce0cd7/urban_sim_session.json" \
  --artifacts playtest-artifacts/m50/journey
```

The script asserts a fire shortage, complete immediate recovery and at least
10% less population growth in the half-station continuation. Its half fixture
removes facilities without refunds so the comparison starts with equal cash.

## Local verification — October 19, 2026

Strict Clang builds passed and all 649 C++ tests and seven Python tests passed.
The full rendered run passed 41 of 42 cases. Its only failure was the older
height-cap fixture's missing sanitation; with sanitation and sufficient setup
funds added, that case passed separately. The new capacity case passed again
with a minimum-size inspector capture and exact six-tick GUI/CLI session parity.
Services and inspector captures were visually checked. Default 400-tick CSV is
byte-identical to M46. Hosted CI, sanitizers and packaging were not repeated.
