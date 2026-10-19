#include "src/gameplay/CatalogUnlocks.hpp"
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
#include "src/visualization/RegionalManagement.hpp"
#include "src/gameplay/TerminalTool.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/visualization/VisualizerRender.hpp"


namespace visualizer {
namespace {
std::string schoolCapacityLine(const ServiceCoverageSummary& s) {
  return "SEATS " + std::to_string(std::lround(s.schoolSeatsUsed)) + "/" +
    std::to_string(std::lround(s.schoolSeats)) +
    (s.effectiveEducationCoverage < s.educationCoverage ? " CAP" : " REACH");
}
std::string hospitalCapacityLine(const ServiceCoverageSummary& s) {
  return "BEDS " + std::to_string(std::lround(s.hospitalBedsUsed)) + "/" +
    std::to_string(std::lround(s.hospitalBeds)) + " NEED " + std::to_string(std::lround(s.patientDemand)) +
    (s.healthCoverage > 0.f && s.hospitalCapacityFactor < 1.f ? " CAP" : " REACH");
}
}


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




Rect budgetPanelRect(int windowWidth, int windowHeight) {
  return {(windowWidth - 720) / 2, (windowHeight - 540) / 2, 720, 540};
}
bool budgetControlVisible(int action, bool services, bool trends, bool alerts, const BudgetBondView* bonds) {
  const bool bondTab = bonds && bonds->open && !services && !trends && !alerts;
  if (action == 0 || action == 12 || action == 13 || action == 36 || action == 37 || action == 45) return true;
  if (action >= 46 && action < kBudgetControlCount) return bondTab;
  if (action < 0 || action >= kBudgetControlCount || bondTab) return false;
  if (action == 43 || action == 44) return services && !trends && !alerts;
  if (action >= 38) return alerts;
  if (alerts) return false;
  if (trends) return false;
  return !((action >= 2 && action < 12 && services) || (action >= 14 && !services));
}
bool budgetControlEnabled(int action,const BudgetPolicy& policy,bool services,bool trends,bool alerts,const BudgetBondView* bonds) {
  if (action<0 || action>=kBudgetControlCount || !budgetControlVisible(action,services,trends,alerts,bonds)) return false;
  if (action == 52 && !bonds->unlocked) return false;
  if (action>=46 && action<=52) return !bonds->bond.active();
  if (action==53) return bonds->bond.active() && bonds->funds>=TreasurySystem::bondRemaining(bonds->bond);
  if (action>=2 && action<12) {const int v=policy.taxPercent[(action-2)/2];return action%2 ? v<20 : v>0;}
  if (action==43 || action==44) {const int v=policy.servicePercent[11];return action==44 ? v<100 : v>0;}
  if (action>=14 && action<36) {const int v=policy.servicePercent[(action-14)/2];return action%2 ? v<100 : v>0;}
  return true;
}
Rect budgetControlRect(int action, int windowWidth, int windowHeight, bool services, bool, bool) {
  const auto panel = budgetPanelRect(windowWidth, windowHeight);
  if (action == 43 || action == 44) return {panel.x + 220 + (action - 43) * 40, panel.y + 130 + 11 * 18, 30, 16};
  if (action == 0) return {panel.x + 664, panel.y + 12, 40, 26};
  if (action == 45) return {panel.x + 344, panel.y + 76, 76, 24};
  if (action >= 46 && action < 49) return {panel.x + 24 + (action - 46) * 108, panel.y + 150, 100, 24};
  if (action >= 49 && action < 52) return {panel.x + 24 + (action - 49) * 108, panel.y + 214, 100, 24};
  if (action == 52 || action == 53) return {panel.x + 24 + (action - 52) * 172, panel.y + 270, 160, 26};
  if (action == 1) return {panel.x + 24, panel.y + (services ? 352 : 278), 240, 24};
  if (action >= 38 && action < 43) return {panel.x + 552, panel.y + 116 + (action - 38) * 54, 144, 24};
  if (action == 37) return {panel.x + 264, panel.y + 76, 76, 24};
  if (action == 36) return {panel.x + 184, panel.y + 76, 76, 24};
  if (action == 12 || action == 13) return {panel.x + 24 + (action - 12) * 80, panel.y + 76, 76, 24};
  if (action >= 14 && action < 36)
    return {panel.x + 220 + ((action - 14) % 2) * 40,
            panel.y + 130 + ((action - 14) / 2) * 18, 30, 16};
  if (action >= 2 && action < 12)
    return {panel.x + 220 + ((action - 2) % 2) * 40,
            panel.y + 112 + ((action - 2) / 2) * 30, 30, 26};
  return {};
}
int budgetHitTest(int x, int y, int windowWidth, int windowHeight, bool services, bool trends, bool alerts, const BudgetBondView* bonds) {
  for (int action = 0; action < kBudgetControlCount; ++action) {
    if (!budgetControlVisible(action, services, trends, alerts, bonds)) continue;
    const auto r = budgetControlRect(action, windowWidth, windowHeight, services);
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return action;
  }
  return -1;
}
void drawBudgetPanel(DrawList* renderer, int windowWidth, int windowHeight,
  const BudgetPolicy& policy, const TreasuryBreakdown& preview,
  const TreasuryLedger& ledger, int64_t funds, int64_t debt, int mouseX, int mouseY,
  bool services, const std::array<int64_t, kServiceTypeCount>& serviceCosts,
  const std::array<float, kServiceTypeCount>& serviceCoverage,
  bool trends, const CityHistory& history, bool alerts, const CityAlerts& diagnostics,int focus,uint32_t inspectedTick,const std::vector<ServiceFacility>* facilities,const BudgetBondView* bonds,const ServiceCoverageSummary* departments) {
  const bool bondTab = bonds && bonds->open && !services && !trends && !alerts;
  const bool taxTab = !services && !trends && !alerts && !bondTab;
  drawFilledRect(renderer, 0, 0, windowWidth, windowHeight, {5, 10, 18}, 195);
  const auto panel = budgetPanelRect(windowWidth, windowHeight);
  const int x = panel.x, y = panel.y;
  drawFilledRect(renderer, x, y, panel.w, panel.h, {22, 28, 37}, 255);
  drawRectOutline(renderer, x, y, panel.w, panel.h, {125, 170, 200});
  drawText(renderer, x + 24, y + 18, "CITY BUDGET", {240, 245, 250}, 2);
  drawText(renderer, x + 24, y + 50,
    "CASH $" + std::to_string(funds) + "   DEBT $" + std::to_string(debt), {255, 220, 130}, 1);
  drawText(renderer,x+360,y+50,"TAB/UP/DOWN FOCUS. ENTER SELECT.",{165,185,205},1);
  const std::array<const char*, 5> taxNames{{"RESIDENTIAL", "COMMERCIAL", "INDUSTRIAL", "OFFICE", "INCOME"}};
  if (taxTab) for (size_t i = 0; i < taxNames.size(); ++i) {
    drawText(renderer, x + 24, y + 121 + static_cast<int>(i) * 30, taxNames[i], {205, 220, 235}, 1);
    drawText(renderer, x + 156, y + 118 + static_cast<int>(i) * 30,
             std::to_string(policy.taxPercent[i]) + "%", {240, 245, 250}, 2);
  }
  if (taxTab) {
    const auto attraction = policy.taxAttraction();
    const char* names[] = {"R", "C", "I", "O"};
    for (size_t i = 0; i < attraction.size(); ++i) {
      const int percent = static_cast<int>(std::lround(attraction[i] * 100.f));
      drawText(renderer, x + 24 + static_cast<int>(i) * 78, y + 262,
        std::string(names[i]) + " " + std::to_string(percent) + "%", {135, 215, 240}, 1);
    }
    drawText(renderer, x + 24, y + 306, "ATTRACTION VS DEFAULT TAXES", {165, 185, 205}, 1);
  }
  if (services && !trends && !alerts) {
    drawText(renderer, x + 24, y + 112, "SERVICE", {135, 215, 240}, 1);
    drawText(renderer, x + 156, y + 112, "FUNDING", {135, 215, 240}, 1);
    drawText(renderer, x + 360, y + 112, "COST/TICK", {135, 215, 240}, 1);
    drawText(renderer, x + 490, y + 112, "COV SERVED/UNSERVED", {135, 215, 240}, 1);
    for (size_t i = 0; i < kServiceTypeCount; ++i) {
      const int row = y + 133 + static_cast<int>(i) * 18;
      drawText(renderer, x + 24, row, ServiceSystem::serviceTypeToString(static_cast<ServiceType>(i)), {205, 220, 235}, 1);
      drawText(renderer, x + 156, row, std::to_string(policy.servicePercent[i]) + "%", {235, 245, 250}, 1);
      drawText(renderer, x + 360, row, "$" + std::to_string(serviceCosts[i]), {255, 195, 135}, 1);
      std::string coverage = std::to_string(static_cast<int>(serviceCoverage[i] * 100.f)) + "%";
      const int index = ServiceSystem::departmentIndex(static_cast<ServiceType>(i));
      if (departments && departments->boundedDepartments && index >= 0) {
        const auto& capacity = departments->departments[static_cast<size_t>(index)];
        coverage += " " + std::to_string(std::llround(capacity.served)) + "/" + std::to_string(std::llround(capacity.unserved));
      }
      drawText(renderer, x + 490, row, coverage, {135, 225, 175}, 1);
    }
    if (facilities) {
      int raw = 0, treated = 0, inland = 0;
      for (const auto& facility : *facilities) if (facility.type == ServiceType::Sanitation) {
        if (facility.sewageTreatment) ++treated;
        else if (facility.surfaceWater) ++raw;
        else ++inland;
      }
      drawText(renderer, x + 360, y + 370, "SEWAGE RAW " + std::to_string(raw) + " TREATED " +
        std::to_string(treated) + " INLAND " + std::to_string(inland), {160,220,210}, 1);
    }
    drawText(renderer, x + 360, y + 344, "LANDFILL FREE " + std::to_string(diagnostics.inputs.landfillRemaining)
      + "  LAST FILL " + std::to_string(diagnostics.inputs.landfillFillRate) + "/TICK", {210,210,160}, 1);
    drawText(renderer, x + 360, y + 357, "CIVIC/TRANSPORT $" + std::to_string(preview.expenses[3]) + "/TICK", {255, 220, 130}, 1);
  }
  for (int action = 0; action < kBudgetControlCount; ++action) {
    if (!budgetControlVisible(action, services, trends, alerts, bonds)) continue;
    const auto r = budgetControlRect(action, windowWidth, windowHeight, services);
    const bool hovered = mouseX >= r.x && mouseX < r.x + r.w && mouseY >= r.y && mouseY < r.y + r.h;
    bool disabled = false;
    if (action >= 2 && action < 12) {
      const int rate = policy.taxPercent[static_cast<size_t>((action - 2) / 2)];
      disabled = (action % 2 == 0 && rate == 0) || (action % 2 == 1 && rate == 20);
    }
    if (action == 43 || action == 44 || action >= 46) disabled = !budgetControlEnabled(action, policy, services, trends, alerts, bonds);
    if (action >= 14 && action < 36) {
      const int percent = policy.servicePercent[static_cast<size_t>((action - 14) / 2)];
      disabled = (action % 2 == 0 && percent == 0) || (action % 2 == 1 && percent == 100);
    }
    drawFilledRect(renderer, r.x, r.y, r.w, r.h,
                   disabled ? RGB{28, 32, 40} : (hovered ? RGB{65, 90, 115} : RGB{42, 58, 75}));
    if (action==focus) drawRectOutline(renderer,r.x-2,r.y-2,r.w+4,r.h+4,{235,245,255});
    const bool activeTab = (action == 12 && taxTab) || (action == 45 && bondTab) || (action == 13 && services && !trends && !alerts) || (action == 36 && trends && !alerts) || (action == 37 && alerts);
    drawRectOutline(renderer, r.x, r.y, r.w, r.h, activeTab ? RGB{255, 225, 110} :
                    (disabled ? RGB{65, 70, 80} : RGB{135, 165, 190}));
    const bool chosen = bondTab && ((action >= 46 && action < 49 && action - 46 == bonds->principalChoice) ||
      (action >= 49 && action < 52 && action - 49 == bonds->termChoice));
    if (chosen) drawRectOutline(renderer, r.x, r.y, r.w, r.h, {255, 225, 110});
    static const char* bondLabels[] = {"BONDS", "$10K", "$50K", "$200K", "100 TICKS", "200 TICKS", "400 TICKS", "ISSUE BOND", "PAY OFF BOND"};
    const char* label = action >= 45 ? bondLabels[action - 45] : action == 0 ? "X" : (action == 1 ? (services ? "RESTORE FULL FUNDING" : "RESET DEFAULT TAXES") :
      (action == 12 ? "TAXES" : (action == 13 ? "SERVICES" : (action == 36 ? "TRENDS" : (action == 37 ? "ALERTS" : (action == 43 ? "-" : (action == 44 ? "+" : (action >= 38 ? "VIEW" : (action % 2 == 0 ? "-" : "+")))))))));
    drawText(renderer, r.x + 10, r.y + (((action >= 14 && action < 36) || action == 43 || action == 44) ? 3 : 7), label, disabled ? RGB{95, 100, 110} : RGB{235, 245, 250}, 1);
  }
  if (alerts) {
    const auto& v = diagnostics.inputs;
    auto percent = [](float value) { return std::to_string(static_cast<int>(std::lround(value * 100.f))) + "%"; };
    for (size_t i = 0; i < diagnostics.records.size(); ++i) {
      const auto& r = diagnostics.records[i];
      const int row = y + 118 + static_cast<int>(i) * 54;
      const std::string state = !diagnostics.checkedTick ? "UNCHECKED" : (r.active ? "ACTIVE" : (r.badTicks ? "WATCHING" : "CLEAR"));
      drawText(renderer, x + 24, row, std::string(CityAlerts::name(i)) + " - " + state,
        r.active ? RGB{255, 180, 110} : RGB{165, 205, 190}, 1);
      std::string measured, advice;
      if (i < 2) {
        const float coverage = i == 0 ? v.powerCoverage : v.waterCoverage;
        const float supply = i == 0 ? v.powerSupply : v.waterSupply;
        measured = "ROAD COVERAGE " + percent(coverage) + "  SUPPLY " + percent(supply);
        advice = supply < .95f ? "CHECK CAPACITY AND DEPARTMENT FUNDING." : "CHECK ROAD LINKS AND FACILITY REACH.";
        if (!v.requireUtilities) advice = "UTILITY REQUIREMENT DISABLED.";
      } else if (i == 2) {
        measured = percent(v.unemployment) + " OF RESIDENTS NOT EMPLOYED.";
        advice = "CHECK JOB ZONES, EDUCATION AND UTILITY ACCESS.";
      } else if (i == 3) {
        measured = "LOWEST FIRE/POLICE/HEALTH/EDU COVERAGE " + percent(v.serviceCoverage);
        advice = "CHECK FACILITY REACH, ROAD LINKS AND FUNDING.";
      } else if (i == 5) {
        measured = (v.fullLandfills ? "FULL SITE " + std::to_string(v.landfillX) + "," + std::to_string(v.landfillY)
          : std::string("NO FULL SITES")) + "  FREE " + std::to_string(v.landfillRemaining) + " FILL " + std::to_string(v.landfillFillRate) + "/TICK";
        advice = "ADD LANDFILL, RECYCLING OR INCINERATION.";
      } else {
        measured = "REVENUE $" + std::to_string(v.revenue) + "  EXPENSES $" + std::to_string(v.expenses);
        advice = "REVIEW TAXES, FUNDING AND CONSTRUCTION SPENDING.";
      }
      if (!diagnostics.checkedTick) {
        measured = "NO COMPLETED TICK MEASUREMENT.";
        advice = "RUN A SIMULATION TICK TO UPDATE DIAGNOSTICS.";
      }
      drawText(renderer, x + 24, row + 19, measured, {205, 220, 235}, 1);
      drawText(renderer, x + 24, row + 35, advice, {165, 185, 205}, 1);
    }
    drawText(renderer, x + 24, y + 450, diagnostics.checkedTick ? "LAST CHECKED TICK " + std::to_string(diagnostics.checkedTick) : "NO COMPLETED TICK CHECKED YET", {135, 215, 240}, 1);
    drawText(renderer, x + 24, y + 470, "ALERTS NEED 3 BAD TICKS. RECOVERY NEEDS 2 HEALTHY TICKS.", {165, 185, 205}, 1);
    drawText(renderer, x + 24, y + 490, "REPEAT NOTICES WAIT 60 TICKS. PAUSED EDITS WAIT FOR A TICK.", {165, 185, 205}, 1);
    drawText(renderer, x + 24, y + 512, "VIEW: OVERLAY/BUDGET. U: REGIONAL AND TOURISM ALERTS.", {165, 185, 205}, 1);
    return;
  }
  if (trends) {
    if (history.samples.empty()) {
      drawText(renderer, x + 24, y + 140, "NO COMPLETED TICKS IN HISTORY", {185, 205, 220}, 1);
      drawText(renderer, x + 24, y + 164, "HISTORY STARTS WITH THE NEXT SIMULATION TICK.", {165, 185, 205}, 1);
      return;
    }
    const size_t selected=trendSampleIndex(history,inspectedTick);
    const auto& latest = history.samples[selected];
    const char* names[] = {"POPULATION", "NET POP CHANGE", "REVENUE", "EXPENSES", "CASH", "DEBT"};
    auto value = [](const CityHistorySample& s, int chart) -> int64_t {
      switch (chart) {
        case 0: return s.population;
        case 1: return s.migration.net;
        case 2: return s.revenue;
        case 3: return s.expenses;
        case 4: return s.funds;
        default: return s.debt;
      }
    };
    for (int chart = 0; chart < 6; ++chart) {
      const int cx = x + 24 + (chart % 2) * 340;
      const int cy = y + 116 + (chart / 2) * 110;
      int64_t lo = 0, hi = 0;
      for (const auto& sample : history.samples) {
        lo = std::min(lo, value(sample, chart));
        hi = std::max(hi, value(sample, chart));
      }
      drawText(renderer, cx, cy, std::string(names[chart]) + " " + std::to_string(value(latest, chart)), {220, 235, 245}, 1);
      drawText(renderer, cx, cy + 16, std::to_string(lo) + " TO " + std::to_string(hi), {145, 165, 185}, 1);
      const int top = cy + 32, bottom = cy + 92, width = 308;
      drawRectOutline(renderer, cx, top, width + 1, bottom - top + 1, {65, 85, 100});
      auto px = [&](size_t i) { return cx + static_cast<int>(i * width / std::max(size_t{1}, history.samples.size() - 1)); };
      auto py = [&](int64_t v) { return bottom - static_cast<int>(
        (static_cast<double>(v) - static_cast<double>(lo)) /
        std::max(1.0, static_cast<double>(hi) - static_cast<double>(lo)) * (bottom - top)); };
      setDrawColor(renderer, 65, 85, 100, 255);
      drawLine(renderer, cx, py(0), cx + width, py(0));
      for (size_t i = 0; i < history.samples.size(); ++i) {
        if (history.policyChanged(i)) {
          setDrawColor(renderer, 230, 190, 80, 255);
          drawLine(renderer, px(i), top, px(i), bottom);
        }
      }
      setDrawColor(renderer, 120, 215, 240, 255);
      for (size_t i = 1; i < history.samples.size(); ++i)
        drawLine(renderer, px(i - 1), py(value(history.samples[i - 1], chart)), px(i), py(value(history.samples[i], chart)));
      setDrawColor(renderer,235,245,255,255);
      drawLine(renderer,px(selected),top,px(selected),bottom);
      drawFilledRect(renderer, px(selected) - 1, py(value(latest, chart)) - 1, 3, 3, {235,245,255});
    }
    drawText(renderer,x+24,y+450,"TICKS " + std::to_string(history.samples.front().tick)+" TO "+std::to_string(history.samples.back().tick)+
      "  INSPECT "+std::to_string(latest.tick)+" IN "+std::to_string(latest.migration.arrivals)+
      " OUT "+std::to_string(latest.migration.departures)+" DEATHS "+std::to_string(latest.migration.deaths),{135,215,240},1);
    drawText(renderer,x+24,y+468,trendTaxDetail(history,selected),{230,195,105},1);
    drawText(renderer,x+24,y+486,trendFundingDetail(history,selected),{230,195,105},1);
    drawText(renderer,x+24,y+512,"HOVER/ARROWS: INSPECT. HOME/END: FIRST/LATEST. NET INCLUDES DEATHS.",{165,185,205},1);
    return;
  }
  if (taxTab) {
  drawText(renderer, x + 24, y + 324, "TAXES AFFECT GROWTH AND MIGRATION.", {165, 185, 205}, 1);
  drawText(renderer, x + 24, y + 342, "DEFAULT RATES ARE NEUTRAL.", {165, 185, 205}, 1);
  drawText(renderer, x + 436, y + 76, "NEXT TICK ESTIMATE", {135, 215, 240}, 1);
  const std::array<const char*, 6> revenueNames{{"RES TAX", "COM TAX", "IND TAX", "OFF TAX", "INCOME TAX", "EXPORTS"}};
  for (size_t i = 0; i < revenueNames.size(); ++i) {
    const int row = y + 102 + static_cast<int>(i) * 16;
    drawText(renderer, x + 360, row, revenueNames[i], {185, 205, 220}, 1);
    drawText(renderer, x + 580, row, "+$" + std::to_string(preview.revenue[i]), {120, 225, 155}, 1);
  }
  const std::array<const char*, 5> costNames{{"BUILDING UPKEEP", "PARK UPKEEP", "IMPORTS", "CIVIC/TRANSPORT", "BUILDING GROWTH"}};
  for (size_t i = 0; i < costNames.size(); ++i) {
    const int row = y + 208 + static_cast<int>(i) * 16;
    drawText(renderer, x + 360, row, costNames[i], {185, 205, 220}, 1);
    drawText(renderer, x + 580, row, "-$" + std::to_string(preview.expenses[i]), {250, 170, 125}, 1);
  }
  const int64_t bondDue = bonds ? bonds->nextDue.installment + bonds->nextDue.interest : 0;
  drawText(renderer, x + 360, y + 288, "BOND PAYMENT", {185, 205, 220}, 1);
  drawText(renderer, x + 580, y + 288, "-$" + std::to_string(bondDue), {250, 170, 125}, 1);
  drawText(renderer, x + 360, y + 306, "IN $" + std::to_string(preview.totalRevenue) +
           " OUT $" + std::to_string(preview.totalExpenses + bondDue), {235, 240, 250}, 1);
  drawText(renderer, x + 360, y + 324, "SURPLUS $" + std::to_string(preview.totalRevenue - preview.totalExpenses - bondDue), {255, 220, 130}, 1);
  drawText(renderer, x + 360, y + 344, "EXCLUDES GROWTH, VISITORS, OVERDRAFT", {165, 185, 205}, 1);
  }
  if (bondTab) {
    const auto& bond = bonds->bond;
    drawText(renderer, x + 24, y + 128, "PRINCIPAL", {135, 215, 240}, 1);
    drawText(renderer, x + 24, y + 192, "TERM", {135, 215, 240}, 1);
    drawText(renderer, x + 24, y + 316, "ONE BOND AT A TIME. EQUAL PRINCIPAL PARTS.", {165, 185, 205}, 1);
    drawText(renderer, x + 24, y + 334, "INTEREST $15 PER $10000 REMAINING PER TICK", {165, 185, 205}, 1);
    drawText(renderer, x + 360, y + 128, !bonds->unlocked ? "UNLOCKS AT 1200 RESIDENTS" : bond.active() ? "OPEN BOND" : "NO OPEN BOND", {135, 215, 240}, 1);
    if (bond.active()) {
      drawText(renderer, x + 360, y + 150, "PRINCIPAL $" + std::to_string(bond.principal), {215, 230, 240}, 1);
      drawText(renderer, x + 360, y + 168, "TICK " + std::to_string(bond.ticksElapsed) + " OF " + std::to_string(bond.term), {215, 230, 240}, 1);
      drawText(renderer, x + 360, y + 186, "REMAINING $" + std::to_string(TreasurySystem::bondRemaining(bond)), {255, 220, 130}, 1);
      drawText(renderer, x + 360, y + 210, "NEXT INSTALLMENT $" + std::to_string(bonds->nextDue.installment), {250, 170, 125}, 1);
      drawText(renderer, x + 360, y + 228, "NEXT INTEREST $" + std::to_string(bonds->nextDue.interest), {250, 170, 125}, 1);
      drawText(renderer, x + 360, y + 252, "PAY OFF NEEDS $" + std::to_string(TreasurySystem::bondRemaining(bond)) + " CASH", {165, 185, 205}, 1);
    } else {
      drawText(renderer, x + 360, y + 150, "ISSUING ADDS THE PRINCIPAL TO CASH.", {165, 185, 205}, 1);
      drawText(renderer, x + 360, y + 168, "INSTALLMENTS COME BEFORE EXPENSES.", {165, 185, 205}, 1);
      drawText(renderer, x + 360, y + 186, "UNPAID PARTS JOIN THE OVERDRAFT.", {165, 185, 205}, 1);
    }
  }
  drawFilledRect(renderer, x + 24, y + (services ? 382 : 368), panel.w - 48, 1, {85, 105, 125});
  drawText(renderer, x + 24, y + 512, services ? "CUTS REDUCE REACH AND CAPACITY. ZERO SUSPENDS OPERATION." :
    (bondTab ? "ISSUE AND PAY OFF ARE OUTSIDE TICK TOTALS." : "TOOL PURCHASES ARE PAID OUTSIDE TICK TOTALS."), {165, 185, 205}, 1);
  if (!ledger.valid) {
    drawText(renderer, x + 24, y + 392, "NO COMPLETED TICK RECORDED YET", {185, 205, 220}, 1);
    return;
  }
  drawText(renderer, x + 24, y + 386, "LAST COMPLETED TICK " + std::to_string(ledger.tick), {135, 215, 240}, 1);
  drawText(renderer, x + 24, y + 410, "IN $" + std::to_string(ledger.flow.revenue) +
    " OUT $" + std::to_string(ledger.flow.expenses) + " GROWTH $" +
    std::to_string(ledger.breakdown.expenses[4]), {215, 230, 240}, 1);
  drawText(renderer, x + 24, y + 432, "BORROWED $" + std::to_string(ledger.flow.debtIssued) +
    " REPAID $" + std::to_string(ledger.flow.debtRepaid) + " INTEREST $" +
    std::to_string(ledger.flow.interestCharged), {255, 195, 135}, 1);
  drawText(renderer, x + 24, y + 454, "CASH $" + std::to_string(ledger.fundsBefore) +
    " TO $" + std::to_string(ledger.fundsAfter), {230, 240, 245}, 1);
  drawText(renderer, x + 24, y + 476, "DEBT $" + std::to_string(ledger.debtBefore) +
    " TO $" + std::to_string(ledger.flow.outstandingDebt), {230, 240, 245}, 1);
  if (ledger.bond.any()) {
    drawText(renderer, x + 360, y + 454, "BOND PAID $" + std::to_string(ledger.bond.paid) +
      " MISSED $" + std::to_string(ledger.bond.missed), {255, 195, 135}, 1);
    drawText(renderer, x + 360, y + 476, "BOND LEFT $" + std::to_string(ledger.bond.remainingBefore) +
      " TO $" + std::to_string(ledger.bond.remainingAfter), {230, 240, 245}, 1);
  }
  drawText(renderer,x+24,y+494,"VISITOR FEES $"+std::to_string(ledger.breakdown.tourismFees)+
    " SERVICES $"+std::to_string(ledger.breakdown.tourismServices)+" NET $"+
    std::to_string(ledger.breakdown.tourismFees-ledger.breakdown.tourismServices),{150,215,200},1);
}

bool transportControlEnabled(int action,const std::vector<TransitRoute>& routes,size_t selected) {
  if (action<0 || action>=7) return false;
  if (action==0) return true;
  if (routes.empty()) return false;
  if (action==1 || action==2) return routes.size()>1;
  const auto& route=routes[std::min(selected,routes.size()-1)];
  if (action==3) return route.mode==TransitMode::Bus && route.vehicleCount>0;
  if (action==4) return route.mode==TransitMode::Bus && route.vehicleCount<20 &&
    static_cast<int64_t>(route.vehicleCount+1)*route.capacityPerVehicle<=INT32_MAX;
  return !route.stops.empty();
}
Rect transportControlRect(int action,int width,int height) {
  const auto p=budgetPanelRect(width,height);
  switch (action) {
    case 0: return {p.x+664,p.y+12,40,26};
    case 1: return {p.x+24,p.y+64,90,26};
    case 2: return {p.x+126,p.y+64,90,26};
    case 3: return {p.x+220,p.y+142,40,26};
    case 4: return {p.x+270,p.y+142,40,26};
    case 5: return {p.x+24,p.y+438,220,30};
    case 6: return {p.x+260,p.y+438,220,30};
    default: return {};
  }
}
int transportHitTest(int x,int y,int width,int height) {
  for (int i=0;i<7;++i) { const auto r=transportControlRect(i,width,height);
    if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) return i;
  }
  return -1;
}
void drawTransportPanel(DrawList* renderer,int width,int height,const std::vector<TransitRoute>& routes,
  size_t selected,const TransitReport& report,float peakCongestion,int linkedStops,int focus) {
  drawFilledRect(renderer,0,0,width,height,{5,10,18},195);
  const auto p=budgetPanelRect(width,height); const int x=p.x,y=p.y;
  drawFilledRect(renderer,x,y,p.w,p.h,{22,28,37},255);
  drawRectOutline(renderer,x,y,p.w,p.h,{125,170,200});
  drawText(renderer,x+24,y+18,"TRANSPORT MANAGEMENT",{240,245,250},2);
  drawText(renderer,x+24,y+48,"TAB/UP/DOWN FOCUS. ENTER SELECT. LEFT/RIGHT ROUTES.",{165,185,205},1);
  const char* labels[]={"X","PREV","NEXT","-","+","VIEW COVERAGE","VIEW BOTTLENECK"};
  for (int i=0;i<7;++i) {
    const auto r=transportControlRect(i,width,height);
    const bool disabled=!transportControlEnabled(i,routes,selected);
    if (i==focus) drawRectOutline(renderer,r.x-2,r.y-2,r.w+4,r.h+4,{235,245,255});
    drawFilledRect(renderer,r.x,r.y,r.w,r.h,disabled ? RGB{28,32,40} : RGB{42,58,75});
    drawRectOutline(renderer,r.x,r.y,r.w,r.h,{135,165,190});
    drawText(renderer,r.x+10,r.y+7,labels[i],disabled ? RGB{95,100,110} : RGB{235,245,250},1);
  }
  drawText(renderer,x+24,y+490,"BUS FLEETS COST $2/VEHICLE/TICK. RAIL IS INSPECT ONLY.",{165,185,205},1);
  drawText(renderer,x+24,y+512,"PAUSED EDITS CHANGE NEXT-TICK CAPACITY AND COST. F8 OR ESC CLOSES.",{165,185,205},1);
  if (routes.empty()) {
    drawText(renderer,x+24,y+118,"NO ROUTES YET. BUS ROUTES UNLOCK AT 1200 RESIDENTS.",{185,205,220},1);
    if (report.valid) {
      uint64_t covered=0; for (const auto& row:report.routes) covered+=row.demand;
      drawText(renderer,x+24,y+164,"LAST TICK COMMUTERS OUTSIDE ROUTE REACH " +
        std::to_string(report.commuters-static_cast<uint32_t>(std::min<uint64_t>(covered,report.commuters))),{205,220,235},1);
    } else drawText(renderer,x+24,y+164,"NO COMPLETED-TICK USAGE YET",{165,185,205},1);
    return;
  }
  const auto& route=routes[std::min(selected,routes.size()-1)];
  drawText(renderer,x+240,y+72,"ROUTE " + std::to_string(route.id) + (route.mode==TransitMode::Bus ? " BUS" : " RAIL"),{135,215,240},1);
  drawText(renderer,x+24,y+112,"STOPS " + std::to_string(route.stops.size()) + "  ROAD LINKED " + (linkedStops<0 ? "?" : std::to_string(linkedStops)) + "  REACH " + std::to_string(route.stopCoverageRadius) + " ROAD HOPS",{205,220,235},1);
  drawText(renderer,x+24,y+148,"VEHICLES " + std::to_string(route.vehicleCount),{235,240,250},1);
  drawText(renderer,x+24,y+184,"NEXT CAPACITY " + std::to_string(route.totalCapacity()) + "  COST $" + std::to_string(TransitSystem::operatingCost(route)) + "/TICK",{255,220,130},1);
  drawText(renderer,x+24,y+210,"ALL BUS ROUTES $" + std::to_string(TransitSystem::operatingCost(routes)) + "/TICK IN CIVIC/TRANSIT EXPENSES",{165,185,205},1);
  if (!route.stops.empty()) drawText(renderer,x+24,y+232,"ENDS " + std::to_string(route.stops.front().x) + "," +
    std::to_string(route.stops.front().y) + " TO " + std::to_string(route.stops.back().x) + "," + std::to_string(route.stops.back().y),{165,185,205},1);
  const auto used=std::find_if(report.routes.begin(),report.routes.end(),[&](const auto& u){return u.id==route.id;});
  if (report.valid && used!=report.routes.end()) {
    drawText(renderer,x+24,y+252,"LAST COMPLETED TICK " + std::to_string(report.tick),{135,215,240},1);
    drawText(renderer,x+24,y+276,"RIDERS " + std::to_string(used->ridership) + "  ASSIGNED DEMAND " + std::to_string(used->demand) + "  CAPACITY " + std::to_string(used->capacity),{205,220,235},1);
    const auto utilization=used->capacity ? static_cast<uint32_t>(100.0*used->ridership/used->capacity) : 0;
    drawText(renderer,x+24,y+300,"UTILIZATION " + std::to_string(utilization) + "%  UNMET ASSIGNED " + std::to_string(used->demand-used->ridership),{205,220,235},1);
    uint64_t covered=0; for (const auto& row:report.routes) covered+=row.demand;
    drawText(renderer,x+24,y+324,"CITY COMMUTERS OUTSIDE ROUTE REACH " + std::to_string(report.commuters-static_cast<uint32_t>(std::min<uint64_t>(covered,report.commuters))),{205,220,235},1);
  } else drawText(renderer,x+24,y+260,"NO COMPLETED-TICK USAGE FOR THIS ROUTE",{185,205,220},1);
  drawText(renderer,x+24,y+364,"CURRENT COVERAGE PEAK CONGESTION " + std::to_string(static_cast<int>(peakCongestion*100)) + "%",{235,180,135},1);
  drawText(renderer,x+24,y+394,"OVERLAPS USE EXISTING ROUTE ORDER. DEMAND IS NOT DOUBLE COUNTED.",{165,185,205},1);
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
    case HudAction::Transport: return {panelX + 242, y, 128, 24};
    case HudAction::Alerts: return {panelX + 242, y + 30, 128, 24};
    case HudAction::Budget: return {panelX + 210, 322, 160, 24};
    default: return {0, 0, 0, 0};
  }
}

