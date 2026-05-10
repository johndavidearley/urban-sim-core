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

RGB tileColor(
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  Coord coord,
  OverlayMode overlayMode,
  const std::vector<ServiceFacility>& facilities
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

std::string makeHudTitle(
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population,
  const ServiceCoverageSummary& serviceSummary,
  const TrafficSummary& trafficSummary,
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
      << "Buildings:" << store.getBuildingCount() << " (R:" << residential
      << " C:" << commercial << " I:" << industrial << ")"
      << " | Roads:" << roads.getRoadCount()
      << " | Pop:" << population.getTotalPopulation()
      << " | Commute:" << trafficSummary.averageCommuteTime
      << " | Service:" << static_cast<int>(serviceSummary.overallCoverage * 100.0f) << "%"
      << " | Zoom:" << tilePixels
      << " | View:" << viewX << "," << viewY
      << " | Overlay:" << overlayModeName(overlayMode)
      << " [1=zone 2=land 3=pollution 4=service 5=traffic]";
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
  const ServiceCoverageSummary serviceSummary = ServiceSystem::evaluateCoverage(store, roads, facilities);
  const TrafficSummary trafficSummary = TrafficSystem::simulateCommutes(store, population, roads, 98765u);

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

  int tilePixels = 12;
  int viewX = 0;
  int viewY = 0;
  OverlayMode overlayMode = OverlayMode::Zone;
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

    SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
    SDL_RenderClear(renderer);

    for (int ty = 0; ty < visibleTilesY; ++ty) {
      for (int tx = 0; tx < visibleTilesX; ++tx) {
        const Coord coord{viewX + tx, viewY + ty};
        if (!map.isValid(coord)) {
          continue;
        }

        const RGB color = tileColor(map, roads, store, coord, overlayMode, facilities);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

        SDL_Rect rect{tx * tilePixels, ty * tilePixels, tilePixels, tilePixels};
        SDL_RenderFillRect(renderer, &rect);
      }
    }

    const uint32_t nowMs = SDL_GetTicks();
    if (nowMs - lastHudRefreshMs >= 250) {
      const std::string hudTitle = makeHudTitle(
        roads,
        store,
        population,
        serviceSummary,
        trafficSummary,
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
