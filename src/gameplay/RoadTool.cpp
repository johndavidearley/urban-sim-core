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

RoadPlan RoadTool::plan(const CityMap &map, const RoadNetwork &roads,
                        Coord start, Coord end, int64_t availableFunds,
                        RoadClass roadClass,
                        const std::vector<ServiceFacility>* facilities) {
  RoadPlan result;
  result.roadClass = roadClass;
  if (roadClass != RoadClass::Local && roadClass != RoadClass::Arterial) {
    result.error = "unsupported road class";
    return result;
  }
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
    if (mapTile.regionalConnectionId) {
      result.error = "route crosses a regional terminal";
      return result;
    }
    if (mapTile.type == 2) {
      result.error = "route crosses water";
      return result;
    }
    if (mapTile.buildingId != 0 && !mapTile.hasRoad) {
      result.error = "route crosses a building";
      return result;
    }
    if (facilities != nullptr) {
      for (const ServiceFacility& facility : *facilities) {
        if (facility.type == ServiceType::Power &&
            facility.powerSource == PowerSourceType::Solar &&
            facilityCoversTile(facility, tile)) {
          result.error = "route crosses a solar plant";
          return result;
        }
      }
    }
  }

  for (size_t i = 1; i < result.tiles.size(); ++i) {
    if (!roads.hasRoad(result.tiles[i - 1], result.tiles[i])) {
      ++result.newSegments;
    } else if (roadClass == RoadClass::Arterial &&
               roads.getRoadClass(result.tiles[i - 1], result.tiles[i]) ==
                   RoadClass::Local) {
      ++result.upgradedSegments;
    }
  }
  result.cost =
      static_cast<int64_t>(result.newSegments) *
          (roadClass == RoadClass::Arterial ? kArterialCostPerSegment
                                            : kCostPerSegment) +
      static_cast<int64_t>(result.upgradedSegments) * kUpgradeCostPerSegment;

  if (result.newSegments == 0 && result.upgradedSegments == 0) {
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
  int64_t& availableFunds,
  const std::vector<ServiceFacility>* facilities
) {
  if (!plan.valid || plan.cost > availableFunds || plan.tiles.size() < 2) {
    return false;
  }

  const RoadPlan current =
      RoadTool::plan(map, roads, plan.tiles.front(), plan.tiles.back(),
                     availableFunds, plan.roadClass, facilities);
  if (!current.valid || current.tiles != plan.tiles || current.cost != plan.cost) {
    return false;
  }

  for (size_t i = 1; i < plan.tiles.size(); ++i) {
    roads.buildRoad(plan.tiles[i - 1], plan.tiles[i]);
    if (plan.roadClass == RoadClass::Arterial)
      roads.setRoadClass(plan.tiles[i - 1], plan.tiles[i], RoadClass::Arterial);
  }
  availableFunds -= plan.cost;
  roads.updateConnectivity(plan.tiles.front());
  return true;
}
