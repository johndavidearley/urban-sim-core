#include <gtest/gtest.h>
#include "src/systems/DistrictSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"

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

TEST_F(DistrictSystemTests, CanSetServiceBudgetCap) {
  DistrictId id = DistrictSystem::createDistrict("Budgeted", {0, 0}, {10, 10});

  EXPECT_TRUE(DistrictSystem::setDistrictServiceBudgetCap(id, 1200));
  const District* district = DistrictSystem::getDistrictConst(id);
  ASSERT_NE(district, nullptr);
  EXPECT_EQ(district->serviceBudgetCap, 1200);

  EXPECT_TRUE(DistrictSystem::setDistrictServiceBudgetCap(id, -1));
  EXPECT_EQ(district->serviceBudgetCap, -1);
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

TEST_F(DistrictSystemTests, DistrictTaxRatesApplyToDistrictScopedEconomy) {
  CityMap map({32, 32});
  EntityStore store;
  PopulationStore population;

  EntityId lowTaxBuildingId = store.createBuilding(BuildingType::Commercial, {5, 5}, 120);
  EntityId highTaxBuildingId = store.createBuilding(BuildingType::Commercial, {20, 20}, 120);

  Building* lowTaxBuilding = store.getBuilding(lowTaxBuildingId);
  Building* highTaxBuilding = store.getBuilding(highTaxBuildingId);
  ASSERT_NE(lowTaxBuilding, nullptr);
  ASSERT_NE(highTaxBuilding, nullptr);
  lowTaxBuilding->occupancy = 100;
  highTaxBuilding->occupancy = 100;

  population.createGroup(IncomeBand::Middle, 400, 300);

  DistrictId lowTaxDistrictId = DistrictSystem::createDistrict("LowTax", {0, 0}, {10, 10});
  DistrictId highTaxDistrictId = DistrictSystem::createDistrict("HighTax", {16, 16}, {28, 28});

  TaxRates lowRates;
  lowRates.commercialRate = 0.03f;
  EXPECT_TRUE(DistrictSystem::setDistrictTaxRates(lowTaxDistrictId, lowRates));

  TaxRates highRates;
  highRates.commercialRate = 0.15f;
  EXPECT_TRUE(DistrictSystem::setDistrictTaxRates(highTaxDistrictId, highRates));

  DistrictMetrics lowMetrics = DistrictSystem::evaluateDistrictMetrics(lowTaxDistrictId, map, store, population);
  DistrictMetrics highMetrics = DistrictSystem::evaluateDistrictMetrics(highTaxDistrictId, map, store, population);

  EXPECT_EQ(lowMetrics.commercialBuildings, 1u);
  EXPECT_EQ(highMetrics.commercialBuildings, 1u);
  EXPECT_GT(highMetrics.revenue, lowMetrics.revenue);
}

TEST_F(DistrictSystemTests, AssignedFacilitiesAndServicePrioritiesAffectDistrictCoverage) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  EntityId districtABuildingId = store.createBuilding(BuildingType::Residential, {3, 3}, 50);
  EntityId districtBBuildingId = store.createBuilding(BuildingType::Residential, {18, 18}, 50);
  map.getTile({3, 3}).buildingId = static_cast<uint32_t>(districtABuildingId);
  map.getTile({18, 18}).buildingId = static_cast<uint32_t>(districtBBuildingId);

  roads.buildRoad({3, 3}, {3, 4});
  roads.buildRoad({3, 4}, {3, 5});
  roads.buildRoad({18, 18}, {18, 17});
  roads.buildRoad({18, 17}, {18, 16});

  DistrictId districtAId = DistrictSystem::createDistrict("A", {0, 0}, {10, 10});
  DistrictId districtBId = DistrictSystem::createDistrict("B", {12, 12}, {23, 23});

  ServicePriority fireOnly;
  fireOnly.fireWeight = 1.0f;
  fireOnly.policeWeight = 0.0f;
  fireOnly.healthWeight = 0.0f;
  fireOnly.educationWeight = 0.0f;
  EXPECT_TRUE(DistrictSystem::setDistrictServicePriorities(districtAId, fireOnly));
  EXPECT_TRUE(DistrictSystem::setDistrictServiceAllocation(districtAId, 1.0f));

  ServicePriority healthOnly;
  healthOnly.fireWeight = 0.0f;
  healthOnly.policeWeight = 0.0f;
  healthOnly.healthWeight = 1.0f;
  healthOnly.educationWeight = 0.0f;
  EXPECT_TRUE(DistrictSystem::setDistrictServicePriorities(districtBId, healthOnly));
  EXPECT_TRUE(DistrictSystem::setDistrictServiceAllocation(districtBId, 1.0f));

  std::vector<ServiceFacility> facilities;
  facilities.push_back(ServiceFacility{ServiceType::Fire, {3, 5}, 5, 1.0f});
  facilities.push_back(ServiceFacility{ServiceType::Health, {18, 16}, 5, 1.0f});

  EXPECT_TRUE(DistrictSystem::assignFacilityToDistrict(districtAId, 1));
  EXPECT_TRUE(DistrictSystem::assignFacilityToDistrict(districtBId, 2));

  DistrictMetrics districtAMetrics = DistrictSystem::evaluateDistrictMetrics(
    districtAId,
    map,
    store,
    population,
    &roads,
    &facilities
  );

  DistrictMetrics districtBMetrics = DistrictSystem::evaluateDistrictMetrics(
    districtBId,
    map,
    store,
    population,
    &roads,
    &facilities
  );

  EXPECT_GT(districtAMetrics.serviceCoverage, 0.9f);
  EXPECT_GT(districtBMetrics.serviceCoverage, 0.9f);
  EXPECT_GT(districtAMetrics.happiness, 0.5f);
  EXPECT_GT(districtBMetrics.happiness, 0.5f);
}

