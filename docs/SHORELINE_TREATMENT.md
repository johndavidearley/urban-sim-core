# Shoreline treatment (M42)

Sanitation has three construction modes. With the sanitation service selected,
V cycles inland → raw shoreline outlet → shoreline treatment → inland. The
existing water-tool V toggle still selects inland water or shoreline intake.

| Mode | Construction | Upkeep at 100% funding | Raw discharge |
|---|---:|---:|---|
| Inland sanitation | $7,000 | $26/tick | None |
| Raw shoreline outlet | $2,000 | $8/tick | Existing M37 water-body exposure |
| Shore treatment | $9,000 | $34/tick | None |

Treatment uses the existing sanitation radius and shoreline/road placement
checks. Building treatment directly over a raw outlet replaces that outlet for
the full $9,000 construction price, without a refund or duplicate facility.
Other occupied service sites remain unavailable. Placement is revalidated at
purchase time.

The next service pass rebuilds the cache after a mode change. Intake cleanliness
ignores treatment plants, so removing a raw outlet restores the capacity it
previously reduced. Other raw outlets still count. Separate water bodies remain
isolated by the existing 12-hop orthogonal water traversal. Treatment supplies
sanitation coverage; zero funding suspends coverage and upkeep, and never adds
discharge. Partial funding follows the existing sanitation funding rules.

The inspector names Shore Treatment and shows no raw discharge. The Services
budget tab counts raw, treated and inland sites alongside sanitation funding,
cost and coverage. Intake placement previews continue to show current funded
source cleanliness. Treatment previews explain that they add no contamination.
The isometric shoreline pipe uses a teal accent for treatment.

MCP service placement accepts `surface_water=true, sewage_treatment=true` with
`service="sanitation"`. Facility inspection returns `sewage_treatment` and the
variant name. Sessions save optional `sewageTreatment: true`; absent fields load
as false. Invalid treatment/type/shore-mode combinations reject atomically.
Session-based headless continuation reads the same mode and service logic.
Autonomous construction still creates the existing facilities, so a default
`--simulate` run is unchanged.

Validation is recorded in STATUS.md. Native acceptance uses two disconnected
water bodies and checks replacement, cleanliness, cost, funding, minimum-window
UI, exact persisted save continuation and gameplay/headless parity. There are
no currents, pollutant advection, pipes or additional treatment levels.
