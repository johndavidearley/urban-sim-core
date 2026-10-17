#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "src/gameplay/ServiceTool.hpp"
#include "src/world/Zoning.hpp"
#include "src/systems/EconomySystem.hpp"

// Citywide integer percentages. Defaults match the existing economy exactly.
// District fiscal policy is separate; this controls the city's treasury taxes.
struct BudgetPolicy {
  std::array<int, 5> taxPercent{{5, 8, 6, 9, 10}};
  std::array<int, kServiceTypeCount> servicePercent{{100,100,100,100,100,100,100,100,100,100,100,100}};
  // Defaults are neutral. Each percentage point changes attraction by 4%.
  // Household decisions combine property and income taxes with equal weight.
  std::array<float, 4> taxAttraction() const {
    const std::array<int, 4> defaults{{5, 8, 6, 9}};
    std::array<float, 4> result{};
    for (size_t i = 0; i < result.size(); ++i) {
      float delta = static_cast<float>(std::clamp(taxPercent[i], 0, 20) - defaults[i]);
      if (i == 0) delta = (delta + std::clamp(taxPercent[4], 0, 20) - 10) * 0.5f;
      result[i] = std::clamp(1.0f - 0.04f * delta, 0.4f, 1.4f);
    }
    return result;
  }
  ZoneDemand adjustedDemand(ZoneDemand demand) const {
    const auto attraction = taxAttraction();
    demand.residential = std::clamp(demand.residential * attraction[0], 0.f, 1.f);
    demand.commercial = std::clamp(demand.commercial * attraction[1], 0.f, 1.f);
    demand.industrial = std::clamp(demand.industrial * attraction[2], 0.f, 1.f);
    demand.office = std::clamp(demand.office * attraction[3], 0.f, 1.f);
    return demand;
  }
  std::array<int64_t, kServiceTypeCount> serviceCosts(const std::vector<ServiceFacility>& facilities) const {
    std::array<int64_t, kServiceTypeCount> costs{};
    for (const auto& facility : facilities) {
      const auto index = static_cast<size_t>(facility.type);
      if (index < costs.size()) costs[index] += ServiceTool::operatingCostPerTick(facility.type, facility.surfaceWater);
    }
    for (size_t i = 0; i < costs.size(); ++i)
      costs[i] = static_cast<int64_t>(std::llround(costs[i] * std::clamp(servicePercent[i], 0, 100) / 100.0));
    return costs;
  }
  int64_t serviceCost(const std::vector<ServiceFacility>& facilities) const {
    int64_t total = 0;
    for (const auto cost : serviceCosts(facilities)) total += cost;
    return total;
  }
  std::vector<ServiceFacility> fundedFacilities(const std::vector<ServiceFacility>& facilities) const {
    std::vector<ServiceFacility> result;
    result.reserve(facilities.size());
    for (auto facility : facilities) {
      const auto index = static_cast<size_t>(facility.type);
      if (index >= servicePercent.size()) continue;
      const int percent = std::clamp(servicePercent[index], 0, 100);
      if (percent == 0) continue;
      if (percent != 100) {
        const float scale = percent / 100.f;
        facility.maxTravelDistance = static_cast<int>(
          static_cast<int64_t>(facility.maxTravelDistance) * percent / 100);
        facility.quality *= scale;
        facility.powerCapacityMW *= scale;
        facility.waterSupplyUnits *= scale;
      }
      result.push_back(facility);
    }
    return result;
  }
  static std::array<float, kServiceTypeCount> coverage(const ServiceCoverageSummary& s) {
    return {{s.fireCoverage, s.policeCoverage, s.healthCoverage, s.educationCoverage,
      s.powerCoverage, s.waterCoverage, s.sanitationCoverage, s.garbageCoverage,
      s.recyclingCoverage, s.cemeteryCoverage, s.crematoriumCoverage, s.incineratorCoverage}};
  }
  bool valid() const {
    return std::all_of(taxPercent.begin(), taxPercent.end(),
                       [](int rate) { return rate >= 0 && rate <= 20; }) &&
      std::all_of(servicePercent.begin(), servicePercent.end(),
                  [](int percent) { return percent >= 0 && percent <= 100; });
  }
  TaxRates rates() const {
    TaxRates result;
    result.residentialRate = std::clamp(taxPercent[0], 0, 20) / 100.f;
    result.commercialRate = std::clamp(taxPercent[1], 0, 20) / 100.f;
    result.industrialRate = std::clamp(taxPercent[2], 0, 20) / 100.f;
    result.officeRate = std::clamp(taxPercent[3], 0, 20) / 100.f;
    result.incomeRate = std::clamp(taxPercent[4], 0, 20) / 100.f;
    return result;
  }
};