TEST_F(DistrictSystemTests, CanUnassignPreviouslyAssignedFacility) {
  DistrictId id = DistrictSystem::createDistrict("Ops", {0, 0}, {10, 10});
  ASSERT_NE(id, 0u);

  EXPECT_TRUE(DistrictSystem::assignFacilityToDistrict(id, 7));

  const District* district = DistrictSystem::getDistrictConst(id);
  ASSERT_NE(district, nullptr);
  EXPECT_EQ(district->assignedFacilityIds.size(), 1u);
  EXPECT_EQ(*district->assignedFacilityIds.begin(), 7u);

  EXPECT_TRUE(DistrictSystem::unassignFacilityFromDistrict(id, 7));

  district = DistrictSystem::getDistrictConst(id);
  ASSERT_NE(district, nullptr);
  EXPECT_TRUE(district->assignedFacilityIds.empty());

  EXPECT_FALSE(DistrictSystem::unassignFacilityFromDistrict(id, 7));
}

TEST_F(DistrictSystemTests, ServiceBudgetCapConstrainsCoverageAndBudgetAllocation) {
  CityMap map({20, 20});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  EntityId buildingId = store.createBuilding(BuildingType::Residential, {6, 6}, 120);
  map.getTile({6, 6}).buildingId = static_cast<uint32_t>(buildingId);
  roads.buildRoad({6, 6}, {6, 7});
  roads.buildRoad({6, 7}, {6, 8});

  population.createGroup(IncomeBand::Middle, 200, 160);

  DistrictId id = DistrictSystem::createDistrict("CapTest", {0, 0}, {12, 12});
  EXPECT_TRUE(DistrictSystem::setDistrictServiceAllocation(id, 1.0f));

  ServicePriority priorities;
  priorities.fireWeight = 1.0f;
  priorities.policeWeight = 1.0f;
  priorities.healthWeight = 1.0f;
  priorities.educationWeight = 1.0f;
  EXPECT_TRUE(DistrictSystem::setDistrictServicePriorities(id, priorities));

  std::vector<ServiceFacility> facilities;
  facilities.push_back(ServiceFacility{ServiceType::Fire, {6, 8}, 6, 1.0f});
  EXPECT_TRUE(DistrictSystem::assignFacilityToDistrict(id, 1));

  DistrictMetrics uncapped = DistrictSystem::evaluateDistrictMetrics(
    id,
    map,
    store,
    population,
    &roads,
    &facilities
  );

  EXPECT_GT(uncapped.serviceBudgetTarget, 0);
  EXPECT_FALSE(uncapped.serviceBudgetCapApplied);

  EXPECT_TRUE(DistrictSystem::setDistrictServiceBudgetCap(id, 1));

  DistrictMetrics capped = DistrictSystem::evaluateDistrictMetrics(
    id,
    map,
    store,
    population,
    &roads,
    &facilities
  );

  EXPECT_TRUE(capped.serviceBudgetCapApplied);
  EXPECT_EQ(capped.serviceBudgetAllocated, 1);
  EXPECT_LT(capped.serviceCoverage, uncapped.serviceCoverage);
}

TEST_F(DistrictSystemTests, SharedPoolBalancingDistributesWithinPoolLimit) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  EntityId buildingA = store.createBuilding(BuildingType::Residential, {4, 4}, 120);
  EntityId buildingB = store.createBuilding(BuildingType::Residential, {18, 18}, 120);
  map.getTile({4, 4}).buildingId = static_cast<uint32_t>(buildingA);
  map.getTile({18, 18}).buildingId = static_cast<uint32_t>(buildingB);

  roads.buildRoad({4, 4}, {4, 5});
  roads.buildRoad({4, 5}, {4, 6});
  roads.buildRoad({18, 18}, {18, 17});
  roads.buildRoad({18, 17}, {18, 16});

  population.createGroup(IncomeBand::Middle, 500, 420);

  DistrictId idA = DistrictSystem::createDistrict("A", {0, 0}, {10, 10});
  DistrictId idB = DistrictSystem::createDistrict("B", {12, 12}, {23, 23});
  EXPECT_TRUE(DistrictSystem::setDistrictServiceAllocation(idA, 1.0f));
  EXPECT_TRUE(DistrictSystem::setDistrictServiceAllocation(idB, 1.0f));

  std::vector<ServiceFacility> facilities;
  facilities.push_back(ServiceFacility{ServiceType::Fire, {4, 6}, 6, 1.0f});
  facilities.push_back(ServiceFacility{ServiceType::Health, {18, 16}, 6, 1.0f});
  EXPECT_TRUE(DistrictSystem::assignFacilityToDistrict(idA, 1));
  EXPECT_TRUE(DistrictSystem::assignFacilityToDistrict(idB, 2));

  const int64_t sharedPool = 10;
  const std::vector<DistrictMetrics> balanced = DistrictSystem::evaluateAllDistricts(
    map,
    store,
    population,
    &roads,
    &facilities,
    sharedPool
  );

  ASSERT_EQ(balanced.size(), 2u);
  int64_t totalAllocated = 0;
  for (const DistrictMetrics& metrics : balanced) {
    totalAllocated += metrics.serviceBudgetAllocated;
    EXPECT_LE(metrics.serviceBudgetAllocated, metrics.serviceBudgetTarget);
  }
  EXPECT_LE(totalAllocated, sharedPool);
}

