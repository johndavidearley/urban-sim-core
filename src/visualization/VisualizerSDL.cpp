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

#include <SDL2/SDL.h>

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
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"

namespace {
bool gReadableUiText = true;

struct RGB {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

enum class OverlayMode {
  Zone = 0,
  LandValue = 1,
  Pollution = 2,
  ServiceCoverage = 3,
  TrafficCongestion = 4,
  Demand = 5,
  Happiness = 6,
  RouteHeatmap = 7,
};

enum class PaletteTool {
  None,
  Road,
  Zone,
  Bulldoze,
  Service,
};

const char* overlayModeName(OverlayMode mode) {
  switch (mode) {
    case OverlayMode::Zone:
      return "zone";
    case OverlayMode::LandValue:
      return "land-value";
    case OverlayMode::Pollution:
      return "pollution";
    case OverlayMode::ServiceCoverage:
      return "service";
    case OverlayMode::TrafficCongestion:
      return "traffic";
    case OverlayMode::Demand:
      return "demand";
    case OverlayMode::Happiness:
      return "happiness";
    case OverlayMode::RouteHeatmap:
      return "route-heat";
    default:
      return "unknown";
  }
}

constexpr int kOverlayPanelX = 12;
constexpr int kOverlayPanelY = 12;
constexpr int kOverlayKeyStartX = kOverlayPanelX + 10;
constexpr int kOverlayKeyY = kOverlayPanelY + 10;
constexpr int kOverlayKeyWidth = 52;
constexpr int kOverlayKeyHeight = 20;
constexpr int kOverlayKeyGap = 8;

const std::array<OverlayMode, 8>& overlayModes() {
  static const std::array<OverlayMode, 8> modes = {
    OverlayMode::Zone,
    OverlayMode::LandValue,
    OverlayMode::Pollution,
    OverlayMode::ServiceCoverage,
    OverlayMode::TrafficCongestion,
    OverlayMode::Demand,
    OverlayMode::Happiness,
    OverlayMode::RouteHeatmap,
  };
  return modes;
}

bool overlayHitTest(int mouseX, int mouseY, OverlayMode& outMode) {
  const auto& modes = overlayModes();
  for (size_t i = 0; i < modes.size(); ++i) {
    const SDL_Rect rect{
      kOverlayKeyStartX + static_cast<int>(i) * (kOverlayKeyWidth + kOverlayKeyGap),
      kOverlayKeyY,
      kOverlayKeyWidth,
      kOverlayKeyHeight
    };
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      outMode = modes[i];
      return true;
    }
  }
  return false;
}

ZoneType nextPlayableZone(ZoneType zone) {
  switch (zone) {
    case ZoneType::Residential: return ZoneType::Commercial;
    case ZoneType::Commercial: return ZoneType::Industrial;
    case ZoneType::Industrial: return ZoneType::Office;
    case ZoneType::Office:
    default: return ZoneType::Residential;
  }
}

ServiceType nextPlayableService(ServiceType type) {
  switch (type) {
    case ServiceType::Fire: return ServiceType::Police;
    case ServiceType::Police: return ServiceType::Health;
    case ServiceType::Health: return ServiceType::Education;
    case ServiceType::Education: return ServiceType::Power;
    case ServiceType::Power: return ServiceType::Water;
    case ServiceType::Water: return ServiceType::Sanitation;
    case ServiceType::Sanitation: return ServiceType::Garbage;
    case ServiceType::Garbage: return ServiceType::Recycling;
    case ServiceType::Recycling: return ServiceType::Cemetery;
    case ServiceType::Cemetery: return ServiceType::Crematorium;
    case ServiceType::Crematorium:
    default: return ServiceType::Fire;
  }
}

RGB serviceFacilityColor(ServiceType type) {
  switch (type) {
    case ServiceType::Fire: return {220, 55, 45};
    case ServiceType::Police: return {50, 105, 220};
    case ServiceType::Health: return {235, 235, 245};
    case ServiceType::Education: return {235, 195, 55};
    case ServiceType::Power: return {255, 225, 70};
    case ServiceType::Water: return {55, 190, 235};
    case ServiceType::Sanitation: return {95, 175, 105};
    case ServiceType::Garbage: return {120, 95, 70};
    case ServiceType::Recycling: return {55, 205, 125};
    case ServiceType::Cemetery: return {115, 125, 105};
    case ServiceType::Crematorium: return {175, 105, 185};
    default: return {180, 180, 180};
  }
}

uint8_t toByte(float normalized) {
  const float clamped = std::max(0.0f, std::min(1.0f, normalized));
  return static_cast<uint8_t>(clamped * 255.0f);
}

RGB zoneColor(int zone) {
  switch (zone) {
    case 1:
      return {119, 221, 119};
    case 2:
      return {100, 149, 237};
    case 3:
      return {255, 179, 71};
    case 4:
      return {80, 200, 120};
    case 5:
      return {147, 112, 219};
    default:
      return {230, 230, 230};
  }
}

RGB terrainTint(const Tile& tile, RGB base) {
  if (tile.type == 2) {
    return {102, 153, 255};
  }
  if (tile.type == 1) {
    base.r = static_cast<uint8_t>(std::max(0, base.r - 25));
    base.g = static_cast<uint8_t>(std::max(0, base.g - 25));
    base.b = static_cast<uint8_t>(std::max(0, base.b - 25));
  }
  return base;
}

RGB landValueColor(float landValue) {
  const float normalized = (landValue - 50.0f) / 150.0f;
  const uint8_t value = toByte(normalized);
  return {value, static_cast<uint8_t>(255 - value / 2), static_cast<uint8_t>(255 - value)};
}

RGB pollutionColor(float pollution) {
  const uint8_t value = toByte(pollution);
  return {value, static_cast<uint8_t>(180 - value / 2), static_cast<uint8_t>(80 - value / 4)};
}

RGB serviceCoverageColor(float score) {
  const uint8_t value = toByte(score);
  return {static_cast<uint8_t>(255 - value), static_cast<uint8_t>(80 + (value / 2)), value};
}

RGB congestionColor(float score) {
  const uint8_t value = toByte(score);
  return {value, static_cast<uint8_t>(255 - value), static_cast<uint8_t>(80 - value / 4)};
}

RGB demandColor(float score) {
  const uint8_t value = toByte(score);
  return {static_cast<uint8_t>(120 + value / 2), static_cast<uint8_t>(80 + value / 3), static_cast<uint8_t>(255 - value / 3)};
}

RGB happinessColor(float score) {
  const uint8_t value = toByte(score);
  return {static_cast<uint8_t>(255 - value / 2), static_cast<uint8_t>(120 + value / 2), static_cast<uint8_t>(80 + value / 4)};
}

RGB routeHeatColor(float score) {
  const uint8_t value = toByte(score);
  const uint8_t warm = static_cast<uint8_t>(80 + (value * 3) / 5);
  return {value, warm, static_cast<uint8_t>(255 - value / 2)};
}

struct LiveSimulationState {
  uint32_t tick = 0;
  bool paused = false;
  uint32_t tickIntervalMs = 350;
  uint32_t lastTickMs = 0;
  ServiceCoverageSummary serviceSummary;
  TrafficSummary trafficSummary;
  std::unordered_map<Coord, float, Vec2Hash> routeHeatByTile;
  RouteDiagnosticsFilter routeFilter;
  ZoneDemand demand;
  EconomyState economy;
  int64_t treasuryRevenue = 0;
  int64_t treasuryExpenses = 0;
  int64_t treasuryNet = 0;
  int64_t treasuryShortfall = 0;
  bool lowFunds = false;
  bool bankrupt = false;
  WasteSummary waste;
  DeathcareState deathcareState;
  DeathcareSummary deathcare;
  uint32_t populationTarget = 480;
};

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
    color = {64, 64, 64};
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

void drawFilledRect(SDL_Renderer* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha = 255) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
  SDL_Rect rect{x, y, w, h};
  SDL_RenderFillRect(renderer, &rect);
}

void drawRectOutline(SDL_Renderer* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha = 255) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
  SDL_Rect rect{x, y, w, h};
  SDL_RenderDrawRect(renderer, &rect);
}

void drawGradientBar(SDL_Renderer* renderer, int x, int y, int w, int h, OverlayMode mode) {
  for (int i = 0; i < w; ++i) {
    const float t = (w > 1) ? static_cast<float>(i) / static_cast<float>(w - 1) : 0.0f;
    const RGB c = overlaySampleColor(mode, t);
    drawFilledRect(renderer, x + i, y, 1, h, c, 255);
  }
  drawRectOutline(renderer, x, y, w, h, {220, 220, 220}, 255);
}

std::array<uint8_t, 5> glyphRows(char c) {
  switch (c) {
    case 'A': return {0b010, 0b101, 0b111, 0b101, 0b101};
    case 'B': return {0b110, 0b101, 0b110, 0b101, 0b110};
    case 'C': return {0b011, 0b100, 0b100, 0b100, 0b011};
    case 'D': return {0b110, 0b101, 0b101, 0b101, 0b110};
    case 'E': return {0b111, 0b100, 0b110, 0b100, 0b111};
    case 'F': return {0b111, 0b100, 0b110, 0b100, 0b100};
    case 'G': return {0b011, 0b100, 0b101, 0b101, 0b011};
    case 'H': return {0b101, 0b101, 0b111, 0b101, 0b101};
    case 'I': return {0b111, 0b010, 0b010, 0b010, 0b111};
    case 'J': return {0b001, 0b001, 0b001, 0b101, 0b010};
    case 'K': return {0b101, 0b101, 0b110, 0b101, 0b101};
    case 'L': return {0b100, 0b100, 0b100, 0b100, 0b111};
    case 'M': return {0b101, 0b111, 0b111, 0b101, 0b101};
    case 'N': return {0b101, 0b111, 0b111, 0b111, 0b101};
    case 'O': return {0b010, 0b101, 0b101, 0b101, 0b010};
    case 'P': return {0b110, 0b101, 0b110, 0b100, 0b100};
    case 'Q': return {0b010, 0b101, 0b101, 0b111, 0b011};
    case 'R': return {0b110, 0b101, 0b110, 0b101, 0b101};
    case 'S': return {0b011, 0b100, 0b010, 0b001, 0b110};
    case 'T': return {0b111, 0b010, 0b010, 0b010, 0b010};
    case 'U': return {0b101, 0b101, 0b101, 0b101, 0b111};
    case 'V': return {0b101, 0b101, 0b101, 0b101, 0b010};
    case 'W': return {0b101, 0b101, 0b111, 0b111, 0b101};
    case 'X': return {0b101, 0b101, 0b010, 0b101, 0b101};
    case 'Y': return {0b101, 0b101, 0b010, 0b010, 0b010};
    case 'Z': return {0b111, 0b001, 0b010, 0b100, 0b111};
    case '0': return {0b111, 0b101, 0b101, 0b101, 0b111};
    case '1': return {0b010, 0b110, 0b010, 0b010, 0b111};
    case '2': return {0b110, 0b001, 0b111, 0b100, 0b111};
    case '3': return {0b110, 0b001, 0b111, 0b001, 0b110};
    case '4': return {0b101, 0b101, 0b111, 0b001, 0b001};
    case '5': return {0b111, 0b100, 0b111, 0b001, 0b110};
    case '6': return {0b011, 0b100, 0b111, 0b101, 0b111};
    case '7': return {0b111, 0b001, 0b001, 0b001, 0b001};
    case '8': return {0b111, 0b101, 0b111, 0b101, 0b111};
    case '-': return {0b000, 0b000, 0b111, 0b000, 0b000};
    case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
    case '$': return {0b010, 0b111, 0b110, 0b011, 0b010};
    case '/': return {0b001, 0b001, 0b010, 0b100, 0b100};
    default: return {0b000, 0b000, 0b000, 0b000, 0b000};
  }
}

