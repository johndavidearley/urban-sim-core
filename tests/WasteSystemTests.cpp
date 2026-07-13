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
