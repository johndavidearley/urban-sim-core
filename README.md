# UrbanSimCore

A high-performance simulation engine for modeling living cities. Designed to simulate population dynamics, urban growth, economics, traffic, and services at scale.

Current implementation and validation details are maintained in
[docs/STATUS.md](docs/STATUS.md). The detailed [roadmap](docs/ROADMAP.md) is a
milestone history and idea backlog rather than the active status source.

Power facilities can be generic coverage utilities or typed generators with
capacity and lifecycle emissions:

```bash
./build/bin/UrbanSimCore-cli --size 32 --add-power-source SOLAR 10 10 12 25 --run-service-evaluation --print-service-summary
```

## Quick Start

### Build

The default build includes the visualizer. Install the
[graphics prerequisites](#live-visualizer-glfw--vulkan) first, or configure with
`-DURBAN_SIM_BUILD_VISUALIZER=OFF` to build only the CLI and CPU tests.

```bash
git clone --recursive <repo-url>
# or, if already cloned without --recursive:
git submodule update --init --recursive

mkdir -p build && cd build
cmake ..
cmake --build . --config Release
```

The googletest submodule is required to build the test suite; without it, test
targets are skipped. nlohmann/json is required for snapshot persistence and is
fetched automatically at configure time if not installed locally.

### Run Headless Simulation

```bash
./build/bin/UrbanSimCore-cli --help
```

### Live Visualizer (GLFW + Vulkan)

The default build adds `UrbanSimCore-visualizer`. It uses an installed GLFW
3.4+ package or fetches GLFW 3.4. Vulkan headers, the loader, and `glslc`
(Vulkan SDK or shaderc) are required to build it; shaders are embedded, so
players do not need a shader compiler. Use
`-DURBAN_SIM_BUILD_VISUALIZER=OFF` for a graphics-independent headless build.

Install prerequisites:

- **Windows:** install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
  and a GPU driver with Vulkan support. Open a new terminal after SDK installation.
- **macOS:** install the Vulkan SDK or run
  `brew install glfw vulkan-headers vulkan-loader molten-vk shaderc`.
  For a Homebrew development build, set
  `VK_DRIVER_FILES="$(brew --prefix molten-vk)/etc/vulkan/icd.d/MoltenVK_icd.json"`.
  MoltenVK runs Vulkan over Metal.
- **Ubuntu/Debian:** install `libvulkan-dev glslc mesa-vulkan-drivers xorg-dev
  libwayland-dev libxkbcommon-dev wayland-protocols`. GLFW builds with X11 and
  Wayland support; use a recent packaged GLFW (3.4+) or the automatic fetch.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/bin/UrbanSimCore-visualizer
cmake --install build --config Release --prefix ./install
```

Windows executables are in `build/bin/Release` with multi-configuration
builds. Windows/Linux use the system Vulkan loader and GPU drivers. macOS
installation bundles the loader, MoltenVK, GLFW when shared, and the driver
manifest beside the executable; keep the installed `bin` and `lib` directories
together. No SDK is required to run that installed macOS build.

The renderer batches existing city and HUD geometry in painter's order.
Logical window coordinates drive both layout and picking, including high-DPI
screens. Resize, minimize/restore, and focus loss are handled by the host.

See [MCP desktop playtesting](docs/PLAYTESTING.md) to control the rendered game,
inspect state, capture PNG screenshots, and record/replay isolated scenarios.

See [graphics validation and troubleshooting](docs/GRAPHICS.md) for smoke
captures, validation layers, runtime requirements, and the platform checklist.

Controls:
- On launch, choose a 32, 64, or 96 tile map, toggle procedural terrain,
  start a new city, or load the existing session
- `F1`: show or hide the new-city guide
- `F2`: toggle large or compact UI text (large is the default)
- `F3`: toggle the isometric and top-down map views
- `F4`: toggle detailed panels and the compact clean-gameplay HUD
- `F`: center the isometric camera on the developed area
- Click the bottom tool palette to select Roads, Zoning, Bulldoze, Services,
  or Districts; clicking an active Zoning, Service, or District button cycles
  its subtype
- Click Play/Pause or `1X`/`2X`/`3X` in the HUD to control simulation time
- Click Save/Load in the HUD, or press `F5`/`F9`, to persist the playable session
- `R`: toggle the road construction tool
- `Z`: activate the zoning tool; press repeatedly to cycle Residential,
  Commercial, Industrial, Office, and Park
- `B`: toggle the bulldozer tool
- `S`: activate the service tool; press repeatedly to cycle Fire, Police,
  Health, Education, Power, Water, Sanitation, Garbage, Recycling, Cemetery,
  and Crematorium
- `P`: activate the district tool; press repeatedly to cycle General,
  Industrial, and Tech Hub archetypes. Left-drag paints a policy rectangle
  (free). Click an existing district to select it; ARCH cycles its archetype
  and STARVE/FUND toggles a service-budget cap that slows G-mode growth.
  Dragging the same multi-tile bounds again removes that district;
  right-click a district to delete it. Industrial bans housing/office growth
  under G-mode; Tech Hub bans industry. The selected district is highlighted;
  the tile inspector shows the district name.
- `V` or **V PORTS**: cycle Cargo, Passenger, Remove, On/Off, and Capacity.
  Click a valid 2x1 shore site to build, or an existing port to manage it.
  Cargo costs $15,000 + $20/tick; passenger terminals $10,000 + $10/tick;
  whole-port demolition costs $500. Disabled ports have no upkeep or flow.
  The first cargo port enables fulfilled-trade rules and a 100-goods road-gateway
  limit; removing it retains those rules. Passenger service remains inactive.
  The port panel shows access, capacity, costs and the last completed cargo flow.
- Left-drag: preview and build an x-then-y road while the road tool is active
- Right-click: cancel the current road drag
- Arrow keys: pan viewport
- Middle-button drag: pan the map
- Isometric middle-button dragging pans smoothly at pixel precision
- Mouse wheel: zoom while keeping the tile beneath the cursor anchored
- `+` / `-`: zoom in/out
- `1`: zone overlay
- `2`: land value overlay
- `3`: pollution overlay
- `4`: service coverage overlay
- `5`: traffic congestion overlay
- `6`: demand overlay
- `7`: happiness overlay
- `8`: route heatmap overlay
- `9`: transit routes, stops, and network ridership/capacity
- Click buttons `1`–`9` in the legend to select the matching overlay directly
- `O`: cycle route heatmap origin filter (residential anchors)
- `D`: cycle route heatmap destination filter (job anchors)
- `C`: clear route heatmap filters
- `Space`: pause/resume live deterministic ticks
- `.` or `N`: single simulation tick while paused
- `H`: show/hide in-window legend panel
- `Esc`: cancel the active tool/drag, or quit when no tool is active

The playable-builder road tool starts with a $50,000 construction fund and
charges $100 for each new road segment. Green previews are buildable; red
previews cross blocked terrain/buildings or exceed available funds. Existing
segments included in a dragged route are not charged again.

Zoning costs $25 per changed tile and uses rectangular drag selection.
Park zoning costs $100 per changed tile and $2 per tile per playable tick.
Cycle `Z` past Office to select Park. Parks need empty land, cannot cover civic
facilities, and never grow buildings. Nearby zoned land gains up to 30 land
value and the happiness overlay gains up to 0.08, tapering to zero at four
Manhattan tiles. Nearest-park benefits do not stack. Parks have paths, benches,
and trees in both projections, and existing snapshot zoning persists them.
Bulldozing or rezoning a park removes its upkeep and neighborhood benefit. It
cannot cover water or roads, and occupied tiles cannot be changed to another
zone type. Reapplying the same zone is free and does not consume funds.

The bulldozer uses rectangular drag selection and removes buildings, zoning,
and every road segment touching the selection. Demolition costs $200 per
building, $20 per road segment, and $5 per zoned tile. Orange previews are
valid; empty or unaffordable selections appear red.

Service buildings use single-click placement and require an empty, non-road
tile adjacent to the road network. Fire costs $5,000, Police $4,500, Health
$6,500, Education $5,500, Power $12,000, Water $8,000, and Sanitation $7,000.
The placement preview and map marker use a distinct color for each service,
and coverage refreshes immediately.

Garbage facilities cost $9,000 and provide 400 units of collection capacity;
Recycling facilities cost $11,000 and divert up to 35% of generated waste with
200 units of processing capacity. Uncollected waste raises pollution on zoned
land and lowers the Happiness overlay. The HUD reports the current waste
collection percentage, while both facility types add operating costs and can
be removed with the bulldozer.

Population mortality accumulates deterministically from a baseline rate plus
illness and pollution risk. Deaths reduce population and create a deceased
backlog. Cemeteries cost $10,000 and process 50 per tick; crematoriums cost
$14,000 and process 100 per tick, subject to road coverage. An unprocessed
backlog reduces happiness and raises health pressure. Mortality remainder,
population target, and backlog persist in saved sessions.

New buildings require both road-reachable power and water coverage. Sanitation
adds sewage-service coverage and operating cost but is not a construction
gate; uncovered sewage raises illness, which hospitals still mitigate. The
tile inspector reports current Power and Water connectivity, and the
new-city guide walks through placing both utilities before a civic service.

The in-window HUD remains visible independently of the debug legend and shows
simulation state, tick, population, building count, construction funds,
illness, crime, waste, deathcare, and the selected tool. Keyboard shortcuts
remain available alongside the clickable palette.

Four live demand bars in the HUD show residential, commercial, industrial,
and office demand using the same colors as their zoning tools. The speed
buttons run ticks at approximately 700 ms, 350 ms, or 120 ms intervals.

Invalid placement reasons are displayed beside the cursor while previewing a
tool. Every attempted road, zone, demolition, or service action also produces
a short-lived in-window success or error notification, including the amount
spent for successful actions.

Hovering over the map outlines the inspected tile and opens an information
panel with coordinates, terrain, zone, land value, pollution, road congestion,
building type and occupancy, and civic-service type and range. Inspection is
suppressed while the pointer is over HUD panels or the tool palette.

Clickable controls brighten on hover, while selected tools, overlays, map
sizes, terrain mode, and simulation speed retain their stronger active-state
highlight. This feedback applies to both the start screen and in-game UI.

Compact labels in the built-in bitmap font render at twice their original size
by default for readability on Retina and other high-density displays. `F2`
restores the legacy compact size when more panel space is preferred.

The normal map view uses deterministic procedural detail at every zoom level:
terrain variation, moving water bands, connected road surfaces and lane
markings, dimensional building facades with capacity-driven height, windows,
shadows, and distinct civic facility markers. Analytical overlays remain flat
and unobstructed so their data stays readable.
Unzoned land also gains sparse procedural trees and shoreline transitions,
roads sit within visible sidewalk tiles, and zoom is constrained to keep the
map filling the window instead of exposing an empty edge.

The renderer now also has a tested isometric projection boundary with forward
tile projection and inverse mouse picking. This is the foundation for moving
the live terrain, depth-sorted buildings, and construction tools into an
optional isometric view without coupling projection math to simulation state.
The live isometric view centers the city vertically and adds wider road
surfaces, scaled building height, vegetation, shoreline accents, and shaded
map edges while retaining `F3` as a top-down fallback. Buildings use projected
wall and roof faces, directional facade shading, sloped window rows, grounded
shadows, pitched residential roofs, and height-aligned rooftop details. Seeded
facade variation distinguishes neighboring buildings, and water has animated
ripples. Isometric streets use parcel-bound road surfaces, single or double
lane markings, and junction crosswalks following the actual road network.
Softer terrain grids, seeded ground variation, edge-following sandy shorelines,
and shaded tree canopies give undeveloped land more depth. Diamond surfaces
use two triangles regardless of zoom level. Civic facilities have distinct
isometric landmarks: fire-station bays, a police flag mast, a hospital cross,
a school tower, a raised water reservoir, utility stacks and treatment tanks,
waste bins, and cemetery headstones. Buildings also gain entrance paths and
projected doors, while shops have awnings following their facade perspective.

Hovering over tool-palette, overlay, playback, speed, save, and load buttons
also displays a descriptive tooltip. Tooltips explain interactions, current
subtypes, construction costs, simulation timing, and overlay meaning.

Quitting routes through an in-window confirmation dialog with Save & Quit,
Quit Without Saving, and Cancel choices. Active tools are still cancelled by
the first Escape press; a subsequent Escape opens the dialog. Simulation time
stops while confirmation is open, and closing the OS window uses the same flow.

The default save slot is `urban_sim_session.json`, with the validated core city
snapshot stored beside it as `urban_sim_session.json.city.json`. A session
restores the map, roads, buildings, population, civic facilities, transit
routes, funds, tick, pause state, simulation speed, G-mode extent, and current
demand values.

The launch screen can also be controlled with Enter/`N` for a new city, `L`
to load, `T` to toggle terrain, and Escape to quit. Loading inspects the saved
city dimensions before constructing the simulation map.

New cities now begin paused and genuinely empty rather than loading a seeded
demonstration layout. The in-window guide walks through constructing the first
road, zoning land, starting simulation time, and placing a civic service.
Loaded sessions bypass the guide. Start with several connected blocks of homes
and workplaces, then power and water. Expand the tax base before buying more
civic services: a tiny settlement cannot cover full utility upkeep, and a
redundant utility still incurs operating costs. Watch the HUD's IN and OUT
figures before adding facilities. Low-demand growth only retires vacant
buildings; occupied homes and workplaces are not automatically demolished.

The construction fund is also the live city treasury. Each simulation tick
applies one percent of the economy system's tax/export revenue and
maintenance/import expenses to match the visualizer's short tick cadence.
Playable property taxes use occupied space valued at $200 per unit, and income
tax uses $200 of earnings per employed resident, adjusted by income band.
Maintenance scales with building capacity relative to its type's base building,
so empty towers cost money rather than generating capacity-based tax windfalls.
The headless simulator retains its existing default fiscal valuation.
The HUD shows the latest scaled income, expenses, and net treasury change;
session saves keep those figures plus outstanding municipal debt.

Each placed service also has a recurring per-tick operating cost: Fire $25,
Police $22, Health $35, and Education $30. These costs join the HUD's `OUT`
figure without the economy scaling step because they are already expressed in
playable tick units. The HUD warns below $5,000. An unpaid deficit becomes
municipal debt (`DEBT $n`) instead of evaporating at $0; surplus cash repays
principal and leftover debt accrues a small per-tick interest. Toasts announce
taking on debt, paying it off, and treasury recovery.

Bulldozer selections include civic facilities as first-class demolition
targets. Removing one costs $100, clears its map marker and coverage, and
immediately removes its recurring upkeep from the projected `OUT` figure.

### Run Tests

```bash
cd build
ctest --verbose
```

### Strict and Sanitizer Builds

With CMake 3.21 or newer, presets provide the shortest path to each maintained
configuration:

```bash
cmake --preset strict && cmake --build --preset strict && ctest --preset strict
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

The normal build remains unchanged. The equivalent explicit strict-build
commands are:

```bash
cmake -S . -B build-strict -DURBAN_SIM_WARNINGS_AS_ERRORS=ON
cmake --build build-strict -j
ctest --test-dir build-strict --output-on-failure
```

Clang and GCC builds can also enable AddressSanitizer plus UBSan, or
ThreadSanitizer. Use separate directories because TSan cannot be combined with
ASan/UBSan:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DURBAN_SIM_ENABLE_ASAN_UBSAN=ON
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DURBAN_SIM_ENABLE_TSAN=ON
```

On macOS, AppleClang's ASan runtime may not support LeakSanitizer; run tests
with `ASAN_OPTIONS=detect_leaks=0` there. Address and undefined-behavior checks
remain enabled.

GitHub Actions runs strict RelWithDebInfo builds on Linux, macOS, and Windows,
plus Linux Debug ASan/UBSan with leak detection and Debug ThreadSanitizer, for
every pull request and push to `main`.

### Procedural Terrain

```bash
./build/bin/UrbanSimCore-cli --size 40 --seed 7 --generate-terrain --print-map
./build/bin/UrbanSimCore-cli --size 40 --seed 7 --terrain-water 0.30 --render-map city.ppm
```

`--generate-terrain` stamps water and rough terrain onto the map from a seeded,
smoothed height field — deterministic for a given `--seed`. Water tiles cannot
be zoned or built on, so growth routes around lakes. Tune water coverage with
`--terrain-water FRAC` (0.0–1.0, implies `--generate-terrain`). Generation runs
only on a fresh map; loading a snapshot restores its saved terrain instead.

### Autonomous Demand-Driven Simulation

```bash
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 80
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --generate-terrain --simulate 80 --simulate-report run.csv
./build/bin/UrbanSimCore-cli --size 96 --seed 7 --simulate 50 --simulate-no-traffic
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 80 --simulate-inflation-rate 0.02
./build/bin/UrbanSimCore-cli --size 32 --seed 5 --zone-rect 10 10 15 15 OFFICE --place-road 10 16 15 16 --run-growth 20 --print-budget-summary
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 100
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 100 --simulate-no-transit
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 60 --simulate-district Downtown 20 20 44 44 --simulate-district Suburbs 46 20 63 44
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 60 --simulate-district Factory 20 20 44 44 --simulate-district-archetype Factory INDUSTRIAL --simulate-district Innovation 46 20 63 44 --simulate-district-archetype Innovation TECHHUB
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --simulate 100 --simulate-disasters --simulate-fire-risk 3
```

`--simulate N` grows a city autonomously from a near-empty map for N ticks. Each
tick it derives residential/commercial/industrial demand from the city's own
state (housing, jobs, employment), extends a road grid outward, zones land in
proportion to demand, lets the growth system build, repopulates housing and
jobs, and records a metrics row. The result is an emergent city: residential
demand seeds the first homes, residents create demand for jobs (industrial, then
commercial), and the loop compounds until land runs out.

Residents migrate in gradually rather than instantly filling new housing, at a
rate set by the city's desirability — jobs being plentiful, traffic not too
congested, pollution low, and public services well-covered. Industry emits
pollution, and zoning steers housing onto the cleanest land and industry onto
the dirtiest, so districts self-segregate over time. The city also builds
fire/police/health/education facilities as it grows (spread across the
developed area); their coverage feeds desirability, and pollution and
under-coverage both feed back to slow migration.

Land value (Phase 5, M12) is recomputed every tick from a zone base value plus
three location factors: distance to the nearest job (commercial/industrial
building, via a road-network BFS), distance to the nearest covered service
facility, and local pollution — replacing what used to be a value fixed once
at zoning time and never updated. It drives the reported average land value
and, through it, the economy's health score. The job-access BFS is the
costliest phase in the tick breakdown at city scale; throttle it with
`--simulate-land-value-interval N` (like the existing
`--simulate-service-interval`/`--simulate-traffic-interval` knobs) on large
simulations — land value simply persists at its last computed value between
recomputes.

The economy also models a supply chain (Phase 5, M12): industrial occupancy
produces goods, commercial occupancy consumes them, and the city trades the
net difference with the outside world — a surplus earns export revenue, a
shortfall costs import expense at a deliberately higher rate (importing is a
real economic penalty, not a wash). A city with commerce but no local
industry visibly pays for its imports in the budget summary; a heavily
industrial city with modest commerce earns export revenue instead. This
requires no extra setup — it's computed automatically from existing building
occupancy data every time the economy is calculated.

Inflation (Phase 5, M12) is opt-in via `--simulate-inflation-rate RATE` (default
0 = disabled, leaving every existing run bit-for-bit unaffected). It's a
compounding per-tick price-level index applied to maintenance costs and trade
prices — both are external, world-market costs — but deliberately not to tax
revenue, which is a percentage of the city's own building stock and only grows
when the city actually builds more. That asymmetry is the point: a city that
stops growing sees its costs climb while its revenue holds flat, the same
budget pressure a real municipality that stagnates faces. The current
multiplier is reported per tick and in the final budget summary.

Office demand (Phase 5, M12) adds a fourth RCI-like category alongside
residential/commercial/industrial: `BuildingType::Office` and `ZoneType::Office`
(zoned with `--zone-rect ... OFFICE`, symbol `O`). Office space follows an
established commercial base rather than leading it - an empty or purely
residential city generates no office demand - and once it ramps in,
autonomous zoning assigns it to the cleanest available land (it commands the
highest base land value of any zone). Office jobs are a genuine third
employment track: high-income residents skew toward office-and-commercial
work while low-income residents skew industrial, and offices pay their own
(highest) tax rate but sit outside the industrial/commercial goods-trade
model entirely - white-collar work neither produces nor consumes physical
goods in this simulation. This is the one M12 mechanic that isn't
behavior-neutral by default: because it's a genuine new zoning category
competing for land, it changes the city's growth mix (not just reported
metrics) for every `--simulate` run, the same way adding an RCI category
naturally would.

Public transit (Phase 5, M13 — complete) auto-places bus routes and, once a
city is large enough, rail lines, as it grows: each route is a fixed path of
stops along the road network (static infrastructure, like a service facility,
not a literal moving vehicle — `TrafficMicroSim` already covers agent-level
simulation for cars) with a per-tick rider capacity. If a route's stop
coverage reaches both the home and work ends of a commute, some of those
commuters ride transit instead of driving, taking that load off the road
network — modal split in action. Capacity constrains ridership (a small
route visibly caps out below demand, reported separately), and routes are on
by default (`--simulate-no-transit` to compare against the pre-transit
baseline). Because commute pairs are sampled uniformly at random each tick
(the same simplification `TrafficSystem` has always used, not something
transit changes), ridership shows up reliably over the course of a run
rather than on any single tick.

Rail (`TransitMode::Rail`) is a second transit mode sharing bus's exact
coverage/offload machinery — only its placement and parameters differ. Bus
routes connect to the *nearest* job (short local hops, denser, modest
capacity); rail connects to the *farthest* reachable job (long trunk lines
spanning the city, sparser, far higher capacity, and a wider walk-to-station
radius). Rail only starts appearing once a city is substantially larger
(roughly one line per 4,000 residents, capped at 3), and its reach noticeably
lifts modal share once it does — commonly 10-20% in a ~7,000-population test
city, versus under 1% from buses alone in the same city.

Districts and policy (Phase 5, M14 — complete) let `--simulate-district NAME
X1 Y1 X2 Y2` (repeatable) define administrative boundaries whose service
budget genuinely shapes growth, not just an after-the-fact report. Each
district's revenue funds a service budget (capped or uncapped, via the same
`DistrictSystem` the standalone `--create-district`/`--set-district-*`
commands already exposed); how well that budget is met, plus how sparse the
district still is, feeds a per-tile build-chance multiplier for its bounds
on the next tick — a district starved of service budget visibly grows slower
than an equally-sized, equally-central one that isn't. A "District Summary"
table prints after the run. This is opt-in only: no `--simulate-district`
flags means no districts, and the simulation behaves exactly as before.

Districts can also carry a zoning ordinance — a list of zone types
autonomous growth isn't allowed to assign within their bounds (manual
`--zone-rect` ignores it; that's a sandbox tool, not a government).
`--simulate-district-archetype NAME ARCHETYPE` applies a named preset:
`INDUSTRIAL` bans Residential/Office and prioritizes fire safety; `TECHHUB`
bans Industrial and prioritizes education; `GENERAL` (the default) applies
no restriction. If demand's preferred type is banned for a tile, growth
falls back to the first still-allowed type rather than leaving the land
stranded — a district that happens to cover the city's growth origin and
bans the type startup demand wants most still develops (just as something
else), instead of deadlocking the whole city.

Disasters (Phase 5, M15 — started) add fire as a per-tick stochastic hazard,
opt-in via `--simulate-disasters` (destructive, so - unlike every other M12-
M14 mechanic - it's off by default; every existing `--simulate` call is
completely unaffected unless you ask for it). Buildings can ignite each tick,
weighted by type (industrial is far more fire-prone) and local pollution; an
ignited building is destroyed immediately and its tile keeps burning for a
few ticks, posing a spread risk to adjacent occupied tiles. Fire station
coverage (the same `ServiceSystem` number that already feeds desirability)
cuts ignition chance, spread chance, and burn duration alike — a proxy for
faster emergency response. `--simulate-fire-risk F` scales the base ignition
chance for tuning; cumulative losses are reported per tick and in the final
summary.

It prints an evolution table (RCI demand, population, building counts, roads,
budget) plus a per-phase timing breakdown, so the same run doubles as an
end-to-end performance profile. Combine with `--generate-terrain` to grow around
water; add `--simulate-report FILE` for a per-tick CSV (including pollution and
land value), or `--simulate-no-traffic` to skip the (dominant) commute phase.

### Traffic Micro-Simulation (Phase 5, M11 — complete)

```bash
./build/bin/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30
./build/bin/UrbanSimCore-cli --size 64 --seed 7 --micro-traffic 40 --micro-traffic-steps 400
./build/bin/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-incidents 5
./build/bin/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-lanes 4
./build/bin/UrbanSimCore-cli --size 48 --seed 7 --micro-traffic 30 --micro-traffic-following-gap 0.25
```

`--micro-traffic N` grows a city for N ticks, then runs an individual
vehicle-agent traffic model on it: one vehicle per commute batch, each routed
home→job over the road graph and stepped along its path. Each road has
`--micro-traffic-lanes` lanes (default 2), and each lane is tracked as its own
independent single-file channel — a vehicle picks the least-loaded lane when
entering a road and switches lanes mid-edge to overtake a congested one, so
congestion is *emergent* per lane rather than a static batch load or an
aggregate edge-wide count. Within a lane, vehicles car-follow: a follower's
speed each step is capped so it keeps at least `--micro-traffic-following-gap`
(default 0.15, a fraction of an edge) behind whichever vehicle is immediately
ahead of it in the same lane, so a slow or stopped leader visibly backs up
traffic behind it instead of every vehicle in the lane sharing one progress
value. Road junctions with 3+ connections are signalized (alternating green by
axis, offset by coordinates for a rough green wave), and vehicles queue on red
rather than passing through. It reports agent-level stats (vehicles
spawned/arrived, mean trip length in steps, peak/average edge occupancy,
signalized junctions, mean signal wait). Tune the step budget with
`--micro-traffic-steps`. This runs alongside the aggregate `TrafficSystem`
(used elsewhere), which is unchanged.

Add `--micro-traffic-incidents N` to also dispatch N emergency vehicles, each
routed from the nearest of a few demo facility sites to a random building (the
"incident"). Emergency vehicles ignore both congestion slowing and red
signals and get a speed boost, so their response time is a direct, deterministic
comparison against ordinary commute trip times (e.g. ~12 steps for an
emergency dispatch versus ~34 for an average commute on the same city).

There is no literal 2D position within a lane (a vehicle's in-lane placement
is a single float along the edge, not an x/y offset), and lane width/vehicle
size are not simulated — the following gap is a fraction of edge length, not
a physical distance. That level of physical detail is out of scope at this
milestone's fidelity.

### Phase 5 Benchmarking

```bash
./build/bin/UrbanSimCore-cli --size 96 --benchmark-phase5 24
./build/bin/UrbanSimCore-cli --size 96 --benchmark-phase5 24 --benchmark-phase5-focus TRAFFIC
./build/bin/UrbanSimCore-cli --size 96 --benchmark-phase5 24 --benchmark-phase5-focus SERVICE
```

Use `--benchmark-phase5-focus` with one of `ALL`, `GROWTH`, `POPULATION`, `TRAFFIC`, `ECONOMY`, or `SERVICE` to isolate timing for a single subsystem while still executing the full simulation pipeline.

### Traffic Route Diagnostics

```bash
./build/bin/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5
./build/bin/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-origin 10 10
./build/bin/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-destination 15 10
./build/bin/UrbanSimCore-cli --size 32 --seed-population 500 --run-commute-simulation --print-top-edges 5 --traffic-origin 10 10 --traffic-destination 15 10
```

Use `--traffic-origin X Y` and/or `--traffic-destination X Y` with `--print-top-edges N` to inspect congestion hotspots for specific commute route subsets.

### District Management

```bash
./build/bin/UrbanSimCore-cli --size 64 --seed 42 --create-district Downtown 5 5 25 25 --create-district Industrial 35 35 55 55 --list-districts
./build/bin/UrbanSimCore-cli --size 64 --seed 42 --run-growth 10 --seed-population 2000 --create-district Downtown 5 5 25 25 --print-district-summary 1
```

Create districts with `--create-district NAME X1 Y1 X2 Y2` (rectangular bounds). List all districts with `--list-districts`. Print district metrics with `--print-district-summary DIST_ID`.

## Project Structure

- **src/core/** — EntityId, seeded RNG, ThreadPool, tile scale (`SimulationTime` exists but is unused by ticks)
- **src/world/** — CityMap, Tile, Zoning, TerrainGenerator (a tile is the parcel)
- **src/entities/** — EntityStore (buildings), PopulationStore (aggregate groups)
- **src/networks/** — RoadNetwork (lazy graph), Pathfinding (A*)
- **src/systems/** — Static `*System` classes, `CitySimulator`, `PlayableCityTick`, `CitySimSupport`
- **src/gameplay/** — Road/Zone/Bulldoze/Service tools and TreasurySystem
- **src/metrics/** — CityMetrics, GrowthMetrics
- **src/persistence/** — City snapshot, gameplay session, replay checksum
- **src/cli/** — Flag CLI (not part of `urban_sim_core`)
- **src/visualization/** — Headless PPM/iso in the library; GLFW/Vulkan visualizer sources in the optional exe
- **tests/** — GoogleTest suites
- **docs/** — Architecture, status, roadmap, historical logs

## Current Status

Phase 1–5 milestone work is complete. See [docs/STATUS.md](docs/STATUS.md)
for the live implementation, test, and priority baseline, and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for system boundaries.

## Technology Stack

- **Language:** C++17
- **Build:** CMake 3.16+
- **Testing:** Google Test
- **Serialization:** nlohmann/json
- **Optional:** GLFW/Vulkan live visualizer (`UrbanSimCore-visualizer`)

## Vision

> Simulate 10,000 citizens using aggregate population groups, grid-based land parcels, graph-based road connectivity, and monthly city budget updates.

From there, expand into traffic micro-simulation, utilities, services, and policies—without losing performance or clarity.

## Documentation

- [STATUS.md](docs/STATUS.md) — Current implementation, validation, priorities
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — Implemented engine boundaries and data flow
- [ROADMAP.md](docs/ROADMAP.md) — Milestone history and future ideas
- [MVP_SPEC.md](docs/MVP_SPEC.md) — Original product scope
- [IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) — Historical implementation journal
- [NEXT_STEPS.md](docs/NEXT_STEPS.md) — Historical backlog sequence

### Freight and arterial roads

Goods now add batched freight to commuter congestion. CLI
`--simulate-no-freight` provides a comparison run. The trade gate is at
`(0, map height/2)`; disconnected cargo appears as unrouted goods in the HUD,
CSV and MCP inspection. While the road tool is active, **A** switches local
(capacity 100) and arterial (400) roads. Drag to build or upgrade: $100 per new
local segment, $400 per new arterial, $300 per local segment upgraded.
Road classes survive save/load. See [MCP playtesting](docs/PLAYTESTING.md) for
freight metrics and road inspection.

## License

MIT

Road capacities express aggregate trips/trucks per traffic pass. Commutes use
occupied homes and workplaces with batches of at most 32 workers, reducing
sampling artifacts in dense cities. The route diagnostics use each road's
actual class capacity. Transit takes riders out of car loads; congestion
continues to represent demand divided by capacity on used edges.

Congestion-aware commute routing assigns relative free-flow costs of 1.25
per local-road edge and 1.0 per arterial edge, encouraging modest arterial
detours. Road-class changes invalidate cached commute paths immediately;
reapplying the same class preserves the cache. Distance-only paths used for
coverage, freight, and other spatial checks retain unit edge lengths.

Transit overlay shows bus routes in cyan and rail routes in gold, with white
stop markers in either projection. Consecutive stops follow shortest connected
road paths. The totals report network ridership, eligible demand, and capacity;
they do not represent per-line passenger counts or a separate rail network.


### M22: disasters and saved cities on both hosts

In the visualizer, press F4 for the detailed HUD and click **DISASTERS OFF**
to enable fires, earthquakes, and floods. Events are off for new cities.
Burning lots show flame markers and cannot rebuild until the blaze expires.
The HUD reports active fires and cumulative fire/other disaster building losses.
Disabling events extinguishes active fires and preserves loss totals. Building
loss can displace residents; this model does not add direct disaster fatalities.
The toggle, burning tiles, loss totals, and simulation seed survive save/load.
Older saves load with events disabled and no fire history.

Autonomous `--simulate` now tracks cash, municipal debt, fractional interest,
and repayments using `TreasurySystem`. Each tick charges operating and growth
construction costs; unpaid expenses become debt, cash repays principal, and
remaining debt accrues 0.1% interest per tick. Its table prints CASH and DEBT;
CSV adds treasury flow columns. Existing economic valuation is retained.

Continue a playable save headlessly under the same rules and saved seed:

```sh
./build/bin/UrbanSimCore-cli --simulate 10 \
  --simulate-session /path/to/urban_sim_session.json \
  --simulate-session-out /path/to/continued.json \
  --simulate-report /path/to/continued.csv
```

The input is unchanged unless explicitly chosen as the output. This finite
session mode uses playable rules (including saved G-mode and disasters),
rather than the autonomous simulator's valuation and construction policy.
`--simulate-disasters` can additionally enable events for that continuation.
Use the default session settings when comparing the visualizer and CLI.


### M23: city budget controls

Press **F6**, or click **F6 BUDGET** in the detailed HUD, to inspect the city
budget. Change residential, commercial, industrial, office, and income taxes
with the minus/plus buttons (0–20%, in whole percentage points). **Reset
default taxes** restores 5/8/6/9/10. Tax edits refresh the estimate immediately
without advancing time, spending cash, or changing the last completed ledger.
Taxes also affect construction demand and household migration; see M24 below.

The panel separates the **next tick estimate** from the **last completed
tick**. The estimate itemizes taxes, exports, building/park upkeep, imports,
and service operation, excluding new building growth and debt flows. The last
tick records actual income, expenses, building growth costs, borrowing,
repayment, interest, and before/after cash and debt. Road, zone, and service
purchases are paid immediately, outside that simulation-tick ledger.
Displayed category amounts reconcile to totals after currency rounding.

F6 or Escape closes the panel; Space toggles pause, N or period steps a paused
city, and F5 saves while the panel is open. Policy and ledger history persist
in playable saves and `--simulate-session` continuation. Older saves retain
the default rates and begin recording ledger history on their next tick.
The Services tab adds per-service funding controls (described below).


In **F6 → Services**, adjust any of the 11 departments from 0–100% in
25-point steps. All start at 100%. The table shows funding, operating cost
per tick, and road-reachable building coverage. **Restore full funding**
restores services without changing taxes; the Taxes tab's reset preserves
service funding.

Cuts reduce each facility's road-hop reach, along with power/water supply and
waste/deathcare processing quality and capacity. Coverage depends on the road
network and building positions, so it need not fall in proportion to funding.
At 0% a department stops operating and incurs no operating expense; its
facilities remain installed and can resume when funding is restored. The
original facility ratings are preserved, so repeated cuts/restoration never
compound. Construction prices and park upkeep are unchanged.

Paused funding edits refresh coverage, utility connectivity, land value and
the budget estimate without advancing time, charging cash, processing waste
or deaths, or changing the last-tick ledger. Consequences for growth,
migration, schooling, waste and mortality occur through normal subsequent
ticks. Utility capacity and reach are separate constraints; the coverage
column shows road reach, while utility supply/shortage is reported in the HUD.
Funding persists with the city and is honored by both simulation hosts.


### M24: tax effects on growth and migration

The Taxes tab shows R/C/I/O attraction relative to default rates (100%).
Each percentage point above a zone's default tax reduces its demand multiplier
by 4%; lower taxes increase it. Residential attraction uses the average of
residential and income tax deviations. Multipliers stay within 40–140%, and
adjusted demand stays within 0–100%. A tax cut cannot create demand when there
is no underlying need for that zone.

Household attraction also scales arrivals. Above-default household taxes add
at most 0.3% of current population in departure pressure per simulation tick;
normal jobs, housing, services and utilities still govern migration. These
are relative attraction factors, not promised population growth percentages.
Policy edits while paused change the preview only. Responses begin on the
next tick, and default taxes preserve prior behavior. Existing saved tax
policies now have these effects in both GUI and headless simulations.


### M25: population and budget trends

Open **F6 → Trends** for the latest 240 completed ticks: population, net
population change, revenue, expenses, cash and debt. Each chart labels its own
range and latest value; scales differ between charts. Net population change
includes deaths, with last-tick arrivals, departures and deaths below the charts.
Revenue and expenses are actual tick billings, separate from the Taxes tab's
next-tick estimate. Cash includes purchases made before that tick's sample.

Gold lines mark the first completed tick that used a changed tax or service
funding policy, compared with the preceding recorded tick. The first retained
sample is the baseline. Several paused edits before a tick produce one applied
policy, and changing back before stepping produces no marker. History starts
empty for new cities and older saves, persists through save/load and headless
playable continuation, and drops the oldest sample after 240 ticks. Paused
policy edits and tool actions do not create samples. Charts show observed
results; policy markers alone do not establish causation.


### M26: city alerts and diagnostics

Press **F7**, click **F7 ALERTS** in the detailed HUD, or choose the budget's
**Alerts** tab. Five diagnostics cover power, water, unemployment, essential
service gaps and budget deficits. Each row shows its last completed-tick
measurement and a suggested check; **View** opens the service overlay, demand
overlay or Taxes budget tab. Viewing an alert clears the active placement tool.

An alert activates after three consecutive bad ticks and clears after two
recovered ticks. Power/water use the lower of road reach and supply ratio
(bad below 95%, recovered at 99%); they are disabled when utilities are not
required or there are no buildings. Unemployment activates above 20% and
recovers at 15%. Service gaps use the lowest fire, police, health or education
coverage (bad below 50%, recovered at 65%). Deficit means actual tick expenses
exceed revenue. Threshold deadbands prevent flicker. Watching means a problem
has not yet reached the three-tick activation threshold.

Repeat notices wait at least 60 simulation ticks per diagnostic, including
recurrences. Active state, measurements, counters and cooldowns survive saves
and headless playable continuation. New and legacy saves start unchecked.
Paused edits do not run checks, change past measurements or emit notices;
load does not reissue a toast. The panel labels its last checked tick, and
only completed ticks update it. Advice identifies useful checks rather than
claiming a single proven cause.


### Phase 8 / M27: building and neighborhood diagnostics

Hover a map tile for an expanded inspector. It shows local growth blockers,
measured zone demand, civic road reach and road congestion, plus vacant housing
or unfilled job slots. Advice identifies a useful next check. A clear local
site can still wait for citywide balance and seeded construction/redevelopment
chance. Existing buildings follow redevelopment rules; utility/road requirements
for new construction are not falsely presented as redevelopment gates.

The nearby summary covers radius 4 (up to 9×9 tiles, clipped at map edges).
It totals residents/housing and filled/available job slots; crime and illness
are resident-weighted. City joblessness is labeled citywide: nearby job slots
cannot establish which nearby residents are employed. Service reach is based
on funded facilities and road anchors, distinct from delivered utility supply.
Demand is the last evaluated growth demand; paused tool refreshes may update
connectivity and services without advancing that demand or simulation time.

MCP `inspect(section="diagnostics", x=..., y=...)` exposes all blocker codes,
checks, measured local factors, service mask and neighborhood coverage by
service type. These read-only diagnostics add no save fields. The roadmap
tracks M28 transport management, M29 UI polish and M30 stabilization.


### M28: transport management

Press **F8** or click **F8 TRANSIT** in the detailed HUD. Browse routes with
Prev/Next, inspect stop endpoints, road-linked stops and reach, then adjust a
bus fleet from 0–20 vehicles. Capacity is vehicles × capacity per vehicle;
zero suspends carrying riders but keeps the route and stops. Each assigned bus
vehicle costs $2 per completed tick, including buses on uncovered or idle
routes. This cost is part of the budget's **Civic and transit** expense category.
Existing saved bus fleets acquire this operating cost when continued. Rail is
inspect-only, with its existing cost behavior unchanged.

Configured capacity/cost are next-tick policy; the usage section is the last
completed tick's immutable report, including ridership, assigned demand,
utilization and unmet assigned demand. City commuters outside all route reach
are separate from riders turned away by capacity. Overlapping routes retain
the existing route-order assignment; per-route demand is attributed to the
route chosen for a batch, or the first covering route when all are full. It
is not double-counted, and increasing one route's capacity can change which
route receives later demand. This does not add transfers or change matching.

**View coverage** opens the transit overlay near the selected route's first
stop. **View bottleneck** opens congestion near its busiest covered road edge;
this is current road congestion in the coverage area, not proof that transit
capacity alone caused it. Fleet edits do not advance time, charge cash or
rewrite usage/treasury history. Fleet policy and completed-tick usage survive
save/load and headless playable continuation. New and legacy saves without a
usage report start unmeasured. F8/Escape closes; Space pauses, N/period steps,
and F5 saves while the panel is open.


### M29: management navigation and inspection

In Budget (F6/F7) and Transport (F8), use **Tab/Down** to advance focus,
**Up** to go back and **Enter** to activate. A white outline marks focus;
unavailable controls are skipped. **F2** switches readable/compact labels
inside either panel. Transport also supports **Left/Right** route browsing.

In Trends, hover or click a plot to inspect a retained tick across all six
charts. **Left/Right** steps samples, **Home** selects the oldest and **End**
follows the latest. Readouts show that sample's values, migration and deaths.
Policy details compare it with the previous retained sample: tax changes list
old/new values; funding shows the first changed department plus a count of
additional changes. The oldest row is a baseline when no earlier sample remains.

On the map, **I** pins the hovered tile's inspector; **I/Escape** releases it.
Alert View actions center and pin a relevant building when available: missing
power/water, missing civic reach, or unfilled job capacity. These are measured
selection hints, not historical causal claims. Budget alerts open Taxes.
Inspection and navigation do not advance the simulation or change saved policy.


M30 stabilization adds retained growth, utility stress/recovery, legacy-save and
exact continuation scenarios in `playtest/examples/phase8_stabilization.py`.
New snapshots preserve the entity allocator counter so continuing in the GUI
and headless CLI assigns identical new IDs, including after removed entities.
Older snapshots remain supported. See `docs/PLAYTESTING.md` for reproduction.
