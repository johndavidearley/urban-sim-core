#include <algorithm>
#include <climits>
#include "src/persistence/CityHistoryJson.hpp"
#include "src/persistence/TransitReportJson.hpp"
#include "src/persistence/RegionalSettlementJson.hpp"
#include "src/persistence/TourismJson.hpp"
#include "src/systems/RegionalLogistics.hpp"
#include "src/persistence/CityAlertsJson.hpp"
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
#include "src/gameplay/PortTool.hpp"
#include "src/gameplay/HotelTool.hpp"
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
#include "src/visualization/TransitOverlay.hpp"
#include <unordered_set>
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
      liveState.budgetPolicy = session.budgetPolicy;
      liveState.treasuryLedger = session.treasuryLedger;
      liveState.history = session.history;
      liveState.alerts = session.alerts;
      liveState.transitReport = session.transitReport;
      liveState.regionalConnections = session.regionalConnections;
      liveState.regionalSettlement = session.regionalSettlement;
      liveState.tourism = session.tourism;
      liveState.regionalLogisticsEnabled = session.regionalLogisticsEnabled;
      liveState.nextRegionalConnectionId = session.nextRegionalConnectionId;
      liveState.simulationSeed = session.simulationSeed;
      liveState.enableDisasters = session.enableDisasters;
      liveState.disasters = session.disasters;
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
  bool showBudgetPanel = false;
  bool showTransportPanel = false;
  size_t selectedTransitRoute = 0;
  int budgetFocus=-1,transportFocus=-1;
  uint32_t inspectedTrendTick=0;
  Coord pinnedInspector{-1,-1};
  bool budgetServices = false;
  bool budgetTrends = false;
  bool budgetAlerts = false;
  bool cleanUiMode = false;
  int isometricCameraX = 0;
  int isometricCameraY = 0;
  TransitOverlayCache transitOverlayCache;
  OverlayMode overlayMode = OverlayMode::Zone;
  bool showLegend = true;
  bool roadToolActive = false;
  RoadClass selectedRoadClass = RoadClass::Local;
  bool zoneToolActive = false;
  bool bulldozeToolActive = false;
  bool serviceToolActive = false;
  bool districtToolActive = false;
  bool portToolActive = false;
  int portAction = 0; // Cargo, passenger, remove, enable/disable, capacity.
  const std::array<const char*,7> portActions{{"CARGO","PASSENGER","REMOVE","ON/OFF","CAPACITY","AIRPORT","HOTEL"}};
  auto portButtonRect=[&]() {return visualizer::Rect{windowWidth-126,windowHeight-108,112,32};};
  auto overPortButton=[&](int x,int y) {
    const auto r=portButtonRect();return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;
  };
  auto airportButtonRect=[&]() {return visualizer::Rect{windowWidth-126,windowHeight-148,112,32};};
  auto overAirportButton=[&](int x,int y) {
    const auto r=airportButtonRect();return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;
  };
  auto hotelButtonRect=[&]() {return visualizer::Rect{windowWidth-126,windowHeight-188,112,32};};
  auto overHotelButton=[&](int x,int y) {
    const auto r=hotelButtonRect();return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;
  };
  auto tourismButtonRect=[&]() {return visualizer::Rect{windowWidth-126,windowHeight-228,112,32};};
  auto overTourismButton=[&](int x,int y) {
    const auto r=tourismButtonRect();return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;
  };
  auto terminalPlacement=[&]() {return portAction<2 || portAction==5;};
  auto terminalKind=[&]() {return portAction==0 ? regional::Kind::CargoPort :
    (portAction==1 ? regional::Kind::PassengerTerminal : regional::Kind::Airport);};
  auto overPortBanner=[&](int x,int y) {
    return portToolActive && !cleanUiMode && x>=14 && x<windowWidth-140 &&
      y>=windowHeight-166 && y<windowHeight-66;
  };
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
    } else if (liveState.alerts.hasNotice()) {
      notify("CITY ALERTS " + std::to_string(liveState.alerts.activeCount()) + " ACTIVE - F7", false);
    }
  };

  auto saveSession = [&]() -> bool {
    GameplaySessionState session;
    session.funds = funds;
    session.tick = liveState.tick;
    session.budgetPolicy = liveState.budgetPolicy;
    session.treasuryLedger = liveState.treasuryLedger;
    session.history = liveState.history;
    session.alerts = liveState.alerts;
    session.transitReport = liveState.transitReport;
    session.regionalConnections = liveState.regionalConnections;
    session.regionalSettlement = liveState.regionalSettlement;
    session.tourism = liveState.tourism;
    session.regionalLogisticsEnabled = liveState.regionalLogisticsEnabled;
    session.nextRegionalConnectionId = liveState.nextRegionalConnectionId;
    session.simulationSeed = liveState.simulationSeed;
    session.enableDisasters = liveState.enableDisasters;
    session.disasters = liveState.disasters;
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
    liveState.budgetPolicy = session.budgetPolicy;
    liveState.treasuryLedger = session.treasuryLedger;
    liveState.history = session.history;
    liveState.alerts = session.alerts;
    liveState.transitReport = session.transitReport;
    liveState.regionalConnections = session.regionalConnections;
    liveState.regionalSettlement = session.regionalSettlement;
    liveState.tourism = session.tourism;
    liveState.regionalLogisticsEnabled = session.regionalLogisticsEnabled;
    liveState.nextRegionalConnectionId = session.nextRegionalConnectionId;
    liveState.simulationSeed = session.simulationSeed;
    liveState.enableDisasters = session.enableDisasters;
    liveState.disasters = session.disasters;
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
    pinnedInspector={-1,-1};inspectedTrendTick=0;budgetFocus=transportFocus=-1;selectedTransitRoute=0;
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
    portToolActive = false;
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
    portToolActive = tool == PaletteTool::Port;
    toolDragging = false;
    roadPlan = {};
    zonePlan = {};
    bulldozePlan = {};
    servicePlan = {};
    districtPlan = {};
  };

  auto setTourism=[&](bool enabled) {
    liveState.tourism.enabled=enabled;sessionDirty=true;
    notify(enabled ? "TOURISM ON - ARRIVALS REQUIRE REACHABLE PARKS" :
      "TOURISM OFF - STAYS DEPART NEXT TICK",true);
  };
  auto setHotelOpen=[&](uint32_t id,bool open) {
    if (!HotelTool::setOpen(store,id,open)) throw std::runtime_error("Select a hotel");
    sessionDirty=true;
    notify(open ? "HOTEL OPEN - BEDS AVAILABLE NEXT TICK" :
      "HOTEL CLOSED - GUESTS DEPART NEXT TICK; JOBS/UPKEEP CONTINUE",true);
  };
  auto portCivicSites=[&]() {
    std::vector<Coord> sites;
    for (const auto& f:facilities) sites.push_back(f.position);
    return sites;
  };
  auto planTerminalAt=[&](Coord tile) {
    return TerminalTool::plan(map,roads,liveState.regionalConnections,tile,terminalKind(),
      liveState.nextRegionalConnectionId,portCivicSites(),funds);
  };
  auto selectPortAction=[&]() {
    const int next=portToolActive && portAction<5 ? (portAction+1)%5 : 0;
    activatePaletteTool(PaletteTool::Port); portAction=next;
    notify(std::string("PORT ")+portActions[portAction]+" - V CYCLES, ESC CANCELS",true);
  };
  auto applyPort=[&](Coord tile,int capacity=-1) {
    if (!map.isValid(tile)) {notify("TERMINAL OUTSIDE MAP",false);return;}
    std::string error, message;
    if (portAction==6) {
      const auto* existing=store.getBuilding(map.getTile(tile).buildingId);
      if (existing && existing->hotelBeds) {
        setHotelOpen(existing->id,!existing->hotelOpen);
        return;
      }
      const auto plan=HotelTool::plan(map,roads,facilities,tile,funds);
      if (!plan.valid) {notify(plan.error,false);return;}
      if (!HotelTool::build(map,roads,store,facilities,plan,funds)) {notify("HOTEL SITE CHANGED",false);return;}
      liveState.tourism.enabled=true;
      message="HOTEL BUILT $10000 - 32 BEDS / 12 JOBS; TOURISM ON";
    } else if (terminalPlacement()) {
      const auto plan=planTerminalAt(tile);
      if (!plan.valid) {notify(plan.error,false);return;}
      if (!TerminalTool::build(map,roads,liveState.regionalConnections,plan.connection,
            plan.destination,portCivicSites(),funds,&error)) {notify(error,false);return;}
      liveState.nextRegionalConnectionId=static_cast<uint64_t>(plan.connection.id)+1;
      if (portAction==0) liveState.regionalLogisticsEnabled=true;
      message=portAction==0 ? "CARGO PORT BUILT $15000 - FINITE LOGISTICS ON; ROAD 100/TICK"
                            : (portAction==5 ? "AIRPORT BUILT $25000 - $30/TICK; VISITORS REQUIRE TOURISM ON AND PARK ACCESS"
                            : "PASSENGER TERMINAL BUILT $10000 - $10/TICK; VISITORS REQUIRE TOURISM ON AND PARK ACCESS");
    } else {
      const auto id=map.getTile(tile).regionalConnectionId;
      const auto found=std::find_if(liveState.regionalConnections.begin(),liveState.regionalConnections.end(),
        [&](const auto& c){return c.id==id && c.constructed &&
          regional::terminalSpec(c.kind)!=nullptr;});
      if (!id || found==liveState.regionalConnections.end()) {notify("SELECT A CONSTRUCTED TERMINAL",false);return;}
      if (portAction==2) {
        const auto cost=regional::terminalSpec(found->kind)->demolitionCost;
        if (!TerminalTool::remove(map,liveState.regionalConnections,id,found->kind,funds,&error)) {notify(error,false);return;}
        message="TERMINAL REMOVED $"+std::to_string(cost)+" - CARGO RULES UNCHANGED";
      } else if (portAction==3) {
        found->enabled=!found->enabled;
        message=found->enabled ? "TERMINAL ENABLED - UPKEEP RESUMED" : "TERMINAL DISABLED - NO UPKEEP OR FLOW";
      } else {
        const bool cargo=found->kind==regional::Kind::CargoPort;
        auto& value=cargo ? found->goodsPerTick : found->passengersPerTick;
        const auto* spec=regional::terminalSpec(found->kind);
        const uint32_t high=cargo ? spec->goods : spec->passengers, low=high/4;
        if (capacity<-1 || capacity>static_cast<int>(high)) {notify("CAPACITY OUTSIDE DESIGN LIMIT",false);return;}
        value=capacity>=0 ? static_cast<uint32_t>(capacity) : (value<low ? low : (value<high ? std::min(high,value*2) : low));
        message="TERMINAL CAPACITY "+std::to_string(value)+(cargo ? " GOODS/TICK" : " PASSENGERS/TICK");
      }
    }
    // Rebuild autonomous candidates after reserving/releasing sites. Removal
    // must make the released land eligible again, not retain a stale cache.
    liveState.construction.zoningCandidates={};
    liveState.construction.zoningCandidatesExtent=0;
    sessionDirty=true;
    refreshLiveDerivedState(map,roads,store,population,facilities,liveState,
      1003u+liveState.tick*31u,false,true);
    refreshRouteHeat(store,population,roads,liveState,1003u+liveState.tick*31u);
    notify(message,true);
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
  auto setBudgetPolicy = [&](const BudgetPolicy& policy) {
    if (policy.taxPercent == liveState.budgetPolicy.taxPercent && policy.servicePercent == liveState.budgetPolicy.servicePercent) return;
    const bool fundingChanged = policy.servicePercent != liveState.budgetPolicy.servicePercent;
    liveState.budgetPolicy = policy;
    if (fundingChanged)
      refreshLiveDerivedState(map, roads, store, population, facilities, liveState,
                              1003u + liveState.tick * 31u, false, false);
    else {
      liveState.economy = EconomySystem::calculateEconomy(
        store, population, policy.rates(), &map, TradeRates{}, 1.f, {}, EconomyValuation::playable());
      if (liveState.regionalLogisticsEnabled)
        regional::applyFulfilledTrade(liveState.economy,liveState.trafficSummary.freight);
    }
    sessionDirty = true;
  };
  auto viewCityAlert = [&](size_t index) {
    if (index >= 5) throw std::runtime_error("Alert index must be 0 to 4");
    showTransportPanel=false;
    budgetAlerts = false; budgetTrends = false;
    toolDragging = false; cameraPanning = false;
    activatePaletteTool(PaletteTool::None);
    if (index == 4) { showBudgetPanel = true; budgetServices = false; }
    else {
      showBudgetPanel = false;
      overlayMode = index == 2 ? OverlayMode::Demand : OverlayMode::ServiceCoverage;
      std::vector<EntityId> ids;
      for (const auto& item:store.getBuildings()) ids.push_back(item.first);
      std::sort(ids.begin(),ids.end());
      const Building* target=nullptr; int best=-1;
      for (const auto id:ids) {
        const auto* b=store.getBuilding(id); const auto& tile=map.getTile(b->position);
        int score=-1;
        if (index==0 && !tile.connectedToPower) score=1;
        if (index==1 && !tile.connectedToWater) score=1;
        if (index==2 && b->type!=BuildingType::Residential) score=std::max(0,b->capacity-b->occupancy);
        if (index==3) {
          const auto mask=BuildingDiagnostics::serviceMask(roads,liveState.serviceCache,b->position);
          score=4; for (int bit=0;bit<4;++bit) if (mask&(1u<<bit)) --score;
        }
        if (score>best) {best=score;target=b;}
      }
      pinnedInspector=target ? target->position : Coord{-1,-1};
      if (target) {isometricMode=false;viewX=std::max(0,target->position.x-5);viewY=std::max(0,target->position.y-8);}
    }
  };
  auto budgetPreview = [&]() {
    const auto serviceCosts = liveState.budgetPolicy.serviceCost(facilities) + TransitSystem::operatingCost(liveState.transitRoutes) + regional::regionalOperatingCost(liveState.regionalConnections);
    return TreasurySystem::breakdown(liveState.economy, 0.01, serviceCosts);
  };
  auto routePeak = [&](size_t index) -> std::pair<float,Coord> {
    if (liveState.transitRoutes.empty()) return {0.f,{0,0}};
    index=std::min(index,liveState.transitRoutes.size()-1);
    const auto& route=liveState.transitRoutes[index];
    Coord target=route.stops.empty() ? Coord{0,0} : route.stops.front();
    float peak=0;
    const auto entry=std::find_if(liveState.transitCache.entries.begin(),liveState.transitCache.entries.end(),
      [&](const auto& e){return e.routeId==route.id;});
    if (entry!=liveState.transitCache.entries.end()) for (const auto& edge:roads.getAllEdgeTraffic()) {
      if (entry->distanceField.count(edge.from) && entry->distanceField.count(edge.to) && edge.congestion>peak) {
        peak=edge.congestion; target=edge.from;
      }
    }
    return {peak,target};
  };
  auto setTransitVehicles = [&](TransitRouteId id,int vehicles) {
    if (vehicles<0 || vehicles>20) throw std::runtime_error("Bus fleet must be 0 to 20 vehicles");
    const auto found=std::find_if(liveState.transitRoutes.begin(),liveState.transitRoutes.end(),[&](const auto& r){return r.id==id;});
    if (found==liveState.transitRoutes.end() || found->mode!=TransitMode::Bus)
      throw std::runtime_error("Choose an existing bus route");
    if (static_cast<int64_t>(vehicles)*found->capacityPerVehicle>INT_MAX) throw std::runtime_error("Fleet capacity exceeds supported range");
    found->vehicleCount=vehicles;
    projectTreasuryHud(liveState,facilities);
    sessionDirty=true;
  };
  auto transportAction = [&](int action) {
    if (action==0) {showTransportPanel=false;return;}
    if (liveState.transitRoutes.empty()) return;
    selectedTransitRoute=std::min(selectedTransitRoute,liveState.transitRoutes.size()-1);
    if (action==1) selectedTransitRoute=(selectedTransitRoute+liveState.transitRoutes.size()-1)%liveState.transitRoutes.size();
    if (action==2) selectedTransitRoute=(selectedTransitRoute+1)%liveState.transitRoutes.size();
    const auto& route=liveState.transitRoutes[selectedTransitRoute];
    if ((action==3 || action==4) && route.mode==TransitMode::Bus) {
      const int vehicles=static_cast<int>(std::clamp(static_cast<int64_t>(route.vehicleCount)+(action==3?-1:1),int64_t{0},int64_t{20}));
      if (static_cast<int64_t>(vehicles)*route.capacityPerVehicle<=INT_MAX) setTransitVehicles(route.id,vehicles);
    }
    if ((action==5 || action==6) && !route.stops.empty()) {
      const Coord target=action==6 ? routePeak(selectedTransitRoute).second : route.stops.front();
      showTransportPanel=false; showBudgetPanel=false;
      overlayMode=action==6 ? OverlayMode::TrafficCongestion : OverlayMode::Transit;
      isometricMode=false; viewX=std::max(0,target.x-5); viewY=std::max(0,target.y-8);
      activatePaletteTool(PaletteTool::None);
    }
  };
  auto budgetStatus = [&]() -> Json {
    const auto preview = budgetPreview();
    const auto& ledger = liveState.treasuryLedger;
    return {{"tax_percent", liveState.budgetPolicy.taxPercent},
      {"transit_cost",TransitSystem::operatingCost(liveState.transitRoutes)},
      {"transit_report",transitReportJson(liveState.transitReport)},
      {"history", cityHistoryJson(liveState.history)},
      {"alerts", cityAlertsJson(liveState.alerts)},
      {"tax_attraction", liveState.budgetPolicy.taxAttraction()},
      {"service_percent", liveState.budgetPolicy.servicePercent},
      {"service_costs", liveState.budgetPolicy.serviceCosts(facilities)},
      {"service_coverage", BudgetPolicy::coverage(liveState.serviceSummary)},
      {"preview", {{"revenue", preview.revenue}, {"expenses", preview.expenses},
                   {"total_revenue", preview.totalRevenue}, {"total_expenses", preview.totalExpenses}}},
      {"last_tick", {{"valid", ledger.valid}, {"tick", ledger.tick},
        {"tourism_fees",ledger.breakdown.tourismFees},{"tourism_services",ledger.breakdown.tourismServices},
        {"revenue", ledger.breakdown.revenue}, {"expenses", ledger.breakdown.expenses},
        {"total_revenue", ledger.flow.revenue}, {"total_expenses", ledger.flow.expenses},
        {"funds_before", ledger.fundsBefore}, {"funds_after", ledger.fundsAfter},
        {"debt_before", ledger.debtBefore}, {"debt_after", ledger.flow.outstandingDebt},
        {"borrowed", ledger.flow.debtIssued}, {"repaid", ledger.flow.debtRepaid},
        {"interest", ledger.flow.interestCharged}, {"net", ledger.flow.net}}}};
  };
  auto activateBudgetAction = [&](int action) {
    if (!budgetControlEnabled(action,liveState.budgetPolicy,budgetServices,budgetTrends,budgetAlerts)) return;
          if (action == 0) showBudgetPanel = false;
          if (action == 12) { budgetServices = false; budgetTrends = false; budgetAlerts = false; }
          if (action == 13) { budgetServices = true; budgetTrends = false; budgetAlerts = false; }
          if (action == 36) { budgetTrends = true; budgetAlerts = false; }
          if (action == 37) { budgetAlerts = true; budgetTrends = false; }
          if (action >= 38 && action < 43) viewCityAlert(static_cast<size_t>(action - 38));
          if (action == 1) {
            auto policy = liveState.budgetPolicy;
            if (budgetServices) policy.servicePercent = BudgetPolicy{}.servicePercent;
            else policy.taxPercent = BudgetPolicy{}.taxPercent;
            setBudgetPolicy(policy);
          }
          if (action >= 14 && action < 36) {
            auto policy = liveState.budgetPolicy;
            const size_t index = static_cast<size_t>((action - 14) / 2);
            policy.servicePercent[index] = std::clamp(policy.servicePercent[index] + (action % 2 == 0 ? -25 : 25), 0, 100);
            setBudgetPolicy(policy);
          }
          if (action >= 2 && action < 12) {
            auto policy = liveState.budgetPolicy;
            const size_t index = static_cast<size_t>((action - 2) / 2);
            policy.taxPercent[index] = std::clamp(policy.taxPercent[index] + (action % 2 == 0 ? -1 : 1), 0, 20);
            setBudgetPolicy(policy);
          }
  };
  auto cycleBudgetFocus = [&](int direction) {
    int candidate=budgetFocus<0 ? (direction>0?-1:0) : budgetFocus;
    for (int i=0;i<43;++i) {
      candidate=(candidate+direction+43)%43;
      if (budgetControlEnabled(candidate,liveState.budgetPolicy,budgetServices,budgetTrends,budgetAlerts)) {budgetFocus=candidate;return;}
    }
  };
  auto cycleTransportFocus = [&](int direction) {
    int candidate=transportFocus<0 ? (direction>0?-1:0) : transportFocus;
    for (int i=0;i<7;++i) {
      candidate=(candidate+direction+7)%7;
      if (transportControlEnabled(candidate,liveState.transitRoutes,selectedTransitRoute)) {transportFocus=candidate;return;}
    }
  };
  bool running = true;
  bool suspended = false;
  auto state = [&]() {
    Json result = bridge->metadata(
        host, gpu, showQuitDialog ? "quit_dialog" : "gameplay", liveState.tick);
    Json regionalGateways=Json::array();
    for (const auto& u:liveState.trafficSummary.freight.gateways)
      regionalGateways.push_back({{"id",u.gateway.id},{"entrance",{u.gateway.position.x,u.gateway.position.y}},
        {"goods_per_tick",u.gateway.goodsPerTick},{"enabled",u.gateway.enabled},
        {"imports",u.importedGoods},{"exports",u.exportedGoods}});
    result.update(
        {{"population_change",
          {{"arrivals", liveState.populationChange.arrivals},
           {"departures", liveState.populationChange.departures},
           {"deaths", liveState.populationChange.deaths},
           {"net", liveState.populationChange.net},
           {"desirability", liveState.populationChange.desirability}}},
         {"budget", budgetStatus()},
         {"funds", funds},
         {"paused", liveState.paused},
         {"disasters", {{"enabled", liveState.enableDisasters},
                         {"active_fires", liveState.disasters.fire.activeFires},
                         {"lost_fire", liveState.disasters.buildingsLostToFire},
                         {"lost_disaster", liveState.disasters.buildingsLostToDisaster},
                         {"earthquake", liveState.disasters.disaster.earthquakeOccurred},
                         {"flood", liveState.disasters.disaster.floodOccurred}}},
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
           {"port",portToolActive && portAction!=6},{"port_action",portAction},
           {"hotel",portToolActive && portAction==6},
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
           {"regional_gateways",regionalGateways},
           {"regional_logistics_enabled",liveState.regionalLogisticsEnabled},
           {"port_operating_cost",regional::portOperatingCost(liveState.regionalConnections)},
           {"airport_operating_cost",regional::regionalOperatingCost(liveState.regionalConnections)-regional::portOperatingCost(liveState.regionalConnections)},
           {"regional_operating_cost",regional::regionalOperatingCost(liveState.regionalConnections)},
           {"tourism_enabled",liveState.tourism.enabled},
           {"tourism_tick",liveState.tourism.report.tick},
           {"visitor_day_arrivals",liveState.tourism.report.dayArrivals},
           {"visitor_overnight_arrivals",liveState.tourism.report.overnightArrivals},
           {"visitors_staying",liveState.tourism.report.staying},
           {"visitor_pending_departures",liveState.tourism.state.pendingEarlyDepartures},
           {"visitor_road_trips",liveState.tourism.travel.road},
           {"visitor_transit_trips",liveState.tourism.travel.transit},
           {"visitor_walking_trips",liveState.tourism.travel.walking},
           {"visitor_unrouted_trips",liveState.tourism.travel.unrouted},
           {"tourism_fees",liveState.tourism.accounting.fees()},
           {"tourism_services",liveState.tourism.accounting.services},
           {"goods_availability",liveState.economy.goodsAvailability},
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
           {"budget", showBudgetPanel},
           {"transport",showTransportPanel},
           {"budget_focus",budgetFocus},{"transport_focus",transportFocus},
           {"trends_tick",liveState.history.samples.empty() ? 0 : liveState.history.samples[trendSampleIndex(liveState.history,inspectedTrendTick)].tick},
           {"port_button",{{"x",portButtonRect().x},{"y",portButtonRect().y},
             {"width",portButtonRect().w},{"height",portButtonRect().h}}},
           {"pinned_tile",{pinnedInspector.x,pinnedInspector.y}},
           {"budget_services", budgetServices},
           {"budget_trends", budgetTrends},
           {"budget_alerts", budgetAlerts},
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
      if (showTransportPanel) {
        if (event.type==EventType::Quit) showQuitDialog=true;
        if (event.type==EventType::KeyDown) {
          if (event.key.sym==Key::F2) gReadableUiText=!gReadableUiText;
          if (event.key.sym==Key::Tab || event.key.sym==Key::Down) cycleTransportFocus(1);
          if (event.key.sym==Key::Up) cycleTransportFocus(-1);
          if (event.key.sym==Key::Enter && transportControlEnabled(transportFocus,liveState.transitRoutes,selectedTransitRoute)) transportAction(transportFocus);
          if (event.key.sym==Key::Left) transportAction(1);
          if (event.key.sym==Key::Right) transportAction(2);
          if (event.key.sym==Key::Escape || event.key.sym==Key::F8) showTransportPanel=false;
          if (event.key.sym==Key::F6 || event.key.sym==Key::F7) {showTransportPanel=false;showBudgetPanel=true;budgetAlerts=event.key.sym==Key::F7;budgetTrends=false;}
          if (event.key.sym==Key::Space) {liveState.paused=!liveState.paused;sessionDirty=true;}
          if (liveState.paused && (event.key.sym==Key::N || event.key.sym==Key::Period)) executeSimulationTick();
          if (event.key.sym==Key::F5) saveSession();
        }
        if (event.type==EventType::MouseMotion) {mouseX=event.motion.x;mouseY=event.motion.y;}
        if (event.type==EventType::MouseDown && event.button.button==MouseButton::Left)
          {transportFocus=transportHitTest(event.button.x,event.button.y,windowWidth,windowHeight);if (transportFocus>=0) transportAction(transportFocus);}
        continue;
      }
      if (showBudgetPanel) {
        if (event.type==EventType::KeyDown) {
          if (event.key.sym==Key::F2) gReadableUiText=!gReadableUiText;
          if (event.key.sym==Key::Tab || event.key.sym==Key::Down) cycleBudgetFocus(1);
          if (event.key.sym==Key::Up) cycleBudgetFocus(-1);
          if (event.key.sym==Key::Enter && budgetFocus>=0) activateBudgetAction(budgetFocus);
          if (budgetTrends && !liveState.history.samples.empty()) {
            auto index=trendSampleIndex(liveState.history,inspectedTrendTick);
            if (event.key.sym==Key::Left && index) --index;
            if (event.key.sym==Key::Right) index=std::min(index+1,liveState.history.samples.size()-1);
            if (event.key.sym==Key::Home) index=0;
            if (event.key.sym==Key::End) inspectedTrendTick=0;
            else if (event.key.sym==Key::Left || event.key.sym==Key::Right || event.key.sym==Key::Home) inspectedTrendTick=liveState.history.samples[index].tick;
          }
        }
        if (event.type==EventType::KeyDown && event.key.sym==Key::F8) {showBudgetPanel=false;showTransportPanel=true;continue;}
        if (event.type == EventType::Quit) showQuitDialog = true;
        if (event.type == EventType::KeyDown &&
            (event.key.sym == Key::Escape || event.key.sym == Key::F6 || (event.key.sym == Key::F7 && budgetAlerts))) showBudgetPanel = false;
        else if (event.type == EventType::KeyDown && event.key.sym == Key::F7) {
          budgetAlerts = true; budgetTrends = false;
        }
        if (event.type == EventType::KeyDown && event.key.sym == Key::Space) {
          liveState.paused = !liveState.paused;
          sessionDirty = true;
        }
        if (event.type == EventType::KeyDown && liveState.paused &&
            (event.key.sym == Key::Period || event.key.sym == Key::N)) executeSimulationTick();
        if (event.type == EventType::KeyDown && event.key.sym == Key::F5) saveSession();
        if (event.type == EventType::MouseMotion) {
          mouseX = event.motion.x; mouseY = event.motion.y;
          if (budgetTrends && !budgetAlerts) {const auto tick=trendHitTick(mouseX,mouseY,windowWidth,windowHeight,liveState.history);if (tick) inspectedTrendTick=tick;}
        }
        if (event.type == EventType::MouseDown && event.button.button == MouseButton::Left) {
          const int action = budgetHitTest(event.button.x, event.button.y, windowWidth, windowHeight, budgetServices, budgetTrends, budgetAlerts);
          budgetFocus=action;
          if (action<0 && budgetTrends) {const auto tick=trendHitTick(event.button.x,event.button.y,windowWidth,windowHeight,liveState.history);if (tick) inspectedTrendTick=tick;}
          activateBudgetAction(action);
        }
        continue;
      }
      OverlayMode clickedOverlay = overlayMode;
      if (event.type == EventType::Quit) {
        showQuitDialog = true;
      } else if (event.type == EventType::KeyDown) {
        switch (event.key.sym) {
        case Key::K:
          activatePaletteTool(PaletteTool::Port); portAction=6;
          notify("HOTEL $10000 - 32 BEDS / 12 JOBS; EMPTY LAND, ROAD AND UTILITIES",true);
          break;
        case Key::J:
          activatePaletteTool(PaletteTool::Port); portAction=5;
          notify("AIRPORT $25000 + $30/TICK - 4X2 LAND; UTILITIES ON EVERY TILE",true);
          break;
        case Key::V:
          selectPortAction();
          break;
        case Key::I:
          if (map.isValid(pinnedInspector)) pinnedInspector={-1,-1};
          else {pinnedInspector=tileAtScreen(mouseX,mouseY);activatePaletteTool(PaletteTool::None);}
          break;
        case Key::Escape:
          if (map.isValid(pinnedInspector)) {pinnedInspector={-1,-1};break;}
          if (toolDragging || roadToolActive || zoneToolActive ||
              bulldozeToolActive || serviceToolActive || districtToolActive || portToolActive) {
            toolDragging = false;
            roadToolActive = false;
            zoneToolActive = false;
            bulldozeToolActive = false;
            serviceToolActive = false;
            districtToolActive = false;
            portToolActive = false;
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
        case Key::Num9:
          overlayMode = OverlayMode::Transit;
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
        case Key::F8:
          showTransportPanel=true;showBudgetPanel=false;toolDragging=false;cameraPanning=false;
          break;
        case Key::F7:
          showBudgetPanel = true; budgetAlerts = true; budgetTrends = false;
          toolDragging = false; cameraPanning = false;
          break;
        case Key::F6:
          showBudgetPanel = true;
          toolDragging = false;
          cameraPanning = false;
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
          portToolActive = false;
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
          portToolActive = false;
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
          portToolActive = false;
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
          portToolActive = false;
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
                                mouseY >= 12 && mouseY < (overlayMode == OverlayMode::Transit ? 210 : 158);
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
        case HudAction::Transport:
          showTransportPanel=true;showBudgetPanel=false;toolDragging=false;cameraPanning=false;
          break;
        case HudAction::Alerts:
          showBudgetPanel = true; budgetAlerts = true; budgetTrends = false;
          toolDragging = false; cameraPanning = false;
          break;
        case HudAction::Budget:
          showBudgetPanel = true;
          toolDragging = false;
          cameraPanning = false;
          break;
        case HudAction::ToggleDisasters:
          liveState.enableDisasters = !liveState.enableDisasters;
          if (!liveState.enableDisasters) liveState.disasters.stopEvents();
          sessionDirty = true;
          notify(liveState.enableDisasters ? "DISASTERS ON - BUILDINGS CAN BE LOST" : "DISASTERS OFF", true);
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
      } else if (event.type == EventType::MouseDown && !cleanUiMode &&
                 event.button.button == MouseButton::Left && overPortButton(event.button.x,event.button.y)) {
        selectPortAction();
      } else if (event.type == EventType::MouseDown && !cleanUiMode &&
                 event.button.button==MouseButton::Left && overAirportButton(event.button.x,event.button.y)) {
        activatePaletteTool(PaletteTool::Port);portAction=5;
      } else if (event.type == EventType::MouseDown && !cleanUiMode &&
                 event.button.button==MouseButton::Left && overHotelButton(event.button.x,event.button.y)) {
        activatePaletteTool(PaletteTool::Port);portAction=6;
      } else if (event.type == EventType::MouseDown && !cleanUiMode &&
                 event.button.button==MouseButton::Left && overTourismButton(event.button.x,event.button.y)) {
        setTourism(!liveState.tourism.enabled);
      } else if (event.type == EventType::MouseDown && overPortBanner(event.button.x,event.button.y)) {
        // The informational port panel consumes clicks rather than building underneath it.
      } else if (event.type == EventType::MouseDown && portToolActive) {
        if (event.button.button==MouseButton::Left) applyPort(tileAtScreen(event.button.x,event.button.y));
        else if (event.button.button==MouseButton::Right) activatePaletteTool(PaletteTool::None);
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

    const auto deliveredFacilities = liveState.budgetPolicy.fundedFacilities(facilities);
    const auto& overlayFacilities = (overlayMode == OverlayMode::Zone || overlayMode == OverlayMode::Transit) ? facilities : deliveredFacilities;
    std::unordered_set<Coord, Vec2Hash> burningCoords;
    for (const auto& tile : liveState.disasters.burningTiles) burningCoords.insert(tile.position);
    HotelPlan hoveredHotelPlan;
    if (portToolActive && portAction==6 && !showBudgetPanel && !showTransportPanel)
      hoveredHotelPlan=HotelTool::plan(map,roads,facilities,tileAtScreen(mouseX,mouseY),funds);
    PortPlan hoveredPortPlan;
    if (portToolActive && terminalPlacement() && !showBudgetPanel && !showTransportPanel)
      hoveredPortPlan=planTerminalAt(tileAtScreen(mouseX,mouseY));
    std::unordered_map<uint32_t,const regional::Connection*> portById;
    for (const auto& c:liveState.regionalConnections) if (c.constructed) portById.emplace(c.id,&c);
    const auto& transitMarks = transitOverlayCache.update(roads, liveState.transitRoutes);
    if (isometricMode) {
      const IsometricProjection projection = isometricProjection();
      for (int depth = 0; depth <= (mapSize - 1) * 2; ++depth) {
        for (int x = 0; x < mapSize; ++x) {
          const int y = depth - x;
          if (y < 0 || y >= mapSize)
            continue;
          drawIsometricTile(renderer, map, roads, store, overlayFacilities,
                            liveState.routeHeatByTile,
                            liveState.waste.happinessPenalty +
                                liveState.deathcare.happinessPenalty,
                            overlayMode == OverlayMode::Transit ? OverlayMode::Zone : overlayMode, projection, {x, y}, &districts, selectedDistrictId,
                            overlayMode == OverlayMode::Transit ? &transitMarks : nullptr);
          const auto port=portById.find(map.getTile({x,y}).regionalConnectionId);
          if (port!=portById.end() && port->second->kind==regional::Kind::Airport)
            drawAirportTile(renderer,projection.tileTop({x,y}),projection.tileWidth(),projection.tileHeight(),true,
              port->second->enabled,x-port->second->origin.x,y-port->second->origin.y);
          else if (port!=portById.end()) drawPortTile(renderer,projection.tileTop({x,y}),
            projection.tileWidth(),projection.tileHeight(),true,port->second->kind,port->second->enabled,
            x==port->second->origin.x);
          if (burningCoords.count({x, y}) != 0) {
            const auto top = projection.tileTop({x, y});
            drawFireMarker(renderer, {top.x, top.y + projection.tileHeight() / 2}, projection.tileWidth());
          }
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

          const RGB color = tileColor(map, roads, store, coord, overlayMode == OverlayMode::Transit ? OverlayMode::Zone : overlayMode,
                                      overlayFacilities, liveState.routeHeatByTile,
                                      liveState.waste.happinessPenalty +
                                          liveState.deathcare.happinessPenalty);
          setDrawColor(renderer, color.r, color.g, color.b, 255);

          visualizer::Rect rect{tx * tilePixels, ty * tilePixels, tilePixels,
                                tilePixels};
          fillRect(renderer, &rect);

          if (overlayMode == OverlayMode::Zone || overlayMode == OverlayMode::Transit) {
            drawNormalTileDetail(renderer, map, store, facilities, coord,
                                 rect.x, rect.y, tilePixels, &roads);
          }
          const auto port=portById.find(map.getTile(coord).regionalConnectionId);
          if (port!=portById.end() && port->second->kind==regional::Kind::Airport)
            drawAirportTile(renderer,{rect.x,rect.y},tilePixels,tilePixels,false,port->second->enabled,
              coord.x-port->second->origin.x,coord.y-port->second->origin.y);
          else if (port!=portById.end()) drawPortTile(renderer,{rect.x,rect.y},tilePixels,tilePixels,false,
            port->second->kind,port->second->enabled,coord.x==port->second->origin.x);
          if (burningCoords.count(coord) != 0)
            drawFireMarker(renderer, {rect.x + tilePixels / 2, rect.y + tilePixels / 2}, tilePixels);
          if (overlayMode == OverlayMode::Transit && map.getTile(coord).hasRoad)
            drawTransitTile(renderer, transitMarks, coord,
                            {rect.x + tilePixels / 2, rect.y + tilePixels / 2},
                            tilePixels, tilePixels, false);
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

    if (overlayMode == OverlayMode::Transit && !cleanUiMode && showLegend) {
      const auto& summary = liveState.transitSummary;
      drawFilledRect(renderer, 12, 164, 520, 46, {14, 16, 20}, 220);
      drawText(renderer, 24, 174, "BUS " + std::to_string(summary.busRoutes) +
               " RAIL " + std::to_string(summary.railRoutes) +
               " STOPS " + std::to_string(summary.totalStops), {235, 245, 250}, 1);
      drawText(renderer, 24, 190, "RIDERS " + std::to_string(summary.ridership) +
               " DEMAND " + std::to_string(summary.demand) +
               " CAPACITY " + std::to_string(summary.totalCapacity), {235, 245, 250}, 1);
    }

    const bool mouseOverHud =
        cleanUiMode
            ? (mouseX >= windowWidth - 342 && mouseY >= 12 && mouseY <
                ((liveState.enableDisasters || liveState.disasters.buildingsLostToFire > 0 ||
                  liveState.disasters.buildingsLostToDisaster > 0) ? 130 : 94))
            : (mouseX >= windowWidth - kHudPanelWidth - 12 && mouseY >= 12 &&
               mouseY < 12 + kHudPanelHeight);
    const bool mouseOverLegend = !cleanUiMode && showLegend && mouseX >= 12 &&
                                 mouseX < 532 && mouseY >= 12 && mouseY < (overlayMode == OverlayMode::Transit ? 210 : 158);
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
    const Coord hoveredTile = map.isValid(pinnedInspector) ? pinnedInspector : tileAtScreen(mouseX, mouseY);
    const bool inspectMap = !showTransportPanel && !showBudgetPanel &&
      (map.isValid(pinnedInspector) || (!mouseOverHud && !mouseOverLegend && !mouseOverPalette && !mouseOverDistrictPanel && !mouseOverGuide &&
        (cleanUiMode || (!overPortButton(mouseX,mouseY) && !overAirportButton(mouseX,mouseY) && !overHotelButton(mouseX,mouseY) && !overTourismButton(mouseX,mouseY))) && !overPortBanner(mouseX,mouseY))) && map.isValid(hoveredTile);
    if (inspectMap && !portToolActive) {
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

    if (portToolActive && !showBudgetPanel && !showTransportPanel &&
        (cleanUiMode || (!overPortButton(mouseX,mouseY) && !overAirportButton(mouseX,mouseY) && !overHotelButton(mouseX,mouseY) && !overTourismButton(mouseX,mouseY))) && !overPortBanner(mouseX,mouseY)) {
      const Coord hover=tileAtScreen(mouseX,mouseY);
      if (map.isValid(hover)) {
        std::vector<Coord> tiles;
        bool valid=false;
        if (portAction==6) {
          tiles.push_back(hover);valid=hoveredHotelPlan.valid;
          const auto* hotel=store.getBuilding(map.getTile(hover).buildingId);
          if (hotel && hotel->hotelBeds) valid=true;
        } else if (terminalPlacement()) {
          const auto& plan=hoveredPortPlan;
          tiles=plan.tiles;valid=plan.valid;
        } else {
          const auto id=map.getTile(hover).regionalConnectionId;
          const auto found=portById.find(id);
          if (found!=portById.end()) {
            for (int y=found->second->origin.y;y<found->second->origin.y+found->second->height;++y)
              for (int x=found->second->origin.x;x<found->second->origin.x+found->second->width;++x) tiles.push_back({x,y});
            valid=portAction!=2 || funds>=regional::terminalSpec(found->second->kind)->demolitionCost;
          }
        }
        for (const auto tile:tiles) if (map.isValid(tile)) {
          const RGB color=valid ? RGB{80,220,170} : RGB{235,75,75};
          if (isometricMode) {
            const auto projection=isometricProjection();
            drawDiamond(renderer,projection.tileTop(tile),projection.tileWidth(),projection.tileHeight(),color,110);
          } else drawRectOutline(renderer,(tile.x-viewX)*tilePixels,(tile.y-viewY)*tilePixels,tilePixels,tilePixels,color);
        }
      }
    }
    if (!cleanUiMode && showLegend) {
      drawLegendPanel(renderer, overlayMode, windowWidth, windowHeight, mouseX,
                      mouseY);
    }
    if (!cleanUiMode && inspectMap && !portToolActive) {
      const bool burning = std::any_of(liveState.disasters.burningTiles.begin(), liveState.disasters.burningTiles.end(),
        [&](const auto& tile) { return tile.position == hoveredTile; });
      const auto diagnostics = BuildingDiagnostics::evaluate(map,roads,store,hoveredTile,liveState.demand,
        liveState.serviceCache,burning,&liveState.districtGrowthModifiers);
      const auto residents = population.getTotalPopulation();
      const float joblessness = residents ? static_cast<float>(residents-std::min(residents,population.getTotalEmployed()))/residents : 0.f;
      drawTileInspector(renderer, map, roads, store, deliveredFacilities, hoveredTile,
                        &districts, &diagnostics, joblessness,map.isValid(pinnedInspector));
    }
    if (!cleanUiMode) {
      if (const District *selected =
              districts.getDistrictConst(selectedDistrictId)) {
        drawDistrictPanel(renderer, *selected, mouseX, mouseY);
      }
    }

    std::string activeToolLabel = "NONE";
    if (portToolActive) activeToolLabel=portAction==6 ? "HOTEL" : std::string("TERMINAL ")+portActions[portAction];
    else if (roadToolActive) {
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
                             liveState.trafficSummary.freight,
                             &liveState.disasters, liveState.enableDisasters);
    } else {
      drawGameplayHud(renderer, windowWidth, population.getTotalPopulation(),
                      store.getBuildingCount(), funds, activeToolLabel,
                      liveState, population, mouseX, mouseY);
    }
    drawToolPalette(renderer, windowWidth, windowHeight, roadToolActive,
                    zoneToolActive, bulldozeToolActive, serviceToolActive,
                    districtToolActive, mouseX, mouseY);
    if (!cleanUiMode && !showBudgetPanel && !showTransportPanel) {
      const auto r=portButtonRect();
      drawFilledRect(renderer,r.x,r.y,r.w,r.h,portToolActive ? RGB{40,100,110} : RGB{28,43,53});
      drawRectOutline(renderer,r.x,r.y,r.w,r.h,{135,170,190});
      drawText(renderer,r.x+8,r.y+8,"V PORTS",{225,240,245},1);
      const auto a=airportButtonRect();
      drawFilledRect(renderer,a.x,a.y,a.w,a.h,portToolActive && portAction==5 ? RGB{40,100,110} : RGB{28,43,53});
      drawRectOutline(renderer,a.x,a.y,a.w,a.h,{135,170,190});
      drawText(renderer,a.x+6,a.y+8,"J AIRPORT",{225,240,245},1);
      const auto h=hotelButtonRect();
      drawFilledRect(renderer,h.x,h.y,h.w,h.h,portToolActive && portAction==6 ? RGB{40,100,110} : RGB{28,43,53});
      drawRectOutline(renderer,h.x,h.y,h.w,h.h,{135,170,190});
      drawText(renderer,h.x+8,h.y+8,"K HOTEL",{225,240,245},1);
      const auto t=tourismButtonRect();
      drawFilledRect(renderer,t.x,t.y,t.w,t.h,liveState.tourism.enabled ? RGB{40,100,110} : RGB{28,43,53});
      drawRectOutline(renderer,t.x,t.y,t.w,t.h,{135,170,190});
      drawText(renderer,t.x+4,t.y+8,liveState.tourism.enabled ? "TOURISM ON" : "TOURISM OFF",{225,240,245},1);
      if (portToolActive && portAction==6) {
        const int x=14, y=windowHeight-166;
        drawFilledRect(renderer,x,y,windowWidth-x-140,100,{14,24,32},245);
        drawText(renderer,x+8,y+7,"HOTEL - K SELECTS - CLICK HOTEL TO OPEN/CLOSE",{150,225,235},1);
        drawText(renderer,x+8,y+24,"$10000; 32 BEDS/12 JOBS; FEES $2 DAY/$4 STAY; SERVICE $1",{230,215,165},1);
        const auto& travel=liveState.tourism.travel;
        const auto travelInfo=travel.measured ? "LAST TRIPS ROAD "+std::to_string(travel.road)+
          " TRANSIT "+std::to_string(travel.transit)+" WALK "+std::to_string(travel.walking)+
          " UNROUTED "+std::to_string(travel.unrouted) : "VISITOR TRIPS USE TRANSIT AFTER RESIDENTS";
        drawText(renderer,x+8,y+42,travelInfo,{175,200,210},1);
        std::string hotelInfo=hoveredHotelPlan.valid ? "VALID SITE - CLICK TO BUILD" : hoveredHotelPlan.error;
        const auto hover=tileAtScreen(mouseX,mouseY);
        const auto* building=map.isValid(hover) ? store.getBuilding(map.getTile(hover).buildingId) : nullptr;
        if (building && building->hotelBeds) {
          uint32_t guests=0;
          for (const auto& stay:liveState.tourism.state.stays)
            if (stay.hotelId==building->id) guests+=stay.guests;
          hotelInfo=std::string(building->hotelOpen ? "OPEN: " : "CLOSED: ")+std::to_string(guests)+"/"+std::to_string(building->hotelBeds)+
            " BEDS; JOBS/UPKEEP CONTINUE";
        }
        drawText(renderer,x+8,y+61,hotelInfo,{200,220,230},1);
        const auto& visitors=liveState.tourism.report;
        const auto visitorInfo=visitors.tick ? "LAST T"+std::to_string(visitors.tick)+
          " DAY "+std::to_string(visitors.dayArrivals)+" NIGHT "+std::to_string(visitors.overnightArrivals)+
          " STAY "+std::to_string(visitors.staying)+
          " OUT "+std::to_string(visitors.scheduledDepartures+visitors.earlyDepartures)+
          " EARLY "+std::to_string(visitors.earlyDepartures)+" UNMET "+
          std::to_string(visitors.unmetDayTransport+visitors.unmetOvernightTransport+visitors.unmetOvernightBeds) :
          "PARKS ATTRACT VISITORS; 3-TICK STAYS; B BULLDOZES";
        drawText(renderer,x+8,y+79,visitorInfo,{175,205,215},1);
      } else if (portToolActive) {
        const int x=14, y=windowHeight-166;
        drawFilledRect(renderer,x,y,windowWidth-x-140,100,{14,24,32},245);
        drawText(renderer,x+8,y+7,std::string("TERMINAL ")+portActions[portAction]+" - V MANAGES - ESC CANCELS",{150,225,235},1);
        std::string info=portAction==5 ? "$25000 + $30/TICK; 4X2 LAND; 120 PASSENGERS/TICK"
          : portAction==0 ? "$15000 + $20/TICK; DESIGN CAPACITY 200 GOODS/TICK"
          : (portAction==1 ? "$10000 + $10/TICK; 60 PASSENGERS; TOURISM CONTROLS ARRIVALS"
          : (portAction==2 ? "REMOVE WHOLE TERMINAL: PORT $500 / AIRPORT $1000; NO REFUND"
          : (portAction==3 ? "CLICK PORT TO ENABLE/DISABLE; DISABLED PORTS HAVE NO UPKEEP"
          : "CLICK TO CYCLE CAPACITY; PORT 200 GOODS/60 PASS; AIRPORT 120 PASS")));
        drawText(renderer,x+8,y+24,info,{230,215,165},1);
        if (portAction==0) drawText(renderer,x+8,y+42,
          "FIRST PORT: DELIVERED TRADE ONLY; REMOVAL KEEPS ROAD LIMIT 100/TICK",{230,215,165},1);
        else drawText(renderer,x+8,y+42,portAction==5 ?
          "UTILITIES EVERY TILE; ROAD BOTH LONG SIDES; TOURISM NEEDS PARK ACCESS" :
          "HOVER A TERMINAL FOR ACCESS AND LAST COMPLETED FLOW",{175,200,210},1);
        const auto hover=tileAtScreen(mouseX,mouseY);
        if (map.isValid(hover)) {
          const auto found=portById.find(map.getTile(hover).regionalConnectionId);
          if (found!=portById.end()) {
            const auto& c=*found->second;
            const auto access=regional::diagnoseOperation(c,map,roads,regional::SeaAccess(map),c.entrance);
            info="ID "+std::to_string(c.id)+" "+regional::accessName(access)+" CAP "+
              std::to_string(c.goodsPerTick ? c.goodsPerTick : c.passengersPerTick)+" UPKEEP $"+
              std::to_string(regional::regionalOperatingCost({c}))+"/TICK";
          } else if (terminalPlacement()) {
            const auto& plan=hoveredPortPlan;
            info=plan.valid ? "VALID SHORE SITE - ROAD GATEWAY LIMITED TO 100 GOODS/TICK AFTER CARGO" : plan.error;
          } else info="SELECT A CONSTRUCTED TERMINAL";
          if (found!=portById.end() && terminalPlacement()) info="OCCUPIED - "+info;
          drawText(renderer,x+8,y+61,info,{200,220,230},1);
          if (found!=portById.end()) {
            const auto& report=liveState.regionalSettlement;
            const auto row=std::find_if(report.allocation.connections.begin(),report.allocation.connections.end(),
              [&](const auto& u){return u.id==found->second->id;});
            if (report.valid && row!=report.allocation.connections.end()) info="LAST T"+
              std::to_string(report.allocation.tick)+" IN "+std::to_string(row->imports)+" OUT "+
              std::to_string(row->exports)+" PASS "+std::to_string(row->passengers)+"/"+
              (row->capacityMeasured ? std::to_string(row->availablePassengers) : "?")+"; UNMET TRADE "+
              std::to_string(static_cast<uint64_t>(report.allocation.unmet.imports)+report.allocation.unmet.exports);
            else info="NO COMPLETED FLOW RECORDED FOR THIS TERMINAL";
            if (found->second->kind==regional::Kind::Airport || found->second->kind==regional::Kind::PassengerTerminal) {
              const auto& visitors=liveState.tourism.report;
              const auto usage=std::find_if(visitors.connections.begin(),visitors.connections.end(),
                [&](const auto& value){return value.id==found->second->id;});
              info=usage!=visitors.connections.end() ? "TOURISM T"+std::to_string(visitors.tick)+
                " ARRIVALS "+std::to_string(usage->day+usage->overnight)+"/"+std::to_string(usage->capacity)+
                " - "+regional::accessName(usage->access) : "NO COMPLETED TOURISM FLOW FOR THIS TERMINAL";
            }
            drawText(renderer,x+8,y+79,info,{175,205,215},1);
          }
        }
      }
    }
    if (!cleanUiMode && showOnboarding && !portToolActive) {
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
      case HudAction::Transport:
        uiHoverText = "TRANSIT: ROUTE UTILIZATION, BUS FLEETS AND OPERATING COST";
        break;
      case HudAction::Alerts:
        uiHoverText = "ALERTS: MEASURED CITY PROBLEMS AND RELEVANT ACTIONS";
        break;
      case HudAction::Budget:
        uiHoverText = "BUDGET: INSPECT TREASURY FLOWS AND CHANGE CITY TAXES";
        break;
      case HudAction::ToggleDisasters:
        uiHoverText = "DISASTERS: TOGGLE DESTRUCTIVE FIRES EARTHQUAKES AND FLOODS";
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
        case OverlayMode::Transit:
          uiHoverText = "TRANSIT: CYAN BUS GOLD RAIL WHITE STOP MARKERS";
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

    if (showBudgetPanel)
      drawBudgetPanel(renderer, windowWidth, windowHeight, liveState.budgetPolicy,
                      budgetPreview(), liveState.treasuryLedger, funds,
                      liveState.treasuryDebt, mouseX, mouseY, budgetServices,
                      liveState.budgetPolicy.serviceCosts(facilities), BudgetPolicy::coverage(liveState.serviceSummary), budgetTrends, liveState.history, budgetAlerts, liveState.alerts,budgetFocus,inspectedTrendTick);
    if (showTransportPanel)
      drawTransportPanel(renderer,windowWidth,windowHeight,liveState.transitRoutes,selectedTransitRoute,
        liveState.transitReport,routePeak(selectedTransitRoute).first,
        liveState.transitRoutes.empty() ? 0 : static_cast<int>(std::count_if(
          liveState.transitRoutes[std::min(selectedTransitRoute,liveState.transitRoutes.size()-1)].stops.begin(),
          liveState.transitRoutes[std::min(selectedTransitRoute,liveState.transitRoutes.size()-1)].stops.end(),
          [&](Coord stop){return roads.getNode(stop)!=nullptr;})),transportFocus);
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
