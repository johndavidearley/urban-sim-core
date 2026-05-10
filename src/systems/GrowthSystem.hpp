#pragma once

#include <cstdint>

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/Zoning.hpp"

struct GrowthStats {
  int evaluatedTiles = 0;
  int spawnedResidential = 0;
  int spawnedCommercial = 0;
  int spawnedIndustrial = 0;

  int totalSpawned() const {
    return spawnedResidential + spawnedCommercial + spawnedIndustrial;
  }
};

class GrowthSystem {
public:
  static GrowthStats runStep(
    CityMap& map,
    const RoadNetwork& roads,
    EntityStore& store,
    const ZoneDemand& demand,
    uint32_t seed,
    float baseChance = 0.25f
  );
};
