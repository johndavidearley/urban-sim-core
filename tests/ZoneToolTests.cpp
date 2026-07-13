#include "gtest/gtest.h"

#include "src/gameplay/ZoneTool.hpp"
#include "src/world/CityMap.hpp"

TEST(ZoneToolTests, PlansRectangleAndChargesOnlyChangedTiles) {
  CityMap map({8, 8});
  map.setZone({1, 1}, static_cast<int>(ZoneType::Residential));

  const ZonePlan plan = ZoneTool::plan(map, {1, 1}, {2, 2}, ZoneType::Residential, 1000);

  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.tiles.size(), 4u);
  EXPECT_EQ(plan.changedTiles, 3);
  EXPECT_EQ(plan.cost, 75);
}

TEST(ZoneToolTests, RejectsWaterRoadsOccupiedRezonesAndUnaffordablePlans) {
  CityMap map({8, 8});
  map.getTile({2, 2}).type = 2;
  EXPECT_FALSE(ZoneTool::plan(map, {1, 1}, {2, 2}, ZoneType::Residential, 1000).valid);

  map.getTile({2, 2}).type = 0;
  map.getTile({2, 2}).hasRoad = true;
  EXPECT_FALSE(ZoneTool::plan(map, {1, 1}, {2, 2}, ZoneType::Residential, 1000).valid);

  map.getTile({2, 2}).hasRoad = false;
  map.getTile({2, 2}).buildingId = 9;
  map.setZone({2, 2}, static_cast<int>(ZoneType::Commercial));
  EXPECT_FALSE(ZoneTool::plan(map, {2, 2}, {2, 2}, ZoneType::Residential, 1000).valid);

  map.getTile({2, 2}).buildingId = 0;
  EXPECT_FALSE(ZoneTool::plan(map, {1, 1}, {2, 2}, ZoneType::Industrial, 50).valid);
}

TEST(ZoneToolTests, AppliesZoneLandValueAndCost) {
  CityMap map({8, 8});
  int64_t funds = 100;
  const ZonePlan plan = ZoneTool::plan(map, {1, 1}, {2, 1}, ZoneType::Office, funds);

  ASSERT_TRUE(ZoneTool::apply(map, plan, funds));
  EXPECT_EQ(map.zone({1, 1}), static_cast<int>(ZoneType::Office));
  EXPECT_EQ(map.zone({2, 1}), static_cast<int>(ZoneType::Office));
  EXPECT_FLOAT_EQ(map.landValue({1, 1}), Zoning::defaultLandValueForZone(ZoneType::Office));
  EXPECT_EQ(funds, 50);
}
