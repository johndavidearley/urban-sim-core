#pragma once
#include "src/visualization/ManagementUi.hpp"
#include "src/gameplay/BuildingDiagnostics.hpp"

#include "src/visualization/VisualizerTypes.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/gameplay/DistrictTool.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/entities/PopulationStore.hpp"

namespace visualizer {

constexpr int kHudPanelWidth = 390;
constexpr int kHudPanelHeight = 370;
constexpr int kDistrictPanelX = 14;
constexpr int kDistrictPanelY = 308;
constexpr int kDistrictPanelWidth = 390;
constexpr int kDistrictPanelHeight = 106;

enum class DistrictPanelAction {
  None,
  CycleArchetype,
  ToggleCap,
  CycleHeight,
};

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
);

Rect budgetPanelRect(int windowWidth, int windowHeight);
// Bond tab: 45 tab, 46-48 principal choice, 49-51 term choice, 52 issue, 53 pay off.
constexpr int kBudgetControlCount = 54;
struct BudgetBondView {
  bool open = false;
  int principalChoice = 0;
  int termChoice = 0;
  MunicipalBond bond;
  int64_t funds = 0;
  BondPayment nextDue;
};
// 0 close, 1 reset, then decrease/increase pairs for the five taxes.
bool budgetControlVisible(int action, bool services, bool trends, bool alerts = false, const BudgetBondView* bonds = nullptr);
bool budgetControlEnabled(int action,const BudgetPolicy& policy,bool services,bool trends,bool alerts, const BudgetBondView* bonds = nullptr);
Rect budgetControlRect(int action, int windowWidth, int windowHeight, bool services = false, bool trends = false, bool alerts = false);
int budgetHitTest(int x, int y, int windowWidth, int windowHeight, bool services = false, bool trends = false, bool alerts = false, const BudgetBondView* bonds = nullptr);
void drawBudgetPanel(DrawList* renderer, int windowWidth, int windowHeight,
  const BudgetPolicy& policy, const TreasuryBreakdown& preview,
  const TreasuryLedger& ledger, int64_t funds, int64_t debt, int mouseX, int mouseY,
  bool services = false, const std::array<int64_t, kServiceTypeCount>& serviceCosts = {},
  const std::array<float, kServiceTypeCount>& serviceCoverage = {},
  bool trends = false, const CityHistory& history = {},
  bool alerts = false, const CityAlerts& diagnostics = {}, int focus = -1, uint32_t inspectedTick = 0, const std::vector<ServiceFacility>* facilities = nullptr,
  const BudgetBondView* bonds = nullptr);

bool transportControlEnabled(int action,const std::vector<TransitRoute>& routes,size_t selected);
Rect transportControlRect(int action,int width,int height);
int transportHitTest(int x,int y,int width,int height);
void drawTransportPanel(DrawList* renderer,int width,int height,const std::vector<TransitRoute>& routes,
  size_t selected,const TransitReport& report,float peakCongestion,int linkedStops = -1,int focus = -1);
Rect hudControlRect(HudAction action, int windowWidth);
HudAction hudHitTest(int mouseX, int mouseY, int windowWidth);
void drawDemandBar(DrawList* renderer, int x, int y, float demand, RGB color, const char* label);

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
);

void drawCompactGameplayHud(DrawList *renderer, int windowWidth, bool paused,
                            uint32_t population, int64_t funds,
                            const std::string &toolLabel,
                            const PopulationChange &change = {},
                            const FreightSummary &freight = {},
                            const CityDisasterState* disasters = nullptr,
                            bool enableDisasters = false,
                            const ServiceCoverageSummary* civic = nullptr);

void drawToast(
  DrawList* renderer,
  int windowWidth,
  const ToastNotification& toast,
  uint32_t nowMs
);

void drawPlacementWarning(
  DrawList* renderer,
  int mouseX,
  int mouseY,
  int windowWidth,
  int windowHeight,
  const std::string& warning
);

void drawUiTooltip(
  DrawList* renderer,
  int mouseX,
  int mouseY,
  int windowWidth,
  int windowHeight,
  const std::string& message
);

void drawOnboarding(DrawList* renderer, int windowHeight, int step);

Rect districtPanelRect();
Rect districtPanelButtonRect(DistrictPanelAction action);
DistrictPanelAction districtPanelHitTest(int mouseX, int mouseY);
bool pointInDistrictPanel(int mouseX, int mouseY);
void drawDistrictPanel(
  DrawList* renderer,
  const District& district,
  int mouseX,
  int mouseY
);

void drawTileInspector(
  DrawList* renderer,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  Coord coord,
  const DistrictSystem* districts = nullptr,
  const BuildingDiagnostics* diagnostics = nullptr, float cityJoblessness = 0.f, bool pinned = false,
  const WasteSummary* waste = nullptr,
  const ServiceCoverageSummary* civic = nullptr
);

Rect quitButtonRect(QuitAction action, int windowWidth, int windowHeight);
QuitAction quitHitTest(int mouseX, int mouseY, int windowWidth, int windowHeight);

void drawQuitDialog(
  DrawList* renderer,
  int windowWidth,
  int windowHeight,
  int mouseX,
  int mouseY,
  bool dirty
);

void drawSwatch(DrawList* renderer, int x, int y, RGB color, bool active = false);
void drawZoneLegend(DrawList* renderer, int x, int y);
void drawSteppedLegend(DrawList* renderer, int x, int y, OverlayMode mode);

void drawLegendPanel(
  DrawList* renderer,
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
  bool districtToolActive,
  DistrictArchetype selectedArchetype,
  const DistrictPlan& districtPlan,
  int64_t funds
);

}  // namespace visualizer