TEST_F(DistrictSystemTests, SharedPoolBalancingRedistributesWhenCapReached) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  EntityId buildingA = store.createBuilding(BuildingType::Residential, {4, 4}, 150);
  EntityId buildingB = store.createBuilding(BuildingType::Residential, {18, 18}, 150);
  map.getTile({4, 4}).buildingId = static_cast<uint32_t>(buildingA);
  map.getTile({18, 18}).buildingId = static_cast<uint32_t>(buildingB);

  roads.buildRoad({4, 4}, {4, 5});
  roads.buildRoad({4, 5}, {4, 6});
  roads.buildRoad({18, 18}, {18, 17});
  roads.buildRoad({18, 17}, {18, 16});

  population.createGroup(IncomeBand::Middle, 600, 500);

  DistrictId idA = DistrictSystem::createDistrict("A", {0, 0}, {10, 10});
  DistrictId idB = DistrictSystem::createDistrict("B", {12, 12}, {23, 23});
  EXPECT_TRUE(DistrictSystem::setDistrictServiceAllocation(idA, 1.0f));
  EXPECT_TRUE(DistrictSystem::setDistrictServiceAllocation(idB, 1.0f));
  EXPECT_TRUE(DistrictSystem::setDistrictServiceBudgetCap(idA, 3));

  std::vector<ServiceFacility> facilities;
  facilities.push_back(ServiceFacility{ServiceType::Fire, {4, 6}, 6, 1.0f});
  facilities.push_back(ServiceFacility{ServiceType::Fire, {18, 16}, 6, 1.0f});
  EXPECT_TRUE(DistrictSystem::assignFacilityToDistrict(idA, 1));
  EXPECT_TRUE(DistrictSystem::assignFacilityToDistrict(idB, 2));

  const int64_t sharedPool = 12;
  const std::vector<DistrictMetrics> balanced = DistrictSystem::evaluateAllDistricts(
    map,
    store,
    population,
    &roads,
    &facilities,
    sharedPool
  );

  ASSERT_EQ(balanced.size(), 2u);

  const DistrictMetrics* a = nullptr;
  const DistrictMetrics* b = nullptr;
  for (const DistrictMetrics& metrics : balanced) {
    if (metrics.districtId == idA) {
      a = &metrics;
    } else if (metrics.districtId == idB) {
      b = &metrics;
    }
  }

  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->serviceBudgetAllocated, 3);
  EXPECT_TRUE(a->serviceBudgetCapApplied);
  EXPECT_GE(b->serviceBudgetAllocated, 1);
  EXPECT_GT(b->serviceBudgetAllocated, a->serviceBudgetAllocated);
}

TEST_F(DistrictSystemTests, GrowthPressureMultiplierStaysBoundedAndCapAware) {
  District district;
  district.minCorner = {0, 0};
  district.maxCorner = {9, 9};

  DistrictMetrics metrics;
  metrics.serviceBudgetTarget = 100;
  metrics.serviceBudgetAllocated = 10;
  metrics.serviceBudgetCapApplied = true;
  metrics.buildings = 8;

  const float multiplier = DistrictSystem::computeGrowthPressureMultiplier(district, metrics);
  EXPECT_GE(multiplier, 0.45f);
  EXPECT_LE(multiplier, 1.15f);
  EXPECT_LT(multiplier, 0.85f);
}

TEST_F(DistrictSystemTests, GrowthPressureMultiplierRewardsFulfillmentForSparseDistricts) {
  District district;
  district.minCorner = {0, 0};
  district.maxCorner = {9, 9};

  DistrictMetrics low;
  low.serviceBudgetTarget = 100;
  low.serviceBudgetAllocated = 20;
  low.serviceBudgetCapApplied = false;
  low.buildings = 5;

  DistrictMetrics high = low;
  high.serviceBudgetAllocated = 90;

  const float lowMultiplier = DistrictSystem::computeGrowthPressureMultiplier(district, low);
  const float highMultiplier = DistrictSystem::computeGrowthPressureMultiplier(district, high);

  EXPECT_GT(highMultiplier, lowMultiplier);
}
