#pragma once

#include <cstdint>
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/metrics/CityMetrics.hpp"
#include "src/world/CityMap.hpp"

struct EconomyState {
  // Revenue sources
  int64_t residentialTaxRevenue = 0;
  int64_t commercialTaxRevenue = 0;
  int64_t industrialTaxRevenue = 0;
  int64_t totalTaxRevenue = 0;

  // Expenses
  int64_t residentialMaintenance = 0;
  int64_t commercialMaintenance = 0;
  int64_t industrialMaintenance = 0;
  int64_t totalMaintenance = 0;

  // Budget state
  int64_t balance = 0;
  int64_t totalRevenue = 0;
  int64_t totalExpenses = 0;

  // Economic indicators
  float averageLandValue = 100.0f;
  float economicHealth = 0.5f; // 0.0 (crisis) to 1.0 (boom)
};

// Tax rates (percentage of building/population value)
struct TaxRates {
  float residentialRate = 0.05f; // 5% property tax
  float commercialRate = 0.08f;  // 8% commercial tax
  float industrialRate = 0.06f;  // 6% industrial tax
  float incomeRate = 0.10f;      // 10% income tax on estimated population wealth

  float maintenanceResidential = 50.0f;  // Fixed cost per building
  float maintenanceCommercial = 100.0f;
  float maintenanceIndustrial = 120.0f;
};

class EconomySystem {
public:
  // Calculate economic state from city current entities and population.
  // If `map` is provided, averageLandValue is the real mean of Tile::landValue
  // across zoned tiles (see LandValueSystem); otherwise it falls back to a
  // building-count-based placeholder, preserving prior behavior for callers
  // that have no map available (e.g. district-scoped sub-economies).
  static EconomyState calculateEconomy(
    const EntityStore& store,
    const PopulationStore& population,
    const TaxRates& rates = TaxRates{},
    const CityMap* map = nullptr
  );

  // Apply economy state to city metrics
  static void applyToMetrics(const EconomyState& economy, CityMetrics& metrics);

  // Update balances based on time passing (e.g., per month)
  static int64_t calculateMonthlyBalance(
    const EconomyState& previous,
    const EconomyState& current
  );

  // Get default tax rates
  static TaxRates getDefaultRates();

  // Utility: estimate population wealth/property value
  static int64_t estimatePopulationWealth(
    const PopulationStore& population,
    float propertyValuePerCapita = 50000.0f
  );

  // Utility: estimate building property value
  static int64_t estimateBuildingValue(
    BuildingType type,
    int capacity,
    float baseValue = 10000.0f
  );
};
