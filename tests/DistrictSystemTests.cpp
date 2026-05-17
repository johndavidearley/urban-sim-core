#include <gtest/gtest.h>
#include "src/systems/DistrictSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"

class DistrictSystemTests : public ::testing::Test {
protected:
  void SetUp() override {
    DistrictSystem::clearDistricts();
  }

  void TearDown() override {
    DistrictSystem::clearDistricts();
  }
};

// Test: Create a district
TEST_F(DistrictSystemTests, CanCreateDistrict) {
  DistrictId id = DistrictSystem::createDistrict("Downtown", {0, 0}, {10, 10});
  ASSERT_NE(id, 0u);

  const auto& districts = DistrictSystem::getDistricts();
  ASSERT_EQ(districts.size(), 1u);
  ASSERT_EQ(districts[0].id, id);
}

// Test: Invalid district boundaries are rejected
TEST_F(DistrictSystemTests, InvalidBoundariesRejected) {
  DistrictId id = DistrictSystem::createDistrict("Invalid", {10, 10}, {5, 5});
  EXPECT_EQ(id, 0u);

  const auto& districts = DistrictSystem::getDistricts();
  EXPECT_EQ(districts.size(), 0u);
}

// Test: District contains/coordinates work correctly
TEST_F(DistrictSystemTests, DistrictBoundaryChecks) {
  DistrictId id = DistrictSystem::createDistrict("Test", {5, 5}, {15, 15});
  District* district = DistrictSystem::getDistrict(id);

  ASSERT_NE(district, nullptr);
  EXPECT_TRUE(district->contains({5, 5}));
  EXPECT_TRUE(district->contains({10, 10}));
  EXPECT_TRUE(district->contains({15, 15}));
  EXPECT_FALSE(district->contains({4, 10}));
  EXPECT_FALSE(district->contains({16, 10}));
  EXPECT_FALSE(district->contains({10, 4}));
  EXPECT_FALSE(district->contains({10, 16}));
}

// Test: District area calculation
TEST_F(DistrictSystemTests, DistrictAreaCalculation) {
  DistrictId id = DistrictSystem::createDistrict("Test", {0, 0}, {9, 9});
  District* district = DistrictSystem::getDistrict(id);

  ASSERT_NE(district, nullptr);
  EXPECT_EQ(district->width(), 10);
  EXPECT_EQ(district->height(), 10);
  EXPECT_EQ(district->area(), 100);
}

// Test: Delete district
TEST_F(DistrictSystemTests, CanDeleteDistrict) {
  DistrictId id = DistrictSystem::createDistrict("Temp", {0, 0}, {10, 10});
  EXPECT_NE(id, 0u);

  EXPECT_TRUE(DistrictSystem::deleteDistrict(id));
  EXPECT_EQ(DistrictSystem::getDistricts().size(), 0u);
  EXPECT_FALSE(DistrictSystem::deleteDistrict(id)); // Already deleted
}

// Test: Set tax rates for district
TEST_F(DistrictSystemTests, CanSetDistrictTaxRates) {
  DistrictId id = DistrictSystem::createDistrict("Test", {0, 0}, {10, 10});

  TaxRates rates;
  rates.residentialRate = 0.10f;
  rates.commercialRate = 0.12f;

  EXPECT_TRUE(DistrictSystem::setDistrictTaxRates(id, rates));

  const District* district = DistrictSystem::getDistrictConst(id);
  ASSERT_NE(district, nullptr);
  EXPECT_EQ(district->taxRates.residentialRate, 0.10f);
  EXPECT_EQ(district->taxRates.commercialRate, 0.12f);
}

// Test: Set service allocation for district
TEST_F(DistrictSystemTests, CanSetServiceAllocation) {
  DistrictId id = DistrictSystem::createDistrict("Test", {0, 0}, {10, 10});

  EXPECT_TRUE(DistrictSystem::setDistrictServiceAllocation(id, 0.75f));

  const District* district = DistrictSystem::getDistrictConst(id);
  ASSERT_NE(district, nullptr);
  EXPECT_EQ(district->serviceAllocation, 0.75f);
}

