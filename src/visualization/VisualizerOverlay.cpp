#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/gameplay/TreasurySystem.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/DeathcareSystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/WasteSystem.hpp"
#include "src/visualization/IsometricProjection.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"

#include "src/visualization/VisualizerOverlay.hpp"


namespace visualizer {

bool hasRoadAdjacency(const RoadNetwork& roads, Coord coord) {
  const RoadNetwork::Node* node = roads.getNode(coord);
  return node != nullptr && !node->adjacent.empty();
}

bool resolveRoadAnchor(const RoadNetwork& roads, Coord coord, Coord& outAnchor) {
  if (hasRoadAdjacency(roads, coord)) {
    outAnchor = coord;
    return true;
  }

  const std::array<Coord, 4> neighbors = {
    Coord{coord.x + 1, coord.y},
    Coord{coord.x - 1, coord.y},
    Coord{coord.x, coord.y + 1},
    Coord{coord.x, coord.y - 1},
  };

  for (const Coord& n : neighbors) {
    if (hasRoadAdjacency(roads, n)) {
      outAnchor = n;
      return true;
    }
  }

  return false;
}

int shortestRoadDistance(const RoadNetwork& roads, Coord start, Coord goal) {
  if (start == goal) {
    return 0;
  }

  std::queue<Coord> frontier;
  std::unordered_map<Coord, int, Vec2Hash> distance;
  frontier.push(start);
  distance[start] = 0;

  while (!frontier.empty()) {
    const Coord current = frontier.front();
    frontier.pop();

    const RoadNetwork::Node* node = roads.getNode(current);
    if (node == nullptr) {
      continue;
    }

    for (const RoadNodeId& neighborId : node->adjacent) {
      const Coord next = neighborId.coord;
      if (distance.find(next) != distance.end()) {
        continue;
      }

      const int nextDistance = distance[current] + 1;
      distance[next] = nextDistance;
      if (next == goal) {
        return nextDistance;
      }
      frontier.push(next);
    }
  }

  return -1;
}

float serviceCoverageAtTile(const RoadNetwork& roads, Coord coord, const std::vector<ServiceFacility>& facilities) {
  if (facilities.empty()) {
    return 0.0f;
  }

  Coord tileAnchor;
  if (!resolveRoadAnchor(roads, coord, tileAnchor)) {
    return 0.0f;
  }

  float best = 0.0f;
  for (const ServiceFacility& facility : facilities) {
    Coord facilityAnchor;
    if (!resolveRoadAnchor(roads, facility.position, facilityAnchor)) {
      continue;
    }

    const int distance = shortestRoadDistance(roads, tileAnchor, facilityAnchor);
    if (distance < 0 || distance > facility.maxTravelDistance || facility.maxTravelDistance <= 0) {
      continue;
    }

    const float normalized = 1.0f - (static_cast<float>(distance) / static_cast<float>(facility.maxTravelDistance));
    best = std::max(best, std::max(0.0f, normalized));
  }

  return best;
}

float localCongestionAtTile(const RoadNetwork& roads, Coord coord) {
  if (!roads.hasNode(coord)) {
    return 0.0f;
  }

  float maxCongestion = 0.0f;
  const std::array<Coord, 4> neighbors = {
    Coord{coord.x + 1, coord.y},
    Coord{coord.x - 1, coord.y},
    Coord{coord.x, coord.y + 1},
    Coord{coord.x, coord.y - 1},
  };

  for (const Coord& n : neighbors) {
    maxCongestion = std::max(maxCongestion, roads.getCongestion(coord, n));
  }

  return maxCongestion;
}

float demandAtTile(const CityMap& map, const RoadNetwork& roads, Coord coord) {
  const Tile& tile = map.getTile(coord);
  const int zone = tile.zone;
  if (zone == 0 || tile.type == 2) {
    return 0.0f;
  }

  const bool hasBuilding = (tile.buildingId != 0);
  Coord anchor;
  const bool roadAdj = resolveRoadAnchor(roads, coord, anchor);

  float baseDemand = 0.25f;
  if (zone == 1) {
    baseDemand = 0.65f;
  } else if (zone == 2) {
    baseDemand = 0.55f;
  } else if (zone == 3) {
    baseDemand = 0.45f;
  }

  if (roadAdj) {
    baseDemand += 0.25f;
  }
  if (hasBuilding) {
    baseDemand -= 0.35f;
  }

  return std::max(0.0f, std::min(1.0f, baseDemand));
}

float happinessAtTile(
  const CityMap& map,
  const RoadNetwork& roads,
  Coord coord,
  const std::vector<ServiceFacility>& facilities,
  float wasteHappinessPenalty
) {
  const float service = serviceCoverageAtTile(roads, coord, facilities);
  const float congestion = localCongestionAtTile(roads, coord);
  const float pollution = std::max(0.0f, std::min(1.0f, map.pollution(coord)));
  const float landNorm = std::max(0.0f, std::min(1.0f, (map.landValue(coord) - 40.0f) / 160.0f));

  float happiness = 0.45f;
  happiness += service * 0.25f;
  happiness += landNorm * 0.2f;
  happiness -= pollution * 0.25f;
  happiness -= congestion * 0.2f;
  happiness -= wasteHappinessPenalty;

  if (map.zone(coord) == 0) {
    happiness -= 0.1f;
  }

  return std::max(0.0f, std::min(1.0f, happiness));
}

std::unordered_map<Coord, float, Vec2Hash> buildRouteHeatByTile(const RoadNetwork& roads) {
  std::unordered_map<Coord, float, Vec2Hash> heat;
  const std::vector<RoadNetwork::EdgeTrafficInfo> allTraffic = roads.getAllEdgeTraffic();

  float maxLoad = 0.0f;
  for (const auto& edge : allTraffic) {
    maxLoad = std::max(maxLoad, edge.currentLoad);
  }
  if (maxLoad <= 0.0f) {
    return heat;
  }

  for (const auto& edge : allTraffic) {
    const float normalized = std::max(0.0f, std::min(1.0f, edge.currentLoad / maxLoad));
    heat[edge.from] = std::max(heat[edge.from], normalized);
    heat[edge.to] = std::max(heat[edge.to], normalized);
  }

  return heat;
}

std::unordered_map<Coord, float, Vec2Hash> buildRouteHeatByTileFromEdges(
  const std::vector<EdgeTrafficData>& edges
) {
  std::unordered_map<Coord, float, Vec2Hash> heat;

  float maxCommuters = 0.0f;
  for (const auto& edge : edges) {
    maxCommuters = std::max(maxCommuters, edge.totalCommuters);
  }
  if (maxCommuters <= 0.0f) {
    return heat;
  }

  for (const auto& edge : edges) {
    const float normalized = std::max(0.0f, std::min(1.0f, edge.totalCommuters / maxCommuters));
    heat[edge.from] = std::max(heat[edge.from], normalized);
    heat[edge.to] = std::max(heat[edge.to], normalized);
  }

  return heat;
}

bool coordLess(const Coord& a, const Coord& b) {
  if (a.x != b.x) {
    return a.x < b.x;
  }
  return a.y < b.y;
}

std::vector<Coord> collectBuildingCoords(const EntityStore& store, bool residentialOnly) {
  std::vector<Coord> coords;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    if (residentialOnly) {
      if (building.type == BuildingType::Residential) {
        coords.push_back(building.position);
      }
    } else {
      if (building.type == BuildingType::Commercial || building.type == BuildingType::Industrial) {
        coords.push_back(building.position);
      }
    }
  }