HudAction hudHitTest(int mouseX, int mouseY, int windowWidth) {
  const std::array<HudAction, 10> actions = {
    HudAction::TogglePause, HudAction::Slow, HudAction::Normal, HudAction::Fast,
    HudAction::Save, HudAction::Load, HudAction::ToggleDisasters, HudAction::Budget, HudAction::Alerts, HudAction::Transport
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
  drawText(renderer, x + 12, y + 268,
           "TRUCKS " + std::to_string(freight.trucks) + " UNROUTED " +
               std::to_string(freight.unroutedGoods) + " GATE " +
               std::to_string(freight.gate.x) + "," + std::to_string(freight.gate.y) +
               (freight.gateConnected ? " OK" : " NO ROAD"),
           {180, 200, 225}, 1);

  const auto& events = liveState.disasters;
  drawText(renderer, x + 12, y + 340,
           "FIRES " + std::to_string(events.fire.activeFires) +
           " LOST FIRE " + std::to_string(events.buildingsLostToFire) +
           " OTHER " + std::to_string(events.buildingsLostToDisaster) +
           " COV " + std::to_string(static_cast<int>(liveState.serviceSummary.fireCoverage * 100.f)) + "%", {255, 175, 110}, 1);
  // Both civic rows end above the disaster and budget buttons at y = 322.
  drawText(renderer, x + 12, y + 283, schoolCapacityLine(liveState.serviceSummary), {185, 215, 225}, 1);
  drawText(renderer, x + 12, y + 298, hospitalCapacityLine(liveState.serviceSummary), {185, 215, 225}, 1);
  drawText(renderer, x + 12, y + 356,
           events.disaster.earthquakeOccurred ? "EARTHQUAKE" :
           (events.disaster.floodOccurred ? "FLOOD" : ""), {255, 145, 100}, 1);
  const std::array<std::pair<HudAction, const char*>, 10> controls = {{
    {HudAction::TogglePause, paused ? "PLAY" : "PAUSE"},
    {HudAction::Slow, "1X"},
    {HudAction::Normal, "2X"},
    {HudAction::Fast, "3X"},
    {HudAction::Save, "F5 SAVE"},
    {HudAction::Load, "F9 LOAD"},
    {HudAction::Budget, "F6 BUDGET"},
    {HudAction::Alerts, "F7 ALERTS"},
    {HudAction::Transport, "F8 TRANSIT"},
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
                            bool enableDisasters, const ServiceCoverageSummary* civic) {
  constexpr int width = 330;
  const bool showDisasters = disasters && (enableDisasters ||
    disasters->buildingsLostToFire > 0 || disasters->buildingsLostToDisaster > 0);
  const int height = (showDisasters ? 118 : 82) + (civic ? 36 : 0);
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
  if (civic) {
    drawText(renderer,x+10,y+83,schoolCapacityLine(*civic),{185,215,225},1);
    drawText(renderer,x+10,y+101,hospitalCapacityLine(*civic),{185,215,225},1);
  }
  const int civicOffset = civic ? 36 : 0;
  if (showDisasters) {
    drawText(renderer, x + 10, y + 83 + civicOffset,
             std::string("DISASTERS ") + (enableDisasters ? "ON" : "OFF") +
             " FIRES " + std::to_string(disasters->fire.activeFires) +
             (civic ? " COV " + std::to_string(static_cast<int>(civic->fireCoverage * 100.f)) + "%" : ""), {255, 175, 110}, 1);
    drawText(renderer, x + 10, y + 101 + civicOffset,
             "LOST FIRE " + std::to_string(disasters->buildingsLostToFire) +
             " OTHER " + std::to_string(disasters->buildingsLostToDisaster), {255, 175, 110}, 1);
  }
}

namespace {
std::string fitUiText(const std::string& text, int width, int scale) {
  if (textWidth(text,scale) <= width) return text;
  std::string result = text;
  while (!result.empty() && textWidth(result + "...",scale) > width) result.pop_back();
  return result + "...";
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
  const int width = std::min(std::min(620,windowWidth-8), std::max(180,28+textWidth(toast.message,2)));
  const int x = (windowWidth - width) / 2;
  constexpr int y = 18;
  drawFilledRect(renderer, x, y, width, 34, {16, 18, 22}, 235);
  drawRectOutline(renderer, x, y, width, 34, toast.color, 255);
  drawText(renderer, x + 12, y + 10, fitUiText(toast.message,width-24,2), toast.color, 2);
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
  const int width = std::min(std::min(620, windowWidth - 8),
      std::max(130, 20 + textWidth(warning,1)));
  const std::string label = fitUiText(warning,width-16,1);
  const int x = std::max(4, std::min(windowWidth - width - 4, mouseX + 16));
  const int y = std::max(4, std::min(windowHeight - 28, mouseY + 18));
  drawFilledRect(renderer, x, y, width, 24, {28, 18, 18}, 235);
  drawRectOutline(renderer, x, y, width, 24, {235, 75, 75}, 255);
  drawText(renderer, x + 8, y + 8, label, {255, 185, 175}, 1);
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
  const int width = std::min(std::min(620,windowWidth-8), std::max(180,20+textWidth(message,1)));
  const int x = std::max(4, std::min(windowWidth - width - 4, mouseX + 16));
  const int preferredY = mouseY + 20;
  const int y = preferredY + 30 < windowHeight ? preferredY : mouseY - 36;
  drawFilledRect(renderer, x, y, width, 28, {20, 25, 32}, 245);
  drawRectOutline(renderer, x, y, width, 28, {145, 190, 245}, 255);
  drawText(renderer, x + 8, y + 9, fitUiText(message,width-16,1), {220, 232, 248}, 1);
}

void drawOnboarding(DrawList* renderer, int windowHeight, int step, const PopulationMilestones* milestones) {
  if (step == 6 && milestones) {
    const int x = 14, y = windowHeight - 154;
    drawFilledRect(renderer, x, y, 650, 92, {16, 20, 26}, 235);
    drawRectOutline(renderer, x, y, 650, 92, {120, 205, 255}, 255);
    drawText(renderer, x + 12, y + 10, "CITY MILESTONE " + std::to_string(milestones->highestReached), {120, 205, 255}, 2);
    drawText(renderer, x + 550, y + 12, "F1 HIDE", {155, 165, 180}, 1);
    drawText(renderer, x + 12, y + 42, catalog::unlockedSummary(milestones->highestReached), {235, 238, 242}, 1);
    const auto next = milestones->nextThreshold();
    drawText(renderer, x + 12, y + 66, next ? "NEXT AT " + std::to_string(next) + " RESIDENTS - UNLOCKS ARE PERMANENT" :
      "ALL POPULATION MILESTONES REACHED - UNLOCKS ARE PERMANENT", {175, 185, 200}, 1);
    return;
  }
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
      drawText(renderer, x + 12, y + 38, "6 REVIEW F6 BUDGET", {235, 238, 242}, 2);
      drawText(renderer, x + 12, y + 62, "CHECK UPKEEP BEFORE ADDING SERVICES", {175, 185, 200}, 1);
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
    case DistrictPanelAction::CycleHeight:
      return {kDistrictPanelX + 270, kDistrictPanelY + 76, 106, 22};
    default:
      return {0, 0, 0, 0};
  }
}

DistrictPanelAction districtPanelHitTest(int mouseX, int mouseY) {
  const std::array<DistrictPanelAction, 3> actions = {
    DistrictPanelAction::CycleArchetype, DistrictPanelAction::ToggleCap,
    DistrictPanelAction::CycleHeight
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
  const std::string height = district.heightCap <= 0
    ? "HEIGHT UNCAPPED"
    : "HEIGHT " + std::to_string(district.heightCap) + "X";
  drawText(renderer, panel.x + 10, panel.y + 80, height, {175, 205, 230}, 1);

  const std::array<std::pair<DistrictPanelAction, const char*>, 3> buttons = {{
    {DistrictPanelAction::CycleArchetype, "ARCH"},
    {DistrictPanelAction::ToggleCap, starved ? "FUND" : "STARVE"},
    {DistrictPanelAction::CycleHeight, "HEIGHT"},
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
  const DistrictSystem* districts, const BuildingDiagnostics* diagnostics, float cityJoblessness,bool pinned, const WasteSummary* waste, const ServiceCoverageSummary* civic
) {
  if (!map.isValid(coord)) {
    return;
  }
  constexpr int x = 14;
  constexpr int y = 170;
  constexpr int width = 360;
  const int height = (diagnostics ? (civic ? 358 : 326) : (civic ? 166 : 132)) +
    (diagnostics && civic && civic->boundedDepartments ? 48 : 0);
  drawFilledRect(renderer, x, y, width, height, {14, 17, 22}, 185);
  drawRectOutline(renderer, x, y, width, height, {150, 160, 175}, 240);

  const Tile& tile = map.getTile(coord);
  const char* terrain = tile.type == 2 ? "WATER" : (tile.type == 1 ? "ROUGH" : "LAND");
  drawText(renderer, x + 10, y + 9,
           std::string(pinned ? "PINNED TILE " : "TILE ") + std::to_string(coord.x) + " " + std::to_string(coord.y),
           {120, 205, 255}, 2);
  drawText(renderer,x+240,y+13,pinned ? "I/ESC UNPIN" : "I PIN",{165,185,205},1);
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
      if (building->hotelBeds) {
        drawText(renderer,x+10,y+111,
          std::string("HOTEL ")+(building->hotelOpen ? "OPEN" : "CLOSED")+
          " BEDS "+std::to_string(building->hotelBeds)+" / U MANAGES",{150,225,235},1);
      }
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
    if (facilityCoversTile(facility, coord)) {
      const std::string serviceLine = facility.type == ServiceType::Power
        ? std::string(ServiceSystem::powerSourceTypeToString(facility.powerSource)) + " " +
            std::to_string(static_cast<int>(facility.powerCapacityMW)) + " MW " +
            std::to_string(static_cast<int>(facility.emissionsKgPerMWh)) + " KG/MWH"
        : std::string("SERVICE ") + ServiceSystem::serviceTypeToString(facility.type, facility.surfaceWater, facility.sewageTreatment)
            + " RANGE " + std::to_string(facility.maxTravelDistance);
      drawText(renderer, x + 10, y + 111, serviceLine, serviceFacilityColor(facility.type), 1);
      if (facility.type == ServiceType::Garbage) {
        drawText(renderer, x + 10, y + 95,
          "FREE " + std::to_string(facility.landfillRemaining) + "/" + std::to_string(facility.landfillCapacity) + " FILL "
            + std::to_string(facility.landfillLastFill) + "/TICK", {210,210,160}, 1);
      }
      if (facility.type == ServiceType::Incinerator && waste) {
        drawText(renderer, x + 10, y + 95, "CITY BURNED " + std::to_string(waste->incinerated)
          + "/TICK POWER " + std::to_string(waste->powerGenerationMW) + " MW", {255,190,105}, 1);
      }
      if (facility.surfaceWater) {
        const std::string info = facility.type == ServiceType::Water
          ? "SOURCE CLEAN " + std::to_string(static_cast<int>(100 * ServiceSystem::intakeCleanFraction(roads, facilities, coord))) + "%"
          : (facility.sewageTreatment ? "TREATED SEWAGE: NO RAW DISCHARGE" : "RAW SEWAGE: INTAKE BUFFER 13 WATER HOPS");
        drawText(renderer, x + 10, y + 95, info, {255,190,105}, 1);
      }
      break;
    }
  }
  if (diagnostics) {
    const auto& d = *diagnostics;
    auto pct = [](float value) { return std::to_string(static_cast<int>(std::lround(value * 100.f))) + "%"; };
    const std::string reason = d.growth.blockers.empty() ? "local_gates_pass" : d.growth.blockers.front();
    std::string label = reason;
    std::replace(label.begin(),label.end(),'_',' ');
    drawText(renderer,x+10,y+132,"GROWTH " + label,{135,215,240},1);
    drawText(renderer,x+10,y+148,BuildingDiagnostics::advice(reason),{235,195,145},1);
    drawText(renderer,x+10,y+164,"DEMAND " + pct(d.growth.demand) + " CIVIC " + pct(d.growth.civicCoverage) +
      " CONGEST " + pct(d.growth.congestion),{190,205,220},1);
    std::string coverage = "REACH F/P/H/E ";
    for (int i = 0; i < 4; ++i) coverage += (d.services & (1u << i)) ? "Y " : "N ";
    drawText(renderer,x+10,y+180,coverage + " FIRE " + pct(d.fireResponse),{190,205,220},1);
    if (d.growth.occupied) {
      drawText(renderer,x+10,y+196,std::string(d.residential ? "VACANT HOUSING SLOTS " : "UNFILLED JOBS ") +
        std::to_string(std::max(0,d.capacity-d.occupancy)),{220,225,210},1);
      drawText(renderer,x+10,y+212,d.residential ? "CHECK JOBS, UTILITIES AND TAX ATTRACTION." :
        "CHECK CITY WORKFORCE, EDUCATION AND UTILITIES.",{165,185,205},1);
    }
    const auto& n = d.neighborhood;
    drawText(renderer,x+10,y+232,"NEARBY R4: " + std::to_string(n.buildings) + " BUILDINGS",{135,215,240},1);
    drawText(renderer,x+10,y+248,"RES " + std::to_string(n.residents) + "/" + std::to_string(n.housingCapacity) +
      " JOBS " + std::to_string(n.filledJobs) + "/" + std::to_string(n.jobCapacity),{190,205,220},1);
    drawText(renderer,x+10,y+264,"RES-WEIGHTED CRIME " + pct(n.crime) + " ILL " + pct(n.illness),{210,190,165},1);
    drawText(renderer,x+10,y+280,"CITY JOBLESS " + pct(cityJoblessness) + " - NOT LOCAL",{165,185,205},1);
    drawText(renderer,x+10,y+302,"LOCAL GATES ONLY. GROWTH IS PROBABILISTIC.",{165,185,205},1);
  }
  if (diagnostics && civic && civic->boundedDepartments) {
    const char* names[] = {"FIRE", "POLICE", "SEWAGE"};
    for (size_t i=0;i<3;++i)
      drawText(renderer,x+10,y+350+static_cast<int>(i)*16,std::string("TILE ")+names[i]+" SERVED "+
        std::to_string(std::llround(diagnostics->departmentServed[i]))+" UNSERVED "+
        std::to_string(std::llround(diagnostics->departmentUnserved[i])),{185,215,225},1);
  }
  if (civic) {
    drawText(renderer,x+10,y+(diagnostics ? 318 : 132),"CITY " + schoolCapacityLine(*civic),{185,215,225},1);
    drawText(renderer,x+10,y+(diagnostics ? 334 : 148),"CITY " + hospitalCapacityLine(*civic),{185,215,225},1);
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
      << "Tick:" << liveState.tick << (liveState.paused ? "(auto paused; N steps)" : "(live)")
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

namespace visualizer {
void drawRegionalPanel(DrawList* renderer,int width,int height,
  const std::vector<regional::Connection>& connections,size_t selected,
  const regional::Settlement& cargo,const tourism::Runtime& tourism,
  uint64_t openBeds,int64_t configuredUpkeep,bool finiteCargo,int focus,
  const std::vector<ManagedHotel>& hotels,bool hotelTab,size_t selectedHotel,size_t selectedAlert) {
  const auto p=regionalPanelRect(width,height);
  drawFilledRect(renderer,p.x,p.y,p.w,p.h,{22,30,39},250);
  drawRectOutline(renderer,p.x,p.y,p.w,p.h,{140,190,210});
  auto line=[&](int y,const std::string& text,RGB color=RGB{220,230,240}) {
    drawText(renderer,p.x+20,p.y+y,text,color,1);
  };
  line(18,"CONNECTIONS / TOURISM",{150,225,235});
  line(42,"U / ESC CLOSE   TAB FOCUS   ENTER APPLY   LEFT/RIGHT SELECT");
  line(68,"POLICY NOW: TOURISM "+std::string(tourism.enabled ? "ON" : "OFF")+
    " / CARGO "+(finiteCargo ? "FINITE" : "LEGACY ROAD"));
  line(88,"OPEN BEDS "+std::to_string(openBeds)+" / TERMINAL UPKEEP $"+
    std::to_string(configuredUpkeep)+" PER TICK");
  const auto& r=tourism.report;
  line(120,r.tick ? "COMPLETED VISITORS T"+std::to_string(r.tick) : "VISITORS: NO COMPLETED TICK");
  line(140,"DAY "+std::to_string(r.dayArrivals)+" / "+std::to_string(r.requested.day)+
    "  OVERNIGHT "+std::to_string(r.overnightArrivals)+" / "+std::to_string(r.requested.overnight)+
    "  STAYING "+std::to_string(r.staying));
  line(160,"UNMET ACCESS "+std::to_string(r.unmetDayTransport+r.unmetOvernightTransport)+
    " / BEDS "+std::to_string(r.unmetOvernightBeds),{245,210,135});
  line(180,"ATTRACTIONS: PARKS "+std::to_string(r.attractions.accessibleParks)+
    " / QUALITY "+std::to_string(r.attractions.qualityPercent)+" PERCENT");
  const auto& travel=tourism.travel;
  line(200,travel.measured ? "LOCAL TRIPS T"+std::to_string(travel.tick)+" ROAD "+std::to_string(travel.road)+
    " TRANSIT "+std::to_string(travel.transit)+" UNROUTED "+std::to_string(travel.unrouted) : "LOCAL TRIPS: NOT MEASURED");
  const auto& a=tourism.accounting;
  line(220,a.measured ? "VISITOR LEDGER T"+std::to_string(a.tick)+" FEES $"+std::to_string(a.fees())+
    " SERVICES $"+std::to_string(a.services)+" NET $"+std::to_string(a.fees()-a.services) : "VISITOR LEDGER: NOT MEASURED");
  line(250,cargo.valid ? "CARGO T"+std::to_string(cargo.allocation.tick)+" DELIVERED IN "+
    std::to_string(cargo.delivered.imports)+" OUT "+std::to_string(cargo.delivered.exports)+
    " UNMET "+std::to_string(static_cast<uint64_t>(cargo.allocation.unmet.imports)+cargo.allocation.unmet.exports)+
    " UNDELIVERED "+std::to_string(static_cast<uint64_t>(cargo.undelivered.imports)+cargo.undelivered.exports) : "CARGO: NO FINITE LOGISTICS MEASUREMENT");
  const auto terminals=managedTerminals(connections);
  if (hotelTab && hotels.empty()) line(294,"NO HOTELS - K BUILDS A HOTEL WITH 32 BEDS AND 12 JOBS");
  else if (hotelTab) {
    selectedHotel=std::min(selectedHotel,hotels.size()-1);
    const auto& hotel=hotels[selectedHotel];
    const auto& building=hotel.building;
    line(294,"HOTEL "+std::to_string(selectedHotel+1)+" / "+std::to_string(hotels.size())+
      " #"+std::to_string(building.id)+" AT "+std::to_string(building.position.x)+","+std::to_string(building.position.y));
    line(316,"POLICY NOW: "+std::string(building.hotelOpen ? "OPEN" : "CLOSED")+
      " BEDS "+std::to_string(building.hotelBeds)+" JOB CAPACITY "+std::to_string(building.capacity));
    line(342,"GUESTS AT T"+std::to_string(tourism.state.tick)+": "+std::to_string(hotel.guests)+
      " / DEPARTURES FOLLOW COMPLETED TICKS");
    line(366,"CLOSURE KEEPS JOBS / UPKEEP. GUESTS LEAVE NEXT TICK.",{245,210,135});
  }
  else if (terminals.empty()) line(294,"NO CONSTRUCTED TERMINALS - ROAD-ONLY TOURISM REMAINS AVAILABLE");
  else {
    selected=std::min(selected,terminals.size()-1);
    const auto& c=terminals[selected];
    const bool goods=c.kind==regional::Kind::CargoPort;
    const std::string kind=goods ? "CARGO PORT" : c.kind==regional::Kind::Airport ? "AIRPORT" : "PASSENGER TERMINAL";
    line(294,"TERMINAL "+std::to_string(selected+1)+" / "+std::to_string(terminals.size())+
      "  #"+std::to_string(c.id)+" "+kind);
    line(316,"POLICY NOW: "+std::string(c.enabled ? "ON" : "OFF")+" CAPACITY "+
      std::to_string(goods ? c.goodsPerTick : c.passengersPerTick)+(goods ? " GOODS/TICK" : " PASSENGERS/TICK")+
      " UPKEEP $"+std::to_string(c.enabled ? regional::terminalSpec(c.kind)->upkeep : 0));
    if (goods) {
      const auto row=std::find_if(cargo.allocation.connections.begin(),cargo.allocation.connections.end(),[&](const auto& u){return u.id==c.id;});
      line(342,cargo.valid && row!=cargo.allocation.connections.end() ? "LAST CARGO: "+std::string(regional::accessName(row->access))+
        " ADMITTED "+std::to_string(static_cast<uint64_t>(row->imports)+row->exports) : "NO COMPLETED CARGO ROW FOR THIS TERMINAL");
    } else {
      const auto row=std::find_if(r.connections.begin(),r.connections.end(),[&](const auto& u){return u.id==c.id;});
      line(342,row!=r.connections.end() ? "LAST VISITORS: "+std::string(regional::accessName(row->access))+
        " ARRIVED "+std::to_string(row->day+row->overnight)+" / "+std::to_string(row->capacity) : "NO COMPLETED VISITOR ROW FOR THIS TERMINAL");
    }
  }
  const auto alerts=regional::diagnose(cargo,tourism);
  if (alerts.empty()) line(394,"NO MEASURED REGIONAL BOTTLENECKS");
  else {
    selectedAlert=std::min(selectedAlert,alerts.size()-1);
    const auto& alert=alerts[selectedAlert];
    line(394,"ALERT "+std::to_string(selectedAlert+1)+"/"+std::to_string(alerts.size())+
      " T"+std::to_string(alert.tick)+" "+regional::problemName(alert.problem),{245,210,135});
    line(414,"UNMET "+std::to_string(alert.unmet)+" - "+regional::problemAdvice(alert.problem),{245,210,135});
  }
  line(446,"EDITS APPLY NEXT TICK. SPACE PAUSES; N STEPS WHILE PAUSED.");
  const char* labels[]={"CLOSE","< PREV","NEXT >","ON/OFF","CAPACITY","TOURISM",
    hotelTab ? "VIEW TERMINALS" : "VIEW HOTELS","LOCATE ON MAP","NEXT ALERT","MANAGE ALERT"};
  for (int action=0;action<10;++action) {
    const auto b=regionalControlRect(action,width,height);
    const bool enabled=regionalControlEnabled(action,hotelTab,hotelTab ? hotels.size() : terminals.size(),alerts.size());
    drawFilledRect(renderer,b.x,b.y,b.w,b.h,enabled ? RGB{42,62,75} : RGB{38,40,43});
    drawRectOutline(renderer,b.x,b.y,b.w,b.h,action==focus ? RGB{255,225,110} : RGB{110,140,160});
    drawText(renderer,b.x+6,b.y+8,labels[action],enabled ? RGB{235,240,245} : RGB{115,120,125},1);
  }
}
}
