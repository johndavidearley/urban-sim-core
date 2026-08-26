#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/systems/MetricsSystem.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/world/CityMap.hpp"

TEST(MetricsSystemTests, CollectCityMetricsCombinesSubsystemResults) {
  PopulationSummary population;
  population.housedPopulation = 120;
  population.availableHousing = 15;
  population.availableJobs = 10;
  population.unemploymentRate = 0.10f;

  TrafficSummary traffic;
  traffic.averageCommuteTime = 8.0f;
  traffic.averageEdgeCongestion = 0.35f;
  traffic.maxEdgeCongestion = 0.65f;
  traffic.totalCommuteBurden = 0.5f;

  EconomyState economy;
  economy.totalRevenue = 250000;
  economy.totalExpenses = 180000;
  economy.averageLandValue = 132.0f;
  economy.economicHealth = 0.8f;

  CityMetrics metrics = MetricsSystem::collectCityMetrics(population, traffic, economy);

  EXPECT_EQ(metrics.population, 120u);
  EXPECT_EQ(metrics.availableHousing, 15u);
  EXPECT_EQ(metrics.availableJobs, 10u);
  EXPECT_FLOAT_EQ(metrics.unemployment, 0.10f);
  EXPECT_EQ(metrics.cityRevenue, 250000);
  EXPECT_EQ(metrics.cityExpenses, 180000);
  EXPECT_FLOAT_EQ(metrics.landValueAverage, 132.0f);
  EXPECT_GT(metrics.commuteBurden, 0.0f);
  EXPECT_GT(metrics.trafficCongestion, 0.0f);
}

TEST(MetricsSystemTests, SummaryReportIncludesBudgetFields) {
  CityMetrics metrics;
  metrics.population = 80;
  metrics.cityRevenue = 90000;
  metrics.cityExpenses = 100000;

  const std::string report = MetricsSystem::createCitySummaryReport(metrics);

  EXPECT_NE(report.find("City Summary:"), std::string::npos);
  EXPECT_NE(report.find("Population: 80"), std::string::npos);
  EXPECT_NE(report.find("Budget Balance: $-10000"), std::string::npos);
  EXPECT_NE(report.find("Budget Status: Deficit"), std::string::npos);
}

TEST(MetricsSystemTests, CollectFromStoreMatchesSummarizeAndPlayable) {
  EntityStore store;
  PopulationStore population;
  store.createBuilding(BuildingType::Residential, {1, 1}, 20);
  store.createBuilding(BuildingType::Commercial, {2, 1}, 10);
  PopulationSystem::allocate(store, population, 12, 3);

  TrafficSummary traffic;
  traffic.maxEdgeCongestion = 0.4f;
  EconomyState economy;
  economy.totalRevenue = 1000;
  economy.totalExpenses = 400;
  economy.averageLandValue = 110.0f;

  CityMetrics fromStore = MetricsSystem::collectCityMetrics(store, population, traffic, economy);
  EXPECT_EQ(fromStore.population, 12u);
  EXPECT_EQ(fromStore.cityRevenue, 1000);
  EXPECT_FLOAT_EQ(fromStore.trafficCongestion, 0.4f);

  PlayableCityTickState playable;
  playable.trafficSummary = traffic;
  playable.economy = economy;
  CityMetrics fromPlayable = MetricsSystem::collectFromPlayable(store, population, playable);
  EXPECT_EQ(fromPlayable.population, fromStore.population);
  EXPECT_EQ(fromPlayable.cityRevenue, fromStore.cityRevenue);
}

TEST(MetricsSystemTests, SimulatorFinalMetricsMatchLiveStock) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  SimOptions options;
  options.runTraffic = false;
  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 20, options);
  ASSERT_FALSE(result.rows.empty());
  EXPECT_EQ(result.finalMetrics.population, result.rows.back().population);
  EXPECT_EQ(result.finalMetrics.population, population.getTotalPopulation());
}
