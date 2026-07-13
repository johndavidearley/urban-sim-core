#include "src/gameplay/BulldozeTool.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

namespace {
using EdgeKey = std::tuple<int, int, int, int>;

EdgeKey canonicalEdge(Coord a, Coord b) {
  if (b.x < a.x || (b.x == a.x && b.y < a.y)) {
    std::swap(a, b);
  }
  return {a.x, a.y, b.x, b.y};
}
} // namespace

BulldozePlan BulldozeTool::plan(
  const CityMap& map,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities,
  Coord start,
  Coord end,
  int64_t availableFunds
) {
  BulldozePlan result;
  const Coord minCorner{std::min(start.x, end.x), std::min(start.y, end.y)};
  const Coord maxCorner{std::max(start.x, end.x), std::max(start.y, end.y)};
  if (!map.isValid(minCorner) || !map.isValid(maxCorner)) {
    result.error = "area is outside the map";
    return result;
  }

  std::set<EdgeKey> edgeKeys;
  for (int y = minCorner.y; y <= maxCorner.y; ++y) {
    for (int x = minCorner.x; x <= maxCorner.x; ++x) {
      const Coord coord{x, y};
      result.tiles.push_back(coord);
      const Tile& tile = map.getTile(coord);
      result.buildings += tile.buildingId != 0 ? 1 : 0;
      result.zonedTiles += map.zone(coord) != static_cast<int>(ZoneType::None) ? 1 : 0;
      for (const ServiceFacility& facility : facilities) {
        result.services += facility.position == coord ? 1 : 0;
      }

      const RoadNetwork::Node* node = roads.getNode(coord);
      if (node == nullptr) {
        continue;
      }
      for (const RoadNodeId neighbor : node->adjacent) {
        edgeKeys.insert(canonicalEdge(coord, neighbor.coord));
      }
    }
  }

  for (const EdgeKey& edge : edgeKeys) {
    result.roadEdges.push_back({
      {std::get<0>(edge), std::get<1>(edge)},
      {std::get<2>(edge), std::get<3>(edge)}
    });
  }
  result.cost = static_cast<int64_t>(result.buildings) * kCostPerBuilding
    + static_cast<int64_t>(result.services) * kCostPerService
    + static_cast<int64_t>(result.roadEdges.size()) * kCostPerRoadSegment
    + static_cast<int64_t>(result.zonedTiles) * kCostPerZonedTile;

  if (result.cost == 0) {
    result.error = "nothing to demolish";
    return result;
  }
  if (result.cost > availableFunds) {
    result.error = "insufficient funds";
    return result;
  }
  result.valid = true;
  return result;
}

bool BulldozeTool::apply(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  std::vector<ServiceFacility>& facilities,
  const BulldozePlan& plan,
  int64_t& availableFunds
) {
  if (!plan.valid || plan.tiles.empty() || plan.cost > availableFunds) {
    return false;
  }
  const BulldozePlan current = BulldozeTool::plan(
    map, roads, facilities, plan.tiles.front(), plan.tiles.back(), availableFunds
  );
  if (!current.valid || current.cost != plan.cost || current.roadEdges.size() != plan.roadEdges.size()) {
    return false;
  }

  for (const BulldozeEdge& edge : plan.roadEdges) {
    roads.removeRoad(edge.from, edge.to);
  }
  for (const Coord coord : plan.tiles) {
    Tile& tile = map.getTile(coord);
    if (tile.buildingId != 0) {
      store.removeBuilding(tile.buildingId);
      tile.buildingId = 0;
    }
    if (map.zone(coord) != static_cast<int>(ZoneType::None)) {
      map.setZone(coord, static_cast<int>(ZoneType::None));
      map.landValue(coord) = Zoning::defaultLandValueForZone(ZoneType::None);
    }
  }
  facilities.erase(
    std::remove_if(
      facilities.begin(), facilities.end(), [&plan](const ServiceFacility& facility) {
        return std::find(plan.tiles.begin(), plan.tiles.end(), facility.position) != plan.tiles.end();
      }),
    facilities.end()
  );

  availableFunds -= plan.cost;
  const std::vector<Coord> remainingRoadTiles = roads.getAllRoadTiles();
  if (remainingRoadTiles.empty()) {
    roads.clear();
  } else {
    roads.updateConnectivity(remainingRoadTiles.front());
  }
  return true;
}
