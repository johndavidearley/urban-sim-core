#pragma once

#include <cstdint>
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
  float avgPollution = 0.0f;  // mean pollution where residents live
};

struct SimPhaseTimings {
  double roadMs = 0.0;
  double zoningMs = 0.0;
  double growthMs = 0.0;
  double populationMs = 0.0;
  double trafficMs = 0.0;
  double economyMs = 0.0;
};

struct SimResult {
  std::vector<SimTickMetrics> rows;
  SimPhaseTimings timings;
};

struct SimOptions {
  int gridSpacing = 4;         // tiles between road grid lines
  int zoneBatchPerTick = 16;   // max tiles newly zoned per tick (paces demand-following land mix)
  float buildChance = 0.5f;    // GrowthSystem base spawn chance
  bool runTraffic = true;      // include commute simulation each tick (exercises the hot path)
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
