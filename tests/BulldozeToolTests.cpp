#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

TEST(BulldozeToolTests, PlansBuildingsZonesAndUniqueRoadEdges) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  std::vector<ServiceFacility> facilities;
  roads.buildRoad({1, 1}, {2, 1});
  roads.buildRoad({2, 1}, {3, 1});
  Zoning::applyZoneRect(map, {1, 1}, {2, 1}, ZoneType::Residential);
  const EntityId id = store.createBuilding(BuildingType::Residential, {1, 1}, 10);
  map.getTile({1, 1}).buildingId = id;

  facilities.push_back({ServiceType::Fire, {2, 1}, 16, 1.0f});
  const BulldozePlan plan = BulldozeTool::plan(map, roads, facilities, {1, 1}, {2, 1}, 1000);

  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.buildings, 1);
  EXPECT_EQ(plan.services, 1);
  EXPECT_EQ(plan.zonedTiles, 2);
  EXPECT_EQ(plan.roadEdges.size(), 2u);
  EXPECT_EQ(plan.cost, 350);
}

TEST(BulldozeToolTests, AppliesDemolitionAndSynchronizesRoadTileFlags) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  std::vector<ServiceFacility> facilities;
  roads.buildRoad({1, 1}, {2, 1});
  Zoning::applyZoneRect(map, {1, 1}, {1, 1}, ZoneType::Commercial);
  const EntityId id = store.createBuilding(BuildingType::Commercial, {1, 1}, 10);
  map.getTile({1, 1}).buildingId = id;
  int64_t funds = 1000;

  facilities.push_back({ServiceType::Police, {1, 1}, 14, 1.0f});
  const BulldozePlan plan = BulldozeTool::plan(map, roads, facilities, {1, 1}, {1, 1}, funds);
  ASSERT_TRUE(BulldozeTool::apply(map, roads, store, facilities, plan, funds));
  EXPECT_EQ(store.getBuildingCount(), 0u);
  EXPECT_EQ(map.getTile({1, 1}).buildingId, 0u);
  EXPECT_EQ(map.zone({1, 1}), static_cast<int>(ZoneType::None));
  EXPECT_FALSE(roads.hasRoad({1, 1}, {2, 1}));
  EXPECT_FALSE(map.getTile({1, 1}).hasRoad);
  EXPECT_FALSE(map.getTile({2, 1}).hasRoad);
  EXPECT_TRUE(facilities.empty());
  EXPECT_EQ(funds, 675);
}

TEST(BulldozeToolTests, RejectsEmptyAndUnaffordableAreas) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  std::vector<ServiceFacility> facilities;
  EXPECT_FALSE(BulldozeTool::plan(map, roads, facilities, {1, 1}, {2, 2}, 1000).valid);

  map.setZone({1, 1}, static_cast<int>(ZoneType::Residential));
  EXPECT_FALSE(BulldozeTool::plan(map, roads, facilities, {1, 1}, {1, 1}, 0).valid);
}

TEST(BulldozeToolTests, ServiceAloneIsDemolishableAndStopsExisting) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  std::vector<ServiceFacility> facilities{{ServiceType::Health, {3, 3}, 18, 1.0f}};
  int64_t funds = 500;

  const BulldozePlan plan = BulldozeTool::plan(map, roads, facilities, {3, 3}, {3, 3}, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.services, 1);
  EXPECT_EQ(plan.cost, BulldozeTool::kCostPerService);
  ASSERT_TRUE(BulldozeTool::apply(map, roads, store, facilities, plan, funds));
  EXPECT_TRUE(facilities.empty());
  EXPECT_EQ(funds, 400);
}
