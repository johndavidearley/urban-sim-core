# Road construction

Activate **R** or the Roads palette. The toolbar supports both precise snapped
placement and continuous freeform geometry in the top-down and 3D views.

| Control | Behavior |
| --- | --- |
| A / road type button | Local → Arterial → Highway → One-way Ramp |
| L / layout button | Grid Drag → Line → Curve → Roundabout |
| O / snap button | Snap placement points to tile centers, or retain exact positions |
| Page Up / Page Down / level button | Ground, overpass or underpass; also selects the demolition level |
| Right-click / Escape / Cancel | Cancel the unfinished placement |

Grid Drag retains the existing Manhattan road tool. Line takes two clicks.
Curve takes start, control point, and end clicks; the control point bends the
road toward it. Roundabout takes center and radius clicks, with radius at least
two tiles. Snapped roundabouts use whole-tile radii. Selecting Highway or Ramp
automatically selects Line when the current layout is Grid Drag.

Build a roundabout, then attach approaches to its ring. For a highway exit,
select Ramp and Curve and place start on the highway, a control point toward
the exit, then end at the local road. For an entrance, place start at the local
road and end on the highway. Arrows indicate direction. Endpoints within 0.6
world units of an existing continuous road join its geometry and align their
end tangents. Existing grid roads join at their tile centers. Roundabouts run
clockwise. Existing shared approach edges retain their original direction.

| Road type | Cost per new routing segment | Capacity per traffic pass |
| --- | ---: | ---: |
| Local | $100 | 100 |
| Arterial | $1,500 | 400 |
| Highway | $2,400 | 800 |
| Ramp | $900 | 200 |

Local-to-arterial upgrades retain the existing $1,400 per segment price.
Non-local roads use the arterial population unlock. Preview validates the
swept width against water, buildings, terminals, facilities, map bounds and
available cash. Build validates again, so a stale preview cannot bypass these
checks. Successful placement charges only new segments and applicable upgrades.

Continuous curves, snap setting, road class, reserved footprint and direction
persist in the city snapshot. Older snapshots retain their original grid roads
and two-way directions. The bulldozer expands a selection touching a continuous
road to remove that whole alignment and reports the cost for all affected links.

Overpasses and underpasses use Line or Curve. Select a level with Page Up /
Page Down while Roads is active, or click the level button. Both ends are
ground-level approaches. The center rises onto a supported deck or descends
into an open cut with retaining walls. A crossing needs at least 12 sampled
routing segments. Keep crossing roads away from the approach slopes; preview
rejects crossings without clearance. Overpasses cost twice the normal segment
price and underpasses three times the normal price. Water, buildings and
facilities still block the reserved footprint.

Elevated highways now accept entrance and exit ramps on their level deck.
Select **Ramp**, **Curve**, and **Overpass**. For an exit, click the highway deck,
then the control point and a ground endpoint; for an entrance, reverse that
order. The endpoint snaps to the deck and aligns its tangent. This works in
both freeform and snapped modes, including clicking the visible deck in 3D.
The join must be on the level center section of an elevated Highway; approach
slopes, other ramps, and underpasses are not attachment targets.

The highway routing link splits at the join without changing total travel
length, capacity, direction or existing loads. Deck junctions have separate
identities from surface roads beneath them. Ramps remain one-way and connect
one ground endpoint to one deck endpoint; deck-to-deck ramps are not supported.
A short level lead-in keeps each merge flush with the deck before the ramp
changes height. Connections and highway split points persist in saves.

Other roads passing above or below each other do not create junctions.
Routing, coverage distances and micro-traffic travel time use the full road
length. Arbitrary stacked deck junctions remain outside this version.

With Bulldoze active, use Page Up / Page Down or its level button to select which
road level to remove. Demolishing an overpass at a crossing preserves the surface
road; ground-level demolition preserves the overpass/underpass. Buildings,
zones and facilities are removed only in ground mode. Cost follows the full
length of the removed road, rather than counting a long crossing as one segment.
Removing a ramp alone preserves its highway. Removing the highway also removes
its attached ramps and includes their demolition cost, preventing dangling decks.

The simulation still resolves traffic and building access at tile resolution.
A continuous road is sampled into cardinal routing links; A* respects one-way
ramps and the roundabout's circulation. Geometry uses the exact saved Bézier
curves. Surface roads crossing at the same tile connect at ground level; separated
crossings connect at their approaches and explicitly attached ramp junctions. This does not yet provide water
bridges, fully enclosed tunnels, arbitrary stacked interchanges, lane-level
merge/yield behavior, or post-build dragging of control points. To change a curve, demolish and redraw it.

The playtest `tool` command accepts `road_layout` (`grid`, `line`, `curve`,
`roundabout`), `elevation` (-1 underpass, 0 ground, 1 overpass), `road_class` (`local`, `arterial`, `highway`, `ramp`), and `snapped`.
For continuous placement, `start_x/start_y/end_x/end_y` retain fractional world
coordinates; curve requires `control_x/control_y`, roundabout uses `radius`.
The existing `x1/y1/x2/y2` tile arguments remain supported as tile-center defaults.
