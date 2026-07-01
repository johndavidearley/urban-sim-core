#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

// One row of the simulation time series.
struct SimTickMetrics {
  int tick = 0;
  float demandResidential = 0.0f;
  float demandCommercial = 0.0f;
  float demandIndustrial = 0.0f;
  uint32_t population = 0;
  uint32_t employed = 0;
  uint32_t residentialBuildings = 0;
  uint32_t commercialBuildings = 0;
  uint32_t industrialBuildings = 0;
  uint32_t roadTiles = 0;
  int64_t budgetBalance = 0;
  float trafficCongestion = 0.0f;
  float avgPollution = 0.0f;   // mean pollution where residents live
  float serviceCoverage = 0.0f;
  uint32_t serviceFacilities = 0;
  float avgLandValue = 100.0f;  // mean Tile::landValue across zoned tiles
};

struct SimPhaseTimings {
  double roadMs = 0.0;
  double zoningMs = 0.0;
  double growthMs = 0.0;
  double populationMs = 0.0;
  double trafficMs = 0.0;
  double economyMs = 0.0;
  double serviceMs = 0.0;
  double landValueMs = 0.0;
};

struct SimResult {
  std::vector<SimTickMetrics> rows;
  SimPhaseTimings timings;
};

struct SimOptions {
  int gridSpacing = 4;         // tiles between road grid lines
  int zoneBatchPerTick = 16;   // max tiles newly zoned per tick (paces demand-following land mix)
  float buildChance = 0.5f;    // GrowthSystem base spawn chance
  bool runTraffic = true;      // include commute simulation (exercises the hot path)
  int trafficInterval = 1;     // run traffic every N ticks (>1 cuts CPU at large populations)
  int serviceInterval = 1;     // run service evaluation every N ticks
  int populationInterval = 1;  // run full population allocation every N ticks
  int landValueInterval = 1;   // run land-value recompute every N ticks (its job-access BFS is the costliest per-tick pass; >1 cuts CPU on large road networks)
  // Called after each tick with the row. Return false to stop the simulation.
  // Used by infinite mode; rows are not accumulated in SimResult when this is set and ticks < 0.
  std::function<bool(const SimTickMetrics&)> tickCallback;
};

// Drives a fully autonomous, RCI-demand-driven city from a near-empty map:
// each tick it derives residential/commercial/industrial demand from the city's
// own state, extends roads outward, zones land in proportion to demand, lets
// GrowthSystem build, repopulates housing/jobs, and records a metrics row.
class CitySimulator {
public:
  // Residential/commercial/industrial demand (each 0-1) derived purely from the
  // current building stock and population. Exposed for testing.
  static ZoneDemand evaluateDemand(const EntityStore& store, const PopulationStore& population);

  // Runs `ticks` autonomous ticks, mutating map/roads/store/population in place.
  // If ticks < 0, runs indefinitely until options.tickCallback returns false.
  // In infinite mode (ticks < 0), rows are not accumulated in SimResult.rows;
  // use options.tickCallback to observe per-tick state.
  static SimResult run(
    CityMap& map,
    RoadNetwork& roads,
    EntityStore& store,
    PopulationStore& population,
    uint32_t seed,
    int ticks,
    const SimOptions& options = {}
  );
};
