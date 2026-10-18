# Local fire response (M41)

With disasters enabled, both simulation hosts use existing service distance
fields for fire response. At a building's road anchor, each fire station supplies
`clamp(1 - hops / (radius + 1), 0, 1)` response; the strongest station wins.
Unreachable buildings and buildings beyond every station radius receive zero.
There is no extra BFS, dispatch simulation, or fire graph.

Local response reduces independent ignition chance, incoming spread chance and
the duration assigned to each newly ignited tile, using the existing FireParams
reductions. Spread uses the receiving building's response. Existing fires keep
their saved countdowns. Buildings are still destroyed immediately on ignition.
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
