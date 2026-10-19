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

TEST(RoadToolTests,
     ArterialUpgradeChargesOnlyLocalSegmentsAndPreservesTopology) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  int64_t funds = 10000;
  auto local = RoadTool::plan(map, roads, {2, 8}, {6, 8}, funds);
  ASSERT_TRUE(RoadTool::build(map, roads, local, funds));
  const auto topology = roads.getTopologyVersion();
  roads.updateCongestion({2, 8}, {3, 8}, 20.0f);
  const auto upgrade =
      RoadTool::plan(map, roads, {2, 8}, {6, 8}, funds, RoadClass::Arterial);
  ASSERT_TRUE(upgrade.valid);
  EXPECT_EQ(upgrade.newSegments, 0);
  EXPECT_EQ(upgrade.upgradedSegments, 4);
  EXPECT_EQ(upgrade.cost, 5600);
  ASSERT_TRUE(RoadTool::build(map, roads, upgrade, funds));
  EXPECT_EQ(funds, 4000);
  EXPECT_EQ(roads.getTopologyVersion(), topology);
  EXPECT_EQ(roads.getRoadClass({2, 8}, {3, 8}), RoadClass::Arterial);
  EXPECT_FLOAT_EQ(roads.getEdgeCapacity({2, 8}, {3, 8}), kArterialRoadCapacity);
  EXPECT_FLOAT_EQ(roads.getCongestion({2, 8}, {3, 8}), 20.0f / kArterialRoadCapacity);
  // Autonomous re-laying and local tool selection never downgrade an arterial.
  roads.buildRoad({2, 8}, {3, 8});
  EXPECT_EQ(roads.getRoadClass({2, 8}, {3, 8}), RoadClass::Arterial);
  EXPECT_FALSE(RoadTool::plan(map, roads, {2, 8}, {6, 8}, funds).valid);
  EXPECT_FALSE(
      RoadTool::plan(map, roads, {2, 8}, {6, 8}, funds, RoadClass::Arterial)
          .valid);
}

TEST(RoadToolTests,
     ArterialMixedRouteCostsAndInsufficientFundsAreTransactional) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({2, 8}, {3, 8});
  const auto mixed =
      RoadTool::plan(map, roads, {2, 8}, {4, 8}, 5000, RoadClass::Arterial);
  ASSERT_TRUE(mixed.valid);
  EXPECT_EQ(mixed.cost, 2900);
  int64_t funds = 2899;
  EXPECT_FALSE(RoadTool::build(map, roads, mixed, funds));
  EXPECT_EQ(funds, 2899);
  EXPECT_EQ(roads.getRoadClass({2, 8}, {3, 8}), RoadClass::Local);
  EXPECT_FALSE(roads.hasRoad({3, 8}, {4, 8}));
  funds = 5000;
  ASSERT_TRUE(RoadTool::build(map, roads, mixed, funds));
  EXPECT_EQ(funds, 2100);
  EXPECT_EQ(roads.getRoadClass({3, 8}, {4, 8}), RoadClass::Arterial);
}
