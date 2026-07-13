#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/world/Tile.hpp"
#include "src/systems/ServiceSystem.hpp"

class CityMap;
class EntityStore;
class RoadNetwork;

struct BulldozeEdge {
  Coord from;
  Coord to;
};

struct BulldozePlan {
  std::vector<Coord> tiles;
  std::vector<BulldozeEdge> roadEdges;
  int buildings = 0;
  int services = 0;
  int zonedTiles = 0;
  int64_t cost = 0;
  bool valid = false;
  std::string error;
};

class BulldozeTool {
public:
  static constexpr int64_t kCostPerBuilding = 200;
  static constexpr int64_t kCostPerService = 100;
  static constexpr int64_t kCostPerRoadSegment = 20;
  static constexpr int64_t kCostPerZonedTile = 5;

  static BulldozePlan plan(
    const CityMap& map,
    const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities,
    Coord start,
    Coord end,
    int64_t availableFunds
  );

  static bool apply(
    CityMap& map,
    RoadNetwork& roads,
    EntityStore& store,
    std::vector<ServiceFacility>& facilities,
    const BulldozePlan& plan,
    int64_t& availableFunds
  );
};
