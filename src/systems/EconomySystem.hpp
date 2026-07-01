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

  // Supply chain / trade: industrial occupancy produces goods, commercial
  // occupancy consumes them (restocking); the city trades the difference
  // with the outside world. Both exportRevenue and importCost already flow
  // into totalRevenue/totalExpenses (and so into balance) above.
  int64_t goodsProduced = 0;
  int64_t goodsConsumed = 0;
  int64_t tradeBalance = 0;   // goodsProduced - goodsConsumed; positive = exportable surplus
  int64_t exportRevenue = 0;  // > 0 only when tradeBalance > 0
  int64_t importCost = 0;     // > 0 only when tradeBalance < 0
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

// Supply-chain trade pricing: industrial workers produce goods, commercial
// workers consume them to restock; the net surplus/shortfall is traded with
// the outside world. Importing costs more per unit than exporting earns (a
// standard asymmetry: a city that can't supply itself pays a premium for it),
// so being import-dependent is a genuine economic penalty, not a wash.
struct TradeRates {
  float goodsPerIndustrialWorker = 2.0f;  // goods produced per employed industrial worker
  float goodsPerCommercialWorker = 1.5f;  // goods consumed (restocked) per employed commercial worker
  float exportPricePerUnit = 40.0f;       // revenue per unit of surplus goods exported
  float importCostPerUnit = 60.0f;        // expense per unit of shortfall goods imported
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
    const CityMap* map = nullptr,
    const TradeRates& tradeRates = TradeRates{}
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

  // Get default trade rates
  static TradeRates getDefaultTradeRates();

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
