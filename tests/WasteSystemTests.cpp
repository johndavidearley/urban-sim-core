#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/systems/WasteSystem.hpp"

TEST(WasteSystemTests, NoFacilitiesLeavesOccupiedCityWasteUncollected) {
  EntityStore store;
  const EntityId id = store.createBuilding(BuildingType::Residential, {1, 1}, 100);
  store.getBuilding(id)->occupancy = 80;
  ServiceCoverageSummary coverage;
  coverage.totalBuildings = 1;

  const WasteSummary waste = WasteSystem::evaluate(store, {}, coverage);
  EXPECT_EQ(waste.generated, 20);
  EXPECT_EQ(waste.uncollected, 20);
  EXPECT_FLOAT_EQ(waste.collectionRate, 0.0f);
  EXPECT_GT(waste.pollutionPenalty, 0.0f);
}

TEST(WasteSystemTests, RecyclingDivertsWasteBeforeLandfill) {
  EntityStore store;
  const EntityId id = store.createBuilding(BuildingType::Residential, {1, 1}, 1000);
  store.getBuilding(id)->occupancy = 1000;
  std::vector<ServiceFacility> facilities{
    {ServiceType::Garbage, {1, 2}, 10, 1.0f},
    {ServiceType::Recycling, {2, 2}, 10, 1.0f}
  };
  ServiceCoverageSummary coverage;
  coverage.garbageCoverage = 1.0f;
  coverage.recyclingCoverage = 1.0f;

  const WasteSummary waste = WasteSystem::evaluate(store, facilities, coverage);
  EXPECT_GT(waste.recycled, 0);
  EXPECT_GT(waste.landfilled, 0);
  EXPECT_EQ(waste.uncollected, 0);
  EXPECT_FLOAT_EQ(waste.collectionRate, 1.0f);
}

TEST(WasteSystemTests, IncinerationUsesResidualWasteAndConservesMass) {
  EntityStore store;
  const auto id = store.createBuilding(BuildingType::Residential, {1,1}, 2000);
  store.getBuilding(id)->occupancy = 2000; // 500 waste units
  std::vector<ServiceFacility> facilities{
    {ServiceType::Garbage, {1,2}, 18, 1.f},
    {ServiceType::Recycling, {2,2}, 18, 1.f},
    {ServiceType::Incinerator, {3,2}, 18, 1.f}};
  ServiceCoverageSummary coverage;
  coverage.garbageCoverage = coverage.recyclingCoverage = coverage.incineratorCoverage = 1.f;
  auto waste = WasteSystem::evaluate(store, facilities, coverage);
  EXPECT_EQ(waste.recycled, 175);
  EXPECT_EQ(waste.incinerated, 300);
  EXPECT_EQ(waste.landfilled, 25);
  EXPECT_EQ(waste.uncollected, 0);
  EXPECT_FLOAT_EQ(waste.powerGenerationMW, 6.f);
  EXPECT_GT(waste.pollutionPenalty, 0.f);
  EXPECT_EQ(waste.generated, waste.recycled + waste.incinerated + waste.landfilled + waste.uncollected);
  facilities[2].quality = .5f;
  waste = WasteSystem::evaluate(store, facilities, coverage);
  EXPECT_EQ(waste.incinerated, 150);
  EXPECT_FLOAT_EQ(waste.powerGenerationMW, 3.f);
  coverage.incineratorCoverage = 0.f;
  EXPECT_EQ(WasteSystem::evaluate(store, facilities, coverage).incinerated, 0);
  store.getBuilding(id)->occupancy = 0;
  EXPECT_FLOAT_EQ(WasteSystem::evaluate(store, facilities, coverage).powerGenerationMW, 0.f);
}