void drawGlyph(SDL_Renderer* renderer, int x, int y, char raw, RGB color, int scale) {
  char c = raw;
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }

  const std::array<uint8_t, 5> rows = glyphRows(c);
  for (int ry = 0; ry < 5; ++ry) {
    for (int rx = 0; rx < 3; ++rx) {
      const bool on = (rows[ry] & (1 << (2 - rx))) != 0;
      if (!on) {
        continue;
      }
      drawFilledRect(renderer, x + rx * scale, y + ry * scale, scale, scale, color, 255);
    }
  }
}

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text, RGB color, int scale = 2) {
  // The original 1x 3-by-5 glyphs are only five physical pixels tall and are
  // effectively unreadable on Retina/high-density displays. Promote only
  // compact labels to 2x by default; headings already passed 2x-4x explicitly
  // and must retain their layout dimensions. F2 toggles the legacy compact
  // mode for unusually small windows.
  const int effectiveScale = (gReadableUiText && scale == 1) ? 2 : scale;
  int cursorX = x;
  for (char c : text) {
    if (c == ' ') {
      cursorX += effectiveScale * 2;
      continue;
    }
    drawGlyph(renderer, cursorX, y, c, color, effectiveScale);
    cursorX += (3 * effectiveScale) + effectiveScale;
  }
}

constexpr int kPaletteButtonWidth = 112;
constexpr int kPaletteButtonHeight = 42;
constexpr int kPaletteGap = 8;
constexpr int kPaletteBottomMargin = 14;

int paletteWidth() {
  return (4 * kPaletteButtonWidth) + (3 * kPaletteGap);
}

SDL_Rect paletteButtonRect(int index, int windowWidth, int windowHeight) {
  const int startX = (windowWidth - paletteWidth()) / 2;
  const int y = windowHeight - kPaletteBottomMargin - kPaletteButtonHeight;
  return {startX + index * (kPaletteButtonWidth + kPaletteGap), y,
          kPaletteButtonWidth, kPaletteButtonHeight};
}

PaletteTool paletteHitTest(int mouseX, int mouseY, int windowWidth, int windowHeight) {
  const std::array<PaletteTool, 4> tools = {
    PaletteTool::Road, PaletteTool::Zone, PaletteTool::Bulldoze, PaletteTool::Service
  };
  for (size_t i = 0; i < tools.size(); ++i) {
    const SDL_Rect rect = paletteButtonRect(static_cast<int>(i), windowWidth, windowHeight);
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      return tools[i];
    }
  }
  return PaletteTool::None;
}

void drawToolPalette(
  SDL_Renderer* renderer,
  int windowWidth,
  int windowHeight,
  bool roadActive,
  bool zoneActive,
  bool bulldozeActive,
  bool serviceActive,
  int mouseX,
  int mouseY
) {
  const std::array<const char*, 4> labels = {"R ROAD", "Z ZONE", "B BULL", "S SERVICE"};
  const std::array<bool, 4> active = {roadActive, zoneActive, bulldozeActive, serviceActive};
  for (size_t i = 0; i < labels.size(); ++i) {
    const SDL_Rect rect = paletteButtonRect(static_cast<int>(i), windowWidth, windowHeight);
    const bool hovered = mouseX >= rect.x && mouseX < rect.x + rect.w
      && mouseY >= rect.y && mouseY < rect.y + rect.h;
    drawFilledRect(renderer, rect.x, rect.y, rect.w, rect.h,
                   active[i] ? RGB{56, 92, 70} : (hovered ? RGB{52, 58, 70} : RGB{34, 38, 46}), 235);
    drawRectOutline(renderer, rect.x, rect.y, rect.w, rect.h,
                    active[i] ? RGB{255, 225, 110}
                              : (hovered ? RGB{185, 205, 235} : RGB{135, 145, 160}), 255);
    drawText(renderer, rect.x + 10, rect.y + 14, labels[i], {235, 238, 242}, 2);
  }
}

enum class HudAction {
  None,
  TogglePause,
  Slow,
  Normal,
  Fast,
  Save,
  Load,
};

constexpr int kHudPanelWidth = 390;
constexpr int kHudPanelHeight = 222;

SDL_Rect hudControlRect(HudAction action, int windowWidth) {
  const int panelX = windowWidth - kHudPanelWidth - 12;
  constexpr int y = 158;
  switch (action) {
    case HudAction::TogglePause: return {panelX + 10, y, 72, 24};
    case HudAction::Slow: return {panelX + 92, y, 42, 24};
    case HudAction::Normal: return {panelX + 140, y, 42, 24};
    case HudAction::Fast: return {panelX + 188, y, 42, 24};
    case HudAction::Save: return {panelX + 10, y + 30, 106, 24};
    case HudAction::Load: return {panelX + 124, y + 30, 106, 24};
    default: return {0, 0, 0, 0};
  }
}

HudAction hudHitTest(int mouseX, int mouseY, int windowWidth) {
  const std::array<HudAction, 6> actions = {
    HudAction::TogglePause, HudAction::Slow, HudAction::Normal, HudAction::Fast,
    HudAction::Save, HudAction::Load
  };
  for (const HudAction action : actions) {
    const SDL_Rect rect = hudControlRect(action, windowWidth);
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      return action;
    }
  }
  return HudAction::None;
}

void drawDemandBar(SDL_Renderer* renderer, int x, int y, float demand, RGB color, const char* label) {
  constexpr int width = 180;
  constexpr int height = 10;
  drawText(renderer, x, y + 1, label, {225, 228, 235}, 1);
  drawFilledRect(renderer, x + 18, y, width, height, {40, 44, 52}, 255);
  const int fill = static_cast<int>(std::max(0.0f, std::min(1.0f, demand)) * width);
  drawFilledRect(renderer, x + 18, y, fill, height, color, 255);
  drawRectOutline(renderer, x + 18, y, width, height, {125, 135, 150}, 255);
}

void drawGameplayHud(
  SDL_Renderer* renderer,
  int windowWidth,
  uint32_t tick,
  bool paused,
  uint32_t population,
  size_t buildings,
  int64_t funds,
  const std::string& toolLabel,
  const ZoneDemand& demand,
  uint32_t tickIntervalMs,
  int64_t treasuryRevenue,
  int64_t treasuryExpenses,
  int64_t treasuryNet,
  bool lowFunds,
  bool bankrupt,
  const WasteSummary& waste,
  const DeathcareSummary& deathcare,
  int mouseX,
  int mouseY
) {
  const int x = windowWidth - kHudPanelWidth - 12;
  constexpr int y = 12;
  drawFilledRect(renderer, x, y, kHudPanelWidth, kHudPanelHeight, {14, 16, 20}, 210);
  drawRectOutline(renderer, x, y, kHudPanelWidth, kHudPanelHeight, {190, 198, 210}, 240);
  drawText(renderer, x + 12, y + 10, paused ? "PAUSED" : "LIVE", paused ? RGB{255, 190, 70} : RGB{90, 220, 125}, 2);
  drawText(renderer, x + 12, y + 30, "TICK " + std::to_string(tick), {225, 228, 235}, 2);
  drawText(renderer, x + 12, y + 50, "POP " + std::to_string(population), {225, 228, 235}, 2);
  drawText(renderer, x + 12, y + 70, "BLDG " + std::to_string(buildings), {225, 228, 235}, 2);
  drawText(renderer, x + 122, y + 30, "$" + std::to_string(funds), {255, 220, 95}, 2);
  drawText(renderer, x + 122, y + 54, toolLabel, {160, 205, 255}, 1);

  drawDemandBar(renderer, x + 12, y + 92, demand.residential, zoneColor(1), "R");
  drawDemandBar(renderer, x + 12, y + 106, demand.commercial, zoneColor(2), "C");
  drawDemandBar(renderer, x + 12, y + 120, demand.industrial, zoneColor(3), "I");
  drawDemandBar(renderer, x + 12, y + 134, demand.office, zoneColor(5), "O");
  drawText(renderer, x + 244, y + 92, "IN $" + std::to_string(treasuryRevenue), {105, 230, 135}, 1);
  drawText(renderer, x + 244, y + 108, "OUT $" + std::to_string(treasuryExpenses), {255, 145, 120}, 1);
  drawText(renderer, x + 244, y + 124, "NET $" + std::to_string(treasuryNet),
           treasuryNet >= 0 ? RGB{105, 230, 135} : RGB{255, 125, 110}, 1);
  if (bankrupt) {
    drawText(renderer, x + 244, y + 142, "DEFICIT UNFUNDED", {255, 90, 80}, 1);
  } else if (lowFunds) {
    drawText(renderer, x + 244, y + 142, "LOW FUNDS", {255, 190, 70}, 1);
  }
  drawText(renderer, x + 244, y + 158,
           "WASTE " + std::to_string(static_cast<int>(waste.collectionRate * 100.0f)),
           waste.collectionRate >= 0.9f ? RGB{105, 230, 135} : RGB{255, 150, 120}, 1);
  drawText(renderer, x + 244, y + 174,
           "DECEASED " + std::to_string(deathcare.awaitingDisposition),
           deathcare.awaitingDisposition == 0 ? RGB{160, 175, 165} : RGB{255, 150, 120}, 1);

  const std::array<std::pair<HudAction, const char*>, 6> controls = {{
    {HudAction::TogglePause, paused ? "PLAY" : "PAUSE"},
    {HudAction::Slow, "1X"},
    {HudAction::Normal, "2X"},
    {HudAction::Fast, "3X"},
    {HudAction::Save, "F5 SAVE"},
    {HudAction::Load, "F9 LOAD"},
  }};
  for (const auto& [action, label] : controls) {
    const SDL_Rect rect = hudControlRect(action, windowWidth);
    const bool hovered = mouseX >= rect.x && mouseX < rect.x + rect.w
      && mouseY >= rect.y && mouseY < rect.y + rect.h;
    const bool active = (action == HudAction::Slow && tickIntervalMs == 700)
      || (action == HudAction::Normal && tickIntervalMs == 350)
      || (action == HudAction::Fast && tickIntervalMs == 120);
    drawFilledRect(renderer, rect.x, rect.y, rect.w, rect.h,
                   active ? RGB{58, 92, 72} : (hovered ? RGB{55, 62, 75} : RGB{38, 42, 50}), 255);
    drawRectOutline(renderer, rect.x, rect.y, rect.w, rect.h,
                    active ? RGB{255, 225, 110}
                           : (hovered ? RGB{185, 205, 235} : RGB{125, 135, 150}), 255);
    drawText(renderer, rect.x + 7, rect.y + 8, label, {235, 238, 242}, 1);
  }
}

struct ToastNotification {
  std::string message;
  RGB color{235, 238, 242};
  uint32_t expiresAtMs = 0;
};

void drawToast(
  SDL_Renderer* renderer,
  int windowWidth,
  const ToastNotification& toast,
  uint32_t nowMs
) {
  if (toast.message.empty() || nowMs >= toast.expiresAtMs) {
    return;
  }
  const int width = std::min(620, std::max(180, 28 + static_cast<int>(toast.message.size()) * 8));
  const int x = (windowWidth - width) / 2;
  constexpr int y = 18;
  drawFilledRect(renderer, x, y, width, 34, {16, 18, 22}, 235);
  drawRectOutline(renderer, x, y, width, 34, toast.color, 255);
  drawText(renderer, x + 12, y + 12, toast.message, toast.color, 2);
}

