#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/systems/DistrictSystem.hpp"

struct PolicySweepOptions {
  std::string outputDir;
  int districtId = -1;
  std::vector<uint32_t> seeds;
  std::vector<int64_t> caps;
  std::vector<float> allocations;
  bool manifestAllDistricts = false;
  int growthSteps = 0;
  uint32_t baseSeed = 42;
  int64_t pressurePool = -1;
};

// Runs the seed/cap/allocation policy sweep against the current city state,
// writing per-scenario growth-pressure reports plus ranking manifests to the
// output directory. Returns the process exit code. Mutates the sweep
// district's cap/allocation as scenarios run (matching historical behavior).
int runPolicySweep(
  PolicySweepOptions options,
  DistrictSystem& districtSystem,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population,
  const std::vector<ServiceFacility>& serviceFacilities
);
