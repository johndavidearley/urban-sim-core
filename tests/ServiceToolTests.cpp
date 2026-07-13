#include "gtest/gtest.h"

#include "src/gameplay/ServiceTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

TEST(ServiceToolTests, PlansRoadAccessibleFacilityWithTypeSpecificCostAndRange) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({4, 4}, {5, 4});
  std::vector<ServiceFacility> facilities;

  const ServicePlan plan = ServiceTool::plan(
    map, roads, facilities, ServiceType::Fire, {4, 3}, 10000
  );

  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.cost, 5000);
  EXPECT_EQ(plan.facility.maxTravelDistance, 16);
  EXPECT_EQ(plan.facility.position, Coord(4, 3));
}

TEST(ServiceToolTests, RejectsBlockedDuplicateDisconnectedAndUnaffordableSites) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({4, 4}, {5, 4});
  std::vector<ServiceFacility> facilities;

  map.getTile({4, 3}).type = 2;
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Fire, {4, 3}, 10000).valid);
  map.getTile({4, 3}).type = 0;
  map.getTile({4, 3}).buildingId = 9;
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Fire, {4, 3}, 10000).valid);
  map.getTile({4, 3}).buildingId = 0;
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Fire, {10, 10}, 10000).valid);

  facilities.push_back({ServiceType::Police, {4, 3}, 14, 1.0f});
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Health, {4, 3}, 10000).valid);
  EXPECT_FALSE(ServiceTool::plan(map, roads, {}, ServiceType::Health, {4, 3}, 100).valid);
}

TEST(ServiceToolTests, BuildsFacilityAndDeductsFunds) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({4, 4}, {5, 4});
  std::vector<ServiceFacility> facilities;
  int64_t funds = 10000;
  const ServicePlan plan = ServiceTool::plan(
    map, roads, facilities, ServiceType::Education, {4, 3}, funds
  );

  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  ASSERT_EQ(facilities.size(), 1u);
  EXPECT_EQ(facilities.front().type, ServiceType::Education);
  EXPECT_EQ(funds, 4500);
}

TEST(ServiceToolTests, CivicTypesHaveDistinctPositiveOperatingCosts) {
  EXPECT_GT(ServiceTool::operatingCostPerTick(ServiceType::Fire), 0);
  EXPECT_GT(ServiceTool::operatingCostPerTick(ServiceType::Police), 0);
  EXPECT_GT(ServiceTool::operatingCostPerTick(ServiceType::Health), 0);
  EXPECT_GT(ServiceTool::operatingCostPerTick(ServiceType::Education), 0);
  EXPECT_NE(ServiceTool::operatingCostPerTick(ServiceType::Fire),
            ServiceTool::operatingCostPerTick(ServiceType::Health));
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Power), 12000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Water), 8000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Sanitation), 7000);
  EXPECT_GT(ServiceTool::coverageDistance(ServiceType::Power), 0);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Garbage), 9000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Recycling), 11000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Cemetery), 10000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Crematorium), 14000);
}
