#include "gtest/gtest.h"

#include <algorithm>

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

TEST(EntityStoreTests, StructuralMutationsAdvanceVersion) {
  EntityStore store;
  const uint64_t initial = store.getMutationVersion();

  const EntityId first = store.createBuilding(BuildingType::Residential, {1, 1}, 8);
  EXPECT_GT(store.getMutationVersion(), initial);
  const uint64_t afterCreate = store.getMutationVersion();

  EXPECT_FALSE(store.removeBuilding(999999));
  EXPECT_EQ(store.getMutationVersion(), afterCreate);

  ASSERT_TRUE(store.removeBuilding(first));
  EXPECT_GT(store.getMutationVersion(), afterCreate);
  const uint64_t afterRemove = store.getMutationVersion();

  Building replacement;
  replacement.id = first;
  replacement.type = BuildingType::Commercial;
  replacement.position = {7, 7};
  replacement.capacity = 20;
  store.upsertBuilding(replacement);
  EXPECT_GT(store.getMutationVersion(), afterRemove);
}

TEST(EntityStoreTests, TypeIndicesAndCapacityAggregates) {
  EntityStore store;
  const EntityId r1 = store.createBuilding(BuildingType::Residential, {1, 1}, 10);
  const EntityId r2 = store.createBuilding(BuildingType::Residential, {2, 2}, 6);
  const EntityId c1 = store.createBuilding(BuildingType::Commercial, {3, 3}, 20);
  const EntityId i1 = store.createBuilding(BuildingType::Industrial, {4, 4}, 30);
  const EntityId o1 = store.createBuilding(BuildingType::Office, {5, 5}, 18);

  EXPECT_EQ(store.countOfType(BuildingType::Residential), 2u);
  EXPECT_EQ(store.capacityOfType(BuildingType::Residential), 16u);
  EXPECT_EQ(store.countOfType(BuildingType::Commercial), 1u);
  EXPECT_EQ(store.capacityOfType(BuildingType::Commercial), 20u);
  EXPECT_EQ(store.jobIds().size(), 3u);

  // IDs stay sorted within each type list and the job list.
  const auto& resIds = store.idsByBuildingType(BuildingType::Residential);
  ASSERT_EQ(resIds.size(), 2u);
  EXPECT_LT(resIds[0], resIds[1]);
  const auto& jobs = store.jobIds();
  EXPECT_TRUE(std::is_sorted(jobs.begin(), jobs.end()));
  EXPECT_NE(std::find(jobs.begin(), jobs.end(), c1), jobs.end());
  EXPECT_NE(std::find(jobs.begin(), jobs.end(), i1), jobs.end());
  EXPECT_NE(std::find(jobs.begin(), jobs.end(), o1), jobs.end());
  EXPECT_EQ(std::find(jobs.begin(), jobs.end(), r1), jobs.end());

  ASSERT_TRUE(store.removeBuilding(r1));
  EXPECT_EQ(store.countOfType(BuildingType::Residential), 1u);
  EXPECT_EQ(store.capacityOfType(BuildingType::Residential), 6u);
  EXPECT_EQ(store.idsByBuildingType(BuildingType::Residential).size(), 1u);
  EXPECT_EQ(store.idsByBuildingType(BuildingType::Residential)[0], r2);

  // Type change via upsert moves indices and capacity between buckets.
  Building moved = *store.getBuilding(c1);
  moved.type = BuildingType::Residential;
  moved.capacity = 12;
  store.upsertBuilding(moved);
  EXPECT_EQ(store.countOfType(BuildingType::Commercial), 0u);
  EXPECT_EQ(store.capacityOfType(BuildingType::Commercial), 0u);
  EXPECT_EQ(store.countOfType(BuildingType::Residential), 2u);
  EXPECT_EQ(store.capacityOfType(BuildingType::Residential), 18u);
  EXPECT_EQ(store.jobIds().size(), 2u);

  store.clear();
  EXPECT_EQ(store.getBuildingCount(), 0u);
  EXPECT_EQ(store.countOfType(BuildingType::Residential), 0u);
  EXPECT_TRUE(store.jobIds().empty());
}
