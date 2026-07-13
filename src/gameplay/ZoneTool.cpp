#include "src/gameplay/ZoneTool.hpp"

#include <algorithm>

ZonePlan ZoneTool::plan(
  const CityMap& map,
  Coord start,
  Coord end,
  ZoneType zone,
  int64_t availableFunds
) {
  ZonePlan result;
  result.zone = zone;
  const Coord minCorner{std::min(start.x, end.x), std::min(start.y, end.y)};
  const Coord maxCorner{std::max(start.x, end.x), std::max(start.y, end.y)};
  if (!map.isValid(minCorner) || !map.isValid(maxCorner)) {
    result.error = "area is outside the map";
    return result;
  }
  if (zone == ZoneType::None || zone == ZoneType::Park) {
    result.error = "unsupported zone type";
    return result;
  }

  for (int y = minCorner.y; y <= maxCorner.y; ++y) {
    for (int x = minCorner.x; x <= maxCorner.x; ++x) {
      const Coord coord{x, y};
      result.tiles.push_back(coord);
      const Tile& tile = map.getTile(coord);
      if (tile.type == 2) {
        result.error = "area includes water";
        return result;
      }
      if (tile.hasRoad) {
        result.error = "area includes a road";
        return result;
      }
      if (tile.buildingId != 0 && map.zone(coord) != static_cast<int>(zone)) {
        result.error = "cannot rezone an occupied tile";
        return result;
      }
      if (map.zone(coord) != static_cast<int>(zone)) {
        ++result.changedTiles;
      }
    }
  }

  result.cost = static_cast<int64_t>(result.changedTiles) * kCostPerTile;
  if (result.changedTiles == 0) {
    result.error = "area already has this zone";
    return result;
  }
  if (result.cost > availableFunds) {
    result.error = "insufficient funds";
    return result;
  }
  result.valid = true;
  return result;
}

bool ZoneTool::apply(CityMap& map, const ZonePlan& plan, int64_t& availableFunds) {
  if (!plan.valid || plan.cost > availableFunds || plan.tiles.empty()) {
    return false;
  }

  const ZonePlan current = ZoneTool::plan(
    map, plan.tiles.front(), plan.tiles.back(), plan.zone, availableFunds
  );
  if (!current.valid || current.tiles != plan.tiles || current.cost != plan.cost) {
    return false;
  }

  for (const Coord coord : plan.tiles) {
    if (map.zone(coord) == static_cast<int>(plan.zone)) {
      continue;
    }
    map.setZone(coord, static_cast<int>(plan.zone));
    map.landValue(coord) = Zoning::defaultLandValueForZone(plan.zone);
  }
  availableFunds -= plan.cost;
  return true;
}
