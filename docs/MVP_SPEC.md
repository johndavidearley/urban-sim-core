# UrbanSimCore MVP Specification

## Goal

**Simulate a functional living city with 10,000 citizens.**

Features:
- Grid-based map with roads and zoning
- Residential, commercial, and industrial zones
- Population and jobs
- Basic demand modeling
- Road connectivity
- City budget
- Save/load capability
- Debug overlays and metrics

---

## Simulation Entities

### Building

Represents residential, commercial, industrial, or service buildings.

```
Building
├─ ID (unique)
├─ Type (residential | commercial | industrial | service)
├─ Position (grid coordinate)
├─ Capacity (residents or jobs)
├─ Current occupancy
├─ Land value
├─ Connected to roads: bool
├─ Connected to power: bool (stub, always true for MVP)
├─ Connected to water: bool (stub, always true for MVP)
└─ Happiness modifier
```

### PopulationGroup

Represents 100-500 citizens as an aggregate group. NOT individual agents yet.

```
PopulationGroup
├─ ID (unique)
├─ Home district
├─ Size (100-1000)
├─ Income level (low | middle | high)
├─ Employed count
├─ Unemployed count
├─ Happiness (0-1)
├─ Education level (stub)
└─ Commute cost
```

### Parcel

A tile-like unit of land that can be zoned and developed.

```
Parcel
├─ Grid position
├─ Zone type (residential | commercial | industrial | empty)
├─ Land value
├─ Road access: bool
├─ Development level (0-5)
├─ Occupant building (if any)
└─ Pollution level
```

---

## Simulation Systems

### 1. RoadSystem

**Frequency:** Continuous (every tick or cull to every N ticks)

**Behavior:**
- Maintain road network graph
- Mark parcels as connected/disconnected
- Calculate shortest paths for commute estimation
- Compute congestion on road segments (simple: sum of commuters vs capacity)

**Input:** Player commands (BUILD_ROAD, REMOVE_ROAD)

**Output:** Connectivity, congestion, travel time

### 2. ZoningSystem

**Frequency:** Weekly or daily

**Behavior:**
- Check each empty parcel for development opportunity
- Growth criteria:
  - Parcel is zoned
  - Parcel has road access
  - Demand exists (residential/commercial/industrial)
  - Land value is within acceptable range for building type
  - Random chance passes (weighted by demand and land value)
- Create building entity when criteria met
- Update land value based on nearby buildings and services

**Input:** Player commands (ZONE_AREA, REZONE), demand metrics

**Output:** New buildings, updated land values

### 3. PopulationSystem

**Frequency:** Daily

**Behavior:**
- For each residential building:
  - Capacity available? → Attract population groups
  - Population migration based on:
    - Available housing
    - Jobs available
    - Happiness of existing residents
    - Land value and desirability
    - Taxes
- Distribute population groups to residential buildings
- Distribute workers to jobs
- Calculate unemployment
- Calculate commute burden
- Update happiness based on:
  - Commute time
  - Job availability
  - Service coverage
  - Taxes
  - Land desirability

**Output:** Population distribution, employment, happiness

### 4. TrafficSystem

**Frequency:** Daily (or every tick for simple load calculation)

**Behavior:**
- For each employed population group:
  - Calculate home → work commute via road graph
  - Add to road congestion
- Calculate average commute time:
  - Base time = distance / speed
  - Congestion multiplier = 1.0 + (0.5 * congestionRatio)
- Output traffic metrics

**Output:** Congestion, travel times

### 5. EconomySystem

**Frequency:** Monthly

**Behavior:**
- Calculate city revenue:
  - Residential tax = population × tax_rate
  - Commercial tax = jobs × tax_rate
  - Industrial tax = jobs × tax_rate
- Calculate city expenses:
  - Road maintenance = number_of_roads × cost_per_segment
  - Service operations (police, fire, schools) = budget_per_service
  - Utilities (power, water) = stub for MVP
- Balance: revenue - expenses = profit/deficit
- Track debt and bankruptcy state

**Output:** Budget, taxes, expenses, surplus/deficit

### 6. ServiceSystem (Simplified for MVP)

**Frequency:** Weekly

**Behavior:**
- For each service building (fire, police, school, hospital):
  - Calculate coverage radius over road graph (not Euclidean)
  - Mark parcels as "covered"
- Aggregate coverage: % of buildings with access to each service
- Happiness modifier based on coverage

**Output:** Service coverage metrics

### 7. UtilitiesSystem (Stub for MVP)

**Frequency:** Weekly

**Behavior:**
- For MVP: assume all connected buildings are powered and watered
- Calculate coverage: % of buildings connected
- Later: add demand, capacity, brownouts

**Output:** Utility coverage metrics

### 8. MetricsSystem

**Frequency:** Every frame or on-demand

**Aggregates:**
- Total population
- Total jobs available
- Unemployment rate
- Available housing
- Average happiness
- Average commute time
- City revenue
- City expenses
- Land values (min, max, average)
- Pollution levels
- Crime rate (stub)
- Service coverage (fire, police, schools, health)
- Utility coverage

