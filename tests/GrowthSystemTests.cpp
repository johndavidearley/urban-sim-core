#include <gtest/gtest.h>

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/world/Zoning.hpp"

TEST(GrowthSystemTests, SpawnsWhenZonedAndRoadAccessible) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_EQ(roads.getRoadCount(), 1);
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {2, 3}, ZoneType::Residential));

  ZoneDemand demand;
  demand.residential = 1.0f;
  demand.commercial = 0.0f;
  demand.industrial = 0.0f;

  const GrowthStats stats = GrowthSystem::runStep(map, roads, store, demand, 7, 1.0f);

  EXPECT_EQ(stats.spawnedResidential, 1);
  EXPECT_EQ(stats.totalSpawned(), 1);
  EXPECT_EQ(store.getBuildingCount(), 1u);
  EXPECT_TRUE(EntityIdUtils::isValid(map.getTile({2, 3}).buildingId));
}

TEST(GrowthSystemTests, NoSpawnWithoutRoadAccess) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  EXPECT_TRUE(Zoning::applyZoneRect(map, {5, 5}, {5, 5}, ZoneType::Commercial));

  ZoneDemand demand;
  demand.residential = 0.0f;
  demand.commercial = 1.0f;
  demand.industrial = 0.0f;

  const GrowthStats stats = GrowthSystem::runStep(map, roads, store, demand, 11, 1.0f);

  EXPECT_EQ(stats.totalSpawned(), 0);
  EXPECT_EQ(store.getBuildingCount(), 0u);
  EXPECT_EQ(map.getTile({5, 5}).buildingId, EntityIdUtils::NullEntity);
}

TEST(GrowthSystemTests, NoSpawnForUnzonedTiles) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({1, 1}, {2, 1});
  EXPECT_EQ(roads.getRoadCount(), 1);

  ZoneDemand demand;
  demand.residential = 1.0f;
  demand.commercial = 1.0f;
  demand.industrial = 1.0f;

  const GrowthStats stats = GrowthSystem::runStep(map, roads, store, demand, 13, 1.0f);

  EXPECT_EQ(stats.totalSpawned(), 0);
  EXPECT_EQ(store.getBuildingCount(), 0u);
}

TEST(GrowthSystemTests, DeterministicForSameSeed) {
  CityMap mapA({8, 8});
  CityMap mapB({8, 8});
  RoadNetwork roadsA(mapA);
  RoadNetwork roadsB(mapB);
  EntityStore storeA;
  EntityStore storeB;

  roadsA.buildRoad({1, 1}, {2, 1});
  roadsA.buildRoad({2, 1}, {3, 1});
  roadsB.buildRoad({1, 1}, {2, 1});
  roadsB.buildRoad({2, 1}, {3, 1});
  EXPECT_EQ(roadsA.getRoadCount(), 2);
  EXPECT_EQ(roadsB.getRoadCount(), 2);

  EXPECT_TRUE(Zoning::applyZoneRect(mapA, {1, 2}, {3, 2}, ZoneType::Industrial));
  EXPECT_TRUE(Zoning::applyZoneRect(mapB, {1, 2}, {3, 2}, ZoneType::Industrial));

  ZoneDemand demand;
  demand.residential = 0.0f;
  demand.commercial = 0.0f;
  demand.industrial = 0.6f;

  const GrowthStats first = GrowthSystem::runStep(mapA, roadsA, storeA, demand, 99, 0.8f);
  const GrowthStats second = GrowthSystem::runStep(mapB, roadsB, storeB, demand, 99, 0.8f);

  EXPECT_EQ(first.totalSpawned(), second.totalSpawned());
  EXPECT_EQ(first.spawnedIndustrial, second.spawnedIndustrial);
  EXPECT_EQ(storeA.getBuildingCount(), storeB.getBuildingCount());
}
