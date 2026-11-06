# Unique buildings (M52)

E, or the Uniques button beside Connections, opens the six-building catalog.
Up/Down/Tab or a row click selects an entry; Enter selects its placement tool.
Click empty, unzoned land next to a road with power and water. E returns to the
catalog; Escape cancels. Locked and already-built entries explain their refusal
before spending. Each city can have one of each. Bulldozing removes the upkeep
and benefit; the earned unlock remains and the building can be rebuilt.

| Building / MCP key | Permanent requirement | Construction | Upkeep/tick | Maximum land value bonus / radius |
|---|---|---:|---:|---:|
| Treasury Hall / `treasury_hall` | Hold $500,000 | $300,000 | $12 | +20 / 6 tiles |
| Transit Exchange / `transit_exchange` | Carry 50,000 resident bus riders | $450,000 | $24 | +30 / 7 tiles |
| Trade Hall / `trade_hall` | Export 1,000,000 goods | $500,000 | $20 | +25 / 6 tiles |
| Visitor Pavilion / `visitor_pavilion` | Admit 10,000 visitors | $400,000 | $24 | +30 / 7 tiles |
| City Academy / `academy` | Educate 2,500 residents | $650,000 | $22 | +30 / 6 tiles |
| Civic Square / `civic_square` | Reach 5,000 residents | $400,000 | $30 | +40 / 8 tiles |

Funds, population and educated residents retain their highest measured value.
Educated residents means population multiplied by effective education coverage,
rounded down. This is the existing school-capacity metric, not a new graduation
simulation. Bus rides, exports and visitor admissions accumulate on completed
ticks only. Bus rides count delivered resident bus ridership, excluding rail;
exports count delivered goods when finite logistics is active; admissions count
day plus overnight arrivals, excluding rejected demand and continuing stays.
Progress stops at the requirement and never decreases. Refreshing a panel or
loading a save does not add activity.

The local bonus tapers linearly with Manhattan distance, reaching zero at the
listed radius. The strongest nearby unique benefit applies; parks still add
their existing benefit. A unique needs road access and power/water connectivity
to provide its benefit. Upkeep continues during an outage and follows ordinary
maintenance inflation. Uniques have no residents, jobs, ordinary property tax or
ordinary office upkeep. They do not count as offices or dilute service coverage.
The budget includes their advertised upkeep in BUILDING UPKEEP. Existing road,
zoning and service placement rules protect their occupied tile.

## Persistence and MCP

Sessions save `uniqueProgress`, six nonnegative counters in catalog order. Saves
without it start with current funds/population and saved education coverage;
they do not invent past rides, exports or visitors. Core snapshots use optional
`Building.uniqueKind` (1–6). The deliberate entity owns one unzoned tile with no
capacity or occupancy. Validation rejects unknown kinds, duplicate kinds, invalid
footprints and malformed progress; a gameplay session must have earned its built
uniques. Ordinary buildings omit the new specialization field.

`budget.unique_buildings` exposes requirement, progress, threshold, unlocked and
built status, cost, upkeep, radius and maximum land-value bonus. Entity inspection
includes `unique_kind`. `budget(action="uniques")` opens the native catalog.
Placement uses the same plan, affordability and duplicate checks as native input:

```json
{"action":"apply","name":"unique","unique":"treasury_hall","x1":47,"y1":46}
```

## Acceptance and pricing

November 5, 2026: strict AppleClang Release build and 660 CTest cases passed,
including all seven unique-building cases. Seven Python unit tests and the native
catalog/placement/save-continuation regression passed. The minimum 800×640 logical
window catalog and placement captures were visually inspected.
All 44 rendered cases passed. The catalog pointer check passed separately after
moving Uniques beside Connections; its final minimum-window capture was inspected.

The historical native journey's 159 construction segments were continued with
current playable rules, carrying achievement progress between segments. Its F9
continuation replay is excluded from new activity. Five buildings are earned by
10,000 residents. The Academy's initial qualification comes from the legacy
checkpoint at tick 6,600; the other four are observed on completed current-rule
ticks. These are historical construction checkpoints, not a new Phase 14 money
curve. M54 retains the revised journey and integrated balance pass.

Prices are fixed against the median surplus of 100 continuation ticks nearest
each first unlock. The optional visitor branch starts at the 5,000-resident
checkpoint, buys a road-accessible park at (0,47), and enables tourism. It admits
10,000 visitors by tick 13,917 without debt.

| Building | First qualification tick | Reference surplus | Price / surplus |
|---|---:|---:|---:|
| Treasury Hall | 3,556 | $662.50 | 452.8 ticks |
| Transit Exchange | 4,157 | $1,082 | 415.9 ticks |
| Trade Hall | 4,658 | $1,249 | 400.3 ticks |
| City Academy | 6,600 | $1,573 | 413.2 ticks |
| Civic Square | 13,413 | $837.50 | 477.6 ticks |
| Visitor Pavilion | 13,917 | $976 | 409.8 ticks |

Native acceptance loads the ordinary 10,324-resident journey save, buys Treasury
Hall and Civic Square with native keyboard/pointer input and normal funds, and
measures nearby land-value increases of 13.24 and 35.00. Native save/load and
six-tick GUI/CLI continuation agree exactly. The default 400-tick autonomous CSV
is byte-identical to the Phase 13 baseline. No simulation changes apply without
built uniques. Hosted CI, sanitizers and release packaging were not repeated.

Evidence is under `playtest-artifacts/m52/`: `journey/report.json`,
`price-calibration.json`, `journey-native/20261105-165408-9eb5dc40/unique-report.json`
and `visitors/20261105-165244-8e9878cf/visitor-report.json`.
The full rendered suite is under `playtest-artifacts/m52-full/`; the final pointer
regression is under `playtest-artifacts/m52-pointer-final/20261105-170002-b2f70989/`.

## Reproduction

Use the Phase 13 native checkpoints retained by the M51 workflow in
[EXPORT_PRICES.md](EXPORT_PRICES.md), and the graphics setup in
[PLAYTESTING.md](PLAYTESTING.md). Substitute the retained run directory for `RUN`:

```sh
cmake --build build-strict --target UrbanSimCore-unique-journey-audit
build-strict/bin/UrbanSimCore-unique-journey-audit \
  playtest-artifacts/m51-journey/RUN/segments.json build/m52-ticks.csv
PYTHONPATH=playtest .venv/bin/python playtest/examples/phase14_uniques.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --source playtest-artifacts/m51-journey/RUN/urban_sim_session.json
PYTHONPATH=playtest .venv/bin/python playtest/examples/phase14_unique_visitors.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --source playtest-artifacts/m51-journey/RUN/segment-0135.json
```

Retain the audit's JSON stdout. `tools/unique_price_calibration.py` takes that
report, its tick CSV and the visitor runner's `visitor-report.json`; it checks
that all six fixed prices cost 300–600 ticks of reference surplus.