---

## MVP Simulation Loop

```
Every tick:
  1. Process commands (road building, zoning)
  2. If day boundary:
       - Road: update connectivity and congestion
       - Population: attract/distribute residents
       - Traffic: calculate commutes
       - Services: update coverage
       - Calculate happiness
  3. If month boundary:
       - Economy: calculate budget
       - Zoning: growth evaluation
       - Demand recalculation
  4. Update metrics
  5. Advance time
  6. Check termination conditions
```

---

## Player Commands (MVP)

```cpp
enum class CommandType {
  BUILD_ROAD,
  DESTROY_ROAD,
  ZONE_AREA,
  REZONE_AREA,
  SET_TAX_RATE,
  SET_SERVICE_BUDGET,
  PAUSE_SIMULATION,
  RESUME_SIMULATION,
  SPEED_UP,
  SPEED_DOWN,
  SAVE_GAME,
  LOAD_GAME
};
```

---

## Initial Map and Configuration

**Map size:** 128×128 tiles (customizable)
**Tile type:** Each tile = 1 parcel
**Starting resources:** Some roads and empty land; player builds from there

**Configuration file** (configs/default.json):
```json
{
  "mapSize": [128, 128],
  "initialPopulation": 100,
  "taxRates": {
    "residential": 0.10,
    "commercial": 0.12,
    "industrial": 0.08
  },
  "timeModel": {
    "ticksPerDay": 24,
    "ticksPerMonth": 720
  },
  "systems": {
    "populationGrowthRate": 1.05,
    "landValueDecay": 0.95
  }
}
```

---

## Key Metrics to Expose

For the MVP, these 15 metrics are essential:

1. **Population** (total)
2. **Available housing** (vacant capacity)
3. **Available jobs** (unfilled positions)
4. **Unemployment rate** (%)
5. **Residential demand** (0-1 scale)
6. **Commercial demand** (0-1 scale)
7. **Industrial demand** (0-1 scale)
8. **Average commute time** (minutes)
9. **Road congestion** (0-1 scale)
10. **Land value** (average)
11. **Pollution** (average, 0-1)
12. **Happiness** (average, 0-1)
13. **City revenue** (per month)
14. **City expenses** (per month)
15. **Service coverage** (avg %, for all services)

Every design decision should affect at least one of these metrics.

---

## Debug Overlays (CLI and Future UI)

When running headless or in debug mode, print:

```
=== City Status: Day 47 ===
Population:          1,240 (71% employed)
Housing:             1,400 available / 2,100 capacity
Jobs:                880 available / 1,050 total
Unemployment:        8.5%
Happiness:           63.2% avg
Commute time:        14.2 min avg
Road congestion:     32%
Land value:          $125k avg
Revenue (month):     $12,400
Expenses (month):    $9,100
Surplus:             $3,300
Service coverage:    Fire 78%, Police 65%, Schools 92%
```

---

## Save/Load Format

Saves are JSON for human readability and easy debugging.

```json
{
  "version": "0.1.0",
  "timestamp": "2026-05-10T14:23:00Z",
  "randomSeed": 42,
  "time": {
    "tickCount": 1234,
    "currentDay": 51,
    "currentMonth": 1
  },
  "map": {
    "dimensions": [128, 128],
    "parcels": [...]
  },
  "entities": {
    "buildings": [...],
    "populationGroups": [...],
    "districts": [...]
  },
  "economy": {
    "revenue": 12400,
    "expenses": 9100,
    "debt": 0
  },
  "commandHistory": [
    { "type": "BUILD_ROAD", "from": [10, 10], "to": [10, 20] },
    ...
  ]
}
```

---

## Test Cases (MVP)

1. **Empty map simulation:** Does the engine tick without crashing?
2. **Road building:** Can roads be built and connectivity computed?
3. **Zoning and growth:** Do buildings spawn when demand and zoning exist?
4. **Population growth:** Does population increase with available housing?
5. **Commute calculation:** Are commute times reasonable given distance and congestion?
6. **Budget math:** Do revenue and expenses balance correctly?
7. **Determinism:** Same seed + same commands = same final state?
8. **Save/load:** Does a loaded game produce identical future states?
9. **Metrics consistency:** Do metrics stay in valid ranges (0-1, non-negative)?

---

## Success Criteria

The MVP is complete when:

- ✅ A 128×128 map can be created and simulated
- ✅ Roads can be built and connectivity is computed
- ✅ Parcels can be zoned and buildings spawn based on demand
- ✅ Population grows and migrates based on housing/jobs/happiness
- ✅ City budget tracks revenue and expenses
- ✅ Commute times affect happiness
- ✅ Average simulation tick runs in <10ms on a modern machine
- ✅ Game can be saved and loaded deterministically
- ✅ Metrics are printed and remain stable over 1000+ ticks
- ✅ All unit and integration tests pass
- ✅ A headless CLI allows control and inspection

