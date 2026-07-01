#include "EconomySystem.hpp"
#include <algorithm>
#include <cmath>

#include "src/systems/LandValueSystem.hpp"

EconomyState EconomySystem::calculateEconomy(
  const EntityStore& store,
  const PopulationStore& population,
  const TaxRates& rates,
  const CityMap* map,
  const TradeRates& tradeRates
) {
  EconomyState state;

  // Calculate property value-based revenue from buildings
  const auto& buildings = store.getBuildings();

  int residentialCount = 0;
  int commercialCount = 0;
  int industrialCount = 0;
  int64_t totalCapacity = 0;
  int64_t totalOccupancy = 0;
  int64_t commercialOccupancy = 0;
  int64_t industrialOccupancy = 0;

  // Currency math runs in double: float's 24-bit mantissa loses integer
  // precision above ~16.7M, which city-scale values exceed.
  for (const auto& [id, building] : buildings) {
    int64_t buildingValue = estimateBuildingValue(building.type, building.capacity);
    totalCapacity += std::max(0, building.capacity);
    totalOccupancy += std::max(0, building.occupancy);

    switch (building.type) {
      case BuildingType::Residential:
        residentialCount++;
        state.residentialTaxRevenue += static_cast<int64_t>(static_cast<double>(buildingValue) * rates.residentialRate);
        state.residentialMaintenance += static_cast<int64_t>(rates.maintenanceResidential);
        break;
      case BuildingType::Commercial:
        commercialCount++;
        commercialOccupancy += std::max(0, building.occupancy);
        state.commercialTaxRevenue += static_cast<int64_t>(static_cast<double>(buildingValue) * rates.commercialRate);
        state.commercialMaintenance += static_cast<int64_t>(rates.maintenanceCommercial);
        break;
      case BuildingType::Industrial:
        industrialCount++;
        industrialOccupancy += std::max(0, building.occupancy);
        state.industrialTaxRevenue += static_cast<int64_t>(static_cast<double>(buildingValue) * rates.industrialRate);
        state.industrialMaintenance += static_cast<int64_t>(rates.maintenanceIndustrial);
        break;
    }
  }

  // Supply chain / trade: industrial workers produce goods, commercial
  // workers consume them; the city trades the net difference with the
  // outside world (see TradeRates for the export/import pricing asymmetry).
  state.goodsProduced = static_cast<int64_t>(static_cast<double>(industrialOccupancy) * tradeRates.goodsPerIndustrialWorker);
  state.goodsConsumed = static_cast<int64_t>(static_cast<double>(commercialOccupancy) * tradeRates.goodsPerCommercialWorker);
  state.tradeBalance = state.goodsProduced - state.goodsConsumed;
  if (state.tradeBalance > 0) {
    state.exportRevenue = static_cast<int64_t>(static_cast<double>(state.tradeBalance) * tradeRates.exportPricePerUnit);
  } else if (state.tradeBalance < 0) {
    state.importCost = static_cast<int64_t>(static_cast<double>(-state.tradeBalance) * tradeRates.importCostPerUnit);
  }

  // Add population-based income tax (from employed population)
  int64_t populationWealth = estimatePopulationWealth(population);
  const uint32_t employedPopulation = population.getTotalEmployed();
  int64_t populationIncomeTax = static_cast<int64_t>(static_cast<double>(populationWealth) * rates.incomeRate);

  state.totalTaxRevenue = state.residentialTaxRevenue + state.commercialTaxRevenue +
                          state.industrialTaxRevenue + populationIncomeTax;

  state.totalMaintenance = state.residentialMaintenance + state.commercialMaintenance +
                           state.industrialMaintenance;

  // Calculate balance (trade revenue/cost flow through here too, so a strong
  // exporter or an import-dependent city shows up directly in the budget).
  state.totalRevenue = state.totalTaxRevenue + state.exportRevenue;
  state.totalExpenses = state.totalMaintenance + state.importCost;
  state.balance = state.totalRevenue - state.totalExpenses;

  size_t totalBuildings = residentialCount + commercialCount + industrialCount;
  if (map != nullptr) {
    // Real, spatially-varying land value (see LandValueSystem) if the caller
    // has a map to read it from.
    state.averageLandValue = LandValueSystem::averageLandValue(*map);
  } else if (totalBuildings > 0) {
    // No map available (e.g. a district-scoped sub-economy): fall back to a
    // building-count-based placeholder.
    state.averageLandValue = 100.0f + (totalBuildings * 2.0f);
  }

  // Economic health based on balance and building count
  if (totalBuildings == 0) {
    state.economicHealth = 0.3f; // Depressed
  } else {
    // Normalize budget quality into [0,1] so outlier surpluses do not dominate.
    const float expenseScale = std::max(1.0f, static_cast<float>(state.totalExpenses));
    const float balanceScale = std::fabs(static_cast<float>(state.balance)) + expenseScale;
    const float budgetScore = 0.5f + 0.5f * (static_cast<float>(state.balance) / balanceScale);

    const float occupancyRatio = totalCapacity > 0
      ? (static_cast<float>(totalOccupancy) / static_cast<float>(totalCapacity))
      : 0.0f;

    const float employmentFactor = employedPopulation == 0 ? 0.0f : 1.0f;
    const float utilizationFactor = std::min(1.0f, occupancyRatio);

    float combinedHealth = (budgetScore * 0.35f) + (utilizationFactor * 0.40f) + (employmentFactor * 0.25f);
    state.economicHealth = std::max(0.0f, std::min(1.0f, combinedHealth));
  }

  return state;
}

