#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/world/Zoning.hpp"

struct ZonePlan {
  std::vector<Coord> tiles;
  ZoneType zone = ZoneType::Residential;
  int changedTiles = 0;
  int64_t cost = 0;
  bool valid = false;
  std::string error;
};

class ZoneTool {
public:
  static constexpr int64_t kCostPerTile = 25;

  static ZonePlan plan(
    const CityMap& map,
    Coord start,
    Coord end,
    ZoneType zone,
    int64_t availableFunds
  );

  static bool apply(CityMap& map, const ZonePlan& plan, int64_t& availableFunds);
};
