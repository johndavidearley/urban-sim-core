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

#include "src/visualization/DrawList.hpp"

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/DistrictTool.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/visualization/IsometricProjection.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"

#include "src/visualization/VisualizerHud.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/VisualizerSession.hpp"
#include "src/visualization/VisualizerTypes.hpp"

#include "src/visualization/GraphicsSmoke.hpp"
#include "src/visualization/VulkanRenderer.hpp"
#include "src/visualization/WindowHost.hpp"
#define GLFW_INCLUDE_NONE
#include "src/playtest/PlaytestBridge.hpp"
#include "src/visualization/VisualizerApp.hpp"
#include <GLFW/glfw3.h>
using namespace visualizer;

int runVisualizer(int argc, char *argv[], visualizer::PlaytestBridge *bridge) {
  WindowHost host(1280, 800);
  host.playtestInput = bridge != nullptr;
  if (bridge)
    host.playtestClock = [&]() {
      return bridge->deterministic
                 ? bridge->animationTime()
                 : static_cast<uint32_t>(glfwGetTime() * 1000);
    };
  VulkanRenderer gpu(host.window());
  DrawList drawList;
  DrawList *renderer = &drawList;
  int windowWidth = 1280, windowHeight = 800;
  if (argc > 1 && (std::string(argv[1]) == "--graphics-smoke" ||
                   std::string(argv[1]) == "--graphics-benchmark")) {
    return runGraphicsSmoke(host, gpu, argc > 2 ? argv[2] : "graphics-smoke",
                            std::string(argv[1]) == "--graphics-benchmark");
  }
  constexpr const char *kSessionPath = "urban_sim_session.json";
  const StartScreenResult start =
      runStartScreen(host, gpu, renderer, windowWidth, windowHeight, bridge);
  if (start.quit)
    return 0;

  int mapSize = start.mapSize;
  std::string startupError;
  if (start.loadSession) {
    int savedWidth = 0;
    int savedHeight = 0;
    if (!GameplaySessionSystem::readMapDimensions(kSessionPath, savedWidth,
                                                  savedHeight, &startupError) ||
        savedWidth != savedHeight) {
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
  DistrictSystem districts;
  LiveSimulationState liveState;
  if (bridge)
    liveState.simulationSeed = bridge->simulationSeed;
  int64_t initialFunds = 50000;
  bool loadedAtStartup = false;

  if (start.loadSession && startupError.empty()) {
    GameplaySessionState session;
    if (GameplaySessionSystem::load(kSessionPath, map, roads, store, population,
                                    session, &startupError)) {
      facilities = std::move(session.facilities);
      initialFunds = session.funds;
      liveState.tick = session.tick;
      liveState.paused = session.paused;
      liveState.tickIntervalMs = session.tickIntervalMs;
      liveState.demand = session.demand;
      liveState.treasuryRevenue = session.treasuryRevenue;
      liveState.treasuryExpenses = session.treasuryExpenses;
      liveState.treasuryNet = session.treasuryNet;
      liveState.treasuryDebt = session.treasuryDebt;
      liveState.treasuryInterestRemainder = session.treasuryInterestRemainder;
      liveState.populationTarget = session.populationTarget;
      liveState.migrationConditions = session.migrationConditions;
      liveState.populationChange = session.populationChange;
      liveState.districtGrowthModifiers = session.growthModifiers;
      liveState.laggedEducationCoverage = session.laggedEducationCoverage;
      liveState.deathcareState.fractionalDeaths = session.fractionalDeaths;
      liveState.deathcareState.awaitingDisposition =
          session.awaitingDisposition;
      liveState.autonomousGrowth = session.autonomousGrowth;
      liveState.construction = city_sim::ConstructionState{};
      liveState.construction.extent = session.autonomousExtent;
      liveState.construction.emptyZonedCount = session.emptyZonedCount;
      liveState.transitRoutes = std::move(session.transitRoutes);
      liveState.transitCache = TransitCoverageCache{};
      applySessionDistricts(districts, session);
      loadedAtStartup = true;
    }
  }

  if (bridge && start.loadSession && !startupError.empty())
    throw std::runtime_error("Session load failed: " + startupError);

  if (!loadedAtStartup) {
    if (start.generateTerrain) {
      TerrainGenerator::generate(map, bridge ? bridge->terrainSeed : 42u);
    }
    // A new game is intentionally empty: the player establishes the first
    // road, zones, and services through the same tools used thereafter.
    facilities.clear();
    liveState.demand = Zoning::calculateDemand(1000u);
    liveState.paused = true;
  }
  refreshLiveDerivedState(map, roads, store, population, facilities, liveState,
                          98765u, false, true, loadedAtStartup);
  refreshRouteHeat(store, population, roads, liveState,
                   bridge ? bridge->simulationSeed : 98765u);

  const int minimumTilePixels =
      std::max(2, (windowWidth + mapSize - 1) / mapSize);
  int tilePixels = std::max(28, minimumTilePixels);
  int viewX = 0;
  int viewY = 0;
  bool isometricMode = true;
  bool cleanUiMode = false;
  int isometricCameraX = 0;
  int isometricCameraY = 0;
  OverlayMode overlayMode = OverlayMode::Zone;
  bool showLegend = true;
  bool roadToolActive = false;
  RoadClass selectedRoadClass = RoadClass::Local;
  bool zoneToolActive = false;
  bool bulldozeToolActive = false;
  bool serviceToolActive = false;
  bool districtToolActive = false;
  ServiceType selectedService = ServiceType::Fire;
  ZoneType selectedZone = ZoneType::Residential;
  DistrictArchetype selectedArchetype = DistrictArchetype::General;
  bool toolDragging = false;
  Coord toolDragStart{0, 0};
  RoadPlan roadPlan;
  ZonePlan zonePlan;
  BulldozePlan bulldozePlan;
  ServicePlan servicePlan;
  DistrictPlan districtPlan;
  DistrictId selectedDistrictId = 0;
  constexpr int64_t kStarvedDistrictBudgetCap = 1;
  int64_t funds = initialFunds;
  liveState.lowFunds = funds > 0 && funds < 5000;
  liveState.bankrupt = funds == 0 && liveState.treasuryDebt > 0;
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

  auto isometricProjection = [&]() {
    const int height = std::max(6, tilePixels / 2);
    const int centeredY = std::max(18, (windowHeight - mapSize * height) / 2);
    return IsometricProjection(
        tilePixels, height,
        {windowWidth / 2 - (viewX - viewY) * (tilePixels / 2) +
             isometricCameraX,
         centeredY - (viewX + viewY) * (height / 2) + isometricCameraY});
  };
  auto tileAtScreen = [&](int screenX, int screenY) {
    if (isometricMode) {
      return isometricProjection().screenToTile({screenX, screenY});
    }
    return Coord{viewX + screenX / tilePixels, viewY + screenY / tilePixels};
  };
  auto focusDevelopedArea = [&]() {
    int minX = mapSize;
    int minY = mapSize;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < mapSize; ++y) {
      for (int x = 0; x < mapSize; ++x) {
        const Tile &tile = map.getTile({x, y});
        if (!tile.hasRoad && tile.buildingId == 0 && tile.zone == 0)
          continue;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
      }
    }
    for (const ServiceFacility &facility : facilities) {
      minX = std::min(minX, facility.position.x);
      minY = std::min(minY, facility.position.y);
      maxX = std::max(maxX, facility.position.x);
      maxY = std::max(maxY, facility.position.y);
    }
    const Coord center = maxX >= 0 ? Coord{(minX + maxX) / 2, (minY + maxY) / 2}
                                   : Coord{mapSize / 2, mapSize / 2};
    if (!isometricMode) {
      const int visibleX = std::max(1, windowWidth / tilePixels);
      const int visibleY = std::max(1, windowHeight / tilePixels);
      viewX = std::clamp(center.x - visibleX / 2, 0, std::max(0, mapSize - visibleX));
      viewY = std::clamp(center.y - visibleY / 2, 0, std::max(0, mapSize - visibleY));
    }
    isometricCameraX = 0;
    isometricCameraY = 0;
    const ScreenPoint projected = isometricProjection().tileCenter(center);
    isometricCameraX += windowWidth / 2 - projected.x;
    isometricCameraY += windowHeight / 2 - projected.y;
  };
  focusDevelopedArea();

  bool lastActionSuccess = true;
  auto notify = [&](const std::string &message, bool success) {
    lastActionSuccess = success;
    toast.message = message;
    toast.color = success ? RGB{105, 230, 135} : RGB{255, 125, 110};
    toast.expiresAtMs = host.timeMs() + 2400u;
  };
  if (!startupError.empty()) {
    notify("LOAD FAILED - NEW CITY STARTED", false);
  } else if (loadedAtStartup) {
    notify("GAME LOADED", true);
  }

  auto executeSimulationTick = [&]() {
    if (bridge)
      ++bridge->revision;
    const bool wasLow = liveState.lowFunds;
    const bool wasBankrupt = liveState.bankrupt;
    const bool wasInDebt = liveState.treasuryDebt > 0;
    runSimulationTick(map, roads, store, population, facilities, districts,
                      liveState, funds);
    sessionDirty = true;
    if (liveState.treasuryDebt > 0 && !wasInDebt) {
      notify("CITY TOOK ON DEBT $" + std::to_string(liveState.treasuryDebt),
             false);
    } else if (liveState.treasuryDebt == 0 && wasInDebt) {
      notify("DEBT PAID OFF", true);
    } else if (liveState.bankrupt && !wasBankrupt) {
      notify("BUDGET DEFICIT CANNOT BE PAID", false);
    } else if (liveState.lowFunds && !wasLow) {
      notify("LOW FUNDS - WATCH THE BUDGET", false);
    } else if (!liveState.lowFunds && !liveState.bankrupt &&
               liveState.treasuryDebt == 0 && (wasLow || wasBankrupt)) {
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
    session.treasuryDebt = liveState.treasuryDebt;
    session.treasuryInterestRemainder = liveState.treasuryInterestRemainder;
    session.populationTarget = liveState.populationTarget;
    session.migrationConditions = liveState.migrationConditions;
    session.populationChange = liveState.populationChange;
    session.growthModifiers = liveState.districtGrowthModifiers;
    session.laggedEducationCoverage = liveState.laggedEducationCoverage;
    session.fractionalDeaths = liveState.deathcareState.fractionalDeaths;
    session.awaitingDisposition = liveState.deathcareState.awaitingDisposition;
    session.autonomousGrowth = liveState.autonomousGrowth;
    session.autonomousExtent = liveState.construction.extent;
    session.emptyZonedCount = liveState.construction.emptyZonedCount;
    session.facilities = facilities;
    session.transitRoutes = liveState.transitRoutes;
    captureSessionDistricts(districts, session);
    std::string error;
    if (GameplaySessionSystem::save(kSessionPath, map, roads, store, population,
                                    session, &error)) {
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
    if (!GameplaySessionSystem::load(kSessionPath, map, roads, store,
                                     population, session, &error)) {
      notify(error.empty() ? "LOAD FAILED" : error, false);
      return false;
    }
    funds = session.funds;
    liveState.tick = session.tick;
    liveState.paused = session.paused;
    liveState.tickIntervalMs = session.tickIntervalMs;
    liveState.demand = session.demand;
    liveState.treasuryRevenue = session.treasuryRevenue;
    liveState.treasuryExpenses = session.treasuryExpenses;
    liveState.treasuryNet = session.treasuryNet;
    liveState.treasuryDebt = session.treasuryDebt;
    liveState.treasuryInterestRemainder = session.treasuryInterestRemainder;
    liveState.treasuryDebtIssued = 0;
    liveState.treasuryDebtRepaid = 0;
    liveState.treasuryInterestCharged = 0;
    liveState.treasuryShortfall = 0;
    liveState.lowFunds = funds > 0 && funds < 5000;
    liveState.bankrupt = funds == 0 && liveState.treasuryDebt > 0;
    liveState.populationTarget = session.populationTarget;
    liveState.migrationConditions = session.migrationConditions;
    liveState.populationChange = session.populationChange;
    liveState.districtGrowthModifiers = session.growthModifiers;
    liveState.laggedEducationCoverage = session.laggedEducationCoverage;
    liveState.deathcareState.fractionalDeaths = session.fractionalDeaths;
    liveState.deathcareState.awaitingDisposition = session.awaitingDisposition;
    liveState.autonomousGrowth = session.autonomousGrowth;
    liveState.construction = city_sim::ConstructionState{};
    liveState.construction.extent = session.autonomousExtent;
    liveState.construction.emptyZonedCount = session.emptyZonedCount;
    liveState.lastTickMs = host.timeMs();
    liveState.transitRoutes = std::move(session.transitRoutes);
    liveState.transitCache = TransitCoverageCache{};
    liveState.transitSummary = TransitSummary{};
    liveState.serviceCache = ServiceCoverageCache{};
    facilities = std::move(session.facilities);
    applySessionDistricts(districts, session);
    refreshLiveDerivedState(map, roads, store, population, facilities,
                            liveState, 3001u + (liveState.tick * 31u), false,
                            true, true);
    refreshRouteHeat(store, population, roads, liveState,
                     3001u + (liveState.tick * 31u));
    roadToolActive = false;
    zoneToolActive = false;
    bulldozeToolActive = false;
    serviceToolActive = false;
    districtToolActive = false;
    toolDragging = false;
    roadPlan = {};
    zonePlan = {};
    bulldozePlan = {};
    servicePlan = {};
    districtPlan = {};
    selectedDistrictId = 0;
    onboardingStep = 6;
    showOnboarding = false;
    sessionDirty = false;
    notify("GAME LOADED", true);
    return true;
  };

  auto activatePaletteTool = [&](PaletteTool tool) {
    if (tool == PaletteTool::Zone && zoneToolActive) {
      selectedZone = nextPlayableZone(selectedZone);
    } else if (tool == PaletteTool::Service && serviceToolActive) {
      selectedService = nextPlayableService(selectedService);
    } else if (tool == PaletteTool::District && districtToolActive) {
      selectedArchetype = nextPlayableArchetype(selectedArchetype);
    }
    roadToolActive = tool == PaletteTool::Road;
    zoneToolActive = tool == PaletteTool::Zone;
    bulldozeToolActive = tool == PaletteTool::Bulldoze;
    serviceToolActive = tool == PaletteTool::Service;
    districtToolActive = tool == PaletteTool::District;
    toolDragging = false;
    roadPlan = {};
    zonePlan = {};
    bulldozePlan = {};
    servicePlan = {};
    districtPlan = {};
  };

  auto applyService = [&](Coord tile) {
    servicePlan =
        ServiceTool::plan(map, roads, facilities, selectedService, tile, funds);
    const int64_t cost = servicePlan.cost;
    if (ServiceTool::build(map, roads, facilities, servicePlan, funds)) {
      sessionDirty = true;
      refreshLiveDerivedState(map, roads, store, population, facilities,
                              liveState, 1001u + (liveState.tick * 31u),
                              /*reallocPopulation=*/false,
                              /*runTraffic=*/false);
      notify(std::string(ServiceSystem::serviceTypeToString(selectedService)) +
                 " BUILT $" + std::to_string(cost),
             true);
      if (onboardingStep == 3 && selectedService == ServiceType::Power) {
        onboardingStep = 4;
      } else if (onboardingStep == 4 && selectedService == ServiceType::Water) {
        onboardingStep = 5;
      } else if (onboardingStep == 5 &&
                 (selectedService == ServiceType::Fire ||
                  selectedService == ServiceType::Police ||
                  selectedService == ServiceType::Health ||
                  selectedService == ServiceType::Education)) {
        onboardingStep = 6;
        showOnboarding = false;
        notify("CITY GUIDE COMPLETE", true);
      }
    } else {
      notify(servicePlan.error.empty() ? "SERVICE BUILD FAILED"
                                       : servicePlan.error,
             false);
    }
    servicePlan =
        ServiceTool::plan(map, roads, facilities, selectedService, tile, funds);
  };
  auto applyPlacement = [&](Coord tile) {
    if (roadToolActive) {
      roadPlan = RoadTool::plan(map, roads, toolDragStart, tile, funds,
                                selectedRoadClass);
      const int64_t cost = roadPlan.cost;
      if (RoadTool::build(map, roads, roadPlan, funds)) {
        sessionDirty = true;
        refreshLiveDerivedState(map, roads, store, population, facilities,
                                liveState, 1003u + (liveState.tick * 31u),
                                /*reallocPopulation=*/false,
                                /*runTraffic=*/false);
        refreshRouteHeat(store, population, roads, liveState,
                         1003u + (liveState.tick * 31u));
        notify("ROAD BUILT $" + std::to_string(cost), true);
        if (onboardingStep == 0) {
          onboardingStep = 1;
        }
      } else {
        notify(roadPlan.error.empty() ? "ROAD BUILD FAILED" : roadPlan.error,
               false);
      }
    } else if (zoneToolActive) {
      zonePlan = ZoneTool::plan(map, toolDragStart, tile, selectedZone, funds, &facilities);
      const int64_t cost = zonePlan.cost;
      if (ZoneTool::apply(map, zonePlan, funds, &facilities)) {
        sessionDirty = true;
        refreshLiveDerivedState(map, roads, store, population, facilities,
                                liveState, 1003u + liveState.tick * 31u,
                                /*reallocPopulation=*/false, /*runTraffic=*/false);
        notify("ZONE APPLIED $" + std::to_string(cost), true);
        if (onboardingStep == 1) {
          onboardingStep = 2;
        }
      } else {
        notify(zonePlan.error.empty() ? "ZONING FAILED" : zonePlan.error,
               false);
      }
    } else if (bulldozeToolActive) {
      bulldozePlan = BulldozeTool::plan(map, roads, facilities, toolDragStart,
                                        tile, funds);
      const int64_t cost = bulldozePlan.cost;
      if (BulldozeTool::apply(map, roads, store, facilities, bulldozePlan,
                              funds)) {
        sessionDirty = true;
        refreshLiveDerivedState(map, roads, store, population, facilities,
                                liveState, 2005u + (liveState.tick * 31u),
                                /*reallocPopulation=*/true,
                                /*runTraffic=*/true);
        refreshRouteHeat(store, population, roads, liveState,
                         2005u + (liveState.tick * 31u));
        notify("DEMOLISHED $" + std::to_string(cost), true);
      } else {
        notify(bulldozePlan.error.empty() ? "DEMOLITION FAILED"
                                          : bulldozePlan.error,
               false);
      }
    } else if (districtToolActive) {
      const bool singleTile =
          toolDragStart.x == tile.x && toolDragStart.y == tile.y;
      const DistrictId underCursor = DistrictTool::districtAt(districts, tile);
      if (singleTile && underCursor != 0) {
        selectedDistrictId = underCursor;
        notify("DISTRICT SELECTED", true);
      } else {
        districtPlan = DistrictTool::plan(map, districts, toolDragStart, tile,
                                          selectedArchetype);
        if (DistrictTool::apply(districts, map, districtPlan)) {
          sessionDirty = true;
          if (districtPlan.removeId != 0) {
            if (selectedDistrictId == districtPlan.removeId) {
              selectedDistrictId = 0;
            }
            notify("DISTRICT REMOVED", true);
          } else {
            if (!isometricMode && !districts.getDistricts().empty()) {
              selectedDistrictId = districts.getDistricts().back().id;
            }
            notify(std::string("DISTRICT ") +
                       DistrictSystem::archetypeToString(selectedArchetype),
                   true);
          }
        } else {
          notify(districtPlan.error.empty() ? "DISTRICT FAILED"
                                            : districtPlan.error,
                 false);
        }
      }
    }
  };
  bool running = true;
  bool suspended = false;
  auto state = [&]() {
    Json result = bridge->metadata(
        host, gpu, showQuitDialog ? "quit_dialog" : "gameplay", liveState.tick);
    result.update(
        {{"population_change",
          {{"arrivals", liveState.populationChange.arrivals},
           {"departures", liveState.populationChange.departures},
           {"deaths", liveState.populationChange.deaths},
           {"net", liveState.populationChange.net},
           {"desirability", liveState.populationChange.desirability}}},
         {"funds", funds},
         {"paused", liveState.paused},
         {"dirty", sessionDirty},
         {"population", population.getTotalPopulation()},
         {"buildings", store.getBuildingCount()},
         {"facilities", facilities.size()},
         {"park_tiles", liveState.economy.parkTiles},
         {"park_upkeep", liveState.economy.parkTiles * 2},
         {"districts", districts.getDistricts().size()},
         {"routes", liveState.transitRoutes.size()},
         {"debt", liveState.treasuryDebt},
         {"revenue", liveState.treasuryRevenue},
         {"expenses", liveState.treasuryExpenses},
         {"autonomous_growth", liveState.autonomousGrowth},
         {"overlay", static_cast<int>(overlayMode)},
         {"projection", isometricMode ? "isometric" : "top_down"},
         {"tile_pixels", tilePixels},
         {"camera",
          {{"view_x", viewX},
           {"view_y", viewY},
           {"offset_x", isometricCameraX},
           {"offset_y", isometricCameraY}}},
         {"tools",
          {{"road", roadToolActive},
           {"road_class",
            selectedRoadClass == RoadClass::Arterial ? "arterial" : "local"},
           {"zone", zoneToolActive},
           {"bulldoze", bulldozeToolActive},
           {"service", serviceToolActive},
           {"district", districtToolActive},
           {"dragging", toolDragging},
           {"zone_type", static_cast<int>(selectedZone)},
           {"service_type", static_cast<int>(selectedService)},
           {"archetype", static_cast<int>(selectedArchetype)}}},
         {"preview",
          {{"road_error", roadPlan.error},
           {"zone_error", zonePlan.error},
           {"bulldoze_error", bulldozePlan.error},
           {"service_error", servicePlan.error},
           {"district_error", districtPlan.error},
           {"road",
            {{"valid", roadPlan.valid},
             {"cost", roadPlan.cost},
             {"new_segments", roadPlan.newSegments},
             {"upgraded_segments", roadPlan.upgradedSegments},
             {"road_class", roadPlan.roadClass == RoadClass::Arterial ? "arterial" : "local"},
             {"tiles", roadPlan.tiles.size()}}},
           {"zone",
            {{"valid", zonePlan.valid},
             {"cost", zonePlan.cost},
             {"tiles", zonePlan.tiles.size()}}},
           {"bulldoze",
            {{"valid", bulldozePlan.valid},
             {"cost", bulldozePlan.cost},
             {"tiles", bulldozePlan.tiles.size()}}},
           {"service",
            {{"valid", servicePlan.valid},
             {"cost", servicePlan.cost},
             {"has_site", servicePlan.hasSite},
             {"x", servicePlan.facility.position.x},
             {"y", servicePlan.facility.position.y}}},
           {"district",
            {{"valid", districtPlan.valid},
             {"tiles", districtPlan.tiles.size()}}}}},
         {"metrics",
          {{"illness_rate", liveState.illnessRate},
           {"crime_rate", liveState.crimeRate},
           {"economic_health", liveState.economy.economicHealth},
           {"average_land_value", liveState.economy.averageLandValue},
           {"commuting_population",
            liveState.trafficSummary.commutingPopulation},
           {"average_commute_time",
            liveState.trafficSummary.averageCommuteTime},
           {"average_congestion",
            liveState.trafficSummary.averageEdgeCongestion},
           {"fire_coverage", liveState.serviceSummary.fireCoverage},
           {"police_coverage", liveState.serviceSummary.policeCoverage},
           {"health_coverage", liveState.serviceSummary.healthCoverage},
           {"education_coverage", liveState.serviceSummary.educationCoverage},
           {"power_coverage", liveState.serviceSummary.powerCoverage},
           {"water_coverage", liveState.serviceSummary.waterCoverage},
           {"sanitation_coverage", liveState.serviceSummary.sanitationCoverage},
           {"garbage_coverage", liveState.serviceSummary.garbageCoverage},
           {"recycling_coverage", liveState.serviceSummary.recyclingCoverage},
           {"cemetery_coverage", liveState.serviceSummary.cemeteryCoverage},
           {"crematorium_coverage",
            liveState.serviceSummary.crematoriumCoverage},
           {"power_demand_mw", liveState.serviceSummary.powerDemandMW},
           {"power_supply_mw", liveState.serviceSummary.powerGenerationMW},
           {"power_supply_ratio", liveState.serviceSummary.powerSupplyRatio},
           {"water_demand", liveState.serviceSummary.waterDemand},
           {"water_supply", liveState.serviceSummary.waterSupply},
           {"water_supply_ratio", liveState.serviceSummary.waterSupplyRatio},
           {"deathcare_backlog", liveState.deathcareState.awaitingDisposition},
           {"freight_trucks", liveState.trafficSummary.freight.trucks},
           {"freight_routes", liveState.trafficSummary.freight.routes},
           {"freight_domestic_goods",
            liveState.trafficSummary.freight.domesticGoods},
           {"freight_exported_goods",
            liveState.trafficSummary.freight.exportedGoods},
           {"freight_imported_goods",
            liveState.trafficSummary.freight.importedGoods},
           {"freight_unrouted_goods",
            liveState.trafficSummary.freight.unroutedGoods},
           {"freight_gate",
            {{"x", 0},
             {"y", mapSize / 2},
             {"connected", liveState.trafficSummary.freight.gateConnected}}}}},
         {"demand",
          {{"residential", liveState.demand.residential},
           {"commercial", liveState.demand.commercial},
           {"industrial", liveState.demand.industrial},
           {"office", liveState.demand.office}}},
         {"ui",
          {{"legend", showLegend},
           {"compact", cleanUiMode},
           {"large_text", gReadableUiText},
           {"onboarding", showOnboarding},
           {"onboarding_step", onboardingStep},
           {"selected_district", selectedDistrictId}}},
         {"toast", toast.message},
         {"speed_interval_ms", liveState.tickIntervalMs}});
    size_t roadTiles = 0, zonedTiles = 0;
    for (int y = 0; y < mapSize; ++y)
      for (int x = 0; x < mapSize; ++x) {
        const auto &t = map.getTile({x, y});
        roadTiles += t.hasRoad;
        zonedTiles += t.zone != 0;
      }
    result["road_tiles"] = roadTiles;
    result["zoned_tiles"] = zonedTiles;
    result["map_size"] = mapSize;
    return result;
  };
  while (running) {
    if (bridge) {
      if (bridge->disconnected())
        break;
      bridge->begin(host,
                    [&](const std::string &command, const Json &args) -> Json {
#include "src/playtest/GameplayCommands.inc"
                    });
    }
    host.dimensions(windowWidth, windowHeight);
    renderer->frameTimeMs = bridge && bridge->deterministic
                                ? bridge->animationTime()
                                : host.timeMs();
    InputEvent event;
    while (host.pollEvent(event)) {
      host.dimensions(windowWidth, windowHeight);
      if (event.type == EventType::FocusLost) {
        toolDragging = false;
        cameraPanning = false;
        roadPlan = {};
        zonePlan = {};
        bulldozePlan = {};
        servicePlan = {};
        districtPlan = {};
        continue;
      }
      if (event.type == EventType::MouseMotion) {
        mouseX = event.motion.x;
        mouseY = event.motion.y;
      } else if (event.type == EventType::MouseDown ||
                 event.type == EventType::MouseUp) {
        mouseX = event.button.x;
        mouseY = event.button.y;
      }
      if (showQuitDialog) {
        if (event.type == EventType::KeyDown && event.key.sym == Key::Escape) {
          showQuitDialog = false;
        } else if (event.type == EventType::MouseDown &&
                   event.button.button == MouseButton::Left) {
          switch (quitHitTest(event.button.x, event.button.y, windowWidth,
                              windowHeight)) {
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
      if (event.type == EventType::Quit) {
        showQuitDialog = true;
      } else if (event.type == EventType::KeyDown) {
        switch (event.key.sym) {
        case Key::Escape:
          if (toolDragging || roadToolActive || zoneToolActive ||
              bulldozeToolActive || serviceToolActive || districtToolActive) {
            toolDragging = false;
            roadToolActive = false;
            zoneToolActive = false;
            bulldozeToolActive = false;
            serviceToolActive = false;
            districtToolActive = false;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
            servicePlan = {};
            districtPlan = {};
          } else {
            showQuitDialog = true;
          }
          break;
        case Key::Equal:
        case Key::Plus:
          tilePixels = std::min(48, tilePixels + 1);
          break;
        case Key::Minus:
          tilePixels = std::max(minimumTilePixels, tilePixels - 1);
          break;
        case Key::Left:
          if (isometricMode)
            isometricCameraX += 18;
          else
            viewX = std::max(0, viewX - 1);
          break;
        case Key::Right:
          if (isometricMode)
            isometricCameraX -= 18;
          else
            viewX = std::min(mapSize - 1, viewX + 1);
          break;
        case Key::Up:
          if (isometricMode)
            isometricCameraY += 18;
          else
            viewY = std::max(0, viewY - 1);
          break;
        case Key::Down:
          if (isometricMode)
            isometricCameraY -= 18;
          else
            viewY = std::min(mapSize - 1, viewY + 1);
          break;
        case Key::Num1:
          overlayMode = OverlayMode::Zone;
          break;
        case Key::Num2:
          overlayMode = OverlayMode::LandValue;
          break;
        case Key::Num3:
          overlayMode = OverlayMode::Pollution;
          break;
        case Key::Num4:
          overlayMode = OverlayMode::ServiceCoverage;
          break;
        case Key::Num5:
          overlayMode = OverlayMode::TrafficCongestion;
          break;
        case Key::Num6:
          overlayMode = OverlayMode::Demand;
          break;
        case Key::Num7:
          overlayMode = OverlayMode::Happiness;
          break;
        case Key::Num8:
          overlayMode = OverlayMode::RouteHeatmap;
          break;
        case Key::Space:
          liveState.paused = !liveState.paused;
          sessionDirty = true;
          break;
        case Key::Period:
        case Key::N:
          if (liveState.paused) {
            executeSimulationTick();
          }
          break;
        case Key::H:
          showLegend = !showLegend;
          break;
        case Key::F1:
          showOnboarding = !showOnboarding;
          break;
        case Key::F3:
          isometricMode = !isometricMode;
          toolDragging = false;
          roadPlan = {};
          zonePlan = {};
          bulldozePlan = {};
          notify(isometricMode ? "ISOMETRIC VIEW" : "TOP DOWN VIEW", true);
          break;
        case Key::F4:
          cleanUiMode = !cleanUiMode;
          notify(cleanUiMode ? "CLEAN UI" : "DETAIL UI", true);
          break;
        case Key::F:
          if (isometricMode) {
            focusDevelopedArea();
            notify("CAMERA FOCUSED", true);
          }
          break;
        case Key::F2:
          gReadableUiText = !gReadableUiText;
          notify(gReadableUiText ? "LARGE TEXT ON" : "COMPACT TEXT ON", true);
          break;
        case Key::G:
          liveState.autonomousGrowth = !liveState.autonomousGrowth;
          notify(liveState.autonomousGrowth
                     ? "AUTONOMOUS GROWTH ON (CITYSIM ROADS/ZONES/SERVICES)"
                     : "AUTONOMOUS GROWTH OFF (PLAYER BUILD ONLY)",
                 true);
          break;
        case Key::F5:
          saveSession();
          break;
        case Key::F9:
          loadSession();
          break;
        case Key::A:
          if (roadToolActive && !event.key.repeat) {
            selectedRoadClass = selectedRoadClass == RoadClass::Local
                                    ? RoadClass::Arterial
                                    : RoadClass::Local;
            toolDragging = false;
            roadPlan = {};
            notify(selectedRoadClass == RoadClass::Arterial
                       ? "ARTERIAL ROAD - DRAG TO BUILD OR UPGRADE"
                       : "LOCAL ROAD",
                   true);
          }
          break;
        case Key::R:
          roadToolActive = !roadToolActive;
          zoneToolActive = false;
          bulldozeToolActive = false;
          serviceToolActive = false;
          districtToolActive = false;
          toolDragging = false;
          roadPlan = {};
          zonePlan = {};
          bulldozePlan = {};
          servicePlan = {};
          districtPlan = {};
          break;
        case Key::Z:
          if (zoneToolActive) {
            selectedZone = nextPlayableZone(selectedZone);
          } else {
            zoneToolActive = true;
            selectedZone = ZoneType::Residential;
          }
          roadToolActive = false;
          bulldozeToolActive = false;
          serviceToolActive = false;
          districtToolActive = false;
          toolDragging = false;
          roadPlan = {};
          zonePlan = {};
          bulldozePlan = {};
          servicePlan = {};
          districtPlan = {};
          break;
        case Key::B:
          bulldozeToolActive = !bulldozeToolActive;
          roadToolActive = false;
          zoneToolActive = false;
          serviceToolActive = false;
          districtToolActive = false;
          toolDragging = false;
          roadPlan = {};
          zonePlan = {};
          bulldozePlan = {};
          servicePlan = {};
          districtPlan = {};
          break;
        case Key::S:
          if (serviceToolActive) {
            selectedService = nextPlayableService(selectedService);
          } else {
            serviceToolActive = true;
            selectedService = ServiceType::Fire;
          }
          roadToolActive = false;
          zoneToolActive = false;
          bulldozeToolActive = false;
          districtToolActive = false;
          toolDragging = false;
          roadPlan = {};
          zonePlan = {};
          bulldozePlan = {};
          servicePlan = {};
          districtPlan = {};
          break;
        case Key::P:
          if (districtToolActive) {
            selectedArchetype = nextPlayableArchetype(selectedArchetype);
          } else {
            districtToolActive = true;
            selectedArchetype = DistrictArchetype::General;
          }
          roadToolActive = false;
          zoneToolActive = false;
          bulldozeToolActive = false;
          serviceToolActive = false;
          toolDragging = false;
          roadPlan = {};
          zonePlan = {};
          bulldozePlan = {};
          servicePlan = {};
          districtPlan = {};
          break;
        case Key::O:
          cycleOriginFilter(liveState, store);
          refreshRouteHeat(store, population, roads, liveState,
                           1003u + (liveState.tick * 31u));
          break;
        case Key::D:
          cycleDestinationFilter(liveState, store);
          refreshRouteHeat(store, population, roads, liveState,
                           1003u + (liveState.tick * 31u));
          break;
        case Key::C:
          clearRouteFilters(liveState);
          refreshRouteHeat(store, population, roads, liveState,
                           1003u + (liveState.tick * 31u));
          break;
        default:
          break;
        }
      } else if (event.type == EventType::MouseDown &&
                 event.button.button == MouseButton::Middle) {
        cameraPanning = true;
        cameraLastX = event.button.x;
        cameraLastY = event.button.y;
        cameraPanRemainderX = 0;
        cameraPanRemainderY = 0;
      } else if (event.type == EventType::MouseUp &&
                 event.button.button == MouseButton::Middle) {
        cameraPanning = false;
      } else if (event.type == EventType::Scroll) {
        host.mousePosition(&mouseX, &mouseY);
        const bool overHud = mouseX >= windowWidth - kHudPanelWidth - 12 &&
                             mouseY >= 12 && mouseY < 12 + kHudPanelHeight;
        const bool overLegend = showLegend && mouseX >= 12 && mouseX < 532 &&
                                mouseY >= 12 && mouseY < 158;
        const bool overPalette =
            paletteHitTest(mouseX, mouseY, windowWidth, windowHeight) !=
            PaletteTool::None;
        const bool overDistrictPanel =
            !cleanUiMode && selectedDistrictId != 0 &&
            districts.getDistrictConst(selectedDistrictId) != nullptr &&
            pointInDistrictPanel(mouseX, mouseY);
        if (!overHud && !overLegend && !overPalette && !overDistrictPanel &&
            event.wheel.y != 0) {
          const Coord anchor = tileAtScreen(mouseX, mouseY);
          const int direction = event.wheel.y > 0 ? 1 : -1;
          tilePixels = std::max(minimumTilePixels,
                                std::min(48, tilePixels + direction * 2));
          if (isometricMode) {
            const ScreenPoint projected =
                isometricProjection().tileCenter(anchor);
            isometricCameraX += mouseX - projected.x;
            isometricCameraY += mouseY - projected.y;
          } else {
            viewX = anchor.x - mouseX / tilePixels;
            viewY = anchor.y - mouseY / tilePixels;
          }
        }
      } else if (event.type == EventType::MouseMotion && cameraPanning) {
        if (isometricMode) {
          isometricCameraX += event.motion.x - cameraLastX;
          isometricCameraY += event.motion.y - cameraLastY;
          cameraLastX = event.motion.x;
          cameraLastY = event.motion.y;
          continue;
        }
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
      } else if (event.type == EventType::MouseDown &&
                 event.button.button == MouseButton::Left && showLegend &&
                 overlayHitTest(event.button.x, event.button.y,
                                clickedOverlay)) {
        overlayMode = clickedOverlay;
      } else if (event.type == EventType::MouseDown &&
                 event.button.button == MouseButton::Left && !cleanUiMode &&
                 hudHitTest(event.button.x, event.button.y, windowWidth) !=
                     HudAction::None) {
        const HudAction action =
            hudHitTest(event.button.x, event.button.y, windowWidth);
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
      } else if (event.type == EventType::MouseDown &&
                 event.button.button == MouseButton::Left && !cleanUiMode &&
                 selectedDistrictId != 0 &&
                 districts.getDistrict(selectedDistrictId) != nullptr &&
                 districtPanelHitTest(event.button.x, event.button.y) !=
                     DistrictPanelAction::None) {
        District *district = districts.getDistrict(selectedDistrictId);
        switch (districtPanelHitTest(event.button.x, event.button.y)) {
        case DistrictPanelAction::CycleArchetype: {
          const DistrictArchetype next =
              nextPlayableArchetype(district->archetype);
          if (districts.setDistrictArchetype(selectedDistrictId, next)) {
            sessionDirty = true;
            notify(std::string("DISTRICT ") +
                       DistrictSystem::archetypeToString(next),
                   true);
          }
          break;
        }
        case DistrictPanelAction::ToggleCap: {
          const bool starve = district->serviceBudgetCap < 0;
          if (districts.setDistrictServiceBudgetCap(
                  selectedDistrictId,
                  starve ? kStarvedDistrictBudgetCap : int64_t{-1})) {
            sessionDirty = true;
            notify(starve ? "DISTRICT GROWTH STARVED"
                          : "DISTRICT GROWTH FUNDED",
                   true);
          }
          break;
        }
        case DistrictPanelAction::None:
        default:
          break;
        }
      } else if (event.type == EventType::MouseDown &&
                 event.button.button == MouseButton::Left &&
                 paletteHitTest(event.button.x, event.button.y, windowWidth,
                                windowHeight) != PaletteTool::None) {
        activatePaletteTool(paletteHitTest(event.button.x, event.button.y,
                                           windowWidth, windowHeight));
      } else if (event.type == EventType::MouseDown && serviceToolActive) {
        if (event.button.button == MouseButton::Right) {
          servicePlan = {};
        } else if (event.button.button == MouseButton::Left) {
          const Coord tile = tileAtScreen(event.button.x, event.button.y);
          applyService(tile);
        }
      } else if (event.type == EventType::MouseDown &&
                 (roadToolActive || zoneToolActive || bulldozeToolActive ||
                  districtToolActive)) {
        if (event.button.button == MouseButton::Right) {
          if (districtToolActive && !toolDragging) {
            const Coord tile = tileAtScreen(event.button.x, event.button.y);
            const DistrictId id = DistrictTool::districtAt(districts, tile);
            if (id != 0 && districts.deleteDistrict(id)) {
              if (selectedDistrictId == id) {
                selectedDistrictId = 0;
              }
              sessionDirty = true;
              notify("DISTRICT REMOVED", true);
            }
          }
          toolDragging = false;
          roadPlan = {};
          zonePlan = {};
          bulldozePlan = {};
          districtPlan = {};
        } else if (event.button.button == MouseButton::Left) {
          const Coord tile = tileAtScreen(event.button.x, event.button.y);
          if (map.isValid(tile)) {
            toolDragging = true;
            toolDragStart = tile;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
            districtPlan = {};
          }
        }
      } else if (event.type == EventType::MouseMotion && serviceToolActive) {
        const Coord tile = tileAtScreen(event.motion.x, event.motion.y);
        servicePlan = ServiceTool::plan(map, roads, facilities, selectedService,
                                        tile, funds);
      } else if (event.type == EventType::MouseMotion && toolDragging) {
        const Coord tile = tileAtScreen(event.motion.x, event.motion.y);
        if (map.isValid(tile)) {
          if (roadToolActive) {
            roadPlan = RoadTool::plan(map, roads, toolDragStart, tile, funds,
                                      selectedRoadClass);
          } else if (zoneToolActive) {
            zonePlan =
                ZoneTool::plan(map, toolDragStart, tile, selectedZone, funds, &facilities);
          } else if (bulldozeToolActive) {
            bulldozePlan = BulldozeTool::plan(map, roads, facilities,
                                              toolDragStart, tile, funds);
          } else if (districtToolActive) {
            districtPlan = DistrictTool::plan(map, districts, toolDragStart,
                                              tile, selectedArchetype);
          }
        }
      } else if (event.type == EventType::MouseUp && toolDragging &&
                 event.button.button == MouseButton::Left) {
        const Coord tile = tileAtScreen(event.button.x, event.button.y);
        if (map.isValid(tile)) {
          applyPlacement(tile);
        }
        toolDragging = false;
        roadPlan = {};
        zonePlan = {};
        bulldozePlan = {};
        districtPlan = {};
      }
    }

    if (onboardingStep == 2 && !liveState.paused) {
      onboardingStep = 3;
    }

    if (!running) {
      if (bridge)
        bridge->complete(state());
      break;
    }
    host.dimensions(windowWidth, windowHeight);
    if (!host.drawable()) {
      suspended = true;
      if (bridge)
        bridge->complete(state());
      if (running)
        host.waitEvents();
      continue;
    }
    if (suspended) {
      liveState.lastTickMs = host.timeMs();
      suspended = false;
    }
    const int visibleTilesX = std::max(1, windowWidth / tilePixels);
    const int visibleTilesY = std::max(1, windowHeight / tilePixels);
    const int maxViewX = std::max(0, mapSize - visibleTilesX);
    const int maxViewY = std::max(0, mapSize - visibleTilesY);
    viewX = std::min(viewX, maxViewX);
    viewY = std::min(viewY, maxViewY);
    viewX = std::max(0, viewX);
    viewY = std::max(0, viewY);

    const uint32_t nowMs = host.timeMs();
    renderer->frameTimeMs =
        bridge && bridge->deterministic ? bridge->animationTime() : nowMs;
    if ((!bridge || !bridge->deterministic) && !showQuitDialog &&
        !liveState.paused &&
        (nowMs - liveState.lastTickMs) >= liveState.tickIntervalMs) {
      executeSimulationTick();
      liveState.lastTickMs = nowMs;
    }

    setDrawColor(renderer, 91, 108, 91, 255);
    clearFrame(renderer);

    if (isometricMode) {
      const IsometricProjection projection = isometricProjection();
      for (int depth = 0; depth <= (mapSize - 1) * 2; ++depth) {
        for (int x = 0; x < mapSize; ++x) {
          const int y = depth - x;
          if (y < 0 || y >= mapSize)
            continue;
          drawIsometricTile(renderer, map, roads, store, facilities,
                            liveState.routeHeatByTile,
                            liveState.waste.happinessPenalty +
                                liveState.deathcare.happinessPenalty,
                            overlayMode, projection, {x, y}, &districts, selectedDistrictId);
          if (x == mapSize - 1 || y == mapSize - 1) {
            const ScreenPoint top = projection.tileTop({x, y});
            const RGB wall{67, 79, 68};
            const int halfWidth = projection.tileWidth() / 2;
            const int bottomY = top.y + projection.tileHeight();
            for (int drop = 1; drop <= 5; ++drop) {
              setDrawColor(renderer, wall.r, wall.g, wall.b, 220);
              if (x == mapSize - 1) {
                drawLine(renderer, top.x, bottomY + drop, top.x - halfWidth,
                         top.y + projection.tileHeight() / 2 + drop);
              }
              if (y == mapSize - 1) {
                drawLine(renderer, top.x, bottomY + drop, top.x + halfWidth,
                         top.y + projection.tileHeight() / 2 + drop);
              }
            }
          }
        }
      }
    } else {
      for (int ty = 0; ty < visibleTilesY; ++ty) {
        for (int tx = 0; tx < visibleTilesX; ++tx) {
          const Coord coord{viewX + tx, viewY + ty};
          if (!map.isValid(coord)) {
            continue;
          }

          const RGB color = tileColor(map, roads, store, coord, overlayMode,
                                      facilities, liveState.routeHeatByTile,
                                      liveState.waste.happinessPenalty +
                                          liveState.deathcare.happinessPenalty);
          setDrawColor(renderer, color.r, color.g, color.b, 255);

          visualizer::Rect rect{tx * tilePixels, ty * tilePixels, tilePixels,
                                tilePixels};
          fillRect(renderer, &rect);

          if (overlayMode == OverlayMode::Zone) {
            drawNormalTileDetail(renderer, map, store, facilities, coord,
                                 rect.x, rect.y, tilePixels, &roads);
          }
        }
      }
    }

    if (selectedDistrictId != 0 &&
        districts.getDistrictConst(selectedDistrictId) == nullptr) {
      selectedDistrictId = 0;
    }
    if (!isometricMode && !districts.getDistricts().empty()) {
      drawDistrictOutlines(renderer, districts, isometricMode,
                           isometricProjection(), viewX, viewY, tilePixels,
                           selectedDistrictId);
    }

    const bool mouseOverHud =
        cleanUiMode
            ? (mouseX >= windowWidth - 342 && mouseY >= 12 && mouseY < 58)
            : (mouseX >= windowWidth - kHudPanelWidth - 12 && mouseY >= 12 &&
               mouseY < 12 + kHudPanelHeight);
    const bool mouseOverLegend = !cleanUiMode && showLegend && mouseX >= 12 &&
                                 mouseX < 532 && mouseY >= 12 && mouseY < 158;
    const bool mouseOverPalette =
        paletteHitTest(mouseX, mouseY, windowWidth, windowHeight) !=
        PaletteTool::None;
    const bool mouseOverDistrictPanel =
        !cleanUiMode && selectedDistrictId != 0 &&
        districts.getDistrictConst(selectedDistrictId) != nullptr &&
        pointInDistrictPanel(mouseX, mouseY);
    const bool mouseOverGuide =
        !cleanUiMode && showOnboarding && mouseX >= 14 && mouseX < 364 &&
        mouseY >= windowHeight - 154 && mouseY < windowHeight - 62;
    const Coord hoveredTile = tileAtScreen(mouseX, mouseY);
    const bool inspectMap = !mouseOverHud && !mouseOverLegend &&
                            !mouseOverPalette && !mouseOverDistrictPanel &&
                            !mouseOverGuide && map.isValid(hoveredTile);
    if (inspectMap) {
      if (isometricMode) {
        const IsometricProjection projection = isometricProjection();
        drawDiamond(renderer, projection.tileTop(hoveredTile),
                    projection.tileWidth(), projection.tileHeight(),
                    {255, 245, 150}, 90);
      } else {
        const int screenX = (hoveredTile.x - viewX) * tilePixels;
        const int screenY = (hoveredTile.y - viewY) * tilePixels;
        drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels,
                        {255, 245, 150}, 255);
      }
    }

    if (toolDragging) {
      const bool valid =
          roadToolActive
              ? roadPlan.valid
              : (zoneToolActive ? zonePlan.valid
                                : (districtToolActive ? districtPlan.valid
                                                      : bulldozePlan.valid));
      const std::vector<Coord> &previewTiles =
          roadToolActive
              ? roadPlan.tiles
              : (zoneToolActive ? zonePlan.tiles
                                : (districtToolActive ? districtPlan.tiles
                                                      : bulldozePlan.tiles));
      const RGB previewColor =
          valid ? (roadToolActive
                       ? RGB{80, 220, 120}
                       : (zoneToolActive
                              ? zoneColor(static_cast<int>(selectedZone))
                              : (districtToolActive
                                     ? (districtPlan.removeId != 0
                                            ? RGB{255, 155, 45}
                                            : districtArchetypeColor(
                                                  selectedArchetype))
                                     : RGB{255, 155, 45})))
                : RGB{235, 75, 75};
      for (const Coord tile : previewTiles) {
        if (isometricMode) {
          const IsometricProjection projection = isometricProjection();
          drawDiamond(renderer, projection.tileTop(tile),
                      projection.tileWidth(), projection.tileHeight(),
                      previewColor, 165);
        } else {
          const int screenX = (tile.x - viewX) * tilePixels;
          const int screenY = (tile.y - viewY) * tilePixels;
          drawFilledRect(renderer, screenX, screenY, tilePixels, tilePixels,
                         previewColor, 150);
          drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels,
                          {245, 245, 245}, 220);
        }
      }
    }

    if (serviceToolActive && servicePlan.hasSite &&
        map.isValid(servicePlan.facility.position)) {
      const Coord tile = servicePlan.facility.position;
      const RGB previewColor = servicePlan.valid
                                   ? serviceFacilityColor(selectedService)
                                   : RGB{235, 75, 75};
      if (isometricMode) {
        const IsometricProjection projection = isometricProjection();
        drawDiamond(renderer, projection.tileTop(tile), projection.tileWidth(),
                    projection.tileHeight(), previewColor, 190);
      } else {
        const int screenX = (tile.x - viewX) * tilePixels;
        const int screenY = (tile.y - viewY) * tilePixels;
        drawFilledRect(renderer, screenX, screenY, tilePixels, tilePixels,
                       previewColor, 190);
        drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels,
                        {255, 255, 255}, 240);
      }
    }

    if (!cleanUiMode && showLegend) {
      drawLegendPanel(renderer, overlayMode, windowWidth, windowHeight, mouseX,
                      mouseY);
    }
    if (!cleanUiMode && inspectMap) {
      drawTileInspector(renderer, map, roads, store, facilities, hoveredTile,
                        &districts);
    }
    if (!cleanUiMode) {
      if (const District *selected =
              districts.getDistrictConst(selectedDistrictId)) {
        drawDistrictPanel(renderer, *selected, mouseX, mouseY);
      }
    }

    std::string activeToolLabel = "NONE";
    if (roadToolActive) {
      activeToolLabel =
          selectedRoadClass == RoadClass::Arterial ? "ARTERIAL" : "LOCAL ROAD";
    } else if (zoneToolActive) {
      activeToolLabel = Zoning::zoneToString(static_cast<int>(selectedZone));
    } else if (bulldozeToolActive) {
      activeToolLabel = "BULLDOZE";
    } else if (serviceToolActive) {
      activeToolLabel = ServiceSystem::serviceTypeToString(selectedService);
    } else if (districtToolActive) {
      activeToolLabel = DistrictSystem::archetypeToString(selectedArchetype);
    }
    if (cleanUiMode) {
      drawCompactGameplayHud(renderer, windowWidth, liveState.paused,
                             population.getTotalPopulation(), funds,
                             activeToolLabel, liveState.populationChange,
                             liveState.trafficSummary.freight);
    } else {
      drawGameplayHud(renderer, windowWidth, population.getTotalPopulation(),
                      store.getBuildingCount(), funds, activeToolLabel,
                      liveState, population, mouseX, mouseY);
    }
    drawToolPalette(renderer, windowWidth, windowHeight, roadToolActive,
                    zoneToolActive, bulldozeToolActive, serviceToolActive,
                    districtToolActive, mouseX, mouseY);
    if (!cleanUiMode && showOnboarding) {
      drawOnboarding(renderer, windowHeight, onboardingStep);
    }

    std::string uiHoverText;
    const PaletteTool hoveredTool =
        paletteHitTest(mouseX, mouseY, windowWidth, windowHeight);
    switch (hoveredTool) {
    case PaletteTool::Road:
      uiHoverText =
          "ROAD: A TOGGLE LOCAL/ARTERIAL - $100/$400 NEW - $300 UPGRADE";
      break;
    case PaletteTool::Zone:
      uiHoverText = std::string("ZONE ") +
                    Zoning::zoneToString(static_cast<int>(selectedZone)) +
                    (selectedZone == ZoneType::Park
                         ? ": $100 PER TILE - $2 UPKEEP PER TICK - DRAG TO PLACE"
                         : ": DRAG AN AREA - $25 PER TILE - CLICK AGAIN TO CYCLE");
      break;
    case PaletteTool::Bulldoze:
      uiHoverText =
          "BULLDOZER: DRAG TO REMOVE ROADS ZONES BUILDINGS OR SERVICES";
      break;
    case PaletteTool::Service:
      uiHoverText =
          std::string("SERVICE ") +
          ServiceSystem::serviceTypeToString(selectedService) +
          ": CLICK BESIDE A ROAD - $" +
          std::to_string(ServiceTool::constructionCost(selectedService));
      break;
    case PaletteTool::District:
      uiHoverText = std::string("DISTRICT ") +
                    DistrictSystem::archetypeToString(selectedArchetype) +
                    ": DRAG TO PAINT - CLICK TO SELECT - RIGHT-CLICK TO DELETE";
      break;
    case PaletteTool::None:
    default:
      break;
    }

    if (uiHoverText.empty()) {
      switch (hudHitTest(mouseX, mouseY, windowWidth)) {
      case HudAction::TogglePause:
        uiHoverText = liveState.paused ? "PLAY: RESUME CITY SIMULATION"
                                       : "PAUSE: STOP CITY SIMULATION";
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

    if (uiHoverText.empty() && !cleanUiMode && selectedDistrictId != 0 &&
        districts.getDistrictConst(selectedDistrictId) != nullptr) {
      switch (districtPanelHitTest(mouseX, mouseY)) {
      case DistrictPanelAction::CycleArchetype:
        uiHoverText =
            "ARCH: CYCLE GENERAL INDUSTRIAL TECH HUB FOR THIS DISTRICT";
        break;
      case DistrictPanelAction::ToggleCap:
        uiHoverText =
            districts.getDistrictConst(selectedDistrictId)->serviceBudgetCap < 0
                ? "STARVE: CAP SERVICE BUDGET SO THIS DISTRICT GROWS SLOWER"
                : "FUND: REMOVE THE SERVICE BUDGET CAP";
        break;
      case DistrictPanelAction::None:
      default:
        break;
      }
    }

    if (uiHoverText.empty() && showLegend) {
      OverlayMode hoveredOverlay = overlayMode;
      if (overlayHitTest(mouseX, mouseY, hoveredOverlay)) {
        switch (hoveredOverlay) {
        case OverlayMode::Zone:
          uiHoverText = "ZONE OVERLAY: ZONING BUILDINGS ROADS AND TERRAIN";
          break;
        case OverlayMode::LandValue:
          uiHoverText = "LAND VALUE OVERLAY: LOW TO HIGH PROPERTY VALUE";
          break;
        case OverlayMode::Pollution:
          uiHoverText = "POLLUTION OVERLAY: CLEAN TO POLLUTED LAND";
          break;
        case OverlayMode::ServiceCoverage:
          uiHoverText = "SERVICE OVERLAY: ROAD-REACHABLE CIVIC COVERAGE";
          break;
        case OverlayMode::TrafficCongestion:
          uiHoverText = "TRAFFIC OVERLAY: LOW TO HIGH ROAD CONGESTION";
          break;
        case OverlayMode::Demand:
          uiHoverText = "DEMAND OVERLAY: DEVELOPMENT PRESSURE ON ZONED LAND";
          break;
        case OverlayMode::Happiness:
          uiHoverText = "HAPPINESS OVERLAY: LOCAL QUALITY OF LIFE";
          break;
        case OverlayMode::RouteHeatmap:
          uiHoverText = "ROUTE HEAT OVERLAY: MOST USED COMMUTE EDGES";
          break;
        default:
          break;
        }
      }
    }
    drawUiTooltip(renderer, mouseX, mouseY, windowWidth, windowHeight,
                  uiHoverText);

    std::string placementWarning;
    if (toolDragging) {
      if (roadToolActive && !roadPlan.valid) {
        placementWarning = roadPlan.error;
      } else if (zoneToolActive && !zonePlan.valid) {
        placementWarning = zonePlan.error;
      } else if (bulldozeToolActive && !bulldozePlan.valid) {
        placementWarning = bulldozePlan.error;
      } else if (districtToolActive && !districtPlan.valid) {
        placementWarning = districtPlan.error;
      }
    } else if (uiHoverText.empty() && serviceToolActive &&
               servicePlan.hasSite && !servicePlan.valid) {
      placementWarning = servicePlan.error;
    }
    drawPlacementWarning(renderer, mouseX, mouseY, windowWidth, windowHeight,
                         placementWarning);
    drawToast(renderer, windowWidth, toast, nowMs);

    if (nowMs - lastHudRefreshMs >= 250) {
      const std::string hudTitle = makeHudTitle(
          roads, store, population, liveState, tilePixels, viewX, viewY,
          overlayMode, liveState.routeFilter, roadToolActive, roadPlan,
          zoneToolActive, selectedZone, zonePlan, bulldozeToolActive,
          bulldozePlan, serviceToolActive, selectedService, servicePlan,
          districtToolActive, selectedArchetype, districtPlan, funds);
      host.title(hudTitle);
      lastHudRefreshMs = nowMs;
    }

    if (showQuitDialog) {
      drawQuitDialog(renderer, windowWidth, windowHeight, mouseX, mouseY,
                     sessionDirty);
    }

    const bool presented =
        gpu.present(*renderer, bridge ? bridge->capturePath : "");
    if (bridge && presented) {
      ++bridge->frame;
      bridge->complete(state());
    }
  }

  return 0;
}