void drawPlacementWarning(
  SDL_Renderer* renderer,
  int mouseX,
  int mouseY,
  int windowWidth,
  int windowHeight,
  const std::string& warning
) {
  if (warning.empty()) {
    return;
  }
  const int width = std::min(360, std::max(130, 20 + static_cast<int>(warning.size()) * 4));
  const int x = std::max(4, std::min(windowWidth - width - 4, mouseX + 16));
  const int y = std::max(4, std::min(windowHeight - 28, mouseY + 18));
  drawFilledRect(renderer, x, y, width, 24, {28, 18, 18}, 235);
  drawRectOutline(renderer, x, y, width, 24, {235, 75, 75}, 255);
  drawText(renderer, x + 8, y + 8, warning, {255, 185, 175}, 1);
}

void drawUiTooltip(
  SDL_Renderer* renderer,
  int mouseX,
  int mouseY,
  int windowWidth,
  int windowHeight,
  const std::string& message
) {
  if (message.empty()) {
    return;
  }
  const int glyphWidth = gReadableUiText ? 8 : 4;
  const int width = std::min(620, std::max(180, 20 + static_cast<int>(message.size()) * glyphWidth));
  const int x = std::max(4, std::min(windowWidth - width - 4, mouseX + 16));
  const int preferredY = mouseY + 20;
  const int y = preferredY + 30 < windowHeight ? preferredY : mouseY - 36;
  drawFilledRect(renderer, x, y, width, 28, {20, 25, 32}, 245);
  drawRectOutline(renderer, x, y, width, 28, {145, 190, 245}, 255);
  drawText(renderer, x + 8, y + 9, message, {220, 232, 248}, 1);
}

void drawOnboarding(SDL_Renderer* renderer, int windowHeight, int step) {
  if (step < 0 || step > 5) {
    return;
  }
  constexpr int x = 14;
  const int y = windowHeight - 154;
  constexpr int width = 350;
  constexpr int height = 92;
  drawFilledRect(renderer, x, y, width, height, {16, 20, 26}, 235);
  drawRectOutline(renderer, x, y, width, height, {120, 205, 255}, 255);
  drawText(renderer, x + 12, y + 10, "CITY GUIDE", {120, 205, 255}, 2);
  drawText(renderer, x + 250, y + 12, "F1 HIDE", {155, 165, 180}, 1);
  switch (step) {
    case 0:
      drawText(renderer, x + 12, y + 38, "1 BUILD A ROAD", {235, 238, 242}, 2);
      drawText(renderer, x + 12, y + 62, "SELECT ROAD THEN DRAG", {175, 185, 200}, 1);
      break;
    case 1:
      drawText(renderer, x + 12, y + 38, "2 ZONE LAND", {235, 238, 242}, 2);
      drawText(renderer, x + 12, y + 62, "DRAG A ZONE BESIDE ROAD", {175, 185, 200}, 1);
      break;
    case 2:
      drawText(renderer, x + 12, y + 38, "3 START TIME", {235, 238, 242}, 2);
      drawText(renderer, x + 12, y + 62, "CLICK PLAY OR PRESS SPACE", {175, 185, 200}, 1);
      break;
    case 3:
      drawText(renderer, x + 12, y + 38, "4 PLACE POWER", {235, 238, 242}, 2);
      drawText(renderer, x + 12, y + 62, "CYCLE SERVICE TO POWER", {175, 185, 200}, 1);
      break;
    case 4:
      drawText(renderer, x + 12, y + 38, "5 PLACE WATER", {235, 238, 242}, 2);
      drawText(renderer, x + 12, y + 62, "CYCLE SERVICE TO WATER", {175, 185, 200}, 1);
      break;
    case 5:
      drawText(renderer, x + 12, y + 38, "6 PLACE A CIVIC SERVICE", {235, 238, 242}, 2);
      drawText(renderer, x + 12, y + 62, "FIRE POLICE HEALTH OR SCHOOL", {175, 185, 200}, 1);
      break;
    default:
      break;
  }
}

const char* buildingTypeLabel(BuildingType type) {
  switch (type) {
    case BuildingType::Residential: return "RESIDENTIAL";
    case BuildingType::Commercial: return "COMMERCIAL";
    case BuildingType::Industrial: return "INDUSTRIAL";
    case BuildingType::Office: return "OFFICE";
    default: return "UNKNOWN";
  }
}

void drawTileInspector(
  SDL_Renderer* renderer,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  Coord coord
) {
  if (!map.isValid(coord)) {
    return;
  }
  constexpr int x = 14;
  constexpr int y = 170;
  constexpr int width = 390;
  constexpr int height = 132;
  drawFilledRect(renderer, x, y, width, height, {14, 17, 22}, 225);
  drawRectOutline(renderer, x, y, width, height, {150, 160, 175}, 240);

  const Tile& tile = map.getTile(coord);
  const char* terrain = tile.type == 2 ? "WATER" : (tile.type == 1 ? "ROUGH" : "LAND");
  drawText(renderer, x + 10, y + 9,
           "TILE " + std::to_string(coord.x) + " " + std::to_string(coord.y),
           {120, 205, 255}, 2);
  drawText(renderer, x + 10, y + 31,
           std::string(terrain) + "  ZONE " + Zoning::zoneToString(map.zone(coord)),
           {220, 225, 232}, 1);
  drawText(renderer, x + 10, y + 47,
           "VALUE " + std::to_string(static_cast<int>(map.landValue(coord)))
             + "  POLL " + std::to_string(static_cast<int>(map.pollution(coord) * 100.0f)),
           {190, 198, 210}, 1);

  drawText(renderer, x + 10, y + 63,
           std::string("POWER ") + (tile.connectedToPower ? "YES" : "NO")
             + "  WATER " + (tile.connectedToWater ? "YES" : "NO"),
           (tile.connectedToPower && tile.connectedToWater) ? RGB{135, 220, 150} : RGB{255, 150, 120}, 1);
  if (tile.hasRoad) {
    drawText(renderer, x + 10, y + 79,
             "ROAD  CONGEST " + std::to_string(static_cast<int>(localCongestionAtTile(roads, coord) * 100.0f)),
             {185, 190, 200}, 1);
  }
  if (tile.buildingId != 0) {
    const Building* building = store.getBuilding(tile.buildingId);
    if (building != nullptr) {
      drawText(renderer, x + 10, y + 95,
               std::string("BLDG ") + buildingTypeLabel(building->type) + " "
                 + std::to_string(building->occupancy) + "/" + std::to_string(building->capacity),
               {210, 225, 210}, 1);
    }
  }
  for (const ServiceFacility& facility : facilities) {
    if (facility.position == coord) {
      drawText(renderer, x + 10, y + 111,
               std::string("SERVICE ") + ServiceSystem::serviceTypeToString(facility.type)
                 + " RANGE " + std::to_string(facility.maxTravelDistance),
               serviceFacilityColor(facility.type), 1);
      break;
    }
  }
}

enum class QuitAction {
  None,
  SaveAndQuit,
  QuitWithoutSaving,
  Cancel,
};

SDL_Rect quitButtonRect(QuitAction action, int windowWidth, int windowHeight) {
  const int y = windowHeight / 2 + 46;
  switch (action) {
    case QuitAction::SaveAndQuit: return {windowWidth / 2 - 255, y, 160, 42};
    case QuitAction::QuitWithoutSaving: return {windowWidth / 2 - 80, y, 190, 42};
    case QuitAction::Cancel: return {windowWidth / 2 + 125, y, 130, 42};
    default: return {0, 0, 0, 0};
  }
}

QuitAction quitHitTest(int mouseX, int mouseY, int windowWidth, int windowHeight) {
  const std::array<QuitAction, 3> actions = {
    QuitAction::SaveAndQuit, QuitAction::QuitWithoutSaving, QuitAction::Cancel
  };
  for (const QuitAction action : actions) {
    const SDL_Rect rect = quitButtonRect(action, windowWidth, windowHeight);
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      return action;
    }
  }
  return QuitAction::None;
}

void drawQuitDialog(
  SDL_Renderer* renderer,
  int windowWidth,
  int windowHeight,
  int mouseX,
  int mouseY,
  bool dirty
) {
  drawFilledRect(renderer, 0, 0, windowWidth, windowHeight, {4, 5, 7}, 175);
  const int x = windowWidth / 2 - 290;
  const int y = windowHeight / 2 - 100;
  drawFilledRect(renderer, x, y, 580, 220, {20, 23, 29}, 255);
  drawRectOutline(renderer, x, y, 580, 220, {185, 195, 210}, 255);
  drawText(renderer, x + 155, y + 28, "QUIT CITY", {235, 238, 242}, 3);
  drawText(renderer, x + 110, y + 78,
           dirty ? "UNSAVED CHANGES" : "CITY IS SAVED",
           dirty ? RGB{255, 190, 70} : RGB{105, 230, 135}, 2);

  const std::array<std::pair<QuitAction, const char*>, 3> buttons = {{
    {QuitAction::SaveAndQuit, "SAVE QUIT"},
    {QuitAction::QuitWithoutSaving, "QUIT NO SAVE"},
    {QuitAction::Cancel, "CANCEL"},
  }};
  for (const auto& [action, label] : buttons) {
    const SDL_Rect rect = quitButtonRect(action, windowWidth, windowHeight);
    const bool hovered = mouseX >= rect.x && mouseX < rect.x + rect.w
      && mouseY >= rect.y && mouseY < rect.y + rect.h;
    drawFilledRect(renderer, rect.x, rect.y, rect.w, rect.h,
                   hovered ? RGB{55, 62, 75} : RGB{36, 40, 48}, 255);
    drawRectOutline(renderer, rect.x, rect.y, rect.w, rect.h,
                    hovered ? RGB{205, 220, 245} : RGB{125, 135, 150}, 255);
    drawText(renderer, rect.x + 12, rect.y + 15, label, {235, 238, 242}, 1);
  }
}

void drawSwatch(SDL_Renderer* renderer, int x, int y, RGB color, bool active = false) {
  drawFilledRect(renderer, x, y, 18, 12, color, 255);
  drawRectOutline(renderer, x, y, 18, 12, active ? RGB{255, 230, 120} : RGB{220, 220, 220}, 255);
}

void drawZoneLegend(SDL_Renderer* renderer, int x, int y) {
  // Categories shown in zone overlay rendering path.
  drawText(renderer, x, y - 12, "ZONE", {230, 230, 230}, 2);
  drawSwatch(renderer, x + 0, y, zoneColor(1));       // Residential
  drawSwatch(renderer, x + 42, y, zoneColor(2));      // Commercial
  drawSwatch(renderer, x + 84, y, zoneColor(3));      // Industrial
  drawSwatch(renderer, x + 126, y, zoneColor(5));     // Office
  drawSwatch(renderer, x + 168, y, {64, 64, 64});     // Road
  drawText(renderer, x + 0, y + 16, "RES", {205, 220, 205}, 1);
  drawText(renderer, x + 42, y + 16, "COM", {205, 220, 205}, 1);
  drawText(renderer, x + 84, y + 16, "IND", {205, 220, 205}, 1);
  drawText(renderer, x + 126, y + 16, "OFF", {205, 220, 205}, 1);
  drawText(renderer, x + 168, y + 16, "ROAD", {205, 220, 205}, 1);

  // Building colors are distinct from zoning base colors.
  drawText(renderer, x + 232, y - 12, "BLDG", {230, 230, 230}, 2);
  drawSwatch(renderer, x + 232, y, {44, 132, 70});    // Residential building
  drawSwatch(renderer, x + 274, y, {31, 84, 163});    // Commercial building
  drawSwatch(renderer, x + 316, y, {179, 93, 29});    // Industrial building
  drawSwatch(renderer, x + 358, y, {147, 112, 219});  // Office building
  drawText(renderer, x + 232, y + 16, "R", {205, 220, 205}, 1);
  drawText(renderer, x + 274, y + 16, "C", {205, 220, 205}, 1);
  drawText(renderer, x + 316, y + 16, "I", {205, 220, 205}, 1);
  drawText(renderer, x + 358, y + 16, "O", {205, 220, 205}, 1);
}

