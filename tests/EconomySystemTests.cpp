#include <gtest/gtest.h>
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/LandValueSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

class EconomySystemTests : public ::testing::Test {
protected:
  void SetUp() override {
    store.reset(new EntityStore());
    population.reset(new PopulationStore());
  }

  std::unique_ptr<EntityStore> store;
  std::unique_ptr<PopulationStore> population;
};

// Test: Empty economy with no buildings has minimal revenue
TEST_F(EconomySystemTests, EmptyEconomyHasNoRevenue) {
  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_EQ(state.totalTaxRevenue, 0);
  EXPECT_EQ(state.totalMaintenance, 0);
  EXPECT_EQ(state.balance, 0);
}

// Test: Residential buildings generate tax revenue
TEST_F(EconomySystemTests, ResidentialBuildingsGenerateRevenue) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 50);
  store->createBuilding(BuildingType::Residential, {11, 10}, 50);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_GT(state.residentialTaxRevenue, 0);
  EXPECT_GT(state.totalTaxRevenue, 0);
  EXPECT_GT(state.residentialMaintenance, 0);
}

// Test: Commercial buildings generate higher tax revenue
TEST_F(EconomySystemTests, CommercialBuildingsGenerateHigherRevenue) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 100);
  EconomyState stateResidential = EconomySystem::calculateEconomy(*store, *population);

  store.reset(new EntityStore());
  store->createBuilding(BuildingType::Commercial, {10, 10}, 100);
  EconomyState stateCommercial = EconomySystem::calculateEconomy(*store, *population);

  // Commercial tax rate (0.08) > residential (0.05)
  EXPECT_GT(stateCommercial.commercialTaxRevenue, stateResidential.residentialTaxRevenue);
}

// Test: Industrial buildings generate revenue
TEST_F(EconomySystemTests, IndustrialBuildingsGenerateRevenue) {
  store->createBuilding(BuildingType::Industrial, {10, 10}, 75);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_GT(state.industrialTaxRevenue, 0);
  EXPECT_GT(state.totalMaintenance, 0);
}

// Test: Maintenance expenses reduce balance
TEST_F(EconomySystemTests, MaintenanceReducesBalance) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 50);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  // Balance = revenue - maintenance
  EXPECT_LT(state.balance, state.totalTaxRevenue);
  EXPECT_GT(state.balance, 0); // Revenue > maintenance for small buildings
}

// Test: Population employed generates income tax
TEST_F(EconomySystemTests, EmployedPopulationGeneratesIncomeTax) {
  population->createGroup(IncomeBand::Middle, 100, 80); // 80 employed

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  // Income tax should be generated from employed population
  EXPECT_GT(state.totalTaxRevenue, 0);
}

// Test: Higher income bands contribute more to tax revenue
TEST_F(EconomySystemTests, HighIncomePopulationContributesMoreTax) {
  population->createGroup(IncomeBand::Low, 100, 80);
  EconomyState stateLow = EconomySystem::calculateEconomy(*store, *population);

  population.reset(new PopulationStore());
  population->createGroup(IncomeBand::High, 100, 80);
  EconomyState stateHigh = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_GT(stateHigh.totalTaxRevenue, stateLow.totalTaxRevenue);
}

// Test: Economic health scales with balance
TEST_F(EconomySystemTests, EconomicHealthScalesWithBalance) {
  // Prosperous economy with many buildings and employed population
  for (int i = 0; i < 5; ++i) {
    store->createBuilding(BuildingType::Residential, {10 + i, 10}, 100);
    store->createBuilding(BuildingType::Commercial, {10 + i, 15}, 100);
  }
  population->createGroup(IncomeBand::Middle, 200, 180);

  EconomyState stateProsper = EconomySystem::calculateEconomy(*store, *population);

  // Empty economy
  store.reset(new EntityStore());
  population.reset(new PopulationStore());
  EconomyState stateDepressed = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_GT(stateProsper.economicHealth, stateDepressed.economicHealth);
}

// Test: Land value increases with building density
TEST_F(EconomySystemTests, LandValueIncreasesWithBuildings) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 50);
  EconomyState state1 = EconomySystem::calculateEconomy(*store, *population);

  store->createBuilding(BuildingType::Residential, {11, 10}, 50);
  store->createBuilding(BuildingType::Commercial, {12, 10}, 100);
  EconomyState state2 = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_GT(state2.averageLandValue, state1.averageLandValue);
}

