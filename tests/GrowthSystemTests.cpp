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

TEST(GrowthSystemTests, VeryLowDemandSuppressesGrowth) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {3, 3}, ZoneType::Residential));

  ZoneDemand demand;
  demand.residential = 0.01f;
  demand.commercial = 0.0f;
  demand.industrial = 0.0f;

  const GrowthStats stats = GrowthSystem::runStep(map, roads, store, demand, 5, 1.0f);

  EXPECT_EQ(stats.totalSpawned(), 0);
  EXPECT_EQ(store.getBuildingCount(), 0u);
}

TEST(GrowthSystemTests, SaturatedZonePreventsMoreGrowth) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;

  // Ensure all zoned tiles are road-accessible.
  roads.buildRoad({1, 1}, {2, 1});
  roads.buildRoad({2, 1}, {3, 1});
  roads.buildRoad({3, 1}, {4, 1});
  roads.buildRoad({4, 1}, {5, 1});
  roads.buildRoad({5, 1}, {6, 1});
  roads.buildRoad({6, 1}, {7, 1});
  roads.buildRoad({7, 1}, {8, 1});
  roads.buildRoad({8, 1}, {9, 1});

  EXPECT_TRUE(Zoning::applyZoneRect(map, {1, 2}, {10, 2}, ZoneType::Industrial));

  // Pre-fill 9/10 tiles with existing buildings to simulate near-saturation.
  for (int x = 1; x <= 9; ++x) {
    map.getTile({x, 2}).buildingId = static_cast<EntityId>(1000 + x);
  }

  ZoneDemand demand;
  demand.residential = 0.0f;
  demand.commercial = 0.0f;
  demand.industrial = 0.1f;

  const GrowthStats stats = GrowthSystem::runStep(map, roads, store, demand, 9, 1.0f);

  EXPECT_EQ(stats.totalSpawned(), 0);
  EXPECT_EQ(store.getBuildingCount(), 0u);
  EXPECT_EQ(map.getTile({10, 2}).buildingId, EntityIdUtils::NullEntity);
}

TEST(GrowthSystemTests, MultiStepGrowthSpawnsAndStabilizes) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  EntityStore store;

  for (int x = 2; x < 13; ++x) {
    roads.buildRoad({x, 2}, {x + 1, 2});
  }

  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {13, 3}, ZoneType::Residential));

  int totalSpawned = 0;
  for (int step = 0; step < 60; ++step) {
    ZoneDemand demand;
    demand.residential = 0.45f;
    demand.commercial = 0.0f;
    demand.industrial = 0.0f;

    const GrowthStats stats = GrowthSystem::runStep(
      map, roads, store, demand, static_cast<uint32_t>(100 + step), 1.0f
    );
    totalSpawned += stats.totalSpawned();
  }

  // Demand/pressure tuning should allow growth, but stop before full saturation.
  EXPECT_GT(totalSpawned, 0);
  EXPECT_LE(totalSpawned, 6);
  EXPECT_EQ(static_cast<int>(store.getBuildingCount()), totalSpawned);
}

TEST(GrowthSystemTests, MultiStepGrowthIsDeterministicAcrossRuns) {
  CityMap mapA({12, 12});
  CityMap mapB({12, 12});
  RoadNetwork roadsA(mapA);
  RoadNetwork roadsB(mapB);
  EntityStore storeA;
  EntityStore storeB;

  for (int x = 1; x < 10; ++x) {
    roadsA.buildRoad({x, 1}, {x + 1, 1});
    roadsB.buildRoad({x, 1}, {x + 1, 1});
  }

  EXPECT_TRUE(Zoning::applyZoneRect(mapA, {1, 2}, {10, 2}, ZoneType::Commercial));
  EXPECT_TRUE(Zoning::applyZoneRect(mapB, {1, 2}, {10, 2}, ZoneType::Commercial));

  int spawnedA = 0;
  int spawnedB = 0;
  for (int step = 0; step < 40; ++step) {
    ZoneDemand demand;
    demand.residential = 0.0f;
    demand.commercial = 0.35f;
    demand.industrial = 0.0f;

    spawnedA += GrowthSystem::runStep(
      mapA, roadsA, storeA, demand, static_cast<uint32_t>(200 + step), 1.0f
    ).totalSpawned();
    spawnedB += GrowthSystem::runStep(
      mapB, roadsB, storeB, demand, static_cast<uint32_t>(200 + step), 1.0f
    ).totalSpawned();
  }

  EXPECT_EQ(spawnedA, spawnedB);
  EXPECT_EQ(storeA.getBuildingCount(), storeB.getBuildingCount());
}