void drawSteppedLegend(SDL_Renderer* renderer, int x, int y, OverlayMode mode) {
  drawText(renderer, x, y - 12, overlayModeName(mode), {230, 230, 230}, 2);

  // Five-step discrete scale for numeric overlays.
  for (int i = 0; i < 5; ++i) {
    const float t = static_cast<float>(i) / 4.0f;
    drawSwatch(renderer, x + i * 24, y, overlaySampleColor(mode, t), i == 2);
  }

  // Low and high anchors are repeated as larger blocks for quick glance.
  drawFilledRect(renderer, x + 140, y - 1, 20, 14, overlaySampleColor(mode, 0.0f), 255);
  drawRectOutline(renderer, x + 140, y - 1, 20, 14, {220, 220, 220}, 255);
  drawFilledRect(renderer, x + 166, y - 1, 20, 14, overlaySampleColor(mode, 1.0f), 255);
  drawRectOutline(renderer, x + 166, y - 1, 20, 14, {220, 220, 220}, 255);

  drawText(renderer, x + 140, y + 16, "LOW", {205, 220, 205}, 1);
  drawText(renderer, x + 166, y + 16, "HIGH", {205, 220, 205}, 1);
  drawText(renderer, x + 58, y + 16, "MID", {205, 220, 205}, 1);
}

void drawLegendPanel(
  SDL_Renderer* renderer,
  OverlayMode overlayMode,
  int windowWidth,
  int windowHeight,
  int mouseX,
  int mouseY
) {
  (void)windowWidth;
  (void)windowHeight;

  constexpr int panelX = kOverlayPanelX;
  constexpr int panelY = kOverlayPanelY;
  constexpr int panelW = 520;
  constexpr int panelH = 146;
  drawFilledRect(renderer, panelX, panelY, panelW, panelH, {14, 16, 20}, 190);
  drawRectOutline(renderer, panelX, panelY, panelW, panelH, {230, 230, 230}, 220);

  // Overlay hotkey strip. Active mode gets bright outline.
  constexpr int keyStartX = kOverlayKeyStartX;
  constexpr int keyY = kOverlayKeyY;
  constexpr int keyW = kOverlayKeyWidth;
  constexpr int keyH = kOverlayKeyHeight;
  constexpr int keyGap = kOverlayKeyGap;
  const auto& modes = overlayModes();

  for (size_t i = 0; i < modes.size(); ++i) {
    const int x = keyStartX + static_cast<int>(i) * (keyW + keyGap);
    const RGB marker = overlaySampleColor(modes[i], 0.75f);
    const bool hovered = mouseX >= x && mouseX < x + keyW
      && mouseY >= keyY && mouseY < keyY + keyH;
    drawFilledRect(renderer, x, keyY, keyW, keyH,
                   hovered ? RGB{58, 65, 78} : RGB{42, 46, 54}, 220);
    drawFilledRect(renderer, x + 4, keyY + 4, 10, keyH - 8, marker, 255);

    const bool active = (modes[i] == overlayMode);
    drawRectOutline(renderer, x, keyY, keyW, keyH,
                    active ? RGB{255, 230, 120}
                           : (hovered ? RGB{185, 205, 235} : RGB{120, 130, 145}), 255);

    drawText(renderer, x + 22, keyY + 6, std::to_string(i + 1), {230, 232, 238}, 2);
  }

  // Mid strip shows overlay-specific legend swatches.
  constexpr int legendX = panelX + 10;
  constexpr int legendY = panelY + 48;
  if (overlayMode == OverlayMode::Zone) {
    drawZoneLegend(renderer, legendX, legendY);
  } else {
    drawSteppedLegend(renderer, legendX, legendY, overlayMode);
  }

  // Lower strip shows continuous scale hints for numeric overlays.
  constexpr int barX = panelX + 10;
  constexpr int barY = panelY + 102;
  constexpr int barW = 280;
  constexpr int barH = 14;
  if (overlayMode == OverlayMode::Zone) {
    drawFilledRect(renderer, barX, barY, barW, barH, {36, 40, 48}, 255);
    drawRectOutline(renderer, barX, barY, barW, barH, {220, 220, 220}, 255);
    drawText(renderer, barX, barY + 18, "CATEGORICAL", {205, 220, 205}, 1);
  } else {
    drawGradientBar(renderer, barX, barY, barW, barH, overlayMode);
    drawText(renderer, barX, barY + 18, "CONTINUOUS", {205, 220, 205}, 1);
  }

  // Low/high indicator blocks for quick visual reference.
  const RGB low = overlayMode == OverlayMode::Zone ? zoneColor(1) : overlaySampleColor(overlayMode, 0.0f);
  const RGB high = overlayMode == OverlayMode::Zone ? RGB{64, 64, 64} : overlaySampleColor(overlayMode, 1.0f);
  drawFilledRect(renderer, barX + barW + 10, barY, 16, barH, low, 255);
  drawRectOutline(renderer, barX + barW + 10, barY, 16, barH, {220, 220, 220}, 255);
  drawFilledRect(renderer, barX + barW + 32, barY, 16, barH, high, 255);
  drawRectOutline(renderer, barX + barW + 32, barY, 16, barH, {220, 220, 220}, 255);

  // Tiny indicator for legend toggle state and command location.
  drawFilledRect(renderer, panelX + panelW - 72, panelY + panelH - 20, 60, 10, {80, 150, 110}, 220);
  drawRectOutline(renderer, panelX + panelW - 72, panelY + panelH - 20, 60, 10, {220, 220, 220}, 255);

  if (overlayMode == OverlayMode::RouteHeatmap) {
    drawText(renderer, panelX + 312, panelY + 104, "O ORIGIN", {205, 220, 205}, 1);
    drawText(renderer, panelX + 312, panelY + 116, "D DEST", {205, 220, 205}, 1);
    drawText(renderer, panelX + 312, panelY + 128, "C CLEAR", {205, 220, 205}, 1);
  }
}

void updatePlayableUtilityConnectivity(
  CityMap& map,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities
) {
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);
  const Coord dims = map.getDimensions();
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      Tile& tile = map.getTile({x, y});
      if (tile.type == 2) {
        tile.connectedToPower = false;
        tile.connectedToWater = false;
        continue;
      }
      Coord anchor;
      const bool anchored = roads.resolveRoadAnchor({x, y}, anchor);
      tile.connectedToPower = anchored && cache.nearestPowerDistance.count(anchor) != 0;
      tile.connectedToWater = anchored && cache.nearestWaterDistance.count(anchor) != 0;
    }
  }
}

void runSimulationTick(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  LiveSimulationState& liveState,
  int64_t& funds
) {
  const uint32_t tickSeed = 1000u + (liveState.tick * 31u);

  updatePlayableUtilityConnectivity(map, roads, facilities);
  liveState.demand = Zoning::calculateDemand(tickSeed);
  GrowthSystem::runStep(
    map, store, liveState.demand, tickSeed + 1u, 0.18f,
    nullptr, {-1, -1}, {-1, -1}, nullptr, true
  );

  const uint32_t requestedPopulation = liveState.populationTarget;
  PopulationSystem::allocate(store, population, requestedPopulation, tickSeed + 2u);

  liveState.trafficSummary = TrafficSystem::simulateCommutes(store, population, roads, tickSeed + 3u);
  liveState.serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
  liveState.waste = WasteSystem::evaluate(store, facilities, liveState.serviceSummary);
  if (liveState.waste.pollutionPenalty > 0.0f) {
    const Coord dims = map.getDimensions();
    for (int y = 0; y < dims.y; ++y) {
      for (int x = 0; x < dims.x; ++x) {
        const Coord coord{x, y};
        if (map.zone(coord) != 0) {
          map.pollution(coord) = std::min(1.0f,
            map.pollution(coord) + liveState.waste.pollutionPenalty * 0.01f);
        }
      }
    }
  }
  float pollutionTotal = 0.0f;
  int pollutionTiles = 0;
  const Coord deathDims = map.getDimensions();
  for (int y = 0; y < deathDims.y; ++y) {
    for (int x = 0; x < deathDims.x; ++x) {
      if (map.zone({x, y}) != 0) {
        pollutionTotal += map.pollution({x, y});
        ++pollutionTiles;
      }
    }
  }
  const float averagePollution = pollutionTiles > 0 ? pollutionTotal / pollutionTiles : 0.0f;
  const float illnessRate = std::min(1.0f,
    (0.06f + averagePollution * 0.5f) * (1.0f - liveState.serviceSummary.healthCoverage * 0.8f));
  liveState.deathcare = DeathcareSystem::step(
    population, facilities, liveState.serviceSummary, illnessRate,
    averagePollution, liveState.deathcareState
  );
  if (liveState.deathcare.deaths > 0) {
    liveState.populationTarget = liveState.populationTarget > liveState.deathcare.deaths
      ? liveState.populationTarget - liveState.deathcare.deaths : 0;
    PopulationSystem::allocate(
      store, population, population.getTotalPopulation(), tickSeed + 5u
    );
  }
  liveState.economy = EconomySystem::calculateEconomy(store, population, TaxRates{}, &map);
  int64_t serviceOperatingCosts = 0;
  for (const ServiceFacility& facility : facilities) {
    serviceOperatingCosts += ServiceTool::operatingCostPerTick(facility.type);
  }
  const TreasuryFlow flow = TreasurySystem::applyEconomy(
    liveState.economy, funds, 0.01, serviceOperatingCosts
  );
  liveState.treasuryRevenue = flow.revenue;
  liveState.treasuryExpenses = flow.expenses;
  liveState.treasuryNet = flow.net;
  liveState.treasuryShortfall = flow.shortfall;
  liveState.lowFunds = funds > 0 && funds < 5000;
  liveState.bankrupt = funds == 0 && flow.shortfall > 0;
  refreshRouteHeat(store, population, roads, liveState, tickSeed + 3u);

  ++liveState.tick;
}