  std::sort(coords.begin(), coords.end(), coordLess);
  coords.erase(std::unique(coords.begin(), coords.end()), coords.end());
  return coords;
}

void cycleOriginFilter(LiveSimulationState& liveState, const EntityStore& store) {
  const std::vector<Coord> origins = collectBuildingCoords(store, true);
  if (origins.empty()) {
    liveState.routeFilter.hasOrigin = false;
    return;
  }

  size_t index = 0;
  if (liveState.routeFilter.hasOrigin) {
    const auto it = std::find(origins.begin(), origins.end(), liveState.routeFilter.origin);
    if (it != origins.end()) {
      index = (static_cast<size_t>(std::distance(origins.begin(), it)) + 1u) % origins.size();
    }
  }

  liveState.routeFilter.hasOrigin = true;
  liveState.routeFilter.origin = origins[index];
}

void cycleDestinationFilter(LiveSimulationState& liveState, const EntityStore& store) {
  const std::vector<Coord> destinations = collectBuildingCoords(store, false);
  if (destinations.empty()) {
    liveState.routeFilter.hasDestination = false;
    return;
  }

  size_t index = 0;
  if (liveState.routeFilter.hasDestination) {
    const auto it = std::find(destinations.begin(), destinations.end(), liveState.routeFilter.destination);
    if (it != destinations.end()) {
      index = (static_cast<size_t>(std::distance(destinations.begin(), it)) + 1u) % destinations.size();
    }
  }

  liveState.routeFilter.hasDestination = true;
  liveState.routeFilter.destination = destinations[index];
}

