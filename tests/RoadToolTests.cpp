#include "gtest/gtest.h"

#include "src/gameplay/RoadTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

TEST(RoadToolTests, PlansDeterministicManhattanRouteAndChargesPerNewSegment) {
  CityMap map({8, 8});
  RoadNetwork roads(map);

  const RoadPlan plan = RoadTool::plan(map, roads, {1, 1}, {3, 3}, 1000);

  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.tiles, (std::vector<Coord>{{1, 1}, {2, 1}, {3, 1}, {3, 2}, {3, 3}}));
  EXPECT_EQ(plan.newSegments, 4);
  EXPECT_EQ(plan.cost, 400);
}

TEST(RoadToolTests, RejectsWaterBuildingsAndUnaffordableRoutes) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  map.getTile({2, 1}).type = 2;
  EXPECT_FALSE(RoadTool::plan(map, roads, {1, 1}, {3, 1}, 1000).valid);

  map.getTile({2, 1}).type = 0;
  map.getTile({2, 1}).buildingId = 42;
  EXPECT_FALSE(RoadTool::plan(map, roads, {1, 1}, {3, 1}, 1000).valid);

  map.getTile({2, 1}).buildingId = 0;
  EXPECT_FALSE(RoadTool::plan(map, roads, {1, 1}, {3, 1}, 100).valid);
}

TEST(RoadToolTests, BuildsRoadAndDeductsFundsWithoutChargingExistingSegments) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  roads.buildRoad({1, 1}, {2, 1});
  int64_t funds = 500;

  const RoadPlan plan = RoadTool::plan(map, roads, {1, 1}, {3, 1}, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.newSegments, 1);
  EXPECT_EQ(plan.cost, 100);
  ASSERT_TRUE(RoadTool::build(map, roads, plan, funds));
  EXPECT_EQ(funds, 400);
  EXPECT_TRUE(roads.hasRoad({2, 1}, {3, 1}));
}
