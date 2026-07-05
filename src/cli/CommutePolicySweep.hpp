#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

struct CommutePolicySweepOptions {
  std::string outputDir;
  std::vector<uint32_t> seeds;                    // at least one; seeds[0] is the baseline seed
  std::vector<float> transitCapacityMultipliers;  // empty defaults to {1.0}; scales SimOptions::transitCapacityMultiplier
  bool includeTransitDisabledScenario = true;      // also run one scenario per seed with transit off entirely (SimOptions::enableTransit = false)
  int ticks = 0;
};

// Sweeps commute-shaping policy levers - transit capacity (bus/rail
// vehicleCount and capacityPerVehicle, scaled by SimOptions::
// transitCapacityMultiplier) and transit on/off - against the current city
// state, running each (seed x multiplier) combination plus one
// transit-disabled scenario per seed from a shared baseline snapshot.
// Writes each scenario's per-tick SimTickMetrics as CSV (same format
// --simulate-report uses) plus a ranking manifest (mean traffic congestion,
// transit modal share, and unmet transit demand, each as a delta vs. the
// baseline scenario - seeds[0], multiplier 1.0, transit enabled) to the
// output directory. Returns the process exit code.
int runCommutePolicySweep(
  CommutePolicySweepOptions options,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population
);
