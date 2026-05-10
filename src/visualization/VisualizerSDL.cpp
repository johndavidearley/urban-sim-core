#include <algorithm>
#include <cstdint>
#include <iostream>

#include <SDL2/SDL.h>

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

namespace {
struct RGB {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

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

RGB tileColor(const CityMap& map, const EntityStore& store, Coord coord) {
  const Tile& tile = map.getTile(coord);
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

bool seedScenario(CityMap& map, RoadNetwork& roads, EntityStore& store) {
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

  return true;
}
} // namespace

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  constexpr int mapSize = 64;
  CityMap map({mapSize, mapSize});
  RoadNetwork roads(map);
  EntityStore store;

  if (!seedScenario(map, roads, store)) {
    std::cerr << "Failed to seed visualization scenario\n";
    return 1;
  }

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

        const RGB color = tileColor(map, store, coord);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

        SDL_Rect rect{tx * tilePixels, ty * tilePixels, tilePixels, tilePixels};
        SDL_RenderFillRect(renderer, &rect);
      }
    }

    SDL_RenderPresent(renderer);
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
