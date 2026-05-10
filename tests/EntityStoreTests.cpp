#include <gtest/gtest.h>

#include "src/entities/EntityStore.hpp"

TEST(EntityStoreTests, CreateBuilding) {
  EntityStore store;
  const EntityId id = store.createBuilding(BuildingType::Residential, {2, 3}, 10);

  EXPECT_TRUE(EntityIdUtils::isValid(id));
  EXPECT_EQ(store.getBuildingCount(), 1u);

  const Building* building = store.getBuilding(id);
  ASSERT_NE(building, nullptr);
  EXPECT_EQ(building->type, BuildingType::Residential);
  EXPECT_EQ(building->position.x, 2);
  EXPECT_EQ(building->position.y, 3);
  EXPECT_EQ(building->capacity, 10);
  EXPECT_EQ(building->occupancy, 0);
}

TEST(EntityStoreTests, CreateMultipleBuildingsHaveDifferentIds) {
  EntityStore store;

  const EntityId a = store.createBuilding(BuildingType::Residential, {1, 1}, 8);
  const EntityId b = store.createBuilding(BuildingType::Commercial, {4, 5}, 20);

  EXPECT_NE(a, b);
  EXPECT_EQ(store.getBuildingCount(), 2u);
}

TEST(EntityStoreTests, GetMissingBuildingReturnsNull) {
  EntityStore store;

  EXPECT_EQ(store.getBuilding(EntityIdUtils::NullEntity), nullptr);
  EXPECT_EQ(store.getBuilding(999999), nullptr);
}

TEST(EntityStoreTests, MutableAccessUpdatesOccupancy) {
  EntityStore store;
  const EntityId id = store.createBuilding(BuildingType::Industrial, {6, 2}, 30);

  Building* building = store.getBuilding(id);
  ASSERT_NE(building, nullptr);
  building->occupancy = 12;

  const Building* same = store.getBuilding(id);
  ASSERT_NE(same, nullptr);
  EXPECT_EQ(same->occupancy, 12);
}
