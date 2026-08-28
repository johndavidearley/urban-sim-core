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
#include "src/gameplay/ZoneTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/visualization/IsometricProjection.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"

#include "src/visualization/VisualizerTypes.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/VisualizerHud.hpp"
#include "src/visualization/VisualizerSession.hpp"

using namespace visualizer;

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
      liveState.autonomousGrowth = session.autonomousGrowth;
      liveState.construction = city_sim::ConstructionState{};
      liveState.construction.extent = session.autonomousExtent;
      liveState.construction.emptyZonedCount = session.emptyZonedCount;
      liveState.transitRoutes = std::move(session.transitRoutes);
      liveState.transitCache = TransitCoverageCache{};
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
  refreshLiveDerivedState(map, roads, store, population, facilities, liveState, 98765u);
  refreshRouteHeat(store, population, roads, liveState, 98765u);

  const int minimumTilePixels = std::max(2, (windowWidth + mapSize - 1) / mapSize);
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

  auto isometricProjection = [&]() {
    const int height = std::max(6, tilePixels / 2);
    const int centeredY = std::max(18, (windowHeight - mapSize * height) / 2);
    return IsometricProjection(
      tilePixels,
      height,
      {windowWidth / 2 - (viewX - viewY) * (tilePixels / 2) + isometricCameraX,
       centeredY - (viewX + viewY) * (height / 2) + isometricCameraY}
    );
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
        const Tile& tile = map.getTile({x, y});
        if (!tile.hasRoad && tile.buildingId == 0 && tile.zone == 0) continue;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
      }
    }
    for (const ServiceFacility& facility : facilities) {
      minX = std::min(minX, facility.position.x);
      minY = std::min(minY, facility.position.y);
      maxX = std::max(maxX, facility.position.x);
      maxY = std::max(maxY, facility.position.y);
    }
    const Coord center = maxX >= 0
      ? Coord{(minX + maxX) / 2, (minY + maxY) / 2}
      : Coord{mapSize / 2, mapSize / 2};
    isometricCameraX = 0;
    isometricCameraY = 0;
    const ScreenPoint projected = isometricProjection().tileCenter(center);
    isometricCameraX += windowWidth / 2 - projected.x;
    isometricCameraY += windowHeight / 2 - projected.y;
  };
  focusDevelopedArea();

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
    session.autonomousGrowth = liveState.autonomousGrowth;
    session.autonomousExtent = liveState.construction.extent;
    session.emptyZonedCount = liveState.construction.emptyZonedCount;
    session.facilities = facilities;
    session.transitRoutes = liveState.transitRoutes;
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
    liveState.autonomousGrowth = session.autonomousGrowth;
    liveState.construction = city_sim::ConstructionState{};
    liveState.construction.extent = session.autonomousExtent;
    liveState.construction.emptyZonedCount = session.emptyZonedCount;
    liveState.lastTickMs = SDL_GetTicks();
    liveState.transitRoutes = std::move(session.transitRoutes);
    liveState.transitCache = TransitCoverageCache{};
    liveState.transitSummary = TransitSummary{};
    liveState.serviceCache = ServiceCoverageCache{};
    facilities = std::move(session.facilities);
    refreshLiveDerivedState(
      map, roads, store, population, facilities, liveState, 3001u + (liveState.tick * 31u));
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
            tilePixels = std::max(minimumTilePixels, tilePixels - 1);
            break;
          case SDLK_LEFT:
            if (isometricMode) isometricCameraX += 18;
            else viewX = std::max(0, viewX - 1);
            break;
          case SDLK_RIGHT:
            if (isometricMode) isometricCameraX -= 18;
            else viewX = std::min(mapSize - 1, viewX + 1);
            break;
          case SDLK_UP:
            if (isometricMode) isometricCameraY += 18;
            else viewY = std::max(0, viewY - 1);
            break;
          case SDLK_DOWN:
            if (isometricMode) isometricCameraY -= 18;
            else viewY = std::min(mapSize - 1, viewY + 1);
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
          case SDLK_F3:
            isometricMode = !isometricMode;
            toolDragging = false;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
            notify(isometricMode ? "ISOMETRIC VIEW" : "TOP DOWN VIEW", true);
            break;
          case SDLK_F4:
            cleanUiMode = !cleanUiMode;
            notify(cleanUiMode ? "CLEAN UI" : "DETAIL UI", true);
            break;
          case SDLK_f:
            if (isometricMode) {
              focusDevelopedArea();
              notify("CAMERA FOCUSED", true);
            }
            break;
          case SDLK_F2:
            gReadableUiText = !gReadableUiText;
            notify(gReadableUiText ? "LARGE TEXT ON" : "COMPACT TEXT ON", true);
            break;
          case SDLK_g:
            liveState.autonomousGrowth = !liveState.autonomousGrowth;
            notify(liveState.autonomousGrowth
                     ? "AUTONOMOUS GROWTH ON (CITYSIM ROADS/ZONES/SERVICES)"
                     : "AUTONOMOUS GROWTH OFF (PLAYER BUILD ONLY)",
                   true);
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
          const Coord anchor = tileAtScreen(mouseX, mouseY);
          const int direction = event.wheel.y > 0 ? 1 : -1;
          tilePixels = std::max(minimumTilePixels, std::min(48, tilePixels + direction * 2));
          if (isometricMode) {
            const ScreenPoint projected = isometricProjection().tileCenter(anchor);
            isometricCameraX += mouseX - projected.x;
            isometricCameraY += mouseY - projected.y;
          } else {
            viewX = anchor.x - mouseX / tilePixels;
            viewY = anchor.y - mouseY / tilePixels;
          }
        }
      } else if (event.type == SDL_MOUSEMOTION && cameraPanning) {
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
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
                 && showLegend
                 && overlayHitTest(event.button.x, event.button.y, clickedOverlay)) {
        overlayMode = clickedOverlay;
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
                 && !cleanUiMode
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
          const Coord tile = tileAtScreen(event.button.x, event.button.y);
          servicePlan = ServiceTool::plan(map, roads, facilities, selectedService, tile, funds);
          const int64_t cost = servicePlan.cost;
          if (ServiceTool::build(map, roads, facilities, servicePlan, funds)) {
            sessionDirty = true;
            refreshLiveDerivedState(
              map, roads, store, population, facilities, liveState,
              1001u + (liveState.tick * 31u), /*reallocPopulation=*/false, /*runTraffic=*/false);
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
          const Coord tile = tileAtScreen(event.button.x, event.button.y);
          if (map.isValid(tile)) {
            toolDragging = true;
            toolDragStart = tile;
            roadPlan = {};
            zonePlan = {};
            bulldozePlan = {};
          }
        }
      } else if (event.type == SDL_MOUSEMOTION && serviceToolActive) {
        const Coord tile = tileAtScreen(event.motion.x, event.motion.y);
        servicePlan = ServiceTool::plan(map, roads, facilities, selectedService, tile, funds);
      } else if (event.type == SDL_MOUSEMOTION && toolDragging) {
        const Coord tile = tileAtScreen(event.motion.x, event.motion.y);
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
        const Coord tile = tileAtScreen(event.button.x, event.button.y);
        if (map.isValid(tile)) {
          if (roadToolActive) {
            roadPlan = RoadTool::plan(map, roads, toolDragStart, tile, funds);
            const int64_t cost = roadPlan.cost;
            if (RoadTool::build(map, roads, roadPlan, funds)) {
              sessionDirty = true;
              refreshLiveDerivedState(
                map, roads, store, population, facilities, liveState,
                1003u + (liveState.tick * 31u), /*reallocPopulation=*/false, /*runTraffic=*/false);
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
              refreshLiveDerivedState(
                map, roads, store, population, facilities, liveState,
                2005u + (liveState.tick * 31u), /*reallocPopulation=*/true, /*runTraffic=*/true);
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

    SDL_SetRenderDrawColor(renderer, 91, 108, 91, 255);
    SDL_RenderClear(renderer);

    if (isometricMode) {
      const IsometricProjection projection = isometricProjection();
      for (int depth = 0; depth <= (mapSize - 1) * 2; ++depth) {
        for (int x = 0; x < mapSize; ++x) {
          const int y = depth - x;
          if (y < 0 || y >= mapSize) continue;
          drawIsometricTile(
            renderer, map, roads, store, facilities, liveState.routeHeatByTile,
            liveState.waste.happinessPenalty + liveState.deathcare.happinessPenalty,
            overlayMode, projection, {x, y}
          );
          if (x == mapSize - 1 || y == mapSize - 1) {
            const ScreenPoint top = projection.tileTop({x, y});
            const RGB wall{67, 79, 68};
            const int halfWidth = projection.tileWidth() / 2;
            const int bottomY = top.y + projection.tileHeight();
            for (int drop = 1; drop <= 5; ++drop) {
              SDL_SetRenderDrawColor(renderer, wall.r, wall.g, wall.b, 220);
              if (x == mapSize - 1) {
                SDL_RenderDrawLine(renderer, top.x, bottomY + drop,
                                   top.x - halfWidth, top.y + projection.tileHeight() / 2 + drop);
              }
              if (y == mapSize - 1) {
                SDL_RenderDrawLine(renderer, top.x, bottomY + drop,
                                   top.x + halfWidth, top.y + projection.tileHeight() / 2 + drop);
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

        const RGB color = tileColor(
          map, roads, store, coord, overlayMode, facilities,
          liveState.routeHeatByTile,
          liveState.waste.happinessPenalty + liveState.deathcare.happinessPenalty
        );
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

        SDL_Rect rect{tx * tilePixels, ty * tilePixels, tilePixels, tilePixels};
        SDL_RenderFillRect(renderer, &rect);

        if (overlayMode == OverlayMode::Zone) {
          drawNormalTileDetail(
            renderer, map, store, facilities, coord,
            rect.x, rect.y, tilePixels
          );
        }
      }
    }
    }

    const bool mouseOverHud = cleanUiMode
      ? (mouseX >= windowWidth - 342 && mouseY >= 12 && mouseY < 58)
      : (mouseX >= windowWidth - kHudPanelWidth - 12
          && mouseY >= 12 && mouseY < 12 + kHudPanelHeight);
    const bool mouseOverLegend = !cleanUiMode && showLegend && mouseX >= 12 && mouseX < 532
      && mouseY >= 12 && mouseY < 158;
    const bool mouseOverPalette = paletteHitTest(mouseX, mouseY, windowWidth, windowHeight)
      != PaletteTool::None;
    const bool mouseOverGuide = !cleanUiMode && showOnboarding && mouseX >= 14 && mouseX < 364
      && mouseY >= windowHeight - 154 && mouseY < windowHeight - 62;
    const Coord hoveredTile = tileAtScreen(mouseX, mouseY);
    const bool inspectMap = !mouseOverHud && !mouseOverLegend && !mouseOverPalette
      && !mouseOverGuide && map.isValid(hoveredTile);
    if (inspectMap) {
      if (isometricMode) {
        const IsometricProjection projection = isometricProjection();
        drawDiamond(renderer, projection.tileTop(hoveredTile), projection.tileWidth(),
                    projection.tileHeight(), {255, 245, 150}, 90);
      } else {
        const int screenX = (hoveredTile.x - viewX) * tilePixels;
        const int screenY = (hoveredTile.y - viewY) * tilePixels;
        drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels, {255, 245, 150}, 255);
      }
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
        if (isometricMode) {
          const IsometricProjection projection = isometricProjection();
          drawDiamond(renderer, projection.tileTop(tile), projection.tileWidth(),
                      projection.tileHeight(), previewColor, 165);
        } else {
          const int screenX = (tile.x - viewX) * tilePixels;
          const int screenY = (tile.y - viewY) * tilePixels;
          drawFilledRect(renderer, screenX, screenY, tilePixels, tilePixels, previewColor, 150);
          drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels, {245, 245, 245}, 220);
        }
      }
    }

    if (serviceToolActive && servicePlan.hasSite && map.isValid(servicePlan.facility.position)) {
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
        drawFilledRect(renderer, screenX, screenY, tilePixels, tilePixels, previewColor, 190);
        drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels, {255, 255, 255}, 240);
      }
    }

    if (!cleanUiMode && showLegend) {
      drawLegendPanel(renderer, overlayMode, windowWidth, windowHeight, mouseX, mouseY);
    }
    if (!cleanUiMode && inspectMap) {
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
    if (cleanUiMode) {
      drawCompactGameplayHud(renderer, windowWidth, liveState.paused,
                             population.getTotalPopulation(), funds, activeToolLabel);
    } else {
      drawGameplayHud(
        renderer,
        windowWidth,
        population.getTotalPopulation(),
        store.getBuildingCount(),
        funds,
        activeToolLabel,
        liveState,
        mouseX,
        mouseY
      );
    }
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
    if (!cleanUiMode && showOnboarding) {
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

