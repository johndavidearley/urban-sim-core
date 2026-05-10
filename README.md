# UrbanSimCore

A high-performance simulation engine for modeling living cities. Designed to simulate population dynamics, urban growth, economics, traffic, and services at scale.

## Quick Start

### Build

```bash
mkdir -p build && cd build
cmake ..
cmake --build . --config Release
```

### Run Headless Simulation

```bash
./build/UrbanSimCore-cli --help
```

### Optional Live Visualizer (SDL2)

If SDL2 is installed, CMake adds an extra executable target named `UrbanSimCore-visualizer`.

```bash
cd build
cmake ..
cmake --build . --config Release
./UrbanSimCore-visualizer
```

Controls:
- Arrow keys: pan viewport
- `+` / `-`: zoom in/out
- `1`: zone overlay
- `2`: land value overlay
- `3`: pollution overlay
- `4`: service coverage overlay
- `5`: traffic congestion overlay
- `6`: demand overlay
- `7`: happiness overlay
- `8`: route heatmap overlay
- `Space`: pause/resume live deterministic ticks
- `.` or `N`: single simulation tick while paused
- `H`: show/hide in-window legend panel
- `Esc`: quit

### Run Tests

```bash
cd build
ctest --verbose
```

### Phase 5 Benchmarking

```bash
./build/UrbanSimCore-cli --size 96 --benchmark-phase5 24
./build/UrbanSimCore-cli --size 96 --benchmark-phase5 24 --benchmark-phase5-focus TRAFFIC
./build/UrbanSimCore-cli --size 96 --benchmark-phase5 24 --benchmark-phase5-focus SERVICE
```

Use `--benchmark-phase5-focus` with one of `ALL`, `GROWTH`, `POPULATION`, `TRAFFIC`, `ECONOMY`, or `SERVICE` to isolate timing for a single subsystem while still executing the full simulation pipeline.

## Project Structure

- **src/core/** — Foundation (Time, Random, EntityId, Commands)
- **src/world/** — Map, Tiles, Parcels, Districts
- **src/entities/** — Building, PopulationGroup, Business, Vehicle
- **src/networks/** — RoadNetwork, UtilityNetwork, Pathfinding
- **src/systems/** — PopulationSystem, ZoningSystem, EconomySystem, etc.
- **src/metrics/** — CityMetrics aggregation
- **src/persistence/** — SaveGame, Snapshot, Replay
- **tests/** — Unit and integration tests
- **examples/** — Example simulations and configs
- **configs/** — Configuration files (JSON/YAML)
- **docs/** — Architecture, roadmap, design docs

## Current Status

✅ **Backlog Slices 1-10 Complete** — Core headless simulation, persistence, replay verification, and visualization scaffolding are implemented.

See [IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) for detailed progress.

## Technology Stack

- **Language:** C++17/20
- **Build:** CMake 3.16+
- **Testing:** Google Test
- **Serialization:** nlohmann/json
- **Optional:** SFML or SDL2 for future visualization

## Vision

> Simulate 10,000 citizens using aggregate population groups, grid-based land parcels, graph-based road connectivity, and monthly city budget updates.

From there, expand into traffic micro-simulation, utilities, services, and policies—without losing performance or clarity.

## Documentation

- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — Engine design, data models, systems
- [ROADMAP.md](docs/ROADMAP.md) — Development milestones
- [MVP_SPEC.md](docs/MVP_SPEC.md) — MVP features and simulation loop
- [NEXT_STEPS.md](docs/NEXT_STEPS.md) — What to build next
- [IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) — Progress tracking

## License

MIT
