# Clinics and central hospitals

With Health selected in Services, press **V** to choose a Clinic or Hospital.

| Facility | Construction | Upkeep/tick | Capacity | Road reach |
| --- | --- | --- | --- | --- |
| Neighborhood clinic | $2,500 | $12 | 200 noncritical outpatient visits; **zero beds** | 18 hops |
| Central hospital | $6,500 | $35 | 400 inpatient/overflow places | 48 hops for new builds |

Clinics handle the noncritical share of care demand within their road catchment,
up to funded visit capacity. They cannot absorb the serious share or create
hospital beds. Hospitals handle serious cases and outpatient overflow. The
simulation's care-demand split is 75% noncritical and 25% serious; these are game
balance parameters, not medical estimates. Illness remains a neighborhood
severity index, rather than a count of individual diagnosed patients.

Both facilities use the existing Health funding slider. Reducing funding lowers
capacity, reach and quality. Road disconnection prevents local care. Clinic
visit eligibility uses road reach and visit capacity, rather than consuming a
fraction of a visit merely because the clinic is farther away. Hospital effects
retain their distance-based strength. The HUD shows hospital beds and clinic
visits separately. Clinics have a low teal building; hospitals keep the larger
medical building. The inspector identifies the facility and its capacity.
In the top-down view only healthcare sites use crosses (green for clinics,
red for hospitals); other services use letter markers rather than medical crosses.

Existing saves without `clinic` remain hospitals with their recorded road range;
they are not silently converted. New clinic saves preserve that subtype. Older
builds do not understand clinic semantics; keep an older backup when downgrading.
An optional fresh journey variant is available through
`m55_native_journey.py --neighborhood-clinics`; it explicitly purchases four
clinics and one hospital. Historical M55 evidence retains its original build.

## Windows acceptance, November 14

`playtest/examples/m56_healthcare_check.py` imports the unchanged earned Windows
10k city, pays to demolish/rebuild its five healthcare sites, and runs 100 ticks.
Four sites become clinics and one a wider-reach hospital. No population, funds,
progression or topology fields are edited in a save.

Final evidence: `build-windows/m56-healthcare-release/20261114-093501-b41ca9b3/`.
The conversion costs $17,000, changes healthcare upkeep from $175 to $83/tick,
and finishes with 10,249 residents, 400 hospital beds, 800 available clinic visits,
about 185 clinic visits used and 240 hospital patients. Save/load preserves the
subtypes and capacities; Vulkan reports zero errors. Unit regressions cover
costs/funding, zero clinic beds, serious-case demand, cache invalidation,
disconnection and legacy save handling. This is a paid continuation of an
earned city, not a newly completed fresh journey under the revised care rules.
The subsequent ten-tick native/CLI continuation matches exactly, including
clinic metadata, funds, facilities, buildings and population. Screenshots are
captured after that continuation. The final playtest executable SHA-256 is
`43763a9cabfa98851987d47a4d9b06fe7fddea0fe9734a09e02cf71b2c328e32`.
