#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/world/Tile.hpp"

class CityMap;
class RoadNetwork;

struct RoadPlan {
  std::vector<Coord> tiles;
  int newSegments = 0;
  int64_t cost = 0;
  bool valid = false;
  std::string error;
};

class RoadTool {
public:
  static constexpr int64_t kCostPerSegment = 100;

  static RoadPlan plan(
    const CityMap& map,
    const RoadNetwork& roads,
    Coord start,
    Coord end,
    int64_t availableFunds
  );

  static bool build(CityMap& map, RoadNetwork& roads, const RoadPlan& plan, int64_t& availableFunds);
};