// Test: Metrics integration
TEST_F(EconomySystemTests, EconomyAppliesToCityMetrics) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 100);
  store->createBuilding(BuildingType::Commercial, {11, 10}, 100);
  population->createGroup(IncomeBand::Middle, 100, 80);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  CityMetrics metrics;
  EconomySystem::applyToMetrics(state, metrics);

  EXPECT_EQ(metrics.cityRevenue, state.totalRevenue);
  EXPECT_EQ(metrics.cityExpenses, state.totalExpenses);
  EXPECT_EQ(metrics.landValueAverage, state.averageLandValue);
}

// Test: Economic crisis (negative balance) reduces happiness
TEST_F(EconomySystemTests, EconomicCrisisReducesHappiness) {
  // Create expensive buildings but no population/income
  for (int i = 0; i < 10; ++i) {
    store->createBuilding(BuildingType::Industrial, {10 + i, 10}, 100);
  }

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  CityMetrics metrics;
  metrics.happiness = 0.8f; // Start with good happiness
  EconomySystem::applyToMetrics(state, metrics);

  // Crisis economy should reduce happiness
  EXPECT_LT(metrics.happiness, 0.8f);
}

// Test: Building value estimation scales with capacity
TEST_F(EconomySystemTests, BuildingValueScalesWithCapacity) {
  int64_t value50 = EconomySystem::estimateBuildingValue(BuildingType::Residential, 50);
  int64_t value100 = EconomySystem::estimateBuildingValue(BuildingType::Residential, 100);

  // Value should roughly double when capacity doubles
  EXPECT_GT(value100, value50);
  EXPECT_NEAR(static_cast<double>(value100) / static_cast<double>(value50), 2.0, 0.1);
}

// Test: Different building types have different values
TEST_F(EconomySystemTests, BuildingTypesHaveDifferentValues) {
  int64_t residential = EconomySystem::estimateBuildingValue(BuildingType::Residential, 100);
  int64_t commercial = EconomySystem::estimateBuildingValue(BuildingType::Commercial, 100);
  int64_t industrial = EconomySystem::estimateBuildingValue(BuildingType::Industrial, 100);

  EXPECT_GT(commercial, residential); // Commercial > residential
  EXPECT_GT(industrial, residential);  // Industrial > residential
}

// Test: Population wealth calculation
TEST_F(EconomySystemTests, PopulationWealthCalculation) {
  population->createGroup(IncomeBand::Low, 100, 50);
  population->createGroup(IncomeBand::Middle, 100, 50);
  population->createGroup(IncomeBand::High, 100, 50);

  int64_t wealth = EconomySystem::estimatePopulationWealth(*population);

  // High-income group (1.5x multiplier) should be most valuable
  EXPECT_GT(wealth, 0);
}

// Test: Monthly balance calculation
TEST_F(EconomySystemTests, MonthlyBalanceCalculation) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 100);
  population->createGroup(IncomeBand::Middle, 100, 80);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  int64_t monthlyBalance = EconomySystem::calculateMonthlyBalance(state, state);

  EXPECT_EQ(monthlyBalance, state.balance);
}

// Test: Multiple building types contribute properly
TEST_F(EconomySystemTests, MultiipleBuildingTypesContributeSeparately) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 100);
  store->createBuilding(BuildingType::Commercial, {11, 10}, 100);
  store->createBuilding(BuildingType::Industrial, {12, 10}, 100);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  int64_t totalTax = state.residentialTaxRevenue + state.commercialTaxRevenue + state.industrialTaxRevenue;
  EXPECT_EQ(state.totalTaxRevenue, totalTax);

  int64_t totalMaint = state.residentialMaintenance + state.commercialMaintenance + state.industrialMaintenance;
  EXPECT_EQ(state.totalMaintenance, totalMaint);
}

// Test: Default tax rates are reasonable
TEST_F(EconomySystemTests, DefaultTaxRatesAreReasonable) {
  TaxRates rates = EconomySystem::getDefaultRates();

  EXPECT_GT(rates.residentialRate, 0.0f);
  EXPECT_GT(rates.commercialRate, 0.0f);
  EXPECT_GT(rates.industrialRate, 0.0f);
  EXPECT_LT(rates.residentialRate, 0.5f);
  EXPECT_LT(rates.commercialRate, 0.5f);
  EXPECT_LT(rates.industrialRate, 0.5f);
}

// Test: Without a map, averageLandValue keeps the old building-count placeholder
// (a regression guard for callers - e.g. district-scoped sub-economies - that
// have no CityMap to read real per-tile values from).
TEST_F(EconomySystemTests, NoMapFallsBackToPlaceholderLandValue) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 50);
  store->createBuilding(BuildingType::Commercial, {11, 10}, 50);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_FLOAT_EQ(state.averageLandValue, 100.0f + (2 * 2.0f));
}

