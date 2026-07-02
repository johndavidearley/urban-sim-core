#pragma once

#include <cstdint>
#include <vector>

#include "src/core/ThreadPool.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/Zoning.hpp"

struct GrowthStats {
  int evaluatedTiles = 0;
  int spawnedResidential = 0;
  int spawnedCommercial = 0;
  int spawnedIndustrial = 0;
  int spawnedOffice = 0;
  int demolishedResidential = 0;
  int demolishedCommercial = 0;
  int demolishedIndustrial = 0;
  int demolishedOffice = 0;
  int redevelopedResidential = 0;
  int redevelopedCommercial = 0;
  int redevelopedIndustrial = 0;
  int redevelopedOffice = 0;

  int totalSpawned() const {
    return spawnedResidential + spawnedCommercial + spawnedIndustrial + spawnedOffice;
  }

  int totalDemolished() const {
    return demolishedResidential + demolishedCommercial + demolishedIndustrial + demolishedOffice;
  }

  int totalRedeveloped() const {
    return redevelopedResidential + redevelopedCommercial + redevelopedIndustrial + redevelopedOffice;
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
  // activeMin/activeMax optionally clamp the tile scan to the city's current
  // developed extent. Pass {-1,-1}/{-1,-1} (default) to scan the full map.
  // If pool is non-null, the read-only zone balance scan is fanned out across
  // pool workers. Mutation passes stay sequential (EntityStore is not thread-safe).
  static GrowthStats runStep(
    CityMap& map,
    EntityStore& store,
    const ZoneDemand& demand,
    uint32_t seed,
    float baseChance = 0.25f,
    const std::vector<GrowthChanceModifier>* chanceModifiers = nullptr,
    Coord activeMin = {-1, -1},
    Coord activeMax = {-1, -1},
    ThreadPool* pool = nullptr
  );
};
