#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimSupport.hpp"
#include "src/systems/DeathcareSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/systems/WasteSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Tile.hpp"
#include "src/world/Zoning.hpp"

namespace visualizer {

extern bool gReadableUiText;

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

enum class HudAction {
  None,
  TogglePause,
  Slow,
  Normal,
  Fast,
  Save,
  Load,
};

enum class QuitAction {
  None,
  SaveAndQuit,
  QuitWithoutSaving,
  Cancel,
};

struct LiveSimulationState {
  uint32_t tick = 0;
  bool paused = false;
  uint32_t tickIntervalMs = 350;
  uint32_t lastTickMs = 0;
  ServiceCoverageSummary serviceSummary;
  ServiceCoverageCache serviceCache;
  TrafficSummary trafficSummary;
  std::unordered_map<Coord, float, Vec2Hash> routeHeatByTile;
  RouteDiagnosticsFilter routeFilter;
  ZoneDemand demand;
  EconomyState economy;
  float illnessRate = 0.0f;
  float crimeRate = 0.0f;
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
  // Transit routes persist across live ticks (same stack as CitySimulator).
  std::vector<TransitRoute> transitRoutes;
  TransitCoverageCache transitCache;
  TransitSummary transitSummary;
  // When true, each live tick also runs CitySimulator-style road/zone/facility
  // expansion (same expandConstruction helper as the autonomous CLI path)
  // before the playable tick.
  bool autonomousGrowth = false;
  int autonomousGridSpacing = 4;
  city_sim::ConstructionState construction;  // extent 0 = not seeded yet
};

struct ToastNotification {
  std::string message;
  RGB color{235, 238, 242};
  uint32_t expiresAtMs = 0;
};

struct StartScreenResult {
  bool quit = false;
  bool loadSession = false;
  int mapSize = 64;
  bool generateTerrain = false;
};

const char* overlayModeName(OverlayMode mode);
const std::array<OverlayMode, 8>& overlayModes();
bool overlayHitTest(int mouseX, int mouseY, OverlayMode& outMode);

ZoneType nextPlayableZone(ZoneType zone);
ServiceType nextPlayableService(ServiceType type);

RGB serviceFacilityColor(ServiceType type);
uint8_t toByte(float normalized);
RGB zoneColor(int zone);
RGB terrainTint(const Tile& tile, RGB base);
RGB landValueColor(float landValue);
RGB pollutionColor(float pollution);
RGB serviceCoverageColor(float score);
RGB congestionColor(float score);
RGB demandColor(float score);
RGB happinessColor(float score);
RGB routeHeatColor(float score);

constexpr int kOverlayPanelX = 12;
constexpr int kOverlayPanelY = 12;
constexpr int kOverlayKeyStartX = kOverlayPanelX + 10;
constexpr int kOverlayKeyY = kOverlayPanelY + 10;
constexpr int kOverlayKeyWidth = 52;
constexpr int kOverlayKeyHeight = 20;
constexpr int kOverlayKeyGap = 8;

}  // namespace visualizer
