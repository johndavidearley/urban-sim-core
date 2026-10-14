#pragma once
#include <algorithm>
#include <cmath>
#include "src/world/Zoning.hpp"

// Parks share ordinary zoning/snapshot storage. Nearest-park benefits taper
// over four Manhattan tiles and never stack beyond the advertised maximum.
class ParkSystem {
public:
  static constexpr int kRadius = 4;
  static constexpr int64_t kPlacementCost = 100;
  static constexpr int64_t kUpkeepPerTick = 2;
  static bool isPark(const CityMap& map, Coord coord) {
    if (!map.isValid(coord)) return false;
    const auto& tile = map.getTile(coord);
    return tile.zone == static_cast<int>(ZoneType::Park) && tile.type != 2 &&
           !tile.hasRoad && tile.buildingId == 0;
  }
  static float proximity(const CityMap& map, Coord coord) {
    int nearest = kRadius;
    for (int dy = -kRadius + 1; dy < kRadius; ++dy)
      for (int dx = -kRadius + 1; dx < kRadius; ++dx) {
        const int distance = std::abs(dx) + std::abs(dy);
        if (distance < nearest && isPark(map, {coord.x + dx, coord.y + dy}))
          nearest = distance;
      }
    return 1.f - static_cast<float>(nearest) / kRadius;
  }
  static int count(const CityMap& map) {
    int parks = 0;
    const auto dims = map.getDimensions();
    for (int y = 0; y < dims.y; ++y)
      for (int x = 0; x < dims.x; ++x) parks += isPark(map, {x, y}) ? 1 : 0;
    return parks;
  }
};
