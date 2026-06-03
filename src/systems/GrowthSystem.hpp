#pragma once

#include <cstdint>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/Zoning.hpp"

struct GrowthStats {
  int evaluatedTiles = 0;
  int spawnedResidential = 0;
  int spawnedCommercial = 0;
  int spawnedIndustrial = 0;
  int demolishedResidential = 0;
  int demolishedCommercial = 0;
  int demolishedIndustrial = 0;

  int totalSpawned() const {
    return spawnedResidential + spawnedCommercial + spawnedIndustrial;
  }

  int totalDemolished() const {
    return demolishedResidential + demolishedCommercial + demolishedIndustrial;
  }
};

struct GrowthChanceModifier {
  glm::ivec2 minCorner{0, 0};
  glm::ivec2 maxCorner{0, 0};
  float multiplier = 1.0f;

  bool contains(Coord coord) const {
    return coord.x >= minCorner.x && coord.x <= maxCorner.x &&
           coord.y >= minCorner.y && coord.y <= maxCorner.y;
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
    float baseChance = 0.25f,
    const std::vector<GrowthChanceModifier>* chanceModifiers = nullptr
  );
};