void EconomySystem::applyToMetrics(const EconomyState& economy, CityMetrics& metrics) {
  metrics.cityRevenue = economy.totalRevenue;
  metrics.cityExpenses = economy.totalExpenses;
  metrics.landValueAverage = economy.averageLandValue;

  // Economic health affects happiness
  // Good economy (health > 0.7) boosts happiness
  // Bad economy (health < 0.3) reduces happiness
  float economicHappinessBonus = (economy.economicHealth - 0.5f) * 0.2f;
  metrics.happiness = std::max(0.0f, std::min(1.0f, metrics.happiness + economicHappinessBonus));

  // Pollution reduced by prosperity (better infrastructure/services)
  metrics.pollution = std::max(0.0f, metrics.pollution * (1.0f - economy.economicHealth * 0.3f));
}

int64_t EconomySystem::calculateMonthlyBalance(
  const EconomyState& previous,
  const EconomyState& current
) {
  // Blend current and prior balance to soften one-tick swings.
  return (current.balance + previous.balance) / 2;
}

TaxRates EconomySystem::getDefaultRates() {
  return TaxRates{};
}

TradeRates EconomySystem::getDefaultTradeRates() {
  return TradeRates{};
}

int64_t EconomySystem::estimatePopulationWealth(
  const PopulationStore& population,
  float propertyValuePerCapita
) {
  int64_t totalWealth = 0;

  const auto& groups = population.getGroups();
  for (const auto& [id, group] : groups) {
    // Income bands earn different amounts
    float incomeMultiplier = 1.0f;
    switch (group.band) {
      case IncomeBand::Low:
        incomeMultiplier = 0.8f;
        break;
      case IncomeBand::Middle:
        incomeMultiplier = 1.0f;
        break;
      case IncomeBand::High:
        incomeMultiplier = 1.5f;
        break;
    }

    int64_t groupWealth = static_cast<int64_t>(
      static_cast<double>(group.employed) * propertyValuePerCapita * incomeMultiplier);
    totalWealth += groupWealth;
  }

  return totalWealth;
}

int64_t EconomySystem::estimateBuildingValue(
  BuildingType type,
  int capacity,
  float baseValue
) {
  float typeMultiplier = 1.0f;
  switch (type) {
    case BuildingType::Residential:
      typeMultiplier = 1.0f;
      break;
    case BuildingType::Commercial:
      typeMultiplier = 1.5f;
      break;
    case BuildingType::Industrial:
      typeMultiplier = 1.2f;
      break;
  }

  // Value scales with capacity
  return static_cast<int64_t>(static_cast<double>(baseValue) * capacity * typeMultiplier);
}
