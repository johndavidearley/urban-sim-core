#include "src/gameplay/BulldozeTool.hpp"

#include <algorithm>
#include <cmath>
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
  int64_t availableFunds,
  int elevation
) {
  BulldozePlan result;result.elevation=elevation;
  if(elevation < -1 || elevation > 1) {result.error="invalid demolition level";return result;}
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
      if (tile.regionalConnectionId) {
        result.error = "remove the regional terminal before bulldozing its site";
        return result;
      }
      if(elevation==0) {
        result.buildings += tile.buildingId != 0 ? 1 : 0;
        result.zonedTiles += map.zone(coord) != static_cast<int>(ZoneType::None) ? 1 : 0;
      }

      const RoadNetwork::Node* node = roads.getNode(coord);
      if (node == nullptr) {
        continue;
      }
      for (const RoadNodeId neighbor : node->adjacent) {
        if(roads.edgeElevation(coord,neighbor.coord)==elevation) edgeKeys.insert(canonicalEdge(coord, neighbor.coord));
      }
    }
  }

  // Removing a highway also removes ramps attached to its deck; removing a
  // ramp alone leaves the host highway and its retained split points intact.
  const auto& alignments=roads.alignments();std::vector<bool> affected(alignments.size(),false);
  for(size_t i=0;i<alignments.size();++i) if(alignments[i].elevation==elevation)
    affected[i]=std::any_of(alignments[i].footprint.begin(),alignments[i].footprint.end(),[&](Coord p) {
      return p.x>=minCorner.x && p.x<=maxCorner.x && p.y>=minCorner.y && p.y<=maxCorner.y;
    });
  for(size_t i=0;i<alignments.size();++i) if(affected[i] && alignments[i].roadClass==RoadClass::Highway) {
    const auto deck=alignments[i].routingPath();
    for(size_t j=0;j<alignments.size();++j) if(alignments[j].roadClass==RoadClass::Ramp && alignments[j].elevation==elevation) {
      const auto route=alignments[j].routingPath();
      for(Coord endpoint:{route.front(),route.back()})
        if(roadLevel(endpoint)==1 && std::find(deck.begin(),deck.end(),endpoint)!=deck.end()) affected[j]=true;
    }
  }
  for(size_t i=0;i<alignments.size();++i) if(affected[i]) {
    const auto route=alignments[i].routingPath();
    for(size_t n=1;n<route.size();++n) {
      const auto key=canonicalEdge(route[n-1],route[n]);bool shared=false;
      for(size_t j=0;j<alignments.size();++j) if(!affected[j] && alignments[j].elevation==elevation) {
        const auto other=alignments[j].routingPath();
        for(size_t k=1;k<other.size();++k) if(canonicalEdge(other[k-1],other[k])==key) shared=true;
      }
      if(!shared) edgeKeys.insert(key);
    }
  }

  if(elevation==0) for (const ServiceFacility& facility : facilities) {
    const bool hit = std::any_of(result.tiles.begin(), result.tiles.end(),
      [&](Coord coord) { return facilityCoversTile(facility, coord); });
    if (hit) ++result.services;
  }

  for (const EdgeKey& edge : edgeKeys) {
    result.roadEdges.push_back({
      {std::get<0>(edge), std::get<1>(edge)},
      {std::get<2>(edge), std::get<3>(edge)}
    });
  }
  int64_t roadCost=0;
  for(const auto& edge:result.roadEdges) roadCost+=int64_t(std::ceil(roads.edgeLength(edge.from,edge.to)))*kCostPerRoadSegment;
  result.cost = static_cast<int64_t>(result.buildings) * kCostPerBuilding
    + static_cast<int64_t>(result.services) * kCostPerService
    + roadCost
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
    map, roads, facilities, plan.tiles.front(), plan.tiles.back(), availableFunds,plan.elevation
  );
  if (!current.valid || current.cost != plan.cost || current.roadEdges.size() != plan.roadEdges.size()) {
    return false;
  }

  for (const BulldozeEdge& edge : plan.roadEdges) {
    roads.removeRoad(edge.from, edge.to);
  }
  if(plan.elevation==0) for (const Coord coord : plan.tiles) {
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
  if(plan.elevation==0) facilities.erase(
    std::remove_if(
      facilities.begin(), facilities.end(), [&plan](const ServiceFacility& facility) {
        return std::any_of(plan.tiles.begin(), plan.tiles.end(),
          [&](Coord coord) { return facilityCoversTile(facility, coord); });
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
