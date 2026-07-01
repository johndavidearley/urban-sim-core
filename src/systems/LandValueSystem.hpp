#pragma once

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/CityMap.hpp"

// Milestone 12 (Advanced Economy): dynamic per-tile land value. Replaces the
// static "set once at zoning time, never changes" placeholder with a value
// recomputed from the tile's zone base plus three location factors: distance
// to the nearest job (commercial/industrial building), distance to the
// nearest covered service facility, and local pollution.
struct LandValueParams {
  float jobAccessRadius = 20.0f;      // road-hops within which job proximity raises value
  float maxJobBonus = 40.0f;
  float serviceAccessRadius = 15.0f;  // road-hops within which service proximity raises value
  float maxServiceBonus = 25.0f;
  float maxPollutionPenalty = 60.0f;  // applied in full at pollution == 1.0
  float minLandValue = 10.0f;
  float maxLandValue = 300.0f;
};

class LandValueSystem {
public:
  // Recomputes Tile::landValue for every non-water tile in [x0,y0]-[x1,y1]
  // (inclusive) from its zone's base value plus job-access, service-access,
  // and pollution factors. Job access is computed via one multi-source BFS
  // seeded from all commercial/industrial buildings (road-anchored), so cost
  // scales with the road network, not with building count. `serviceCache` may
  // be null to skip the service-access factor (e.g. before any facility
  // exists). Deterministic; no RNG.
  static void updateLandValues(
    CityMap& map,
    const RoadNetwork& roads,
    const EntityStore& store,
    const ServiceCoverageCache* serviceCache,
    int x0, int y0, int x1, int y1,
    const LandValueParams& params = {}
  );

  // Mean Tile::landValue across zoned (non-empty, non-water) tiles on the
  // whole map. Returns 100.0 (the neutral base) if none are zoned.
  static float averageLandValue(const CityMap& map);
};
