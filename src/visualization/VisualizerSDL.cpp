#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL2/SDL.h>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

namespace {
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
  if (tile.zone == 0 || tile.type == 2) {
    return 0.0f;
  }

  const bool hasBuilding = (tile.buildingId != 0);
  Coord anchor;
  const bool roadAdj = resolveRoadAnchor(roads, coord, anchor);

  float baseDemand = 0.25f;
  if (tile.zone == 1) {
    baseDemand = 0.65f;
  } else if (tile.zone == 2) {
    baseDemand = 0.55f;
  } else if (tile.zone == 3) {
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
  const std::vector<ServiceFacility>& facilities
) {
  const Tile& tile = map.getTile(coord);

  const float service = serviceCoverageAtTile(roads, coord, facilities);
  const float congestion = localCongestionAtTile(roads, coord);
  const float pollution = std::max(0.0f, std::min(1.0f, tile.pollution));
  const float landNorm = std::max(0.0f, std::min(1.0f, (tile.landValue - 40.0f) / 160.0f));

  float happiness = 0.45f;
  happiness += service * 0.25f;
  happiness += landNorm * 0.2f;
  happiness -= pollution * 0.25f;
  happiness -= congestion * 0.2f;

  if (tile.zone == 0) {
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
  const std::unordered_map<Coord, float, Vec2Hash>& routeHeatByTile
) {
  const Tile& tile = map.getTile(coord);

  if (overlayMode == OverlayMode::LandValue) {
    return landValueColor(tile.landValue);
  }
  if (overlayMode == OverlayMode::Pollution) {
    return pollutionColor(tile.pollution);
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
    return happinessColor(happinessAtTile(map, roads, coord, facilities));
  }
  if (overlayMode == OverlayMode::RouteHeatmap) {
    return routeHeatColor(routeHeatAtTile(routeHeatByTile, coord));
  }

  RGB color = terrainTint(tile, zoneColor(tile.zone));

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

void drawSwatch(SDL_Renderer* renderer, int x, int y, RGB color, bool active = false) {
  drawFilledRect(renderer, x, y, 18, 12, color, 255);
  drawRectOutline(renderer, x, y, 18, 12, active ? RGB{255, 230, 120} : RGB{220, 220, 220}, 255);
}

void drawZoneLegend(SDL_Renderer* renderer, int x, int y) {
  // Categories shown in zone overlay rendering path.
  drawSwatch(renderer, x + 0, y, zoneColor(1));       // Residential
  drawSwatch(renderer, x + 24, y, zoneColor(2));      // Commercial
  drawSwatch(renderer, x + 48, y, zoneColor(3));      // Industrial
  drawSwatch(renderer, x + 72, y, {64, 64, 64});      // Road

  // Building colors are distinct from zoning base colors.
  drawSwatch(renderer, x + 112, y, {44, 132, 70});    // Residential building
  drawSwatch(renderer, x + 136, y, {31, 84, 163});    // Commercial building
  drawSwatch(renderer, x + 160, y, {179, 93, 29});    // Industrial building
}

void drawSteppedLegend(SDL_Renderer* renderer, int x, int y, OverlayMode mode) {
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
}

void drawLegendPanel(SDL_Renderer* renderer, OverlayMode overlayMode, int windowWidth, int windowHeight) {
  (void)windowWidth;
  (void)windowHeight;

  constexpr int panelX = 12;
  constexpr int panelY = 12;
  constexpr int panelW = 520;
  constexpr int panelH = 122;
  drawFilledRect(renderer, panelX, panelY, panelW, panelH, {14, 16, 20}, 190);
  drawRectOutline(renderer, panelX, panelY, panelW, panelH, {230, 230, 230}, 220);

  // Overlay hotkey strip. Active mode gets bright outline.
  constexpr int keyStartX = panelX + 10;
  constexpr int keyY = panelY + 10;
  constexpr int keyW = 52;
  constexpr int keyH = 20;
  constexpr int keyGap = 8;
  const std::array<OverlayMode, 8> modes = {
    OverlayMode::Zone,
    OverlayMode::LandValue,
    OverlayMode::Pollution,
    OverlayMode::ServiceCoverage,
    OverlayMode::TrafficCongestion,
    OverlayMode::Demand,
    OverlayMode::Happiness,
    OverlayMode::RouteHeatmap,
  };

  for (size_t i = 0; i < modes.size(); ++i) {
    const int x = keyStartX + static_cast<int>(i) * (keyW + keyGap);
    const RGB marker = overlaySampleColor(modes[i], 0.75f);
    drawFilledRect(renderer, x, keyY, keyW, keyH, {42, 46, 54}, 220);
    drawFilledRect(renderer, x + 4, keyY + 4, 10, keyH - 8, marker, 255);

    const bool active = (modes[i] == overlayMode);
    drawRectOutline(renderer, x, keyY, keyW, keyH, active ? RGB{255, 230, 120} : RGB{120, 130, 145}, 255);

    // Draw a minimal key indicator glyph as vertical bars (1..8 count).
    for (size_t bar = 0; bar <= i; ++bar) {
      drawFilledRect(renderer, x + 20 + static_cast<int>(bar) * 5, keyY + 5, 3, keyH - 10, {220, 220, 220}, 255);
    }
  }

  // Mid strip shows overlay-specific legend swatches.
  constexpr int legendX = panelX + 10;
  constexpr int legendY = panelY + 40;
  if (overlayMode == OverlayMode::Zone) {
    drawZoneLegend(renderer, legendX, legendY);
  } else {
    drawSteppedLegend(renderer, legendX, legendY, overlayMode);
  }

  // Lower strip shows continuous scale hints for numeric overlays.
  constexpr int barX = panelX + 10;
  constexpr int barY = panelY + 72;
  constexpr int barW = 280;
  constexpr int barH = 14;
  if (overlayMode == OverlayMode::Zone) {
    drawFilledRect(renderer, barX, barY, barW, barH, {36, 40, 48}, 255);
    drawRectOutline(renderer, barX, barY, barW, barH, {220, 220, 220}, 255);
  } else {
    drawGradientBar(renderer, barX, barY, barW, barH, overlayMode);
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
}

void runSimulationTick(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  LiveSimulationState& liveState
) {
  const uint32_t tickSeed = 1000u + (liveState.tick * 31u);

  const ZoneDemand demand = Zoning::calculateDemand(tickSeed);
  GrowthSystem::runStep(map, roads, store, demand, tickSeed + 1u, 0.18f);

  const uint32_t requestedPopulation = 480u + ((liveState.tick % 6u) * 20u);
  PopulationSystem::allocate(store, population, requestedPopulation, tickSeed + 2u);

  liveState.trafficSummary = TrafficSystem::simulateCommutes(store, population, roads, tickSeed + 3u);
  liveState.serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
  liveState.routeHeatByTile = buildRouteHeatByTile(roads);

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
  OverlayMode overlayMode
) {
  const auto& buildings = store.getBuildings();
  size_t residential = 0;
  size_t commercial = 0;
  size_t industrial = 0;
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
    }
  }

  std::ostringstream oss;
  oss << "UrbanSimCore Visualizer | "
      << "Tick:" << liveState.tick << (liveState.paused ? "(paused)" : "(live)")
      << " | "
      << "Buildings:" << store.getBuildingCount() << " (R:" << residential
      << " C:" << commercial << " I:" << industrial << ")"
      << " | Roads:" << roads.getRoadCount()
      << " | Pop:" << population.getTotalPopulation()
      << " | Commute:" << liveState.trafficSummary.averageCommuteTime
      << " | Service:" << static_cast<int>(liveState.serviceSummary.overallCoverage * 100.0f) << "%"
      << " | Zoom:" << tilePixels
      << " | View:" << viewX << "," << viewY
      << " | Overlay:" << overlayModeName(overlayMode)
      << " [1-8 overlays Space pause . step H legend]";
  return oss.str();
}

bool seedScenario(CityMap& map, RoadNetwork& roads, EntityStore& store, PopulationStore& population) {
  const glm::ivec2 dims = map.getDimensions();
  if (dims.x < 8 || dims.y < 8) {
    return false;
  }

  Zoning::applyZoneRect(map, {2, 2}, {dims.x / 2 - 1, dims.y - 3}, ZoneType::Residential);
  Zoning::applyZoneRect(map, {dims.x / 2 + 1, 2}, {dims.x - 3, dims.y / 2}, ZoneType::Commercial);
  Zoning::applyZoneRect(map, {dims.x / 2 + 1, dims.y / 2 + 1}, {dims.x - 3, dims.y - 3}, ZoneType::Industrial);

  const int midY = dims.y / 2;
  for (int x = 2; x < dims.x - 2; ++x) {
    roads.buildRoad({x, midY}, {x + 1, midY});
  }

  const int midX = dims.x / 2;
  for (int y = 2; y < dims.y - 2; ++y) {
    roads.buildRoad({midX, y}, {midX, y + 1});
  }

  roads.updateConnectivity({midX, midY});

  constexpr uint32_t seed = 42;
  for (int i = 0; i < 12; ++i) {
    const ZoneDemand demand = Zoning::calculateDemand(seed + static_cast<uint32_t>(i));
    GrowthSystem::runStep(map, roads, store, demand, seed + static_cast<uint32_t>(i), 0.45f);
  }

  // Seed guaranteed commute-capable buildings directly on the road spine for live traffic overlays.
  const EntityId resA = store.createBuilding(BuildingType::Residential, {midX, midY - 8}, 90);
  const EntityId resB = store.createBuilding(BuildingType::Residential, {midX, midY + 8}, 75);
  const EntityId comA = store.createBuilding(BuildingType::Commercial, {midX - 10, midY}, 80);
  const EntityId comB = store.createBuilding(BuildingType::Commercial, {midX + 10, midY}, 60);
  const EntityId indA = store.createBuilding(BuildingType::Industrial, {midX - 16, midY}, 70);
  const EntityId indB = store.createBuilding(BuildingType::Industrial, {midX + 16, midY}, 85);

  map.getTile({midX, midY - 8}).buildingId = resA;
  map.getTile({midX, midY + 8}).buildingId = resB;
  map.getTile({midX - 10, midY}).buildingId = comA;
  map.getTile({midX + 10, midY}).buildingId = comB;
  map.getTile({midX - 16, midY}).buildingId = indA;
  map.getTile({midX + 16, midY}).buildingId = indB;

  // Add deterministic land value and pollution gradients so overlays show meaningful variation.
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      Tile& tile = map.getTile({x, y});
      const float distX = static_cast<float>(std::abs(x - midX));
      const float distY = static_cast<float>(std::abs(y - midY));
      tile.landValue = std::max(40.0f, 190.0f - (distX * 2.0f) - (distY * 1.25f));

      if (x > midX && y > midY) {
        tile.pollution = std::min(1.0f, 0.1f + ((distX + distY) / 64.0f));
      } else {
        tile.pollution = std::max(0.0f, 0.05f - ((distX + distY) / 128.0f));
      }
    }
  }

