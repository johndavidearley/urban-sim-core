# UrbanSimCore Architecture

## Design Philosophy

The engine is built on these core principles:

1. **Simulation-first:** The engine runs headless. Visualization and UI are completely decoupled.
2. **Deterministic:** Fixed-tick updates with seeded RNG enable replays and testing.
3. **Modular systems:** Each system (Population, Traffic, Economy) is independent and testable.
4. **Aggregate-first, agents-later:** Start with group-level simulation; add agents for visible entities.
5. **Inspectable:** Debug overlays and metrics are part of the core design, not afterthoughts.

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────┐
│           City Simulation Engine                          │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  ┌─────────────┐                                         │
│  │ Time System │ – Ticks, speed, scheduled events       │
│  └─────────────┘                                         │
│                                                           │
│  ┌──────────────┐                                        │
│  │ World Model  │ – Map, tiles, parcels, districts      │
│  └──────────────┘                                        │
│                                                           │
│  ┌──────────────┐                                        │
│  │ Entity Store │ – Buildings, citizens, vehicles       │
│  └──────────────┘                                        │
│                                                           │
│  ┌─────────────────────────────────────────────┐         │
│  │         Simulation Systems                   │         │
│  ├──────────────┬──────────────┬──────────────┤         │
│  │ Population   │ Zoning       │ Economy      │         │
│  ├──────────────┼──────────────┼──────────────┤         │
│  │ Traffic      │ Utilities    │ Services     │         │
│  └──────────────┴──────────────┴──────────────┘         │
│                                                           │
│  ┌──────────────┐     ┌──────────────┐                  │
│  │ Network Sys  │ --- │  Pathfinding │                  │
│  └──────────────┘     └──────────────┘                  │
│                                                           │
│  ┌──────────────┐     ┌──────────────┐                  │
│  │ Metrics      │     │ Persistence  │                  │
│  └──────────────┘     └──────────────┘                  │
│                                                           │
└─────────────────────────────────────────────────────────┘
         ↕ Commands, State Queries
┌─────────────────────────────────────────────────────────┐
│     Visualization / UI / External Tools                  │
└─────────────────────────────────────────────────────────┘
```

### Key Invariant

> **The engine does not depend on the renderer.**

The engine can run in:
- A headless CLI
- Server/networking context
- Automated tests
- A game engine (Unity, Godot, custom)
- Browser (via WASM, future)

---

## Core Components

### 1. Time System

Manages simulation ticks and updates.

```cpp
class SimulationTime {
public:
  uint64_t tickCount;           // Current tick (0-based)
  uint32_t ticksPerDay = 24;    // Ticks per in-game day
  float simulationSpeed = 1.0f; // 1.0 = real-time, 2.0 = 2x speed
  
  float getCurrentTime() const; // Hours (for debug)
  void advance();               // Move to next tick
  bool isDayBoundary() const;
  bool isMonthBoundary() const;
};
```

### 2. Entity System

All city entities (buildings, citizens, vehicles) are stored in a single registry with unique IDs.

```cpp
using EntityId = uint32_t;

class EntityStore {
  std::unordered_map<EntityId, Building> buildings;
  std::unordered_map<EntityId, PopulationGroup> populationGroups;
  std::unordered_map<EntityId, Vehicle> vehicles;
  // ... etc
  
  EntityId createEntity();
  void removeEntity(EntityId id);
};
```

### 3. World Model

The spatial foundation.

```cpp
struct Tile {
  glm::ivec2 position;       // Grid coordinates
  TileType type;             // Terrain, zone, etc.
  ZoneType zone;             // Residential, commercial, industrial
  float landValue;
  float pollution;
  bool hasRoad;
  bool connectedToPower;
  bool connectedToWater;
  EntityId buildingId;       // Optional building on this tile
};

class CityMap {
  std::vector<Tile> tiles;
  glm::ivec2 dimensions;
  
  Tile& getTile(glm::ivec2 coord);
  bool isValid(glm::ivec2 coord) const;
};
```

### 4. Network Systems

Roads, power, water, transit, etc. are modeled as graphs.

```cpp
class RoadNetwork {
  struct Node {
    glm::ivec2 position;
    std::vector<EdgeId> adjacentEdges;
  };
  
  struct Edge {
    NodeId from, to;
    float capacity;
    float currentLoad;
    float congestion;
  };
  
  std::unordered_map<NodeId, Node> nodes;
  std::unordered_map<EdgeId, Edge> edges;
  
  Path findShortestPath(NodeId start, NodeId end);
  float getTrafficCongestion(EdgeId edge) const;
};
```

### 5. Simulation Systems

Each system is independent and updates state based on the simulation time.

```cpp
class SimulationSystem {
  virtual ~SimulationSystem() = default;
  virtual void update(SimulationState& state) = 0;
};

class PopulationSystem : public SimulationSystem {
  void update(SimulationState& state) override;
  // Called every day
};

class EconomySystem : public SimulationSystem {
  void update(SimulationState& state) override;
  // Called every month
};
```

### 6. Commands

Player actions are expressed as immutable commands, enabling replays and testing.

```cpp
class Command {
public:
  virtual ~Command() = default;
  virtual void execute(SimulationState& state) = 0;
  virtual std::string describe() const = 0;
};

class BuildRoadCommand : public Command {
  glm::ivec2 from, to;
  void execute(SimulationState& state) override;
};