void clearRouteFilters(LiveSimulationState& liveState) {
  liveState.routeFilter.hasOrigin = false;
  liveState.routeFilter.hasDestination = false;
}

void refreshRouteHeat(
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  LiveSimulationState& liveState,
  uint32_t seed
) {
  if (!liveState.routeFilter.hasOrigin && !liveState.routeFilter.hasDestination) {
    liveState.routeHeatByTile = buildRouteHeatByTile(roads);
    return;
  }

  const size_t diagnosticLimit = roads.getRoadCount() + 32u;
  const std::vector<EdgeTrafficData> edges = TrafficSystem::getTopRouteDiagnosticEdges(
    store,
    population,
    roads,
    liveState.routeFilter,
    diagnosticLimit,
    seed
  );
  liveState.routeHeatByTile = buildRouteHeatByTileFromEdges(edges);
}

std::string routeFilterLabel(const RouteDiagnosticsFilter& filter) {
  if (!filter.hasOrigin && !filter.hasDestination) {
    return "none";
  }

  std::ostringstream oss;
  if (filter.hasOrigin) {
    oss << "O(" << filter.origin.x << "," << filter.origin.y << ")";
  }
  if (filter.hasDestination) {
    if (filter.hasOrigin) {
      oss << "->";
    }
    oss << "D(" << filter.destination.x << "," << filter.destination.y << ")";
  }
  return oss.str();
}

float routeHeatAtTile(const std::unordered_map<Coord, float, Vec2Hash>& routeHeatByTile, Coord coord) {
  const auto it = routeHeatByTile.find(coord);
  if (it != routeHeatByTile.end()) {
    return it->second;
  }
  return 0.0f;
}

RGB tileColor(
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  Coord coord,
  OverlayMode overlayMode,
  const std::vector<ServiceFacility>& facilities,
  const std::unordered_map<Coord, float, Vec2Hash>& routeHeatByTile,
  float wasteHappinessPenalty
) {
  const Tile& tile = map.getTile(coord);

  if (overlayMode == OverlayMode::LandValue) {
    return landValueColor(map.landValue(coord));
  }
  if (overlayMode == OverlayMode::Pollution) {
    return pollutionColor(map.pollution(coord));
  }
  if (overlayMode == OverlayMode::ServiceCoverage) {
    return serviceCoverageColor(serviceCoverageAtTile(roads, coord, facilities));
  }
  if (overlayMode == OverlayMode::TrafficCongestion) {
    return congestionColor(localCongestionAtTile(roads, coord));
  }
  if (overlayMode == OverlayMode::Demand) {
    return demandColor(demandAtTile(map, roads, coord));
  }
  if (overlayMode == OverlayMode::Happiness) {
    return happinessColor(happinessAtTile(map, roads, coord, facilities, wasteHappinessPenalty));
  }
  if (overlayMode == OverlayMode::RouteHeatmap) {
    return routeHeatColor(routeHeatAtTile(routeHeatByTile, coord));
  }

  RGB color = terrainTint(tile, zoneColor(tile.zone));

  for (const ServiceFacility& facility : facilities) {
    if (facility.position == coord) {
      return serviceFacilityColor(facility.type);
    }
  }

  if (tile.hasRoad) {
    color = {153, 158, 154};
  }

  if (tile.buildingId != 0) {
    const Building* building = store.getBuilding(tile.buildingId);
    if (building != nullptr) {
      switch (building->type) {
        case BuildingType::Residential:
          color = {44, 132, 70};
          break;
        case BuildingType::Commercial:
          color = {31, 84, 163};
          break;
        case BuildingType::Industrial:
          color = {179, 93, 29};
          break;
        case BuildingType::Office:
          color = {147, 112, 219};
          break;
      }
    }
  }

  return color;
}

RGB overlaySampleColor(OverlayMode mode, float value) {
  switch (mode) {
    case OverlayMode::LandValue:
      return landValueColor(50.0f + (value * 150.0f));
    case OverlayMode::Pollution:
      return pollutionColor(value);
    case OverlayMode::ServiceCoverage:
      return serviceCoverageColor(value);
    case OverlayMode::TrafficCongestion:
      return congestionColor(value);
    case OverlayMode::Demand:
      return demandColor(value);
    case OverlayMode::Happiness:
      return happinessColor(value);
    case OverlayMode::RouteHeatmap:
      return routeHeatColor(value);
    case OverlayMode::Zone:
    default:
      return zoneColor(value < 0.33f ? 1 : (value < 0.66f ? 2 : 3));
  }
}


}  // namespace visualizer
