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

Early schools and hospitals cost about 500–591 ticks at the $11 reference;
generic power costs about 1,091. Major newly unlocked facilities now cost
roughly 400–750 ticks at their own tier. Substations remain a cheaper reach-only
purchase. Road and zoning unit prices keep roughly the same ticks per segment
or tile as early local roads and R/C/I zoning.

| Tier | Item | Construction cost | Reference ticks of surplus |
|------|------|-------------------|----------------------------|
| 500 | New arterial segment / upgrade | $1,500 / $1,400 | 8.6 / 8.0 |
| 500 | Park tile | $1,500 | 8.6 |
| 500 | Cemetery / recycling | $80,000 / $100,000 | 457 / 571 |
| 1,200 | Office tile | $1,500 | 2.2 |
| 1,200 | Gas / solar | $400,000 / $300,000 | 596 / 447 |
| 2,500 | Substation | $150,000 | 120 |
| 2,500 | Hydro | $750,000 | 600 |
| 2,500 | Shore treatment | $600,000 | 480 |
| 2,500 | Crematorium / incinerator | $650,000 / $900,000 | 520 / 721 |
| 5,000 | Hotel / passenger terminal | $1,500,000 each | 502 |
| 5,000 | Cargo port | $1,800,000 | 603 |
| 10,000 | Nuclear / airport | $1,800,000 each | 630 |

Local roads stay $100 per new segment; R/C/I stay $25 per changed tile. Every
starting service and power variant retains its original construction price and
upkeep. Districts and height-cap edits remain free; bus placement and municipal
bond terms are unchanged. A default autonomous `--simulate` run is unchanged.
Final integrated balance tuning and the revised player journey belong to M54.

Tests cover exact boundaries, skipped tiers, population decline, malformed and
legacy saves, shared native/MCP refusals, nuclear and airport unlocks, and legacy
airport operation. Older feature acceptance scenarios explicitly grant tiers in
their save fixtures; fresh-city and migration progression checks remain separate.


Local validation: strict Clang, 643 CTest cases, seven Python unit tests and all
41 rendered acceptance cases (39 in the full run, two corrected fixture
assertions verified in a targeted rerun). Native hotel, airport and treatment
prices were inspected at 800×640. Exact affordability, forged/stale plans and
one-time charging are covered across every repriced service. Two retained
pre-pricing early-city continuations match their saved sessions and city
snapshots exactly; the default 400-tick CSV matches M46 byte-for-byte. Evidence
and calibration data are under `playtest-artifacts/m49-prices/`.
