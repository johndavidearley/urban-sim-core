#include "EconomySystem.hpp"
#include <algorithm>
#include <cmath>

#include "src/systems/LandValueSystem.hpp"

namespace {

struct TypeTaxResult {
  int count = 0;
  int64_t taxRevenue = 0;
  int64_t occupancy = 0;
};

// Walk one type's ID index: tax from capacity, optional occupancy sum.
TypeTaxResult accumulateType(
  const EntityStore& store,
  BuildingType type,
  float taxRate
) {
  TypeTaxResult result;
  const std::vector<EntityId>& ids = store.idsByBuildingType(type);
  result.count = static_cast<int>(ids.size());
  for (EntityId id : ids) {
    const Building* building = store.getBuilding(id);
    if (building == nullptr) continue;
    const int64_t buildingValue =
      EconomySystem::estimateBuildingValue(building->type, building->capacity);
    result.taxRevenue += static_cast<int64_t>(static_cast<double>(buildingValue) * taxRate);
    result.occupancy += std::max(0, building->occupancy);
  }
  return result;
}

} // namespace

EconomyState EconomySystem::calculateEconomy(
  const EntityStore& store,
  const PopulationStore& population,
  const TaxRates& rates,
  const CityMap* map,
  const TradeRates& tradeRates,
  float inflationMultiplier,
  EconomyLandValueBounds landValueBounds
) {
  EconomyState state;
  state.inflationMultiplier = inflationMultiplier;

  // Per-type ID indices: one pass per type (no hash-map iteration).
  // Currency math runs in double: float's 24-bit mantissa loses integer
  // precision above ~16.7M, which city-scale values exceed.
  const TypeTaxResult residential = accumulateType(store, BuildingType::Residential, rates.residentialRate);
  const TypeTaxResult commercial = accumulateType(store, BuildingType::Commercial, rates.commercialRate);
  const TypeTaxResult industrial = accumulateType(store, BuildingType::Industrial, rates.industrialRate);
  const TypeTaxResult office = accumulateType(store, BuildingType::Office, rates.officeRate);

  state.residentialTaxRevenue = residential.taxRevenue;
  state.commercialTaxRevenue = commercial.taxRevenue;
  state.industrialTaxRevenue = industrial.taxRevenue;
  state.officeTaxRevenue = office.taxRevenue;

  // Maintenance is a fixed cost per building of that type.
  state.residentialMaintenance = static_cast<int64_t>(
    static_cast<double>(residential.count) * rates.maintenanceResidential * inflationMultiplier);
  state.commercialMaintenance = static_cast<int64_t>(
    static_cast<double>(commercial.count) * rates.maintenanceCommercial * inflationMultiplier);
  state.industrialMaintenance = static_cast<int64_t>(
    static_cast<double>(industrial.count) * rates.maintenanceIndustrial * inflationMultiplier);
  state.officeMaintenance = static_cast<int64_t>(
    static_cast<double>(office.count) * rates.maintenanceOffice * inflationMultiplier);

  const int64_t totalCapacity =
    static_cast<int64_t>(store.capacityOfType(BuildingType::Residential)) +
    static_cast<int64_t>(store.capacityOfType(BuildingType::Commercial)) +
    static_cast<int64_t>(store.capacityOfType(BuildingType::Industrial)) +
    static_cast<int64_t>(store.capacityOfType(BuildingType::Office));
  const int64_t totalOccupancy =
    residential.occupancy + commercial.occupancy + industrial.occupancy + office.occupancy;
  const int64_t commercialOccupancy = commercial.occupancy;
  const int64_t industrialOccupancy = industrial.occupancy;

  // Supply chain / trade: industrial workers produce goods, commercial
  // workers consume them; the city trades the net difference with the
  // outside world (see TradeRates for the export/import pricing asymmetry).
  // Export/import prices are world-market prices, so they inflate too.
  state.goodsProduced = static_cast<int64_t>(static_cast<double>(industrialOccupancy) * tradeRates.goodsPerIndustrialWorker);
  state.goodsConsumed = static_cast<int64_t>(static_cast<double>(commercialOccupancy) * tradeRates.goodsPerCommercialWorker);
  state.tradeBalance = state.goodsProduced - state.goodsConsumed;
  if (state.tradeBalance > 0) {
    state.exportRevenue = static_cast<int64_t>(
      static_cast<double>(state.tradeBalance) * tradeRates.exportPricePerUnit * inflationMultiplier);
  } else if (state.tradeBalance < 0) {
    state.importCost = static_cast<int64_t>(
      static_cast<double>(-state.tradeBalance) * tradeRates.importCostPerUnit * inflationMultiplier);
  }

  // Add population-based income tax (from employed population)
  int64_t populationWealth = estimatePopulationWealth(population);
  const uint32_t employedPopulation = population.getTotalEmployed();
  int64_t populationIncomeTax = static_cast<int64_t>(static_cast<double>(populationWealth) * rates.incomeRate);

  state.totalTaxRevenue = state.residentialTaxRevenue + state.commercialTaxRevenue +
                          state.industrialTaxRevenue + state.officeTaxRevenue + populationIncomeTax;

  state.totalMaintenance = state.residentialMaintenance + state.commercialMaintenance +
                           state.industrialMaintenance + state.officeMaintenance;

  // Calculate balance (trade revenue/cost flow through here too, so a strong
  // exporter or an import-dependent city shows up directly in the budget).
  state.totalRevenue = state.totalTaxRevenue + state.exportRevenue;
  state.totalExpenses = state.totalMaintenance + state.importCost;
  state.balance = state.totalRevenue - state.totalExpenses;

  const size_t totalBuildings = static_cast<size_t>(
    residential.count + commercial.count + industrial.count + office.count);
  if (map != nullptr) {
    // Real, spatially-varying land value (see LandValueSystem) if the caller
    // has a map to read it from. Optional bounds keep large empty maps cheap.
    if (landValueBounds.useBounds) {
      state.averageLandValue = LandValueSystem::averageLandValue(
        *map,
        landValueBounds.x0, landValueBounds.y0,
        landValueBounds.x1, landValueBounds.y1);
    } else {
      state.averageLandValue = LandValueSystem::averageLandValue(*map);
    }
  } else if (totalBuildings > 0) {
    // No map available (e.g. a district-scoped sub-economy): fall back to a
    // building-count-based placeholder.
    state.averageLandValue = 100.0f + (static_cast<float>(totalBuildings) * 2.0f);
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
    case BuildingType::Office:
      typeMultiplier = 1.6f;  // premium office real estate
      break;
  }

  // Value scales with capacity
  return static_cast<int64_t>(static_cast<double>(baseValue) * capacity * typeMultiplier);
}
