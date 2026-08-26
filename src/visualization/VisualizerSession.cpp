#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
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
#include "src/core/ThreadPool.hpp"
#include "src/systems/CitySimSupport.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/systems/PlayableCityTick.hpp"
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

#include "src/visualization/VisualizerSession.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/VisualizerHud.hpp"

namespace visualizer {

namespace {

PlayableCityTickState captureTickState(LiveSimulationState& liveState) {
  PlayableCityTickState tickState;
  tickState.tick = liveState.tick;
  tickState.populationTarget = liveState.populationTarget;
  tickState.deathcareState = liveState.deathcareState;
  tickState.demand = liveState.demand;
  tickState.trafficSummary = liveState.trafficSummary;
  tickState.serviceSummary = liveState.serviceSummary;
  tickState.serviceCache = std::move(liveState.serviceCache);
  tickState.waste = liveState.waste;
  tickState.deathcare = liveState.deathcare;
  tickState.economy = liveState.economy;
  tickState.illnessRate = liveState.illnessRate;
  tickState.crimeRate = liveState.crimeRate;
  tickState.treasuryRevenue = liveState.treasuryRevenue;
  tickState.treasuryExpenses = liveState.treasuryExpenses;
  tickState.treasuryNet = liveState.treasuryNet;
  tickState.treasuryShortfall = liveState.treasuryShortfall;
  tickState.lowFunds = liveState.lowFunds;
  tickState.bankrupt = liveState.bankrupt;
  tickState.transitRoutes = std::move(liveState.transitRoutes);
  tickState.transitCache = std::move(liveState.transitCache);
  tickState.transitSummary = liveState.transitSummary;
  return tickState;
}

void restoreTickState(LiveSimulationState& liveState, PlayableCityTickState& tickState) {
  liveState.tick = tickState.tick;
  liveState.populationTarget = tickState.populationTarget;
  liveState.deathcareState = tickState.deathcareState;
  liveState.demand = tickState.demand;
  liveState.trafficSummary = tickState.trafficSummary;
  liveState.serviceSummary = tickState.serviceSummary;
  liveState.serviceCache = std::move(tickState.serviceCache);
  liveState.waste = tickState.waste;
  liveState.deathcare = tickState.deathcare;
  liveState.economy = tickState.economy;
  liveState.illnessRate = tickState.illnessRate;
  liveState.crimeRate = tickState.crimeRate;
  liveState.treasuryRevenue = tickState.treasuryRevenue;
  liveState.treasuryExpenses = tickState.treasuryExpenses;
  liveState.treasuryNet = tickState.treasuryNet;
  liveState.treasuryShortfall = tickState.treasuryShortfall;
  liveState.lowFunds = tickState.lowFunds;
  liveState.bankrupt = tickState.bankrupt;
  liveState.transitRoutes = std::move(tickState.transitRoutes);
  liveState.transitCache = std::move(tickState.transitCache);
  liveState.transitSummary = tickState.transitSummary;
}

void projectTreasuryHud(LiveSimulationState& liveState, const std::vector<ServiceFacility>& facilities) {
  int64_t serviceOperatingCosts = 0;
  for (const ServiceFacility& facility : facilities) {
    serviceOperatingCosts += ServiceTool::operatingCostPerTick(facility.type);
  }
  liveState.treasuryExpenses = static_cast<int64_t>(std::llround(
    static_cast<double>(liveState.economy.totalExpenses) * 0.01)) + serviceOperatingCosts;
}

}  // namespace

void updatePlayableUtilityConnectivity(
  CityMap& map,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities
) {
  updateUtilityConnectivityFromFacilities(map, roads, facilities);
}

void refreshLiveDerivedState(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  LiveSimulationState& liveState,
  uint32_t seed,
  bool reallocPopulation,
  bool runTraffic
) {
  PlayableCityTickState tickState = captureTickState(liveState);
  DerivedCityRefreshOptions options;
  options.reallocPopulation = reallocPopulation;
  options.runTraffic = runTraffic;
  options.enableTransit = true;
  options.placeTransit = false;
  options.updateLandValues = true;
  options.seed = seed;
  options.transitStopCoverageRadius = std::max(8, liveState.autonomousGridSpacing * 3);
  refreshDerivedCityState(map, roads, store, population, facilities, tickState, options);
  restoreTickState(liveState, tickState);
  projectTreasuryHud(liveState, facilities);
}

void runAutonomousGrowthStep(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  std::vector<ServiceFacility>& facilities,
  LiveSimulationState& liveState
) {
  liveState.demand = (store.getBuildingCount() > 0 || population.getTotalPopulation() > 0)
    ? CitySimulator::evaluateDemand(store, population)
    : Zoning::calculateDemand(1000u + liveState.tick * 17u);

  static thread_local ThreadPool pool(std::max(1u, std::thread::hardware_concurrency() > 0
    ? std::thread::hardware_concurrency() - 1 : 2u));

  city_sim::ConstructionOptions constructionOpts;
  constructionOpts.gridSpacing = liveState.autonomousGridSpacing;
  constructionOpts.placeFacilities = true;
  constructionOpts.includeUtilities = true;
  constructionOpts.includeWasteDeathcare = true;
  // Transit is placed inside playableCityTick; skip a second placement here.
  city_sim::expandConstruction(
    map, roads, store, population, facilities, liveState.transitRoutes,
    liveState.construction, pool, liveState.demand, constructionOpts
  );
}

void runSimulationTick(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  std::vector<ServiceFacility>& facilities,
  LiveSimulationState& liveState,
  int64_t& funds
) {
  if (liveState.autonomousGrowth) {
    runAutonomousGrowthStep(map, roads, store, population, facilities, liveState);
  }

  PlayableCityTickState tickState = captureTickState(liveState);

  PlayableCityTickOptions options;
  options.growthChance = 0.18f;
  options.requireUtilities = true;
  options.treasuryTickScale = 0.01;
  options.baseSeed = 1000u;
  options.enableTransit = true;
  options.transitCapacityMultiplier = 1.0f;
  options.transitStopCoverageRadius = std::max(8, liveState.autonomousGridSpacing * 3);

  playableCityTick(map, roads, store, population, facilities, tickState, funds, options);

  if (liveState.autonomousGrowth) {
    city_sim::applyEmptyZonedDelta(
      liveState.construction,
      static_cast<int64_t>(tickState.buildingsDemolished)
        - static_cast<int64_t>(tickState.buildingsSpawned));
  }

  restoreTickState(liveState, tickState);
  refreshRouteHeat(store, population, roads, liveState, 1000u + ((liveState.tick - 1) * 31u) + 3u);
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

}  // namespace visualizer
