#pragma once

#include "src/visualization/VisualizerTypes.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/entities/PopulationStore.hpp"

namespace visualizer {

constexpr int kHudPanelWidth = 390;
constexpr int kHudPanelHeight = 238;

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
);

SDL_Rect hudControlRect(HudAction action, int windowWidth);
HudAction hudHitTest(int mouseX, int mouseY, int windowWidth);
void drawDemandBar(SDL_Renderer* renderer, int x, int y, float demand, RGB color, const char* label);

void drawGameplayHud(
  SDL_Renderer* renderer,
  int windowWidth,
  uint32_t population,
  size_t buildings,
  int64_t funds,
  const std::string& toolLabel,
  const LiveSimulationState& liveState,
  int mouseX,
  int mouseY
);

void drawCompactGameplayHud(
  SDL_Renderer* renderer,
  int windowWidth,
  bool paused,
  uint32_t population,
  int64_t funds,
  const std::string& toolLabel
);

void drawToast(
  SDL_Renderer* renderer,
  int windowWidth,
  const ToastNotification& toast,
  uint32_t nowMs
);

void drawPlacementWarning(
  SDL_Renderer* renderer,
  int mouseX,
  int mouseY,
  int windowWidth,
  int windowHeight,
  const std::string& warning
);

void drawUiTooltip(
  SDL_Renderer* renderer,
  int mouseX,
  int mouseY,
  int windowWidth,
  int windowHeight,
  const std::string& message
);

void drawOnboarding(SDL_Renderer* renderer, int windowHeight, int step);

void drawTileInspector(
  SDL_Renderer* renderer,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  Coord coord
);

SDL_Rect quitButtonRect(QuitAction action, int windowWidth, int windowHeight);
QuitAction quitHitTest(int mouseX, int mouseY, int windowWidth, int windowHeight);

void drawQuitDialog(
  SDL_Renderer* renderer,
  int windowWidth,
  int windowHeight,
  int mouseX,
  int mouseY,
  bool dirty
);

void drawSwatch(SDL_Renderer* renderer, int x, int y, RGB color, bool active = false);
void drawZoneLegend(SDL_Renderer* renderer, int x, int y);
void drawSteppedLegend(SDL_Renderer* renderer, int x, int y, OverlayMode mode);

void drawLegendPanel(
  SDL_Renderer* renderer,
  OverlayMode overlayMode,
  int windowWidth,
  int windowHeight,
  int mouseX,
  int mouseY
);

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
);

}  // namespace visualizer