// Test: With a map, averageLandValue is the real mean of Tile::landValue
// across zoned tiles (LandValueSystem::averageLandValue), not the placeholder.
TEST_F(EconomySystemTests, MapProvidesRealAverageLandValue) {
  CityMap map({4, 1});
  map.getTile({0, 0}).zone = static_cast<int>(ZoneType::Residential);
  map.getTile({0, 0}).landValue = 120.0f;
  map.getTile({1, 0}).zone = static_cast<int>(ZoneType::Commercial);
  map.getTile({1, 0}).landValue = 180.0f;

  EconomyState state = EconomySystem::calculateEconomy(*store, *population, TaxRates{}, &map);

  EXPECT_FLOAT_EQ(state.averageLandValue, LandValueSystem::averageLandValue(map));
  EXPECT_FLOAT_EQ(state.averageLandValue, 150.0f);
}

// Test: goodsProduced/goodsConsumed scale with industrial/commercial
// occupancy respectively (not capacity, not building count).
TEST_F(EconomySystemTests, GoodsScaleWithOccupancyNotCapacity) {
  EntityId industrialId = store->createBuilding(BuildingType::Industrial, {10, 10}, 100);
  store->getBuilding(industrialId)->occupancy = 20;
  EntityId commercialId = store->createBuilding(BuildingType::Commercial, {11, 10}, 100);
  store->getBuilding(commercialId)->occupancy = 10;

  TradeRates tradeRates = EconomySystem::getDefaultTradeRates();
  EconomyState state = EconomySystem::calculateEconomy(*store, *population, TaxRates{}, nullptr, tradeRates);

  EXPECT_FLOAT_EQ(static_cast<float>(state.goodsProduced), 20.0f * tradeRates.goodsPerIndustrialWorker);
  EXPECT_FLOAT_EQ(static_cast<float>(state.goodsConsumed), 10.0f * tradeRates.goodsPerCommercialWorker);
}

// Test: industrial surplus (more goods produced than consumed) generates
// export revenue and no import cost, and that revenue reaches totalRevenue.
TEST_F(EconomySystemTests, IndustrialSurplusGeneratesExportRevenue) {
  EntityId industrialId = store->createBuilding(BuildingType::Industrial, {10, 10}, 200);
  store->getBuilding(industrialId)->occupancy = 100;  // large producer
  EntityId commercialId = store->createBuilding(BuildingType::Commercial, {11, 10}, 20);
  store->getBuilding(commercialId)->occupancy = 5;    // small consumer

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_GT(state.tradeBalance, 0);
  EXPECT_GT(state.exportRevenue, 0);
  EXPECT_EQ(state.importCost, 0);
  EXPECT_GE(state.totalRevenue, state.totalTaxRevenue + state.exportRevenue);
}

// Test: commercial-heavy, industry-light cities pay import costs instead.
TEST_F(EconomySystemTests, CommercialShortfallGeneratesImportCost) {
  EntityId commercialId = store->createBuilding(BuildingType::Commercial, {10, 10}, 200);
  store->getBuilding(commercialId)->occupancy = 100;  // large consumer
  EntityId industrialId = store->createBuilding(BuildingType::Industrial, {11, 10}, 20);
  store->getBuilding(industrialId)->occupancy = 5;    // small producer

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_LT(state.tradeBalance, 0);
  EXPECT_GT(state.importCost, 0);
  EXPECT_EQ(state.exportRevenue, 0);
  EXPECT_EQ(state.totalExpenses, state.totalMaintenance + state.importCost);
}

// Test: importing a shortfall costs strictly more than exporting the same
// magnitude of surplus would earn (TradeRates' deliberate asymmetry).
TEST_F(EconomySystemTests, ImportCostExceedsExportRevenueForSameMagnitude) {
  const TradeRates rates = EconomySystem::getDefaultTradeRates();
  EXPECT_GT(rates.importCostPerUnit, rates.exportPricePerUnit);
}

// Test: with no buildings at all, trade is a clean zero (no phantom cost or
// revenue from an empty city).
TEST_F(EconomySystemTests, NoBuildingsMeansNoTrade) {
  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_EQ(state.goodsProduced, 0);
  EXPECT_EQ(state.goodsConsumed, 0);
  EXPECT_EQ(state.tradeBalance, 0);
  EXPECT_EQ(state.exportRevenue, 0);
  EXPECT_EQ(state.importCost, 0);
}

// Test: the default inflationMultiplier of 1.0 leaves every existing caller's
// behavior unchanged (no explicit inflation argument passed).
TEST_F(EconomySystemTests, DefaultInflationMultiplierIsOne) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 50);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_FLOAT_EQ(state.inflationMultiplier, 1.0f);
}

