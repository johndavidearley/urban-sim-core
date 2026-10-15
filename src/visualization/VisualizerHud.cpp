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
#include "src/visualization/IsometricProjection.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"

#include "src/visualization/VisualizerHud.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/visualization/VisualizerRender.hpp"


namespace visualizer {

void drawToolPalette(
  DrawList* renderer,
  int windowWidth,
  int windowHeight,
  bool roadActive,
  bool zoneActive,
  bool bulldozeActive,
  bool serviceActive,
  bool districtActive,
  int mouseX,
  int mouseY
) {
  const std::array<const char*, 5> labels = {"R ROAD", "Z ZONE", "B BULL", "S SVC", "P DIST"};
  const std::array<bool, 5> active = {
    roadActive, zoneActive, bulldozeActive, serviceActive, districtActive
  };
  for (size_t i = 0; i < labels.size(); ++i) {
    const Rect rect = paletteButtonRect(static_cast<int>(i), windowWidth, windowHeight);
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



Rect hudControlRect(HudAction action, int windowWidth) {
  const int panelX = windowWidth - kHudPanelWidth - 12;
  constexpr int y = 158;
  switch (action) {
    case HudAction::TogglePause: return {panelX + 10, y, 72, 24};
    case HudAction::Slow: return {panelX + 92, y, 42, 24};
    case HudAction::Normal: return {panelX + 140, y, 42, 24};
    case HudAction::Fast: return {panelX + 188, y, 42, 24};
    case HudAction::Save: return {panelX + 10, y + 30, 106, 24};
    case HudAction::Load: return {panelX + 124, y + 30, 106, 24};
    case HudAction::ToggleDisasters: return {panelX + 10, 322, 180, 24};
    default: return {0, 0, 0, 0};
  }
}

HudAction hudHitTest(int mouseX, int mouseY, int windowWidth) {
  const std::array<HudAction, 7> actions = {
    HudAction::TogglePause, HudAction::Slow, HudAction::Normal, HudAction::Fast,
    HudAction::Save, HudAction::Load, HudAction::ToggleDisasters
  };
  for (const HudAction action : actions) {
    const Rect rect = hudControlRect(action, windowWidth);
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      return action;
    }
  }
  return HudAction::None;
}

void drawDemandBar(DrawList* renderer, int x, int y, float demand, RGB color, const char* label) {
  constexpr int width = 180;
  constexpr int height = 10;
  drawText(renderer, x, y + 1, label, {225, 228, 235}, 1);
  drawFilledRect(renderer, x + 18, y, width, height, {40, 44, 52}, 255);
  const int fill = static_cast<int>(std::max(0.0f, std::min(1.0f, demand)) * width);
  drawFilledRect(renderer, x + 18, y, fill, height, color, 255);
  drawRectOutline(renderer, x + 18, y, width, height, {125, 135, 150}, 255);
}

void drawGameplayHud(
  DrawList* renderer,
  int windowWidth,
  uint32_t population,
  size_t buildings,
  int64_t funds,
  const std::string& toolLabel,
  const LiveSimulationState& liveState,
  const PopulationStore& populationStore,
  int mouseX,
  int mouseY
) {
  const int x = windowWidth - kHudPanelWidth - 12;
  constexpr int y = 12;
  const bool paused = liveState.paused;
  const uint32_t tickIntervalMs = liveState.tickIntervalMs;
  drawFilledRect(renderer, x, y, kHudPanelWidth, kHudPanelHeight, {14, 16, 20}, 175);
  drawRectOutline(renderer, x, y, kHudPanelWidth, kHudPanelHeight, {190, 198, 210}, 240);
  drawText(renderer, x + 12, y + 10, paused ? "PAUSED" : "LIVE", paused ? RGB{255, 190, 70} : RGB{90, 220, 125}, 2);
  drawText(renderer, x + 12, y + 30, "TICK " + std::to_string(liveState.tick), {225, 228, 235}, 2);
  drawText(renderer, x + 12, y + 50, "POP " + std::to_string(population), {225, 228, 235}, 2);
  drawText(renderer, x + 12, y + 70, "BLDG " + std::to_string(buildings), {225, 228, 235}, 2);
  // Cash on hand (treasury) vs accounting surplus (economy.balance / CLI budgetBalance).
  drawText(renderer, x + 122, y + 30, "$" + std::to_string(funds), {255, 220, 95}, 2);
  drawText(renderer, x + 122, y + 50,
           "BAL $" + std::to_string(liveState.economy.balance),
           liveState.economy.balance >= 0 ? RGB{105, 230, 135} : RGB{255, 145, 120}, 1);
  drawText(renderer, x + 122, y + 66, toolLabel, {160, 205, 255}, 1);

  drawDemandBar(renderer, x + 12, y + 92, liveState.demand.residential, zoneColor(1), "R");
  drawDemandBar(renderer, x + 12, y + 106, liveState.demand.commercial, zoneColor(2), "C");
  drawDemandBar(renderer, x + 12, y + 120, liveState.demand.industrial, zoneColor(3), "I");
  drawDemandBar(renderer, x + 12, y + 134, liveState.demand.office, zoneColor(5), "O");
  drawText(renderer, x + 244, y + 92, "IN $" + std::to_string(liveState.treasuryRevenue), {105, 230, 135}, 1);
  drawText(renderer, x + 244, y + 108, "OUT $" + std::to_string(liveState.treasuryExpenses), {255, 145, 120}, 1);
  drawText(renderer, x + 244, y + 124, "NET $" + std::to_string(liveState.treasuryNet),
           liveState.treasuryNet >= 0 ? RGB{105, 230, 135} : RGB{255, 125, 110}, 1);
  if (liveState.treasuryDebt > 0) {
    drawText(renderer, x + 244, y + 142,
             "DEBT $" + std::to_string(liveState.treasuryDebt), {255, 90, 80}, 1);
  } else if (liveState.bankrupt) {
    drawText(renderer, x + 244, y + 142, "DEFICIT UNFUNDED", {255, 90, 80}, 1);
  } else if (liveState.lowFunds) {
    drawText(renderer, x + 244, y + 142, "LOW FUNDS", {255, 190, 70}, 1);
  }
  drawText(renderer, x + 244, y + 158,
           "WASTE " + std::to_string(static_cast<int>(liveState.waste.collectionRate * 100.0f)),
           liveState.waste.collectionRate >= 0.9f ? RGB{105, 230, 135} : RGB{255, 150, 120}, 1);
  drawText(renderer, x + 244, y + 174,
           "DECEASED " + std::to_string(liveState.deathcare.awaitingDisposition),
           liveState.deathcare.awaitingDisposition == 0 ? RGB{160, 175, 165} : RGB{255, 150, 120}, 1);
  drawText(renderer, x + 244, y + 190,
           "ILL " + std::to_string(static_cast<int>(liveState.illnessRate * 100.0f)) + "%",
           liveState.illnessRate <= 0.12f ? RGB{160, 175, 165} : RGB{255, 150, 120}, 1);
  drawText(renderer, x + 244, y + 206,
           "CRIME " + std::to_string(static_cast<int>(liveState.crimeRate * 100.0f)) + "%",
           liveState.crimeRate <= 0.15f ? RGB{160, 175, 165} : RGB{255, 150, 120}, 1);
  const bool utilityShort =
    liveState.serviceSummary.powerSupplyRatio < 0.999f
    || liveState.serviceSummary.waterSupplyRatio < 0.999f;
  drawText(renderer, x + 12, y + 222,
           "PWR " + std::to_string(static_cast<int>(liveState.serviceSummary.powerSupplyRatio * 100.0f))
             + "%  WTR " + std::to_string(static_cast<int>(liveState.serviceSummary.waterSupplyRatio * 100.0f)) + "%",
           utilityShort ? RGB{255, 150, 120} : RGB{105, 230, 135}, 1);
  uint32_t lowIncome = 0;
  uint32_t middleIncome = 0;
  uint32_t highIncome = 0;
  for (const auto& [id, group] : populationStore.getGroups()) {
    (void)id;
    if (group.band == IncomeBand::Low) {
      lowIncome += group.size;
    } else if (group.band == IncomeBand::Middle) {
      middleIncome += group.size;
    } else {
      highIncome += group.size;
    }
  }
  drawText(renderer, x + 12, y + 238,
           "LOW " + std::to_string(lowIncome)
             + "  MID " + std::to_string(middleIncome)
             + "  HIGH " + std::to_string(highIncome),
           {225, 228, 235}, 1);

  const auto &change = liveState.populationChange;
  drawText(renderer, x + 12, y + 254,
           "IN " + std::to_string(change.arrivals) + " OUT " +
               std::to_string(change.departures) + " DEAD " +
               std::to_string(change.deaths) + " NET " +
               std::to_string(change.net),
           {175, 205, 235}, 1);

  const auto &freight = liveState.trafficSummary.freight;
  drawText(renderer, x + 12, y + 270,
           "TRUCKS " + std::to_string(freight.trucks) + " UNROUTED " +
               std::to_string(freight.unroutedGoods),
           {180, 200, 225}, 1);
  drawText(renderer, x + 12, y + 286,
           "TRADE GATE " + std::to_string(freight.gate.x) + " " +
               std::to_string(freight.gate.y) +
               (freight.gateConnected ? " CONNECTED" : " NO ROAD"),
           {180, 200, 225}, 1);

  const auto& events = liveState.disasters;
  drawText(renderer, x + 12, y + 340,
           "FIRES " + std::to_string(events.fire.activeFires) +
           " LOST FIRE " + std::to_string(events.buildingsLostToFire) +
           " OTHER " + std::to_string(events.buildingsLostToDisaster), {255, 175, 110}, 1);
  drawText(renderer, x + 210, y + 318,
           events.disaster.earthquakeOccurred ? "EARTHQUAKE" :
           (events.disaster.floodOccurred ? "FLOOD" : ""), {255, 145, 100}, 1);
  const std::array<std::pair<HudAction, const char*>, 7> controls = {{
    {HudAction::TogglePause, paused ? "PLAY" : "PAUSE"},
    {HudAction::Slow, "1X"},
    {HudAction::Normal, "2X"},
    {HudAction::Fast, "3X"},
    {HudAction::Save, "F5 SAVE"},
    {HudAction::Load, "F9 LOAD"},
    {HudAction::ToggleDisasters, liveState.enableDisasters ? "DISASTERS ON" : "DISASTERS OFF"},
  }};
  for (const auto& [action, label] : controls) {
    const Rect rect = hudControlRect(action, windowWidth);
    const bool hovered = mouseX >= rect.x && mouseX < rect.x + rect.w
      && mouseY >= rect.y && mouseY < rect.y + rect.h;
    const bool active = (action == HudAction::Slow && tickIntervalMs == 700)
      || (action == HudAction::Normal && tickIntervalMs == 350)
      || (action == HudAction::Fast && tickIntervalMs == 120)
      || (action == HudAction::ToggleDisasters && liveState.enableDisasters);
    drawFilledRect(renderer, rect.x, rect.y, rect.w, rect.h,
                   active ? RGB{58, 92, 72} : (hovered ? RGB{55, 62, 75} : RGB{38, 42, 50}), 255);
    drawRectOutline(renderer, rect.x, rect.y, rect.w, rect.h,
                    active ? RGB{255, 225, 110}
                           : (hovered ? RGB{185, 205, 235} : RGB{125, 135, 150}), 255);
    drawText(renderer, rect.x + 7, rect.y + 8, label, {235, 238, 242}, 1);
  }
}

void drawCompactGameplayHud(DrawList *renderer, int windowWidth, bool paused,
                            uint32_t population, int64_t funds,
                            const std::string &toolLabel,
                            const PopulationChange &change,
                            const FreightSummary &freight,
                            const CityDisasterState* disasters,
                            bool enableDisasters) {
  constexpr int width = 330;
  const bool showDisasters = disasters && (enableDisasters ||
    disasters->buildingsLostToFire > 0 || disasters->buildingsLostToDisaster > 0);
  const int height = showDisasters ? 118 : 82;
  const int x = windowWidth - width - 12;
  constexpr int y = 12;
  drawFilledRect(renderer, x, y, width, height, {14, 17, 21}, 165);
  drawRectOutline(renderer, x, y, width, height, {150, 165, 178}, 210);
  drawText(renderer, x + 10, y + 9, paused ? "PAUSED" : "LIVE",
           paused ? RGB{255, 190, 70} : RGB{90, 220, 125}, 2);
  drawText(renderer, x + 86, y + 9, "POP " + std::to_string(population), {225, 230, 235}, 2);
  drawText(renderer, x + 178, y + 9, "$" + std::to_string(funds), {255, 220, 95}, 2);
  drawText(renderer, x + 10, y + 29, "TOOL " + toolLabel + "   F4 DETAILS", {175, 195, 215}, 1);
  drawText(renderer, x + 10, y + 47,
           "IN " + std::to_string(change.arrivals) + " OUT " +
               std::to_string(change.departures) + " D " +
               std::to_string(change.deaths) + " NET " +
               std::to_string(change.net),
           {175, 205, 235}, 1);
  drawText(renderer, x + 10, y + 65,
           "TRUCKS " + std::to_string(freight.trucks) + " UNROUTED " +
               std::to_string(freight.unroutedGoods),
           {175, 205, 235}, 1);
  if (showDisasters) {
    drawText(renderer, x + 10, y + 83,
             std::string("DISASTERS ") + (enableDisasters ? "ON" : "OFF") +
             " FIRES " + std::to_string(disasters->fire.activeFires), {255, 175, 110}, 1);
    drawText(renderer, x + 10, y + 101,
             "LOST FIRE " + std::to_string(disasters->buildingsLostToFire) +
             " OTHER " + std::to_string(disasters->buildingsLostToDisaster), {255, 175, 110}, 1);
  }
}

void drawToast(
  DrawList* renderer,
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
  DrawList* renderer,
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
  DrawList* renderer,
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

void drawOnboarding(DrawList* renderer, int windowHeight, int step) {
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
      drawText(renderer, x + 12, y + 62, "ZONE HOMES AND JOBS BY ROADS", {175, 185, 200}, 1);
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
      drawText(renderer, x + 12, y + 38, "6 EXPAND YOUR CITY", {235, 238, 242}, 2);
      drawText(renderer, x + 12, y + 62, "ADD SERVICES WHEN BUDGET ALLOWS", {175, 185, 200}, 1);
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

Rect districtPanelRect() {
  return {kDistrictPanelX, kDistrictPanelY, kDistrictPanelWidth, kDistrictPanelHeight};
}

Rect districtPanelButtonRect(DistrictPanelAction action) {
  switch (action) {
    case DistrictPanelAction::CycleArchetype:
      return {kDistrictPanelX + 270, kDistrictPanelY + 24, 106, 22};
    case DistrictPanelAction::ToggleCap:
      return {kDistrictPanelX + 270, kDistrictPanelY + 50, 106, 22};
    default:
      return {0, 0, 0, 0};
  }
}

DistrictPanelAction districtPanelHitTest(int mouseX, int mouseY) {
  const std::array<DistrictPanelAction, 2> actions = {
    DistrictPanelAction::CycleArchetype, DistrictPanelAction::ToggleCap
  };
  for (const DistrictPanelAction action : actions) {
    const Rect rect = districtPanelButtonRect(action);
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      return action;
    }
  }
  return DistrictPanelAction::None;
}

bool pointInDistrictPanel(int mouseX, int mouseY) {
  const Rect rect = districtPanelRect();
  return mouseX >= rect.x && mouseX < rect.x + rect.w
      && mouseY >= rect.y && mouseY < rect.y + rect.h;
}

void drawDistrictPanel(
  DrawList* renderer,
  const District& district,
  int mouseX,
  int mouseY
) {
  const Rect panel = districtPanelRect();
  drawFilledRect(renderer, panel.x, panel.y, panel.w, panel.h, {14, 17, 22}, 200);
  drawRectOutline(renderer, panel.x, panel.y, panel.w, panel.h,
                  districtArchetypeColor(district.archetype), 240);
  drawText(renderer, panel.x + 10, panel.y + 8, "DISTRICT " + district.name,
           {120, 205, 255}, 2);
  drawText(renderer, panel.x + 10, panel.y + 30,
           DistrictSystem::archetypeToString(district.archetype),
           districtArchetypeColor(district.archetype), 1);
  const bool starved = district.serviceBudgetCap >= 0;
  drawText(renderer, panel.x + 10, panel.y + 46,
           starved ? "BUDGET STARVED" : "BUDGET FUNDED",
           starved ? RGB{255, 150, 120} : RGB{105, 230, 135}, 1);
  drawText(renderer, panel.x + 10, panel.y + 60,
           starved ? "GROWTH SLOWED" : "GROWTH NORMAL",
           {175, 185, 200}, 1);

  const std::array<std::pair<DistrictPanelAction, const char*>, 2> buttons = {{
    {DistrictPanelAction::CycleArchetype, "ARCH"},
    {DistrictPanelAction::ToggleCap, starved ? "FUND" : "STARVE"},
  }};
  for (const auto& [action, label] : buttons) {
    const Rect rect = districtPanelButtonRect(action);
    const bool hovered = mouseX >= rect.x && mouseX < rect.x + rect.w
      && mouseY >= rect.y && mouseY < rect.y + rect.h;
    drawFilledRect(renderer, rect.x, rect.y, rect.w, rect.h,
                   hovered ? RGB{55, 62, 75} : RGB{38, 42, 50}, 255);
    drawRectOutline(renderer, rect.x, rect.y, rect.w, rect.h,
                    hovered ? RGB{185, 205, 235} : RGB{125, 135, 150}, 255);
    drawText(renderer, rect.x + 8, rect.y + 7, label, {235, 238, 242}, 1);
  }
}

void drawTileInspector(
  DrawList* renderer,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  Coord coord,
  const DistrictSystem* districts
) {
  if (!map.isValid(coord)) {
    return;
  }
  constexpr int x = 14;
  constexpr int y = 170;
  constexpr int width = 390;
  constexpr int height = 132;
  drawFilledRect(renderer, x, y, width, height, {14, 17, 22}, 185);
  drawRectOutline(renderer, x, y, width, height, {150, 160, 175}, 240);

  const Tile& tile = map.getTile(coord);
  const char* terrain = tile.type == 2 ? "WATER" : (tile.type == 1 ? "ROUGH" : "LAND");
  drawText(renderer, x + 10, y + 9,
           "TILE " + std::to_string(coord.x) + " " + std::to_string(coord.y),
           {120, 205, 255}, 2);
  std::string zoneLine = std::string(terrain) + "  ZONE " + Zoning::zoneToString(map.zone(coord));
  if (districts != nullptr) {
    const District* containing = nullptr;
    for (const District& district : districts->getDistricts()) {
      if (district.contains(coord)) {
        containing = &district;
      }
    }
    if (containing != nullptr) {
      zoneLine += std::string("  DST ") + containing->name;
    }
  }
  drawText(renderer, x + 10, y + 31, zoneLine, {220, 225, 232}, 1);
  drawText(renderer, x + 10, y + 47,
           "VALUE " + std::to_string(static_cast<int>(map.landValue(coord)))
             + "  POLL " + std::to_string(static_cast<int>(map.pollution(coord) * 100.0f)),
           {190, 198, 210}, 1);

  drawText(renderer, x + 10, y + 63,
           std::string("POWER ") + (tile.connectedToPower ? "YES" : "NO")
             + "  WATER " + (tile.connectedToWater ? "YES" : "NO"),
           (tile.connectedToPower && tile.connectedToWater) ? RGB{135, 220, 150} : RGB{255, 150, 120}, 1);
  if (tile.hasRoad) {
    bool arterial = false;
    float freightLoad = 0.0f;
    for (Coord delta : {Coord{1, 0}, Coord{-1, 0}, Coord{0, 1}, Coord{0, -1}}) {
      arterial |=
          roads.hasRoad(coord, Coord{coord.x + delta.x, coord.y + delta.y}) &&
          roads.getRoadClass(coord,
                             Coord{coord.x + delta.x, coord.y + delta.y}) ==
              RoadClass::Arterial;
      freightLoad = std::max(
          freightLoad, roads.getFreightLoad(
                           coord, Coord{coord.x + delta.x, coord.y + delta.y}));
    }
    drawText(renderer, x + 10, y + 111,
             "TRUCK EDGE LOAD " + std::to_string(static_cast<int>(freightLoad)),
             {210, 195, 160}, 1);
    drawText(renderer, x + 10, y + 79,
             std::string(arterial ? "ARTERIAL 40" : "LOCAL 10") + " CONGEST " +
                 std::to_string(static_cast<int>(
                     localCongestionAtTile(roads, coord) * 100.0f)),
             {185, 190, 200}, 1);
  }
  if (tile.buildingId != 0) {
    const Building* building = store.getBuilding(tile.buildingId);
    if (building != nullptr) {
      drawText(renderer, x + 10, y + 95,
               std::string("BLDG ") + buildingTypeLabel(building->type) + " "
                 + std::to_string(building->occupancy) + "/" + std::to_string(building->capacity),
               {210, 225, 210}, 1);
      if (building->type == BuildingType::Residential) {
        drawText(
            renderer, x + 10, y + 111,
            "CRIME " +
                std::to_string(static_cast<int>(building->crimeRate * 100.0f)) +
                "%  ILL " +
                std::to_string(
                    static_cast<int>(building->illnessRate * 100.0f)) +
                "%",
            {235, 180, 145}, 1);
      }
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


Rect quitButtonRect(QuitAction action, int windowWidth, int windowHeight) {
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
    const Rect rect = quitButtonRect(action, windowWidth, windowHeight);
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      return action;
    }
  }
  return QuitAction::None;
}

void drawQuitDialog(
  DrawList* renderer,
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
    const Rect rect = quitButtonRect(action, windowWidth, windowHeight);
    const bool hovered = mouseX >= rect.x && mouseX < rect.x + rect.w
      && mouseY >= rect.y && mouseY < rect.y + rect.h;
    drawFilledRect(renderer, rect.x, rect.y, rect.w, rect.h,
                   hovered ? RGB{55, 62, 75} : RGB{36, 40, 48}, 255);
    drawRectOutline(renderer, rect.x, rect.y, rect.w, rect.h,
                    hovered ? RGB{205, 220, 245} : RGB{125, 135, 150}, 255);
    drawText(renderer, rect.x + 12, rect.y + 15, label, {235, 238, 242}, 1);
  }
}

void drawSwatch(DrawList* renderer, int x, int y, RGB color, bool active) {
  drawFilledRect(renderer, x, y, 18, 12, color, 255);
  drawRectOutline(renderer, x, y, 18, 12, active ? RGB{255, 230, 120} : RGB{220, 220, 220}, 255);
}

void drawZoneLegend(DrawList* renderer, int x, int y) {
  // Categories shown in zone overlay rendering path.
  drawText(renderer, x, y - 12, "ZONE", {230, 230, 230}, 2);
  drawSwatch(renderer, x + 0, y, zoneColor(1));       // Residential
  drawSwatch(renderer, x + 36, y, zoneColor(2));      // Commercial
  drawSwatch(renderer, x + 72, y, zoneColor(3));      // Industrial
  drawSwatch(renderer, x + 108, y, zoneColor(5));     // Office
  drawSwatch(renderer, x + 180, y, {64, 64, 64});     // Road
  drawText(renderer, x + 0, y + 16, "RES", {205, 220, 205}, 1);
  drawText(renderer, x + 36, y + 16, "COM", {205, 220, 205}, 1);
  drawText(renderer, x + 72, y + 16, "IND", {205, 220, 205}, 1);
  drawText(renderer, x + 108, y + 16, "OFF", {205, 220, 205}, 1);
  drawText(renderer, x + 180, y + 16, "ROAD", {205, 220, 205}, 1);

  drawSwatch(renderer, x + 144, y, zoneColor(4));
  drawText(renderer, x + 144, y + 16, "PARK", {205, 220, 205}, 1);

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

void drawSteppedLegend(DrawList* renderer, int x, int y, OverlayMode mode) {
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
  DrawList* renderer,
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
  drawFilledRect(renderer, panelX, panelY, panelW, panelH, {14, 16, 20}, 155);
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

  if (overlayMode == OverlayMode::Transit) {
    drawText(renderer, panelX + 12, panelY + 55, "CYAN BUS   GOLD RAIL   WHITE STOPS", {230, 240, 245}, 1);
    drawText(renderer, panelX + 12, panelY + 85, "LINES FOLLOW CONNECTED ROADS", {205, 220, 225}, 1);
    drawText(renderer, panelX + 12, panelY + 112, "ROUTES APPEAR AS YOUR CITY GROWS", {205, 220, 225}, 1);
    return;
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
  bool districtToolActive,
  DistrictArchetype selectedArchetype,
  const DistrictPlan& districtPlan,
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
  } else if (districtToolActive) {
    oss << "DISTRICT-" << DistrictSystem::archetypeToString(selectedArchetype);
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
  } else if (!districtPlan.tiles.empty()) {
    oss << " (" << (districtPlan.valid
                      ? (districtPlan.removeId != 0 ? "remove" : "ok")
                      : districtPlan.error) << ")";
  }
  oss << " [R road | Z zone | B bulldoze | S service | P district]";
  return oss.str();
}


}  // namespace visualizer