  PopulationSystem::allocate(store, population, 480, seed + 99u);
  TrafficSystem::simulateCommutes(store, population, roads, seed + 211u);

  return true;
}

std::vector<ServiceFacility> seedServiceFacilities(const CityMap& map) {
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
} // namespace

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  constexpr int mapSize = 64;
  CityMap map({mapSize, mapSize});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  if (!seedScenario(map, roads, store, population)) {
    std::cerr << "Failed to seed visualization scenario\n";
    return 1;
  }

  const std::vector<ServiceFacility> facilities = seedServiceFacilities(map);
  LiveSimulationState liveState;
  liveState.serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
  liveState.trafficSummary = TrafficSystem::simulateCommutes(store, population, roads, 98765u);
  liveState.routeHeatByTile = buildRouteHeatByTile(roads);

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

  int tilePixels = 12;
  int viewX = 0;
  int viewY = 0;
  OverlayMode overlayMode = OverlayMode::Zone;
  bool showLegend = true;
  uint32_t lastHudRefreshMs = 0;

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
          case SDLK_ESCAPE:
            running = false;
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
            break;
          case SDLK_PERIOD:
          case SDLK_n:
            if (liveState.paused) {
              runSimulationTick(map, roads, store, population, facilities, liveState);
            }
            break;
          case SDLK_h:
            showLegend = !showLegend;
            break;
          default:
            break;
        }
      }
    }

    const int visibleTilesX = std::max(1, windowWidth / tilePixels);
    const int visibleTilesY = std::max(1, windowHeight / tilePixels);
    const int maxViewX = std::max(0, mapSize - visibleTilesX);
    const int maxViewY = std::max(0, mapSize - visibleTilesY);
    viewX = std::min(viewX, maxViewX);
    viewY = std::min(viewY, maxViewY);

    const uint32_t nowMs = SDL_GetTicks();
    if (!liveState.paused && (nowMs - liveState.lastTickMs) >= liveState.tickIntervalMs) {
      runSimulationTick(map, roads, store, population, facilities, liveState);
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

        const RGB color = tileColor(map, roads, store, coord, overlayMode, facilities, liveState.routeHeatByTile);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

        SDL_Rect rect{tx * tilePixels, ty * tilePixels, tilePixels, tilePixels};
        SDL_RenderFillRect(renderer, &rect);
      }
    }

    if (showLegend) {
      drawLegendPanel(renderer, overlayMode, windowWidth, windowHeight);
    }

    if (nowMs - lastHudRefreshMs >= 250) {
      const std::string hudTitle = makeHudTitle(
        roads,
        store,
        population,
        liveState,
        tilePixels,
        viewX,
        viewY,
        overlayMode
      );
      SDL_SetWindowTitle(window, hudTitle.c_str());
      lastHudRefreshMs = nowMs;
    }

    SDL_RenderPresent(renderer);
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
