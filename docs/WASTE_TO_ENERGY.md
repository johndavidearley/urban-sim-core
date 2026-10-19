# Waste-to-energy incinerator

Select **Incinerator** by cycling the service tool with **S** (after Crematorium).
Place it on an empty land tile beside a road. Construction costs $900,000;
full funding costs $60 per tick. F6 → Services controls its department funding.

Recycling runs first. Incinerators then burn reachable residual waste, before
landfills take the remainder. Each fully funded plant processes at most 300
waste units per tick, producing 0.02 MW per unit burned (6 MW maximum).
Unused capacity produces no electricity. Funding reduces processing capacity
and road reach; zero funding closes the facility. A plant without road access
cannot process waste or distribute power.

Power uses the existing road-based utility network and load shedding. The
source reports 650 kg/MWh emissions. Burning waste adds citywide air pollution
(up to 0.03 per tick, scaled by the share of generated waste burned), on top of
the existing penalty for uncollected garbage. This uses the current simplified
waste/pollution model; there is no imported fuel or persistent waste stockpile.
The tile inspector reports citywide waste burned and waste-generated MW.

The facility, funding and budget history persist through save/load. Old saves
with eleven service funding entries load with incinerators at 100% by default.
MCP `service: "incinerator"` places the facility; `set_funding` accepts twelve
entries, appending Incinerator after Crematorium. Eleven-entry legacy requests
preserve the current incinerator funding. Facility inspection exposes
`city_waste_incinerated`, `city_waste_power_mw` and `emissions_kg_per_mwh`.

M49 reprices new incinerator construction; existing plants keep their upkeep and
saved state. See [population milestones](POPULATION_MILESTONES.md).
