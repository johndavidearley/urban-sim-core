# Monuments (M53)

M, or Monuments beside Uniques, opens the four-monument catalog. Use Up/Down/Tab
or click a row, then Enter to select placement. Click empty unzoned land with
road access, power and water. The catalog and MCP list each monument's required
unique buildings, cost, upkeep and effect. Achievements alone do not qualify:
all three required unique buildings must currently exist when purchasing.

| Monument / MCP key | Required built uniques | Cost | Upkeep/tick |
|---|---|---:|---:|
| Medical Citadel / `medical_citadel` | Treasury Hall, City Academy, Civic Square | $6,000,000 | $120 |
| Universal Campus / `universal_campus` | Transit Exchange, City Academy, Civic Square | $6,500,000 | $140 |
| Clean Energy Spire / `clean_energy_spire` | Treasury Hall, Trade Hall, Civic Square | $7,000,000 | $180 |
| Ecology Dome / `ecology_dome` | Trade Hall, Visitor Pavilion, Civic Square | $6,000,000 | $100 |

One of each can exist. A constructed monument keeps its effect while its own
site has road access. Its prerequisite buildings are a construction gate;
removing one later does not disable the monument or make the city unsavable.
Bulldozing a monument removes its upkeep and effect. Rebuilding checks the
currently built prerequisites again. Upkeep continues without road access and
is included in BUILDING UPKEEP, with normal maintenance inflation. Department
funding sliders do not scale these fixed monument costs or effects.

## Citywide effects

- **Medical Citadel:** health coverage is 100%, including roadless homes.
  Hospital care is not limited by ordinary bed counts. The existing illness
  formula uses full health mitigation; this is health coverage, not zero illness.
  Reported beds are sufficient for current patient demand. Crime, sanitation,
  deathcare and fire/police capacity retain their separate rules.
- **Universal Campus:** education coverage and effective education coverage are
  100%, with sufficient seats for current school demand. The next completed tick
  records full education coverage for the following tick's labor allocation,
  retaining the existing one-tick education lag.
- **Clean Energy Spire:** supplies 2,000 MW with zero emissions from the spire.
  It has 30 road hops of power reach. Substations can extend reach; it does not
  grant power to disconnected buildings. Other operating plants keep their own
  emissions. Its derived power source is recreated from the monument, never
  persisted as a second civic facility or billed twice.
- **Ecology Dome:** after all pollution emitters, including waste, each completed
  tick leaves 25% of the map's pollution. It also adds 25 land-value points
  citywide, subject to the usual land-value bounds. Cleanup updates health and
  land-value calculations for that tick. Inspection, paused projections and load
  do not repeatedly clean saved pollution.

Health/education percentages, capacity readouts and inspector masks include the
global effects. Power readouts include the spire. Monuments have no population,
jobs, office counts or ordinary property tax. They use deliberate civic entities
and the existing site protection, demolition and upkeep paths.

## Persistence and MCP

Core snapshots optionally store `Building.monumentKind`, 1–4. A monument owns
one unzoned land tile, has zero capacity/occupancy, and cannot also be a hotel or
unique building. Validation rejects malformed kinds, duplicates and invalid or
overlapping footprints. Gameplay sessions retain M52 achievement progress and
require the monument's prerequisite achievements to have been earned, even when
the prerequisite sites have subsequently been demolished. Old saves omit the
specialization and retain their previous behavior.

`budget.monuments` exposes prerequisites with built status, eligibility, built
and active status, cost, upkeep and effect. `budget(action="monuments")` opens
the native catalog; entity inspection adds `monument_kind`. Native and MCP use
the same revalidated plan, current funds, prerequisite and duplicate checks:

```json
{"action":"apply","name":"monument","monument":"medical_citadel","x1":48,"y1":51}
```

## Pricing and acceptance

The median surplus of the 100 retained continuation ticks nearest 10,000
residents is $1,850, covering populations 9,899–10,100. Prices therefore cost
3,243–3,784 ticks of reference surplus, above the 2,000-tick minimum. These are
fixed prices, not prices derived from a player's current cash. M54 still owns
the new journey's construction choices and integrated balance.

November 6, 2026: strict AppleClang Release build, all 668 CTest cases (including
eight monument tests) and seven Python unit tests passed. The targeted rendered case
checks all four effects, native M catalog/placement, built-prerequisite refusal,
duplicates, read-only pollution and exact save continuation. Minimum 800×640
logical-window catalog and Services captures were visually inspected.
All 45 rendered cases passed; full-suite evidence is under
`playtest-artifacts/m53-full/` and `build/m53-rendered-full.log`.

Native journey acceptance loads M52's ordinary 10k city with $25,215,174 and no
debt. It buys the missing City Academy for $650,000 and the Medical Citadel for
$6,000,000 using native keyboard/pointer input. Health coverage rises from
79.05% to 100%; illness falls from 57.53% to 14.65%. Native save/load and six-tick
GUI/CLI sessions and core cities agree exactly. The city stays debt-free. The
default 400-tick autonomous CSV remains byte-identical to the Phase 13 baseline.
Hosted CI, sanitizers and release packaging were not repeated.

Evidence: `playtest-artifacts/m53/price-calibration.json` and
`playtest-artifacts/m53/journey-native/20261106-123440-5e8133fc/monument-report.json`.

## Reproduction

Use the graphics environment in [PLAYTESTING.md](PLAYTESTING.md) and the ordinary
two-unique native save retained by [UNIQUE_BUILDINGS.md](UNIQUE_BUILDINGS.md):

```sh
PYTHONPATH=playtest .venv/bin/python playtest/examples/phase14_monuments.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --source playtest-artifacts/m52/journey-native/RUN/urban_sim_session.json
.venv/bin/python tools/monument_price_calibration.py \
  playtest-artifacts/m52/journey/ticks.csv
```

Replace `RUN` with the retained M52 run. The native runner requires earned
prerequisites and records purchases, effect measurements and continuation parity.
The calibration script verifies every catalog price against the 2,000-tick floor.
