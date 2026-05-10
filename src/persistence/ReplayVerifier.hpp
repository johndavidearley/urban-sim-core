#pragma once

#include <cstdint>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

struct ReplayConfig {
  int mapSize = 64;
  uint32_t seed = 42;
  int growthSteps = 6;
  int seedPopulation = 120;
  bool runCommutes = true;
  bool runEconomy = true;
};

struct ReplayResult {
  uint64_t firstChecksum = 0;
  uint64_t secondChecksum = 0;
  bool deterministic = false;
};

class ReplayVerifier {
public:
  static uint64_t calculateChecksum(
    const CityMap& map,
    const RoadNetwork& roads,
    const EntityStore& store,
    const PopulationStore& population
  );

  static ReplayResult verifyDeterministicRun(const ReplayConfig& config);
};
