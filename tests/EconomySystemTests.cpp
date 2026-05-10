#include <gtest/gtest.h>
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/systems/EconomySystem.hpp"

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
  EXPECT_NEAR(value100 / value50, 2.0f, 0.1f);
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