std::string makeHudTitle(
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population,
  const LiveSimulationState& liveState,
  int tilePixels,
  int viewX,
  int viewY,
  OverlayMode overlayMode,
  const RouteDiagnosticsFilter& routeFilter,
  bool roadToolActive,
  const RoadPlan& roadPlan,
  bool zoneToolActive,
  ZoneType selectedZone,
  const ZonePlan& zonePlan,
  bool bulldozeToolActive,
  const BulldozePlan& bulldozePlan,
  bool serviceToolActive,
  ServiceType selectedService,
  const ServicePlan& servicePlan,
  int64_t funds
) {
  const auto& buildings = store.getBuildings();
  size_t residential = 0;
  size_t commercial = 0;
  size_t industrial = 0;
  size_t office = 0;
  for (const auto& entry : buildings) {
    switch (entry.second.type) {
      case BuildingType::Residential:
        ++residential;
        break;
      case BuildingType::Commercial:
        ++commercial;
        break;
      case BuildingType::Industrial:
        ++industrial;
        break;
      case BuildingType::Office:
        ++office;
        break;
    }
  }

  std::ostringstream oss;
  oss << "UrbanSimCore Visualizer | "
      << "Tick:" << liveState.tick << (liveState.paused ? "(paused)" : "(live)")
      << " | "
      << "Buildings:" << store.getBuildingCount() << " (R:" << residential
      << " C:" << commercial << " I:" << industrial << " O:" << office << ")"
      << " | Roads:" << roads.getRoadCount()
      << " | Pop:" << population.getTotalPopulation()
      << " | Commute:" << liveState.trafficSummary.averageCommuteTime
      << " | Service:" << static_cast<int>(liveState.serviceSummary.overallCoverage * 100.0f) << "%"
      << " | Zoom:" << tilePixels
      << " | View:" << viewX << "," << viewY
      << " | Overlay:" << overlayModeName(overlayMode)
      << " | RouteFilter:" << routeFilterLabel(routeFilter)
      << " | Funds:$" << funds
      << " | Tool:";
  if (roadToolActive) {
    oss << "ROAD";
  } else if (zoneToolActive) {
    oss << "ZONE-" << Zoning::zoneToString(static_cast<int>(selectedZone));
  } else if (bulldozeToolActive) {
    oss << "BULLDOZE";
  } else if (serviceToolActive) {
    oss << "SERVICE-" << ServiceSystem::serviceTypeToString(selectedService);
  } else {
    oss << "NONE";
  }
  if (!roadPlan.tiles.empty()) {
    oss << " (" << (roadPlan.valid ? "$" + std::to_string(roadPlan.cost) : roadPlan.error) << ")";
  } else if (!zonePlan.tiles.empty()) {
    oss << " (" << (zonePlan.valid ? "$" + std::to_string(zonePlan.cost) : zonePlan.error) << ")";
  } else if (!bulldozePlan.tiles.empty()) {
    oss << " (" << (bulldozePlan.valid ? "$" + std::to_string(bulldozePlan.cost) : bulldozePlan.error) << ")";
  } else if (serviceToolActive && servicePlan.hasSite) {
    oss << " (" << (servicePlan.valid ? "$" + std::to_string(servicePlan.cost) : servicePlan.error) << ")";
  }
  oss << " [R road | Z zone | B bulldoze | S service/cycle | click place]";
  return oss.str();
}

[[maybe_unused]] bool seedScenario(CityMap& map, RoadNetwork& roads, EntityStore& store, PopulationStore& population) {
  const glm::ivec2 dims = map.getDimensions();
  if (dims.x < 8 || dims.y < 8) {
    return false;
  }

  Zoning::applyZoneRect(map, {2, 2}, {dims.x / 2 - 1, dims.y - 3}, ZoneType::Residential);
  Zoning::applyZoneRect(map, {dims.x / 2 + 1, 2}, {dims.x - 3, dims.y / 2}, ZoneType::Commercial);
  Zoning::applyZoneRect(map, {dims.x / 2 + 1, dims.y / 2 + 1}, {dims.x - 3, dims.y - 3}, ZoneType::Industrial);

  const int midY = dims.y / 2;
  for (int x = 2; x < dims.x - 2; ++x) {
    map.getTile({x, midY}).type = 0;
    map.getTile({x + 1, midY}).type = 0;
    roads.buildRoad({x, midY}, {x + 1, midY});
  }

  const int midX = dims.x / 2;
  for (int y = 2; y < dims.y - 2; ++y) {
    map.getTile({midX, y}).type = 0;
    map.getTile({midX, y + 1}).type = 0;
    roads.buildRoad({midX, y}, {midX, y + 1});
  }

  roads.updateConnectivity({midX, midY});

  constexpr uint32_t seed = 42;
  for (int i = 0; i < 12; ++i) {
    const ZoneDemand demand = Zoning::calculateDemand(seed + static_cast<uint32_t>(i));
    GrowthSystem::runStep(map, store, demand, seed + static_cast<uint32_t>(i), 0.45f);
  }

  // Seed guaranteed commute-capable buildings directly on the road spine for live traffic overlays.
  const int maxOffset = std::max(2, std::min(midX, midY) - 2);
  const int resOffset = std::min(8, maxOffset);
  const int comOffset = std::min(10, maxOffset);
  const int indOffset = std::min(16, maxOffset);
  const std::array<Coord, 6> buildingSites = {{
    {midX, midY - resOffset}, {midX, midY + resOffset},
    {midX - comOffset, midY}, {midX + comOffset, midY},
    {midX - indOffset, midY}, {midX + indOffset, midY}
  }};
  for (const Coord site : buildingSites) {
    map.getTile(site).type = 0;
  }
  const EntityId resA = store.createBuilding(BuildingType::Residential, buildingSites[0], 90);
  const EntityId resB = store.createBuilding(BuildingType::Residential, buildingSites[1], 75);
  const EntityId comA = store.createBuilding(BuildingType::Commercial, buildingSites[2], 80);
  const EntityId comB = store.createBuilding(BuildingType::Commercial, buildingSites[3], 60);
  const EntityId indA = store.createBuilding(BuildingType::Industrial, buildingSites[4], 70);
  const EntityId indB = store.createBuilding(BuildingType::Industrial, buildingSites[5], 85);

  const std::array<EntityId, 6> buildingIds = {resA, resB, comA, comB, indA, indB};
  for (size_t i = 0; i < buildingSites.size(); ++i) {
    map.getTile(buildingSites[i]).buildingId = buildingIds[i];
  }

  // Add deterministic land value and pollution gradients so overlays show meaningful variation.
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      const float distX = static_cast<float>(std::abs(x - midX));
      const float distY = static_cast<float>(std::abs(y - midY));
      map.landValue({x, y}) = std::max(40.0f, 190.0f - (distX * 2.0f) - (distY * 1.25f));

      if (x > midX && y > midY) {
        map.pollution({x, y}) = std::min(1.0f, 0.1f + ((distX + distY) / 64.0f));
      } else {
        map.pollution({x, y}) = std::max(0.0f, 0.05f - ((distX + distY) / 128.0f));
      }
    }
  }

  PopulationSystem::allocate(store, population, 480, seed + 99u);
  TrafficSystem::simulateCommutes(store, population, roads, seed + 211u);

  return true;
}

[[maybe_unused]] std::vector<ServiceFacility> seedServiceFacilities(const CityMap& map) {
  const glm::ivec2 dims = map.getDimensions();
  const int midX = dims.x / 2;
  const int midY = dims.y / 2;

  std::vector<ServiceFacility> facilities;
  facilities.push_back(ServiceFacility{ServiceType::Fire, {midX - 6, midY}, 16, 1.0f});
  facilities.push_back(ServiceFacility{ServiceType::Police, {midX + 6, midY}, 14, 1.0f});
  facilities.push_back(ServiceFacility{ServiceType::Health, {midX, midY - 10}, 18, 1.0f});
  facilities.push_back(ServiceFacility{ServiceType::Education, {midX, midY + 10}, 20, 1.0f});
  return facilities;
}

struct StartScreenResult {
  bool quit = false;
  bool loadSession = false;
  int mapSize = 64;
  bool generateTerrain = false;
};

bool pointInRect(int x, int y, const SDL_Rect& rect) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

