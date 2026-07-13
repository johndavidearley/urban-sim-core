#include "src/gameplay/RoadTool.hpp"

#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

namespace {
void appendAxis(std::vector<Coord>& tiles, Coord& current, int target, bool horizontal) {
  while ((horizontal ? current.x : current.y) != target) {
    const int value = horizontal ? current.x : current.y;
    const int step = target > value ? 1 : -1;
    if (horizontal) {
      current.x += step;
    } else {
      current.y += step;
    }
    tiles.push_back(current);
  }
}
} // namespace

RoadPlan RoadTool::plan(
  const CityMap& map,
  const RoadNetwork& roads,
  Coord start,
  Coord end,
  int64_t availableFunds
) {
  RoadPlan result;
  if (!map.isValid(start) || !map.isValid(end)) {
    result.error = "route is outside the map";
    return result;
  }

  result.tiles.push_back(start);
  Coord current = start;
  appendAxis(result.tiles, current, end.x, true);
  appendAxis(result.tiles, current, end.y, false);

  for (const Coord tile : result.tiles) {
    const Tile& mapTile = map.getTile(tile);
    if (mapTile.type == 2) {
      result.error = "route crosses water";
      return result;
    }
    if (mapTile.buildingId != 0 && !mapTile.hasRoad) {
      result.error = "route crosses a building";
      return result;
    }
  }

  for (size_t i = 1; i < result.tiles.size(); ++i) {
    if (!roads.hasRoad(result.tiles[i - 1], result.tiles[i])) {
      ++result.newSegments;
    }
  }
  result.cost = static_cast<int64_t>(result.newSegments) * kCostPerSegment;

  if (result.newSegments == 0) {
    result.error = "route adds no new road";
    return result;
  }
  if (result.cost > availableFunds) {
    result.error = "insufficient funds";
    return result;
  }

  result.valid = true;
  return result;
}

bool RoadTool::build(
  CityMap& map,
  RoadNetwork& roads,
  const RoadPlan& plan,
  int64_t& availableFunds
) {
  if (!plan.valid || plan.cost > availableFunds || plan.tiles.size() < 2) {
    return false;
  }

  const RoadPlan current = RoadTool::plan(map, roads, plan.tiles.front(), plan.tiles.back(), availableFunds);
  if (!current.valid || current.tiles != plan.tiles || current.cost != plan.cost) {
    return false;
  }

  for (size_t i = 1; i < plan.tiles.size(); ++i) {
    roads.buildRoad(plan.tiles[i - 1], plan.tiles[i]);
  }
  availableFunds -= plan.cost;
  roads.updateConnectivity(plan.tiles.front());
  return true;
}