// Test: a higher inflation multiplier raises maintenance costs proportionally.
TEST_F(EconomySystemTests, InflationRaisesMaintenanceCosts) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 50);
  store->createBuilding(BuildingType::Commercial, {11, 10}, 50);
  store->createBuilding(BuildingType::Industrial, {12, 10}, 50);

  EconomyState base = EconomySystem::calculateEconomy(
    *store, *population, TaxRates{}, nullptr, TradeRates{}, 1.0f);
  EconomyState inflated = EconomySystem::calculateEconomy(
    *store, *population, TaxRates{}, nullptr, TradeRates{}, 2.0f);

  EXPECT_FLOAT_EQ(inflated.inflationMultiplier, 2.0f);
  EXPECT_EQ(inflated.totalMaintenance, base.totalMaintenance * 2);
}

// Test: inflation also raises trade prices (export revenue / import cost),
// since both represent external, world-market costs.
TEST_F(EconomySystemTests, InflationRaisesTradePrices) {
  EntityId industrialId = store->createBuilding(BuildingType::Industrial, {10, 10}, 200);
  store->getBuilding(industrialId)->occupancy = 100;
  EntityId commercialId = store->createBuilding(BuildingType::Commercial, {11, 10}, 20);
  store->getBuilding(commercialId)->occupancy = 5;

  EconomyState base = EconomySystem::calculateEconomy(
    *store, *population, TaxRates{}, nullptr, TradeRates{}, 1.0f);
  EconomyState inflated = EconomySystem::calculateEconomy(
    *store, *population, TaxRates{}, nullptr, TradeRates{}, 2.0f);

  ASSERT_GT(base.exportRevenue, 0);
  EXPECT_EQ(inflated.exportRevenue, base.exportRevenue * 2);
}

// Test: the core asymmetry - inflation does NOT scale tax revenue, only
// costs/trade prices, so a stagnant city's revenue stays flat while its
// expenses rise under inflation.
TEST_F(EconomySystemTests, InflationDoesNotAffectTaxRevenue) {
  store->createBuilding(BuildingType::Residential, {10, 10}, 50);
  store->createBuilding(BuildingType::Commercial, {11, 10}, 50);
  store->createBuilding(BuildingType::Industrial, {12, 10}, 50);
  population->createGroup(IncomeBand::Middle, 30, 20); // 20 employed

  EconomyState base = EconomySystem::calculateEconomy(
    *store, *population, TaxRates{}, nullptr, TradeRates{}, 1.0f);
  EconomyState inflated = EconomySystem::calculateEconomy(
    *store, *population, TaxRates{}, nullptr, TradeRates{}, 3.0f);

  EXPECT_EQ(inflated.totalTaxRevenue, base.totalTaxRevenue);
  EXPECT_EQ(inflated.residentialTaxRevenue, base.residentialTaxRevenue);
  EXPECT_EQ(inflated.commercialTaxRevenue, base.commercialTaxRevenue);
  EXPECT_EQ(inflated.industrialTaxRevenue, base.industrialTaxRevenue);
  // Balance still shifts because expenses rose while revenue held steady.
  EXPECT_LT(inflated.balance, base.balance);
}

// Office buildings generate their own tax revenue and maintenance, distinct
// from commercial/industrial/residential, and flow into the totals.
TEST_F(EconomySystemTests, OfficeBuildingsGenerateRevenueAndMaintenance) {
  store->createBuilding(BuildingType::Office, {10, 10}, 60);

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_GT(state.officeTaxRevenue, 0);
  EXPECT_GT(state.officeMaintenance, 0);
  EXPECT_EQ(state.residentialTaxRevenue, 0);
  EXPECT_EQ(state.commercialTaxRevenue, 0);
  EXPECT_EQ(state.industrialTaxRevenue, 0);
  EXPECT_EQ(state.totalTaxRevenue, state.officeTaxRevenue);
  EXPECT_EQ(state.totalMaintenance, state.officeMaintenance);
}

// Office workers sit outside the physical goods supply chain: an
// office-only city produces and consumes no goods, so trade is a clean zero
// regardless of office occupancy.
TEST_F(EconomySystemTests, OfficeOccupancyDoesNotAffectTrade) {
  EntityId officeId = store->createBuilding(BuildingType::Office, {10, 10}, 100);
  store->getBuilding(officeId)->occupancy = 80;

  EconomyState state = EconomySystem::calculateEconomy(*store, *population);

  EXPECT_EQ(state.goodsProduced, 0);
  EXPECT_EQ(state.goodsConsumed, 0);
  EXPECT_EQ(state.tradeBalance, 0);
}
