#pragma once

#include <cstdint>
#include <vector>

#include "src/core/ThreadPool.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/Zoning.hpp"

struct ServiceCoverageCache;

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
  // Sum of buildingConstructionCost for buildings spawned or redeveloped
  // this step. CitySimulator ignores it. The playable tick bills it.
  int64_t constructionSpend = 0;

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

// Capacity of a building placed on an empty zoned tile. Redevelopment
// doubles it, capped at 64× inside GrowthSystem::runStep.
int defaultBuildingCapacity(BuildingType type);

// How many times `capacity` has doubled past defaultBuildingCapacity.
// 0 is a new low building, 1 is 2×, 4 is 16×, and 6 is the 64× cap.
// The visualizer uses this as the height and footprint tier.
int buildingDensityTier(BuildingType type, int capacity);

// Playable treasury. A new low building bills kLowBuildingConstructionCost.
// A redevelopment bills that amount times (new capacity / default), so it
// is always a larger charge than a new low building of the same type.
constexpr int64_t kLowBuildingConstructionCost = 80;
int64_t buildingConstructionCost(BuildingType type, int capacity);

// Land-value normalization for redevelopment. Matches LandValueSystem's
// default clamp: the floor has no densification pressure, the ceiling is
// fully valued.
constexpr float kRedevelopmentLandValueFloor = 10.0f;
constexpr float kRedevelopmentLandValueCeiling = 300.0f;

// Location weight in [0, 1]. Rises with normalized land value and the share
// of fire/police/health/education covering the tile. Falls to 0 as road
// congestion reaches 1. An unserved tile keeps a quarter of its land-value
// weight so a valuable downtown can still densify slowly.
float redevelopmentLocationFactor(float landValue, float civicCoverage, float roadCongestion);

class GrowthSystem {
public:
  // activeMin/activeMax optionally clamp the tile scan to the city's current
  // developed extent. Pass {-1,-1}/{-1,-1} (default) to scan the full map.
  // If pool is non-null, the read-only zone balance scan is fanned out across
  // pool workers. Mutation passes stay sequential (EntityStore is not thread-safe).
  // If requireUtilities is set, a tile also needs Tile::connectedToPower AND
  // connectedToWater before new construction (not redevelopment/demolition)
  // is considered - mirroring the existing road-access gate. Both fields
  // default to true (the M7 utility stub) and are only ever set false by
  // CitySimulator when SimOptions::enableUtilities is on, so this parameter
  // defaulting to false matches prior behavior exactly for every existing
  // caller.
  // `roads` and `services` feed the redevelopment location weight. A null
  // road network contributes no congestion. A null service cache contributes
  // no civic coverage. Land value always comes from the map.
  static GrowthStats runStep(
    CityMap& map,
    EntityStore& store,
    const ZoneDemand& demand,
    uint32_t seed,
    float baseChance = 0.25f,
    const std::vector<GrowthChanceModifier>* chanceModifiers = nullptr,
    Coord activeMin = {-1, -1},
    Coord activeMax = {-1, -1},
    ThreadPool* pool = nullptr,
    bool requireUtilities = false,
    const RoadNetwork* roads = nullptr,
    const ServiceCoverageCache* services = nullptr
  );
};
