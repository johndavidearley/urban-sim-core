# Population milestones (M49 — complete locally)

Player cities permanently earn a catalog tier when resident population reaches
its threshold. Falling population never removes an earned tier. The shared
playable tick observes population before and after migration, before deaths can
remove a threshold crossed during that tick. Autonomous `--simulate` runs retain
their existing behavior.

| Residents | Newly available |
|-----------|-----------------|
| 0 | Local roads; residential, commercial, industrial; generic, coal and wind power; water; inland sanitation and raw shoreline outlets; landfill; school; hospital; fire; police |
| 500 | Arterials; parks; districts; cemetery; recycling |
| 1,200 | Bus routes; office; gas and solar power; district height caps; municipal bonds |
| 2,500 | Substations; hydro; shoreline treatment; crematorium; incinerator |
| 5,000 | Cargo ports; passenger terminals; hotels |
| 10,000 | Nuclear power; airports |

Bus routes continue to appear through the existing automatic route placement
rules. Existing routes can still be managed in a smaller city.

Selecting a locked tool is allowed so the player can see its requirements. The
HUD shows the locked threshold, placement previews are invalid, and native and
MCP placements refuse before charging money or changing the map. The native
city guide (F1) shows the latest earned tier and the next threshold. A milestone
toast appears when a tick earns a new tier, including manual single steps.
MCP `budget` reports `population_milestone`, `next_population_milestone` (0 after
the last tier), `milestone_unlocks`, and `catalog_milestones`, with each tier's
threshold, unlock status, and catalog description.

Sessions write `populationMilestone` as the highest earned threshold. The loader
rejects other numbers and noninteger values before changing the live city.
Older sessions initialize progression from their current resident population.
Existing buildings, facilities, routes, districts and bonds do not grant unlocks
by themselves. They remain in the city and keep their operations, funding,
demolition and pay-off controls. New construction and height-cap edits still
require the relevant tier. Loading a small legacy city with an airport keeps
that airport without authorizing a second.

Construction prices are fixed by catalog item. Earning another tier, losing
population, changing taxes, running a deficit or loading an old city does not
alter a quote. Existing facilities receive no retroactive construction charge.
Upkeep, capacity, demolition charges, taxes and bond terms are unchanged.

## Construction price calibration

The Phase 13 journey's retained `measurements.json` supplies the reference.
For each threshold, select the 100 unique saved history samples nearest that
population and take the median of revenue minus expenses. This includes ordinary
construction expense rather than pretending it is free. The reference is a
fixed baseline, not a forecast of the Phase 14 economy. Reproduce it with:

```sh
python3 tools/catalog_price_calibration.py path/to/measurements.json
```

| Residents | Sample population range | Median surplus/tick |
|-----------|-------------------------|---------------------|
| Early reference | 200–200 | $11 |
| 500 | 477–522 | $175 |
| 1,200 | 1,185–1,215 | $671 |
| 2,500 | 2,405–2,594 | $1,249 |
| 5,000 | 4,781–5,211 | $2,986 |
| 10,000 | 9,900–10,101 | $2,855 |

The historical surplus reference above motivated the first milestone prices.
The November 18 reserve tuning replaces those prices: long density waits and
large required purchases encouraged banking millions before 10,000 residents.
The new target is $200,000–$500,000 on first reaching 10,000 during the fixed
fresh-city journey, with full service funding, no debt, and positive cash flow
including redevelopment over the following 100 ticks. This is a playthrough
target, not a cash cap; cities can continue saving and existing saves keep cash.

| Tier | Item | Construction cost |
|------|------|-------------------|
| 500 | New arterial segment / upgrade | $400 / $300 |
| 500 | Park tile | $250 |
| 500 | Cemetery / recycling | $16,000 / $20,000 |
| 1,200 | Office tile | $250 |
| 1,200 | Gas / solar | $80,000 / $60,000 |
| 2,500 | Substation | $30,000 |
| 2,500 | Hydro | $150,000 |
| 2,500 | Shore treatment | $120,000 |
| 2,500 | Crematorium / incinerator | $65,000 / $90,000 |
| 5,000 | Hotel / passenger terminal | $250,000 / $300,000 |
| 5,000 | Cargo port | $350,000 |
| 10,000 | Nuclear / airport | $350,000 each |

Playable redevelopment uses a base chance of 0.30 rather than 0.05. Demand,
land value, congestion, civic coverage, height caps, and construction charges
still apply. New-building chance stays 0.18. Autonomous GrowthSystem callers
retain the 0.05 default. Native and CLI session continuation use the same
playable rules. Older saves can develop faster after loading, without treasury
rebasing or compensation for earlier purchases.

Local roads stay $100 per new segment; R/C/I stay $25 per changed tile. Starting
services and power variants retain their construction prices and upkeep. Tax
rates, recurring expenses, districts, bus placement, and bond terms are unchanged.
Budget flows remain per simulation tick; no weekly conversion is implied.

Tests cover exact boundaries, skipped tiers, population decline, malformed and
legacy saves, shared native/MCP refusals, nuclear and airport unlocks, and legacy
airport operation. Older feature acceptance scenarios explicitly grant tiers in
their save fixtures; fresh-city and migration progression checks remain separate.


Historical M49 validation (before the reserve tuning): strict Clang, 643 CTest cases, seven Python unit tests and all
41 rendered acceptance cases (39 in the full run, two corrected fixture
assertions verified in a targeted rerun). Native hotel, airport and treatment
prices were inspected at 800×640. Exact affordability, forged/stale plans and
one-time charging are covered across every repriced service. Two retained
pre-pricing early-city continuations match their saved sessions and city
snapshots exactly; the default 400-tick CSV matches M46 byte-for-byte. Evidence
and calibration data are under `playtest-artifacts/m49-prices/`.

November 19 reserve acceptance reaches 10,004 residents with $395,628; the next
100 ticks add $126,687 after growth spending, with all services fully funded and
no debt. Expansion and tax choices still change the result. The retained
[finance journey](PLAYTESTING.md#ten-thousand-resident-finance-balance) records
its checkpoint ancestry and alternative itineraries.
