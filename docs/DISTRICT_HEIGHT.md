# District height caps (M43)

Redevelopment still doubles capacity, up to 64× the default, where land value,
civic coverage, and clear roads allow. A district can stop that ladder earlier.

The cap is a maximum capacity multiple: uncapped, or 1×, 2×, 4×, 8×, 16×, 32×,
or 64×. Uncapped is the default. `1×` is the capacity of a new low building.
Older districts and older saves load uncapped.

`GrowthSystem` will not redevelop a building past the cap of the top-most
district that contains the tile. The chance is unchanged. A building already
above a newly set cap stays until the player bulldozes it. The cap does not
change demand, taxes, zone bans, or new construction. An empty zoned tile
inside a capped district still accepts a low building.

The district panel's HEIGHT button cycles the cap. MCP `district_policy`
accepts `height_cap` with the same values (`0` is uncapped) and rejects any
other value before it changes the district. Inspection reports `height_cap`.
The headless city workflow accepts `--set-district-height-cap`. Sessions save
`heightCap`. A missing field loads as uncapped. A value outside the catalog
rejects the session before the city changes.

Both the playable tick and `CitySimulator` read the cap. An uncapped district,
and a default `--simulate` run with no districts, keeps today's ladder. The
existing capacity-tier drawing is the map check: a capped block stops climbing.

There is no density zone and no automatic demolition of buildings that already
exceed a cap.
