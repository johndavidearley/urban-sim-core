#include "gtest/gtest.h"

#include "src/gameplay/DistrictTool.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/world/CityMap.hpp"

TEST(DistrictToolTests, PlansInclusiveRectangleAndAppliesArchetype) {
  CityMap map({12, 12});
  DistrictSystem districts;

  const DistrictPlan plan = DistrictTool::plan(
    map, districts, {2, 3}, {5, 6}, DistrictArchetype::Industrial);

  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.minCorner, Coord(2, 3));
  EXPECT_EQ(plan.maxCorner, Coord(5, 6));
  EXPECT_EQ(plan.tiles.size(), 16u);
  ASSERT_TRUE(DistrictTool::apply(districts, map, plan));
  ASSERT_EQ(districts.getDistricts().size(), 1u);
  EXPECT_EQ(districts.getDistricts()[0].archetype, DistrictArchetype::Industrial);
  EXPECT_FALSE(districts.getDistricts()[0].allowsZone(ZoneType::Residential));
  EXPECT_FALSE(districts.getDistricts()[0].allowsZone(ZoneType::Office));
  EXPECT_TRUE(districts.getDistricts()[0].allowsZone(ZoneType::Industrial));
}

TEST(DistrictToolTests, RejectsOutOfBounds) {
  CityMap map({8, 8});
  DistrictSystem districts;
  EXPECT_FALSE(DistrictTool::plan(
    map, districts, {-1, 0}, {2, 2}, DistrictArchetype::General).valid);
}

TEST(DistrictToolTests, ReapplyingSameBoundsRemovesDistrict) {
  CityMap map({8, 8});
  DistrictSystem districts;
  const DistrictPlan first = DistrictTool::plan(
    map, districts, {1, 1}, {3, 3}, DistrictArchetype::TechHub);
  ASSERT_TRUE(DistrictTool::apply(districts, map, first));
  ASSERT_EQ(districts.getDistricts().size(), 1u);
  const DistrictId id = districts.getDistricts()[0].id;

  const DistrictPlan removal = DistrictTool::plan(
    map, districts, {3, 3}, {1, 1}, DistrictArchetype::General);
  ASSERT_TRUE(removal.valid) << removal.error;
  EXPECT_EQ(removal.removeId, id);
  ASSERT_TRUE(DistrictTool::apply(districts, map, removal));
  EXPECT_TRUE(districts.getDistricts().empty());
}

TEST(DistrictToolTests, DistrictAtReturnsTopMostContainingDistrict) {
  CityMap map({8, 8});
  DistrictSystem districts;
  const DistrictId outer = districts.createDistrict("Outer", {1, 1}, {6, 6});
  const DistrictId inner = districts.createDistrict("Inner", {2, 2}, {4, 4});
  EXPECT_EQ(DistrictTool::districtAt(districts, {0, 0}), 0u);
  EXPECT_EQ(DistrictTool::districtAt(districts, {1, 1}), outer);
  EXPECT_EQ(DistrictTool::districtAt(districts, {3, 3}), inner);
}

TEST(DistrictToolTests, ApplyRefusesWhenWorldDiverged) {
  CityMap map({8, 8});
  DistrictSystem districts;
  DistrictPlan plan = DistrictTool::plan(
    map, districts, {1, 1}, {2, 2}, DistrictArchetype::General);
  ASSERT_TRUE(plan.valid);
  plan.tiles.push_back({4, 4});
  EXPECT_FALSE(DistrictTool::apply(districts, map, plan));
  EXPECT_TRUE(districts.getDistricts().empty());
}
