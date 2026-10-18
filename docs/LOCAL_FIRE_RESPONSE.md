# Local fire response (M41)

With disasters enabled, both simulation hosts use existing service distance
fields for fire response. At a building's road anchor, each fire station supplies
`clamp(1 - hops / (radius + 1), 0, 1)` response; the strongest station wins.
Unreachable buildings and buildings beyond every station radius receive zero.
There is no extra BFS, dispatch simulation, or fire graph.

Local response reduces independent ignition chance and incoming spread chance,
using the existing FireParams reductions. Spread uses the receiving building's
response.

## Burning buildings (M47)

Ignition no longer destroys a building. It burns for 4 ticks and stays standing,
and can spread fire to occupied neighbors. Each tick, before it spreads, local
response × 0.8 is the chance that it is put out and the building survives. If
the countdown ends first, the building burns down. A building with full
response is lost about 0.2% of the time once it ignites; one beyond every
station's reach always is. Base ignition is 0.00005 per building per tick, down
from 0.0004, so about 5% of buildings ignite per 1,000 ticks before response.

In the 10,000-resident M36 journey city (241 buildings) with disasters on for
1,000 ticks, M44 lost 274 buildings, counting rebuilt ones that burned again,
and halved the population. Before M41, M38
lost 161. M47 loses 14–23 across three seeds, and population ends at
10,479–11,233. That city's five stations reach 61% of its buildings. Logged
losses show buildings with response of at least 0.25 lose about 2% per 1,000
ticks, and buildings with no response about 16%.

`FireSummary` reports `extinguished`. Saves record it as `fireExtinguished`
(absent in older saves). MCP state reports it as `fires_put_out`. Saves from
before M47 can hold burning tiles whose building is already gone; those simply
clear when their countdown ends.
Funding and road edits use the existing service-cache invalidation rules.

Without a cached fire station, the caller's citywide coverage fraction remains
the fallback. The scalar FireSystem API remains supported. Disasters are off by
default. The citywide building-coverage fraction stays separate from local
response and is shown as COV in the disaster HUD. The tile inspector shows FIRE
response alongside its service reach flags; MCP diagnostics expose
`local_fire_response` in [0, 1]. No new save fields are required.

Validation is recorded in STATUS.md. Tests cover road-distance falloff,
unreachable/out-of-radius buildings, paired seeded losses, incoming spread,
per-tile burn duration, scalar fallback and native inspector/save continuation.