// Test: Service allocation clamping
TEST_F(DistrictSystemTests, ServiceAllocationClamping) {
  DistrictId id = DistrictSystem::createDistrict("Test", {0, 0}, {10, 10});

  DistrictSystem::setDistrictServiceAllocation(id, 1.5f);
  const District* district = DistrictSystem::getDistrictConst(id);
  EXPECT_EQ(district->serviceAllocation, 1.0f);

  DistrictSystem::setDistrictServiceAllocation(id, -0.5f);
  EXPECT_EQ(district->serviceAllocation, 0.0f);
}

// Test: Multiple districts can exist simultaneously
TEST_F(DistrictSystemTests, MultipleDistrictsCoexist) {
  DistrictId id1 = DistrictSystem::createDistrict("D1", {0, 0}, {10, 10});
  DistrictId id2 = DistrictSystem::createDistrict("D2", {15, 15}, {25, 25});
  DistrictId id3 = DistrictSystem::createDistrict("D3", {5, 20}, {15, 30});

  const auto& districts = DistrictSystem::getDistricts();
  EXPECT_EQ(districts.size(), 3u);

  EXPECT_NE(id1, id2);
  EXPECT_NE(id2, id3);
  EXPECT_NE(id1, id3);
}

// Test: Evaluate district metrics with empty city
TEST_F(DistrictSystemTests, EvaluateDistrictMetricsEmptyCity) {
  CityMap map({32, 32});
  EntityStore store;
  PopulationStore population;

  DistrictId id = DistrictSystem::createDistrict("Empty", {5, 5}, {10, 10});

  DistrictMetrics metrics = DistrictSystem::evaluateDistrictMetrics(
    id, map, store, population
  );

  EXPECT_EQ(metrics.districtId, id);
  EXPECT_EQ(metrics.buildings, 0u);
  EXPECT_EQ(metrics.population, 0u);
}

// Test: Evaluate district with buildings
TEST_F(DistrictSystemTests, EvaluateDistrictMetricsWithBuildings) {
  CityMap map({32, 32});
  EntityStore store;
  PopulationStore population;

  // Create buildings in district bounds
  store.createBuilding(BuildingType::Residential, {5, 5}, 100);
  store.createBuilding(BuildingType::Commercial, {6, 6}, 50);
  store.createBuilding(BuildingType::Industrial, {7, 7}, 75);

  // Create building outside district
  store.createBuilding(BuildingType::Residential, {20, 20}, 100);

  DistrictId id = DistrictSystem::createDistrict("Mixed", {5, 5}, {10, 10});

  DistrictMetrics metrics = DistrictSystem::evaluateDistrictMetrics(
    id, map, store, population
  );

  EXPECT_EQ(metrics.buildings, 3u);
  EXPECT_EQ(metrics.residentialBuildings, 1u);
  EXPECT_EQ(metrics.commercialBuildings, 1u);
  EXPECT_EQ(metrics.industrialBuildings, 1u);
}

// Test: Evaluate all districts
TEST_F(DistrictSystemTests, EvaluateAllDistricts) {
  CityMap map({32, 32});
  EntityStore store;
  PopulationStore population;

  DistrictId id1 = DistrictSystem::createDistrict("D1", {0, 0}, {10, 10});
  DistrictId id2 = DistrictSystem::createDistrict("D2", {15, 15}, {20, 20});

  store.createBuilding(BuildingType::Residential, {5, 5}, 100);
  store.createBuilding(BuildingType::Commercial, {16, 16}, 50);

  std::vector<DistrictMetrics> allMetrics = DistrictSystem::evaluateAllDistricts(
    map, store, population
  );

  EXPECT_EQ(allMetrics.size(), 2u);
  EXPECT_EQ(allMetrics[0].districtId, id1);
  EXPECT_EQ(allMetrics[0].buildings, 1u);
  EXPECT_EQ(allMetrics[1].districtId, id2);
  EXPECT_EQ(allMetrics[1].buildings, 1u);
}
