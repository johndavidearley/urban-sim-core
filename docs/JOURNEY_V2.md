# Journey v2 and integrated balance (M54)

Accepted locally November 8, 2026. Evidence:
`playtest-artifacts/m54-v2/20261108-095702-1d15586e/`.

| Measure | V2 result | Acceptance |
| --- | ---: | --- |
| First 10k checkpoint | 10,052 residents at tick 15,300 | At least 10,000 |
| Built uniques | 4 | At least 2 |
| Debt over all 15,400 completed ticks | $0 | $0 |
| Revenue / expenses at 10k | 1.440 | 1.2–1.6 |
| Previous 100-tick revenue / expenses | 1.390 | 1.2–1.6 |
| Funds at tick 12,000 | $2,227,631 | At most $3,480,000 |
| Original funds at tick 12,000 | $17,447,667 | Retained comparison |
| Opening revenue/expense difference | 0% over 2,400 ticks | At most 5% |
| Native reload and CLI continuation | Exact over 100 ticks | Exact |

Strict AppleClang Release, all 669 CTest cases, seven Python unit tests and all
45 rendered cases pass. The default autonomous 400-tick CSV is identical to the
M52 reference. Hosted CI has not been rerun for this milestone. The operating
ratios describe the first 10k checkpoint and its preceding 100 ticks.

`playtest/examples/phase14_native_journey.py` starts a fresh 96×96 flat city on
the same map and seeds as the retained Phase 13 journey. Construction, taxes and
time advancement use native keyboard and pointer events. The original
`phase10_native_journey.py` is unchanged. The new runner follows a fixed itinerary
with explicit population and cash reserves; it does not search for budget policy,
grant unlocks, import a starting city or enable autonomous growth.

The opening is retained through tick 2,400. Local streets and commercial zones
replace the old premature arterial/office expansion. Recycling, cemetery and
parks wait for 500 residents; the first office neighborhood waits for 1,200.
Incineration and cremation wait for 2,500. Treasury Hall and Transit Exchange
are bought after their achievements and cash reserves are earned. Further
serviced neighborhoods provide housing, jobs and capacity; City Academy and
Civic Square are later purchases. The fixed tax schedule starts at
10/12/12/12/12, changes to 5/8/6/9/5 at the office stage, and to 4/8/6/9/4
after the final serviced neighborhood. The last cut follows a measured run
that reached 10,166 residents, with a 1.578 average operating ratio but a 1.618
checkpoint ratio, just above the 1.6 ceiling. Both readings must pass.

Initial probes found a starting-city regression from M50: the sanitation growth
multiplier penalized the original school/hospital opening before its first
sanitation purchase. Growth now retains its previous sanitation factor below
500 resident occupants. Department coverage, illness, capacity, upkeep, catalog
prices and milestone gates retain their rules. The 499/500 regression checks
the boundary, and the fresh opening is compared with the historical budget.
Other probes corrected a crematorium site on the connecting street and added
cash reserves for office zoning, whose price is $1,500 per tile. These are
itinerary corrections, with no credit or milestone grants.

Run from the repository root after building the strict Release targets:

```sh
PYTHONPATH=playtest .venv/bin/python playtest/examples/phase14_native_journey.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --artifacts playtest-artifacts/m54-v2 \
  --baseline playtest-artifacts/m51-journey/20261105-161754-21c13cef/measurements.json
```

On macOS with the temporary Vulkan SDK, also set `VK_ICD_FILENAMES` to its
`macOS/share/vulkan/icd.d/MoltenVK_icd.json`. The baseline must first be retained
using the M51 reproduction procedure. It is evidence input, never imported into
the v2 city.

Each run records `decisions.json`, native scenario inputs, stable milestone
checkpoints, samples and every completed tick in `money.csv`. The balance report
exports the historical curve as `baseline-money.csv`, compares common ticks,
and fails unless the city reaches 10,000 residents with at least two built
uniques, never borrows, holds at most $3,480,000 at tick 12,000, and earns
1.2–1.6 times expenses both at the first 10k checkpoint and over the 100 ticks
ending there.
The unchanged 2,400-tick opening must preserve revenue and expenses within 5%.
The separate boundary test covers starting growth through 499 residents.

At the final checkpoint the runner verifies 100-tick native save/load and CLI
continuations, comparing treasury and semantic core city contents. Process-local
entity IDs are resolved to building positions before comparison. `result.json`
is written only after continuation and balance acceptance pass.

To regenerate a report independently:

```sh
.venv/bin/python tools/journey_balance_report.py RUN_DIRECTORY \
  --baseline playtest-artifacts/m51-journey/20261105-161754-21c13cef/measurements.json
```