class ZoneAreaCommand : public Command {
  Rect area;
  ZoneType zone;
  void execute(SimulationState& state) override;
};
```

### 7. Metrics

Aggregated statistics about the city.

```cpp
struct CityMetrics {
  uint32_t population = 0;
  uint32_t availableHousing = 0;
  uint32_t availableJobs = 0;
  float unemployment = 0.0f;
  float happiness = 0.5f;
  float pollution = 0.0f;
  float landValue = 0.0f;
  Money cityRevenue;
  Money cityExpenses;
  // ... 20+ more metrics
};
```

---

## Data Flow

### Simulation Loop

```cpp
while (simulation.isRunning()) {
  // 1. Process player commands
  for (auto& cmd : commandQueue) {
    cmd->execute(state);
  }
  
  // 2. Update systems based on time
  if (state.time.isDayBoundary()) {
    populationSystem.update(state);
    trafficSystem.update(state);
    serviceSystem.update(state);
  }
  
  if (state.time.isMonthBoundary()) {
    economySystem.update(state);
    zoningSystem.update(state);
  }
  
  // 3. Update metrics
  metricsSystem.calculateMetrics(state);
  
  // 4. Advance time
  state.time.advance();
}
```

### Update Frequency Guide

```
Every tick (high frequency):
  - Road traffic (simple)
  
Every day:
  - Commute simulation
  - Happiness update
  - Service coverage check
  
Every week:
  - Zoning growth evaluation
  - Building upgrades
  
Every month:
  - City budget (taxes, expenses)
  - Population migration
  - Demand recalculation
  - Economy updates
```

---

## Data Model

### Building

```cpp
struct Building {
  EntityId id;
  BuildingType type;              // Residential, Commercial, Industrial, Service
  glm::ivec2 position;
  uint8_t level;                  // 1-5, affects capacity
  uint16_t capacity;              // Max residents/jobs
  uint16_t occupants;             // Current residents or employees
  uint16_t jobsProvided;          // For commercial/industrial
  float landValue;
  float happinessEffect;          // Local modifier
  bool connectedToRoad;
  bool connectedToPower;
  bool connectedToWater;
  uint32_t yearConstructed;
  float maintenanceDebt;
};
```

### PopulationGroup

```cpp
struct PopulationGroup {
  EntityId id;
  EntityId homeDistrictId;
  uint32_t size;                  // Group population
  IncomeLevel incomeLevel;        // Low, Middle, High
  uint32_t employed;
  uint32_t unemployed;
  float happiness;
  float education;
  float commuteCost;              // Travel burden
};
```

### Parcel

```cpp
struct Parcel {
  glm::ivec2 gridCell;
  ZoneType zoning;
  float landValue;
  bool hasRoadAccess;
  bool hasPowerAccess;
  bool hasWaterAccess;
  float pollution;
  float noiseLevel;
  float serviceCoverage;          // Aggregate coverage %
  uint8_t developmentLevel;       // 0-5
  EntityId occupantBuildingId;    // If developed
};
```

### District

```cpp
struct District {
  EntityId id;
  std::string name;
  std::vector<glm::ivec2> parcels;
  float averageLandValue;
  float pollution;
  float desirability;             // Aggregate
};
```

---

## Time Model

### Tick Convention

- **1 tick = 1 in-game hour**
- **24 ticks = 1 in-game day**
- **30 days ≈ 1 in-game month (720 ticks)**
- **360 ticks ≈ 1 in-game year**

This is adjustable via configuration.

### Update Scheduler

Systems are registered with the scheduler and called only when needed:

```cpp
class UpdateScheduler {
  void registerSystem(std::shared_ptr<SimulationSystem> system, UpdateFrequency freq);
  void updateSystems(SimulationState& state);
};

enum class UpdateFrequency {
  EveryTick,    // Every tick
  Daily,        // Every day boundary
  Weekly,       // Every 7 days
  Monthly,      // Every 30 days
  Yearly        // Every 360 ticks
};
```

---

## Determinism

All randomness is seeded and deterministic:

```cpp
class DeterministicRandom {
  std::mt19937 generator;
  
  DeterministicRandom(uint32_t seed) 
    : generator(seed) {}
  
  float uniform(float min, float max);
  uint32_t integer(uint32_t min, uint32_t max);
};
```

Consequences:

```
City initialSeed = seed_42
Replay: seed_42 + all player commands = same city state

This enables:
- Deterministic testing
- Save/load validation
- Command replay for debugging
```

---

## Save/Load and Snapshots

All state is serializable to JSON.

```cpp
class SaveGame {
  SimulationState state;
  std::vector<std::shared_ptr<Command>> commandHistory;
  uint32_t randomSeed;
  std::string version;
  
  static SaveGame load(const std::string& filepath);
  void save(const std::string& filepath);
};

class Snapshot {
  // Lightweight copy for rollback / branching
  SimulationState capturedState;
  uint64_t capturedAtTick;
};
```

---

## Testing Strategy

1. **Unit tests:** Individual systems with mock state.
2. **Integration tests:** Full simulation loop with known commands.
3. **Determinism tests:** Same seed + commands = same result.
4. **Performance tests:** Tick throughput with large cities.
5. **Replay tests:** Load save, replay commands, compare metrics.

---

## Performance Considerations

- Use `unordered_map` for O(1) entity lookups.
- Batch pathfinding queries when possible.
- Lazy-evaluate metrics (only when queried or needed).
- Use spatial hashing for local neighborhood queries.
- Profile with `perf` or `VTune` before optimizing.

**Initial target:** 60 FPS headless with a 200×200 map, 10,000 population.

---

## Visualization (Future)

The engine outputs JSON state. A separate visualization layer consumes it:

- **2D Isometric or Top-Down:** Unity, Godot, custom SFML/SDL2
- **Web:** Wasm + WebGL
- **Terminal:** ASCII debug overlay

The important point: visualization is not part of `UrbanSimCore`. It's a separate project that reads the engine's JSON output.