StartScreenResult runStartScreen(SDL_Renderer* renderer, int windowWidth, int windowHeight) {
  (void)windowHeight;
  StartScreenResult result;
  const SDL_Rect newButton{windowWidth / 2 - 220, 500, 200, 54};
  const SDL_Rect loadButton{windowWidth / 2 + 20, 500, 200, 54};
  const std::array<int, 3> sizes = {32, 64, 96};
  int mouseX = 0;
  int mouseY = 0;
  bool choosing = true;
  while (choosing) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_MOUSEMOTION) {
        mouseX = event.motion.x;
        mouseY = event.motion.y;
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
        mouseX = event.button.x;
        mouseY = event.button.y;
      }
      if (event.type == SDL_QUIT) {
        result.quit = true;
        choosing = false;
      } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          result.quit = true;
          choosing = false;
        } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_n) {
          choosing = false;
        } else if (event.key.keysym.sym == SDLK_l) {
          result.loadSession = true;
          choosing = false;
        } else if (event.key.keysym.sym == SDLK_t) {
          result.generateTerrain = !result.generateTerrain;
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        if (pointInRect(event.button.x, event.button.y, newButton)) {
          choosing = false;
        } else if (pointInRect(event.button.x, event.button.y, loadButton)) {
          result.loadSession = true;
          choosing = false;
        }
        for (size_t i = 0; i < sizes.size(); ++i) {
          const SDL_Rect rect{windowWidth / 2 - 190 + static_cast<int>(i) * 130, 380, 110, 44};
          if (pointInRect(event.button.x, event.button.y, rect)) {
            result.mapSize = sizes[i];
          }
        }
        const SDL_Rect terrainButton{windowWidth / 2 - 130, 444, 260, 38};
        if (pointInRect(event.button.x, event.button.y, terrainButton)) {
          result.generateTerrain = !result.generateTerrain;
        }
      }
    }

    SDL_SetRenderDrawColor(renderer, 20, 23, 29, 255);
    SDL_RenderClear(renderer);
    drawText(renderer, windowWidth / 2 - 205, 130, "URBAN SIM CORE", {120, 205, 255}, 4);
    drawText(renderer, windowWidth / 2 - 155, 185, "CITY BUILDER", {235, 238, 242}, 3);
    drawText(renderer, windowWidth / 2 - 105, 330, "MAP SIZE", {205, 212, 225}, 2);

    for (size_t i = 0; i < sizes.size(); ++i) {
      const SDL_Rect rect{windowWidth / 2 - 190 + static_cast<int>(i) * 130, 380, 110, 44};
      const bool active = result.mapSize == sizes[i];
      const bool hovered = pointInRect(mouseX, mouseY, rect);
      drawFilledRect(renderer, rect.x, rect.y, rect.w, rect.h,
                     active ? RGB{55, 90, 70} : (hovered ? RGB{52, 58, 70} : RGB{36, 40, 48}), 255);
      drawRectOutline(renderer, rect.x, rect.y, rect.w, rect.h,
                      active ? RGB{255, 225, 110}
                             : (hovered ? RGB{185, 205, 235} : RGB{125, 135, 150}), 255);
      drawText(renderer, rect.x + 34, rect.y + 15, std::to_string(sizes[i]), {235, 238, 242}, 2);
    }

    const SDL_Rect terrainButton{windowWidth / 2 - 130, 444, 260, 38};
    const bool terrainHovered = pointInRect(mouseX, mouseY, terrainButton);
    drawFilledRect(renderer, terrainButton.x, terrainButton.y, terrainButton.w, terrainButton.h,
                   result.generateTerrain ? RGB{55, 90, 70}
                                          : (terrainHovered ? RGB{52, 58, 70} : RGB{36, 40, 48}), 255);
    drawRectOutline(renderer, terrainButton.x, terrainButton.y, terrainButton.w, terrainButton.h,
                    result.generateTerrain ? RGB{255, 225, 110}
                                           : (terrainHovered ? RGB{185, 205, 235} : RGB{125, 135, 150}), 255);
    drawText(renderer, terrainButton.x + 14, terrainButton.y + 13,
             result.generateTerrain ? "TERRAIN ON" : "TERRAIN OFF", {235, 238, 242}, 2);

    const bool newHovered = pointInRect(mouseX, mouseY, newButton);
    const bool loadHovered = pointInRect(mouseX, mouseY, loadButton);
    drawFilledRect(renderer, newButton.x, newButton.y, newButton.w, newButton.h,
                   newHovered ? RGB{58, 105, 80} : RGB{45, 82, 62}, 255);
    drawRectOutline(renderer, newButton.x, newButton.y, newButton.w, newButton.h,
                    newHovered ? RGB{210, 245, 220} : RGB{150, 225, 175}, 255);
    drawText(renderer, newButton.x + 38, newButton.y + 20, "NEW CITY", {235, 245, 238}, 2);
    drawFilledRect(renderer, loadButton.x, loadButton.y, loadButton.w, loadButton.h,
                   loadHovered ? RGB{58, 80, 112} : RGB{45, 62, 88}, 255);
    drawRectOutline(renderer, loadButton.x, loadButton.y, loadButton.w, loadButton.h,
                    loadHovered ? RGB{205, 225, 255} : RGB{145, 190, 245}, 255);
    drawText(renderer, loadButton.x + 24, loadButton.y + 20, "LOAD SESSION", {235, 240, 250}, 2);
    drawText(renderer, windowWidth / 2 - 170, 585, "ENTER NEW   L LOAD   T TERRAIN", {150, 160, 175}, 1);
    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }
  return result;
}
} // namespace

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  constexpr int windowWidth = 1280;
  constexpr int windowHeight = 800;
  SDL_Window* window = SDL_CreateWindow(
    "UrbanSimCore Visualizer (SDL2)",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    windowWidth,
    windowHeight,
    SDL_WINDOW_SHOWN
  );
  if (window == nullptr) {
    std::cerr << "Window creation failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == nullptr) {
    std::cerr << "Renderer creation failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  constexpr const char* kSessionPath = "urban_sim_session.json";
  const StartScreenResult start = runStartScreen(renderer, windowWidth, windowHeight);
  if (start.quit) {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  }

  int mapSize = start.mapSize;
  std::string startupError;
  if (start.loadSession) {
    int savedWidth = 0;
    int savedHeight = 0;
    if (!GameplaySessionSystem::readMapDimensions(
          kSessionPath, savedWidth, savedHeight, &startupError)
        || savedWidth != savedHeight) {
      mapSize = 64;
      if (startupError.empty()) {
        startupError = "saved map must be square";
      }
    } else {
      mapSize = savedWidth;
    }
  }

  CityMap map({mapSize, mapSize});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  std::vector<ServiceFacility> facilities;
  LiveSimulationState liveState;
  int64_t initialFunds = 50000;
  bool loadedAtStartup = false;

  if (start.loadSession && startupError.empty()) {
    GameplaySessionState session;
    if (GameplaySessionSystem::load(
          kSessionPath, map, roads, store, population, session, &startupError)) {
      facilities = std::move(session.facilities);
      initialFunds = session.funds;
      liveState.tick = session.tick;
      liveState.paused = session.paused;
      liveState.tickIntervalMs = session.tickIntervalMs;
      liveState.demand = session.demand;
      liveState.treasuryRevenue = session.treasuryRevenue;
      liveState.treasuryExpenses = session.treasuryExpenses;
      liveState.treasuryNet = session.treasuryNet;
      liveState.populationTarget = session.populationTarget;
      liveState.deathcareState.fractionalDeaths = session.fractionalDeaths;
      liveState.deathcareState.awaitingDisposition = session.awaitingDisposition;
      loadedAtStartup = true;
    }
  }

  if (!loadedAtStartup) {
    if (start.generateTerrain) {
      TerrainGenerator::generate(map, 42u);
    }
    // A new game is intentionally empty: the player establishes the first
    // road, zones, and services through the same tools used thereafter.
    facilities.clear();
    liveState.demand = Zoning::calculateDemand(1000u);
    liveState.paused = true;
  }
  updatePlayableUtilityConnectivity(map, roads, facilities);
  liveState.serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
  liveState.trafficSummary = TrafficSystem::simulateCommutes(store, population, roads, 98765u);
  refreshRouteHeat(store, population, roads, liveState, 98765u);

  int tilePixels = 12;
  int viewX = 0;
  int viewY = 0;
  OverlayMode overlayMode = OverlayMode::Zone;
  bool showLegend = true;
  bool roadToolActive = false;
  bool zoneToolActive = false;
  bool bulldozeToolActive = false;
  bool serviceToolActive = false;
  ServiceType selectedService = ServiceType::Fire;
  ZoneType selectedZone = ZoneType::Residential;
  bool toolDragging = false;
  Coord toolDragStart{0, 0};
  RoadPlan roadPlan;
  ZonePlan zonePlan;
  BulldozePlan bulldozePlan;
  ServicePlan servicePlan;
  int64_t funds = initialFunds;
  uint32_t lastHudRefreshMs = 0;
  int mouseX = 0;
  int mouseY = 0;
  bool cameraPanning = false;
  int cameraLastX = 0;
  int cameraLastY = 0;
  int cameraPanRemainderX = 0;
  int cameraPanRemainderY = 0;
  ToastNotification toast;
  int onboardingStep = loadedAtStartup ? 6 : 0;
  bool showOnboarding = !loadedAtStartup;
  bool sessionDirty = !loadedAtStartup;
  bool showQuitDialog = false;

  auto notify = [&](const std::string& message, bool success) {
    toast.message = message;
    toast.color = success ? RGB{105, 230, 135} : RGB{255, 125, 110};
    toast.expiresAtMs = SDL_GetTicks() + 2400u;
  };
  if (!startupError.empty()) {
    notify("LOAD FAILED - NEW CITY STARTED", false);
  } else if (loadedAtStartup) {
    notify("GAME LOADED", true);
  }

  auto executeSimulationTick = [&]() {
    const bool wasLow = liveState.lowFunds;
    const bool wasBankrupt = liveState.bankrupt;
    runSimulationTick(map, roads, store, population, facilities, liveState, funds);
    sessionDirty = true;
    if (liveState.bankrupt && !wasBankrupt) {
      notify("BUDGET DEFICIT CANNOT BE PAID", false);
    } else if (liveState.lowFunds && !wasLow) {
      notify("LOW FUNDS - WATCH THE BUDGET", false);
    } else if (!liveState.lowFunds && !liveState.bankrupt && (wasLow || wasBankrupt)) {
      notify("TREASURY RECOVERED", true);
    }
  };

  auto saveSession = [&]() -> bool {
    GameplaySessionState session;
    session.funds = funds;
    session.tick = liveState.tick;
    session.paused = liveState.paused;
    session.tickIntervalMs = liveState.tickIntervalMs;
    session.demand = liveState.demand;
    session.treasuryRevenue = liveState.treasuryRevenue;
    session.treasuryExpenses = liveState.treasuryExpenses;
    session.treasuryNet = liveState.treasuryNet;
    session.populationTarget = liveState.populationTarget;
    session.fractionalDeaths = liveState.deathcareState.fractionalDeaths;
    session.awaitingDisposition = liveState.deathcareState.awaitingDisposition;
    session.facilities = facilities;
    std::string error;
    if (GameplaySessionSystem::save(
          kSessionPath, map, roads, store, population, session, &error)) {
      notify("GAME SAVED", true);
      sessionDirty = false;
      return true;
    } else {
      notify(error.empty() ? "SAVE FAILED" : error, false);
      return false;
    }
  };

  auto loadSession = [&]() {
    GameplaySessionState session;
    std::string error;
    if (!GameplaySessionSystem::load(
          kSessionPath, map, roads, store, population, session, &error)) {
      notify(error.empty() ? "LOAD FAILED" : error, false);
      return;
    }
    funds = session.funds;
    liveState.tick = session.tick;
    liveState.paused = session.paused;
    liveState.tickIntervalMs = session.tickIntervalMs;
    liveState.demand = session.demand;
    liveState.treasuryRevenue = session.treasuryRevenue;
    liveState.treasuryExpenses = session.treasuryExpenses;
    liveState.treasuryNet = session.treasuryNet;
    liveState.populationTarget = session.populationTarget;
    liveState.deathcareState.fractionalDeaths = session.fractionalDeaths;
    liveState.deathcareState.awaitingDisposition = session.awaitingDisposition;
    liveState.lastTickMs = SDL_GetTicks();
    facilities = std::move(session.facilities);
    updatePlayableUtilityConnectivity(map, roads, facilities);
    liveState.serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
    liveState.trafficSummary = TrafficSystem::simulateCommutes(
      store, population, roads, 3001u + (liveState.tick * 31u)
    );
    refreshRouteHeat(store, population, roads, liveState, 3001u + (liveState.tick * 31u));
    roadToolActive = false;
    zoneToolActive = false;
    bulldozeToolActive = false;
    serviceToolActive = false;
    toolDragging = false;
    roadPlan = {};
    zonePlan = {};
    bulldozePlan = {};
    servicePlan = {};
    onboardingStep = 6;
    showOnboarding = false;
    sessionDirty = false;
    notify("GAME LOADED", true);
  };

  auto activatePaletteTool = [&](PaletteTool tool) {
    if (tool == PaletteTool::Zone && zoneToolActive) {
      selectedZone = nextPlayableZone(selectedZone);
    } else if (tool == PaletteTool::Service && serviceToolActive) {
      selectedService = nextPlayableService(selectedService);
    }
    roadToolActive = tool == PaletteTool::Road;
    zoneToolActive = tool == PaletteTool::Zone;
    bulldozeToolActive = tool == PaletteTool::Bulldoze;
    serviceToolActive = tool == PaletteTool::Service;
    toolDragging = false;
    roadPlan = {};
    zonePlan = {};
    bulldozePlan = {};
    servicePlan = {};
  };

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_MOUSEMOTION) {
        mouseX = event.motion.x;
        mouseY = event.motion.y;
      } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        mouseX = event.button.x;
        mouseY = event.button.y;
      }
      if (showQuitDialog) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
          showQuitDialog = false;
        } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
          switch (quitHitTest(event.button.x, event.button.y, windowWidth, windowHeight)) {
            case QuitAction::SaveAndQuit:
              if (saveSession()) {
                running = false;
              }
              break;
            case QuitAction::QuitWithoutSaving:
              running = false;
              break;
            case QuitAction::Cancel:
              showQuitDialog = false;
              break;
            case QuitAction::None:
            default:
              break;
          }
        }
        continue;
      }
      OverlayMode clickedOverlay = overlayMode;
      if (event.type == SDL_QUIT) {
        showQuitDialog = true;
      } else if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
          case SDLK_ESCAPE:
            if (toolDragging || roadToolActive || zoneToolActive || bulldozeToolActive || serviceToolActive) {
              toolDragging = false;
              roadToolActive = false;
              zoneToolActive = false;
              bulldozeToolActive = false;
              serviceToolActive = false;
              roadPlan = {};
              zonePlan = {};
              bulldozePlan = {};
              servicePlan = {};
            } else {
              showQuitDialog = true;
            }
            break;
          case SDLK_EQUALS:
          case SDLK_PLUS:
            tilePixels = std::min(48, tilePixels + 1);
            break;
          case SDLK_MINUS:
            tilePixels = std::max(2, tilePixels - 1);
            break;
          case SDLK_LEFT:
            viewX = std::max(0, viewX - 1);
            break;
          case SDLK_RIGHT:
            viewX = std::min(mapSize - 1, viewX + 1);
            break;
          case SDLK_UP:
            viewY = std::max(0, viewY - 1);
            break;
          case SDLK_DOWN:
            viewY = std::min(mapSize - 1, viewY + 1);
            break;
          case SDLK_1:
            overlayMode = OverlayMode::Zone;
            break;
          case SDLK_2:
            overlayMode = OverlayMode::LandValue;
            break;
          case SDLK_3:
            overlayMode = OverlayMode::Pollution;
            break;
          case SDLK_4:
            overlayMode = OverlayMode::ServiceCoverage;
            break;
          case SDLK_5:
            overlayMode = OverlayMode::TrafficCongestion;
            break;
          case SDLK_6:
            overlayMode = OverlayMode::Demand;
            break;
          case SDLK_7:
            overlayMode = OverlayMode::Happiness;
            break;
          case SDLK_8:
            overlayMode = OverlayMode::RouteHeatmap;
            break;
          case SDLK_SPACE:
            liveState.paused = !liveState.paused;
            sessionDirty = true;
            break;
          case SDLK_PERIOD:
          case SDLK_n:
            if (liveState.paused) {
              executeSimulationTick();
            }
            break;
          case SDLK_h:
            showLegend = !showLegend;
            break;
          case SDLK_F1:
            showOnboarding = !showOnboarding;
            break;
          case SDLK_F2:
            gReadableUiText = !gReadableUiText;
            notify(gReadableUiText ? "LARGE TEXT ON" : "COMPACT TEXT ON", true);
            break;
          case SDLK_F5:
            saveSession();
            break;
          case SDLK_F9:
            loadSession();
            break;
          case SDLK_r:
            roadToolActive = !roadToolActive;
            zoneToolActive = false;
            bulldozeToolActive = false;
            serviceToolActive = false;
            toolDragging = false;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
            servicePlan = {};
            break;
          case SDLK_z:
            if (zoneToolActive) {
              selectedZone = nextPlayableZone(selectedZone);
            } else {
              zoneToolActive = true;
              selectedZone = ZoneType::Residential;
            }
            roadToolActive = false;
            bulldozeToolActive = false;
            serviceToolActive = false;
            toolDragging = false;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
            servicePlan = {};
            break;
          case SDLK_b:
            bulldozeToolActive = !bulldozeToolActive;
            roadToolActive = false;
            zoneToolActive = false;
            serviceToolActive = false;
            toolDragging = false;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
            servicePlan = {};
            break;
          case SDLK_s:
            if (serviceToolActive) {
              selectedService = nextPlayableService(selectedService);
            } else {
              serviceToolActive = true;
              selectedService = ServiceType::Fire;
            }
            roadToolActive = false;
            zoneToolActive = false;
            bulldozeToolActive = false;
            toolDragging = false;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
            servicePlan = {};
            break;
          case SDLK_o:
            cycleOriginFilter(liveState, store);
            refreshRouteHeat(store, population, roads, liveState, 1003u + (liveState.tick * 31u));
            break;
          case SDLK_d:
            cycleDestinationFilter(liveState, store);
            refreshRouteHeat(store, population, roads, liveState, 1003u + (liveState.tick * 31u));
            break;
          case SDLK_c:
            clearRouteFilters(liveState);
            refreshRouteHeat(store, population, roads, liveState, 1003u + (liveState.tick * 31u));
            break;
          default:
            break;
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE) {
        cameraPanning = true;
        cameraLastX = event.button.x;
        cameraLastY = event.button.y;
        cameraPanRemainderX = 0;
        cameraPanRemainderY = 0;
      } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_MIDDLE) {
        cameraPanning = false;
      } else if (event.type == SDL_MOUSEWHEEL) {
        SDL_GetMouseState(&mouseX, &mouseY);
        const bool overHud = mouseX >= windowWidth - kHudPanelWidth - 12
          && mouseY >= 12 && mouseY < 12 + kHudPanelHeight;
        const bool overLegend = showLegend && mouseX >= 12 && mouseX < 532
          && mouseY >= 12 && mouseY < 158;
        const bool overPalette = paletteHitTest(mouseX, mouseY, windowWidth, windowHeight)
          != PaletteTool::None;
        if (!overHud && !overLegend && !overPalette && event.wheel.y != 0) {
          const int oldTilePixels = tilePixels;
          const Coord anchor{viewX + mouseX / oldTilePixels, viewY + mouseY / oldTilePixels};
          const int direction = event.wheel.y > 0 ? 1 : -1;
          tilePixels = std::max(2, std::min(48, tilePixels + direction * 2));
          viewX = anchor.x - mouseX / tilePixels;
          viewY = anchor.y - mouseY / tilePixels;
        }
      } else if (event.type == SDL_MOUSEMOTION && cameraPanning) {
        cameraPanRemainderX += event.motion.x - cameraLastX;
        cameraPanRemainderY += event.motion.y - cameraLastY;
        cameraLastX = event.motion.x;
        cameraLastY = event.motion.y;
        while (cameraPanRemainderX >= tilePixels) {
          --viewX;
          cameraPanRemainderX -= tilePixels;
        }
        while (cameraPanRemainderX <= -tilePixels) {
          ++viewX;
          cameraPanRemainderX += tilePixels;
        }
        while (cameraPanRemainderY >= tilePixels) {
          --viewY;
          cameraPanRemainderY -= tilePixels;
        }
        while (cameraPanRemainderY <= -tilePixels) {
          ++viewY;
          cameraPanRemainderY += tilePixels;
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
                 && showLegend
                 && overlayHitTest(event.button.x, event.button.y, clickedOverlay)) {
        overlayMode = clickedOverlay;
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
                 && hudHitTest(event.button.x, event.button.y, windowWidth) != HudAction::None) {
        const HudAction action = hudHitTest(event.button.x, event.button.y, windowWidth);
        switch (action) {
          case HudAction::TogglePause:
            liveState.paused = !liveState.paused;
            sessionDirty = true;
            break;
          case HudAction::Slow:
            liveState.tickIntervalMs = 700;
            sessionDirty = true;
            break;
          case HudAction::Normal:
            liveState.tickIntervalMs = 350;
            sessionDirty = true;
            break;
          case HudAction::Fast:
            liveState.tickIntervalMs = 120;
            sessionDirty = true;
            break;
          case HudAction::Save:
            saveSession();
            break;
          case HudAction::Load:
            loadSession();
            break;
          case HudAction::None:
          default:
            break;
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
                 && paletteHitTest(event.button.x, event.button.y, windowWidth, windowHeight)
                    != PaletteTool::None) {
        activatePaletteTool(
          paletteHitTest(event.button.x, event.button.y, windowWidth, windowHeight)
        );
      } else if (event.type == SDL_MOUSEBUTTONDOWN && serviceToolActive) {
        if (event.button.button == SDL_BUTTON_RIGHT) {
          servicePlan = {};
        } else if (event.button.button == SDL_BUTTON_LEFT) {
          const Coord tile{viewX + event.button.x / tilePixels, viewY + event.button.y / tilePixels};
          servicePlan = ServiceTool::plan(map, roads, facilities, selectedService, tile, funds);
          const int64_t cost = servicePlan.cost;
          if (ServiceTool::build(map, roads, facilities, servicePlan, funds)) {
            sessionDirty = true;
            updatePlayableUtilityConnectivity(map, roads, facilities);
            liveState.serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
            notify(std::string(ServiceSystem::serviceTypeToString(selectedService))
                     + " BUILT $" + std::to_string(cost), true);
            if (onboardingStep == 3 && selectedService == ServiceType::Power) {
              onboardingStep = 4;
            } else if (onboardingStep == 4 && selectedService == ServiceType::Water) {
              onboardingStep = 5;
            } else if (onboardingStep == 5
                       && (selectedService == ServiceType::Fire
                           || selectedService == ServiceType::Police
                           || selectedService == ServiceType::Health
                           || selectedService == ServiceType::Education)) {
              onboardingStep = 6;
              showOnboarding = false;
              notify("CITY GUIDE COMPLETE", true);
            }
          } else {
            notify(servicePlan.error.empty() ? "SERVICE BUILD FAILED" : servicePlan.error, false);
          }
          servicePlan = ServiceTool::plan(map, roads, facilities, selectedService, tile, funds);
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN
                 && (roadToolActive || zoneToolActive || bulldozeToolActive)) {
        if (event.button.button == SDL_BUTTON_RIGHT) {
          toolDragging = false;
          roadPlan = {};
          zonePlan = {};
          bulldozePlan = {};
        } else if (event.button.button == SDL_BUTTON_LEFT) {
          const Coord tile{viewX + event.button.x / tilePixels, viewY + event.button.y / tilePixels};
          if (map.isValid(tile)) {
            toolDragging = true;
            toolDragStart = tile;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
          }
        }
      } else if (event.type == SDL_MOUSEMOTION && serviceToolActive) {
        const Coord tile{viewX + event.motion.x / tilePixels, viewY + event.motion.y / tilePixels};
        servicePlan = ServiceTool::plan(map, roads, facilities, selectedService, tile, funds);
      } else if (event.type == SDL_MOUSEMOTION && toolDragging) {
        const Coord tile{viewX + event.motion.x / tilePixels, viewY + event.motion.y / tilePixels};
        if (map.isValid(tile)) {
          if (roadToolActive) {
            roadPlan = RoadTool::plan(map, roads, toolDragStart, tile, funds);
          } else if (zoneToolActive) {
            zonePlan = ZoneTool::plan(map, toolDragStart, tile, selectedZone, funds);
          } else if (bulldozeToolActive) {
            bulldozePlan = BulldozeTool::plan(map, roads, facilities, toolDragStart, tile, funds);
          }
        }
      } else if (event.type == SDL_MOUSEBUTTONUP && toolDragging
                 && event.button.button == SDL_BUTTON_LEFT) {
        const Coord tile{viewX + event.button.x / tilePixels, viewY + event.button.y / tilePixels};
        if (map.isValid(tile)) {
          if (roadToolActive) {
            roadPlan = RoadTool::plan(map, roads, toolDragStart, tile, funds);
            const int64_t cost = roadPlan.cost;
            if (RoadTool::build(map, roads, roadPlan, funds)) {
              sessionDirty = true;
              updatePlayableUtilityConnectivity(map, roads, facilities);
              liveState.serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
              refreshRouteHeat(store, population, roads, liveState, 1003u + (liveState.tick * 31u));
              notify("ROAD BUILT $" + std::to_string(cost), true);
              if (onboardingStep == 0) {
                onboardingStep = 1;
              }
            } else {
              notify(roadPlan.error.empty() ? "ROAD BUILD FAILED" : roadPlan.error, false);
            }
          } else if (zoneToolActive) {
            zonePlan = ZoneTool::plan(map, toolDragStart, tile, selectedZone, funds);
            const int64_t cost = zonePlan.cost;
            if (ZoneTool::apply(map, zonePlan, funds)) {
              sessionDirty = true;
              notify("ZONE APPLIED $" + std::to_string(cost), true);
              if (onboardingStep == 1) {
                onboardingStep = 2;
              }
            } else {
              notify(zonePlan.error.empty() ? "ZONING FAILED" : zonePlan.error, false);
            }
          } else if (bulldozeToolActive) {
            bulldozePlan = BulldozeTool::plan(map, roads, facilities, toolDragStart, tile, funds);
            const int64_t cost = bulldozePlan.cost;
            if (BulldozeTool::apply(map, roads, store, facilities, bulldozePlan, funds)) {
              sessionDirty = true;
              updatePlayableUtilityConnectivity(map, roads, facilities);
              const uint32_t populationBefore = population.getTotalPopulation();
              PopulationSystem::allocate(
                store, population, populationBefore, 2003u + (liveState.tick * 31u)
              );
              liveState.trafficSummary = TrafficSystem::simulateCommutes(
                store, population, roads, 2005u + (liveState.tick * 31u)
              );
              liveState.serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
              int64_t remainingServiceUpkeep = 0;
              for (const ServiceFacility& facility : facilities) {
                remainingServiceUpkeep += ServiceTool::operatingCostPerTick(facility.type);
              }
              liveState.economy = EconomySystem::calculateEconomy(store, population, TaxRates{}, &map);
              liveState.treasuryExpenses = static_cast<int64_t>(std::llround(
                static_cast<double>(liveState.economy.totalExpenses) * 0.01)) + remainingServiceUpkeep;
              refreshRouteHeat(store, population, roads, liveState, 2005u + (liveState.tick * 31u));
              notify("DEMOLISHED $" + std::to_string(cost), true);
            } else {
              notify(bulldozePlan.error.empty() ? "DEMOLITION FAILED" : bulldozePlan.error, false);
            }
          }
        }
        toolDragging = false;
        roadPlan = {};
        zonePlan = {};
        bulldozePlan = {};
      }
    }

    if (onboardingStep == 2 && !liveState.paused) {
      onboardingStep = 3;
    }

    const int visibleTilesX = std::max(1, windowWidth / tilePixels);
    const int visibleTilesY = std::max(1, windowHeight / tilePixels);
    const int maxViewX = std::max(0, mapSize - visibleTilesX);
    const int maxViewY = std::max(0, mapSize - visibleTilesY);
    viewX = std::min(viewX, maxViewX);
    viewY = std::min(viewY, maxViewY);
    viewX = std::max(0, viewX);
    viewY = std::max(0, viewY);

    const uint32_t nowMs = SDL_GetTicks();
    if (!showQuitDialog && !liveState.paused
        && (nowMs - liveState.lastTickMs) >= liveState.tickIntervalMs) {
      executeSimulationTick();
      liveState.lastTickMs = nowMs;
    }

    SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
    SDL_RenderClear(renderer);

    for (int ty = 0; ty < visibleTilesY; ++ty) {
      for (int tx = 0; tx < visibleTilesX; ++tx) {
        const Coord coord{viewX + tx, viewY + ty};
        if (!map.isValid(coord)) {
          continue;
        }

        const RGB color = tileColor(
          map, roads, store, coord, overlayMode, facilities,
          liveState.routeHeatByTile,
          liveState.waste.happinessPenalty + liveState.deathcare.happinessPenalty
        );
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

        SDL_Rect rect{tx * tilePixels, ty * tilePixels, tilePixels, tilePixels};
        SDL_RenderFillRect(renderer, &rect);
      }
    }

    const bool mouseOverHud = mouseX >= windowWidth - kHudPanelWidth - 12
      && mouseY >= 12 && mouseY < 12 + kHudPanelHeight;
    const bool mouseOverLegend = showLegend && mouseX >= 12 && mouseX < 532
      && mouseY >= 12 && mouseY < 158;
    const bool mouseOverPalette = paletteHitTest(mouseX, mouseY, windowWidth, windowHeight)
      != PaletteTool::None;
    const bool mouseOverGuide = showOnboarding && mouseX >= 14 && mouseX < 364
      && mouseY >= windowHeight - 154 && mouseY < windowHeight - 62;
    const Coord hoveredTile{viewX + mouseX / tilePixels, viewY + mouseY / tilePixels};
    const bool inspectMap = !mouseOverHud && !mouseOverLegend && !mouseOverPalette
      && !mouseOverGuide && map.isValid(hoveredTile);
    if (inspectMap) {
      const int screenX = (hoveredTile.x - viewX) * tilePixels;
      const int screenY = (hoveredTile.y - viewY) * tilePixels;
      drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels, {255, 245, 150}, 255);
    }

    if (toolDragging) {
      const bool valid = roadToolActive
        ? roadPlan.valid
        : (zoneToolActive ? zonePlan.valid : bulldozePlan.valid);
      const std::vector<Coord>& previewTiles = roadToolActive
        ? roadPlan.tiles
        : (zoneToolActive ? zonePlan.tiles : bulldozePlan.tiles);
      const RGB previewColor = valid
        ? (roadToolActive
            ? RGB{80, 220, 120}
            : (zoneToolActive ? zoneColor(static_cast<int>(selectedZone)) : RGB{255, 155, 45}))
        : RGB{235, 75, 75};
      for (const Coord tile : previewTiles) {
        const int screenX = (tile.x - viewX) * tilePixels;
        const int screenY = (tile.y - viewY) * tilePixels;
        drawFilledRect(renderer, screenX, screenY, tilePixels, tilePixels, previewColor, 150);
        drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels, {245, 245, 245}, 220);
      }
    }

    if (serviceToolActive && servicePlan.hasSite && map.isValid(servicePlan.facility.position)) {
      const Coord tile = servicePlan.facility.position;
      const int screenX = (tile.x - viewX) * tilePixels;
      const int screenY = (tile.y - viewY) * tilePixels;
      const RGB previewColor = servicePlan.valid
        ? serviceFacilityColor(selectedService)
        : RGB{235, 75, 75};
      drawFilledRect(renderer, screenX, screenY, tilePixels, tilePixels, previewColor, 190);
      drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels, {255, 255, 255}, 240);
    }

    if (showLegend) {
      drawLegendPanel(renderer, overlayMode, windowWidth, windowHeight, mouseX, mouseY);
    }
    if (inspectMap) {
      drawTileInspector(renderer, map, roads, store, facilities, hoveredTile);
    }

    std::string activeToolLabel = "NONE";
    if (roadToolActive) {
      activeToolLabel = "ROAD";
    } else if (zoneToolActive) {
      activeToolLabel = Zoning::zoneToString(static_cast<int>(selectedZone));
    } else if (bulldozeToolActive) {
      activeToolLabel = "BULLDOZE";
    } else if (serviceToolActive) {
      activeToolLabel = ServiceSystem::serviceTypeToString(selectedService);
    }
    drawGameplayHud(
      renderer,
      windowWidth,
      liveState.tick,
      liveState.paused,
      population.getTotalPopulation(),
      store.getBuildingCount(),
      funds,
      activeToolLabel,
      liveState.demand,
      liveState.tickIntervalMs,
      liveState.treasuryRevenue,
      liveState.treasuryExpenses,
      liveState.treasuryNet,
      liveState.lowFunds,
      liveState.bankrupt,
      liveState.waste,
      liveState.deathcare,
      mouseX,
      mouseY
    );
    drawToolPalette(
      renderer,
      windowWidth,
      windowHeight,
      roadToolActive,
      zoneToolActive,
      bulldozeToolActive,
      serviceToolActive,
      mouseX,
      mouseY
    );
    if (showOnboarding) {
      drawOnboarding(renderer, windowHeight, onboardingStep);
    }

    std::string uiHoverText;
    const PaletteTool hoveredTool = paletteHitTest(mouseX, mouseY, windowWidth, windowHeight);
    switch (hoveredTool) {
      case PaletteTool::Road:
        uiHoverText = "ROAD TOOL: DRAG A ROUTE - $100 PER NEW SEGMENT";
        break;
      case PaletteTool::Zone:
        uiHoverText = std::string("ZONE ") + Zoning::zoneToString(static_cast<int>(selectedZone))
          + ": DRAG AN AREA - $25 PER TILE - CLICK AGAIN TO CYCLE";
        break;
      case PaletteTool::Bulldoze:
        uiHoverText = "BULLDOZER: DRAG TO REMOVE ROADS ZONES BUILDINGS OR SERVICES";
        break;
      case PaletteTool::Service:
        uiHoverText = std::string("SERVICE ") + ServiceSystem::serviceTypeToString(selectedService)
          + ": CLICK BESIDE A ROAD - $"
          + std::to_string(ServiceTool::constructionCost(selectedService));
        break;
      case PaletteTool::None:
      default:
        break;
    }

    if (uiHoverText.empty()) {
      switch (hudHitTest(mouseX, mouseY, windowWidth)) {
        case HudAction::TogglePause:
          uiHoverText = liveState.paused ? "PLAY: RESUME CITY SIMULATION" : "PAUSE: STOP CITY SIMULATION";
          break;
        case HudAction::Slow:
          uiHoverText = "1X SPEED: ONE TICK ABOUT EVERY 700 MS";
          break;
        case HudAction::Normal:
          uiHoverText = "2X SPEED: ONE TICK ABOUT EVERY 350 MS";
          break;
        case HudAction::Fast:
          uiHoverText = "3X SPEED: ONE TICK ABOUT EVERY 120 MS";
          break;
        case HudAction::Save:
          uiHoverText = "SAVE: WRITE THE CURRENT CITY TO THE DEFAULT SESSION";
          break;
        case HudAction::Load:
          uiHoverText = "LOAD: RESTORE THE DEFAULT SESSION";
          break;
        case HudAction::None:
        default:
          break;
      }
    }

    if (uiHoverText.empty() && showLegend) {
      OverlayMode hoveredOverlay = overlayMode;
      if (overlayHitTest(mouseX, mouseY, hoveredOverlay)) {
        switch (hoveredOverlay) {
          case OverlayMode::Zone: uiHoverText = "ZONE OVERLAY: ZONING BUILDINGS ROADS AND TERRAIN"; break;
          case OverlayMode::LandValue: uiHoverText = "LAND VALUE OVERLAY: LOW TO HIGH PROPERTY VALUE"; break;
          case OverlayMode::Pollution: uiHoverText = "POLLUTION OVERLAY: CLEAN TO POLLUTED LAND"; break;
          case OverlayMode::ServiceCoverage: uiHoverText = "SERVICE OVERLAY: ROAD-REACHABLE CIVIC COVERAGE"; break;
          case OverlayMode::TrafficCongestion: uiHoverText = "TRAFFIC OVERLAY: LOW TO HIGH ROAD CONGESTION"; break;
          case OverlayMode::Demand: uiHoverText = "DEMAND OVERLAY: DEVELOPMENT PRESSURE ON ZONED LAND"; break;
          case OverlayMode::Happiness: uiHoverText = "HAPPINESS OVERLAY: LOCAL QUALITY OF LIFE"; break;
          case OverlayMode::RouteHeatmap: uiHoverText = "ROUTE HEAT OVERLAY: MOST USED COMMUTE EDGES"; break;
          default: break;
        }
      }
    }
    drawUiTooltip(renderer, mouseX, mouseY, windowWidth, windowHeight, uiHoverText);

    std::string placementWarning;
    if (toolDragging) {
      if (roadToolActive && !roadPlan.valid) {
        placementWarning = roadPlan.error;
      } else if (zoneToolActive && !zonePlan.valid) {
        placementWarning = zonePlan.error;
      } else if (bulldozeToolActive && !bulldozePlan.valid) {
        placementWarning = bulldozePlan.error;
      }
    } else if (uiHoverText.empty() && serviceToolActive && servicePlan.hasSite && !servicePlan.valid) {
      placementWarning = servicePlan.error;
    }
    drawPlacementWarning(renderer, mouseX, mouseY, windowWidth, windowHeight, placementWarning);
    drawToast(renderer, windowWidth, toast, nowMs);

    if (nowMs - lastHudRefreshMs >= 250) {
      const std::string hudTitle = makeHudTitle(
        roads,
        store,
        population,
        liveState,
        tilePixels,
        viewX,
        viewY,
        overlayMode,
        liveState.routeFilter,
        roadToolActive,
        roadPlan,
        zoneToolActive,
        selectedZone,
        zonePlan,
        bulldozeToolActive,
        bulldozePlan,
        serviceToolActive,
        selectedService,
        servicePlan,
        funds
      );
      SDL_SetWindowTitle(window, hudTitle.c_str());
      lastHudRefreshMs = nowMs;
    }

    if (showQuitDialog) {
      drawQuitDialog(renderer, windowWidth, windowHeight, mouseX, mouseY, sessionDirty);
    }

    SDL_RenderPresent(renderer);
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
