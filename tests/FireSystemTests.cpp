#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/systems/FireSystem.hpp"
#include "src/world/CityMap.hpp"

namespace {
FireParams zeroRiskParams() {
  FireParams params;
  params.baseIgnitionChance = 0.0f;
  params.spreadChance = 0.0f;
  return params;
}

// Buildings only count as "occupying" a tile (for FireSystem's spread/
// destruction checks) once the tile's buildingId is linked back, the same
// way every real caller (GrowthSystem etc.) does it - EntityStore::
// createBuilding alone doesn't touch the map.
EntityId placeBuilding(CityMap& map, EntityStore& store, BuildingType type, Coord pos, int capacity) {
  const EntityId id = store.createBuilding(type, pos, capacity);
  map.getTile(pos).buildingId = static_cast<uint32_t>(id);
  return id;
}
} // namespace

TEST(FireSystemTests, ZeroIgnitionChanceNeverBurnsAnything) {
  CityMap map({10, 10});
  EntityStore store;
  placeBuilding(map, store, BuildingType::Residential, {2, 2}, 20);
  placeBuilding(map, store, BuildingType::Industrial, {5, 5}, 20);

  std::vector<BurningTile> burning;
  const FireSummary summary = FireSystem::step(map, store, burning, /*fireCoverage=*/0.0f, 42, zeroRiskParams());

  EXPECT_EQ(summary.newIgnitions, 0u);
  EXPECT_EQ(summary.activeFires, 0u);
  EXPECT_TRUE(burning.empty());
  EXPECT_EQ(store.getBuildingCount(), 2u);
}

TEST(FireSystemTests, IgnitionBurnsTheBuildingWhichIsLostOnlyWhenTheCountdownEnds) {
  CityMap map({10, 10});
  EntityStore store;
  const EntityId id = placeBuilding(map, store, BuildingType::Industrial, {2, 2}, 20);

  FireParams params;
  params.baseIgnitionChance = 1.0f;   // guaranteed ignition
  params.industrialRiskMultiplier = 1.0f;
  params.spreadChance = 0.0f;

  std::vector<BurningTile> burning;
  FireSummary summary = FireSystem::step(map, store, burning, /*fireCoverage=*/0.0f, 7, params);

  EXPECT_EQ(summary.newIgnitions, 1u);
  EXPECT_EQ(summary.buildingsDestroyed, 0u);
  ASSERT_EQ(burning.size(), 1u);
  EXPECT_EQ(burning[0].position, Coord(2, 2));
  EXPECT_EQ(burning[0].ticksRemaining, params.baseDuration);
  EXPECT_NE(store.getBuilding(id), nullptr);

  // With no response, nothing puts it out: it burns down on the last tick.
  params.baseIgnitionChance = 0.0f;
  for (int tick = 1; tick < params.baseDuration; ++tick) {
    summary = FireSystem::step(map, store, burning, 0.0f, 7u + tick, params);
    EXPECT_EQ(summary.buildingsDestroyed, 0u);
    EXPECT_NE(store.getBuilding(id), nullptr);
  }
  summary = FireSystem::step(map, store, burning, 0.0f, 99, params);
  EXPECT_EQ(summary.buildingsDestroyed, 1u);
  EXPECT_TRUE(burning.empty());
  EXPECT_EQ(store.getBuilding(id), nullptr);
  EXPECT_EQ(map.getTile({2, 2}).buildingId, EntityIdUtils::NullEntity);
}

TEST(FireSystemTests, BurningTileSpreadsToOccupiedNeighborWithGuaranteedSpreadChance) {
  CityMap map({10, 10});
  EntityStore store;
  placeBuilding(map, store, BuildingType::Residential, {3, 2}, 20);  // neighbor of the burning tile below

  FireParams params = zeroRiskParams();
  params.spreadChance = 1.0f;  // guaranteed spread to every occupied neighbor

  std::vector<BurningTile> burning = {{{2, 2}, 3}};  // already burning, unrelated to any building
  const FireSummary summary = FireSystem::step(map, store, burning, /*fireCoverage=*/0.0f, 3, params);

  EXPECT_EQ(summary.newIgnitions, 1u);
  EXPECT_EQ(summary.buildingsDestroyed, 0u);
  EXPECT_NE(map.getTile({3, 2}).buildingId, EntityIdUtils::NullEntity);
  EXPECT_EQ(store.getBuildingCount(), 1u);

  // Both the original and the newly-spread tile should still be burning.
  bool hasOriginal = false, hasSpread = false;
  for (const BurningTile& bt : burning) {
    if (bt.position == Coord(2, 2)) hasOriginal = true;
    if (bt.position == Coord(3, 2)) hasSpread = true;
  }
  EXPECT_TRUE(hasOriginal);
  EXPECT_TRUE(hasSpread);
}

TEST(FireSystemTests, BurningTileExtinguishesAfterItsDuration) {
  CityMap map({10, 10});
  EntityStore store;

  std::vector<BurningTile> burning = {{{2, 2}, 1}};  // one tick left
  const FireSummary summary = FireSystem::step(map, store, burning, /*fireCoverage=*/0.0f, 1, zeroRiskParams());

  EXPECT_TRUE(burning.empty());
  EXPECT_EQ(summary.activeFires, 0u);
}

TEST(FireSystemTests, HigherFireCoverageReducesIgnitionRateOverManyTrials) {
  FireParams params;
  params.baseIgnitionChance = 0.5f;
  params.industrialRiskMultiplier = 1.0f;
  params.spreadChance = 0.0f;
  params.coverageIgnitionReduction = 0.9f;

  uint32_t ignitionsNoCoverage = 0;
  uint32_t ignitionsHighCoverage = 0;
  constexpr uint32_t kTrials = 200;
  for (uint32_t trial = 0; trial < kTrials; ++trial) {
    CityMap mapA({10, 10});
    EntityStore storeA;
    placeBuilding(mapA, storeA, BuildingType::Residential, {2, 2}, 20);
    std::vector<BurningTile> burningA;
    ignitionsNoCoverage += FireSystem::step(mapA, storeA, burningA, /*fireCoverage=*/0.0f, trial, params).newIgnitions;

    CityMap mapB({10, 10});
    EntityStore storeB;
    placeBuilding(mapB, storeB, BuildingType::Residential, {2, 2}, 20);
    std::vector<BurningTile> burningB;
    ignitionsHighCoverage += FireSystem::step(mapB, storeB, burningB, /*fireCoverage=*/1.0f, trial, params).newIgnitions;
  }

  EXPECT_GT(ignitionsNoCoverage, ignitionsHighCoverage);
}

TEST(FireSystemTests, IndustrialBuildingsIgniteMoreOftenThanResidentialOverManyTrials) {
  FireParams params;
  params.baseIgnitionChance = 0.05f;
  params.industrialRiskMultiplier = 5.0f;
  params.spreadChance = 0.0f;

  uint32_t residentialIgnitions = 0;
  uint32_t industrialIgnitions = 0;
  constexpr uint32_t kTrials = 300;
  for (uint32_t trial = 0; trial < kTrials; ++trial) {
    CityMap mapR({10, 10});
    EntityStore storeR;
    placeBuilding(mapR, storeR, BuildingType::Residential, {2, 2}, 20);
    std::vector<BurningTile> burningR;
    residentialIgnitions += FireSystem::step(mapR, storeR, burningR, 0.0f, trial, params).newIgnitions;

    CityMap mapI({10, 10});
    EntityStore storeI;
    placeBuilding(mapI, storeI, BuildingType::Industrial, {2, 2}, 20);
    std::vector<BurningTile> burningI;
    industrialIgnitions += FireSystem::step(mapI, storeI, burningI, 0.0f, trial, params).newIgnitions;
  }

  EXPECT_GT(industrialIgnitions, residentialIgnitions);
}

TEST(FireSystemTests, DeterministicForSameSeed) {
  auto runOnce = [](uint32_t seed) {
    CityMap map({20, 20});
    EntityStore store;
    for (int i = 0; i < 15; ++i) {
      placeBuilding(map, store, i % 2 == 0 ? BuildingType::Industrial : BuildingType::Residential, {i, 0}, 20);
    }
    FireParams params;
    params.baseIgnitionChance = 0.05f;
    std::vector<BurningTile> burning;
    uint32_t destroyed = 0;
    for (int tick = 0; tick < 10; ++tick) {
      destroyed += FireSystem::step(map, store, burning, 0.2f, seed + static_cast<uint32_t>(tick), params).buildingsDestroyed;
    }
    return destroyed;
  };

  EXPECT_EQ(runOnce(99), runOnce(99));
}

namespace {
struct LocalFireCity {
  CityMap map{{20, 12}};
  RoadNetwork roads{map};
  EntityStore store;
  ServiceCoverageCache cache;
  LocalFireCity() {
    for (int x = 2; x < 16; ++x) roads.buildRoad({x, 4}, {x + 1, 4});
    ServiceSystem::buildCache(roads, {{ServiceType::Fire, {2, 5}, 4}}, cache);
  }
};
}

TEST(FireSystemTests, LocalResponseFallsOffAndStopsAtStationRadius) {
  LocalFireCity city;
  EXPECT_FLOAT_EQ(FireSystem::responseAt(city.roads, city.cache, {2, 3}), 1.f);
  EXPECT_NEAR(FireSystem::responseAt(city.roads, city.cache, {6, 3}), .2f, .00001f);
  EXPECT_FLOAT_EQ(FireSystem::responseAt(city.roads, city.cache, {7, 3}, 1.f), 0.f);
  EXPECT_FLOAT_EQ(FireSystem::responseAt(city.roads, city.cache, {2, 10}, 1.f), 0.f);
  ServiceCoverageCache empty;
  EXPECT_FLOAT_EQ(FireSystem::responseAt(city.roads, empty, {2, 10}, .7f), .7f);
}

TEST(FireSystemTests, SameSeedProtectedBlockLosesFewerBuildings) {
  uint32_t nearLoss = 0, farLoss = 0;
  FireParams params = zeroRiskParams();
  params.baseIgnitionChance = .5f;
  params.coverageIgnitionReduction = .9f;
  for (uint32_t seed = 0; seed < 100; ++seed) {
    for (bool near : {true, false}) {
      LocalFireCity city;
      placeBuilding(city.map, city.store, BuildingType::Residential, {near ? 2 : 12, 3}, 20);
      std::vector<BurningTile> burning;
      for (int tick = 0; tick <= params.baseDuration; ++tick)
        (near ? nearLoss : farLoss) += FireSystem::step(city.map, city.store, burning, 1.f,
          seed * 31u + static_cast<uint32_t>(tick), params, &city.roads, &city.cache).buildingsDestroyed;
    }
  }
  EXPECT_GT(farLoss, nearLoss);
}

TEST(FireSystemTests, SpreadUsesReceivingBuildingsLocalResponse) {
  for (bool near : {true, false}) {
    LocalFireCity city;
    const int x = near ? 2 : 12;
    placeBuilding(city.map, city.store, BuildingType::Residential, {x, 3}, 20);
    std::vector<BurningTile> burning{{{x, 2}, 3}};
    auto params = zeroRiskParams();
    params.spreadChance = 1.f;
    params.coverageSpreadReduction = 1.f;
    const auto result = FireSystem::step(city.map, city.store, burning, 1.f, 7, params,
      &city.roads, &city.cache);
    EXPECT_EQ(result.newIgnitions, near ? 0u : 1u);
  }
}

TEST(FireSystemTests, LocalResponsePutsOutFiresBeforeTheyBurnDown) {
  LocalFireCity city;
  const EntityId nearId = placeBuilding(city.map, city.store, BuildingType::Residential, {2, 3}, 20);
  const EntityId farId = placeBuilding(city.map, city.store, BuildingType::Residential, {12, 3}, 20);
  auto params = zeroRiskParams();
  params.extinguishChance = 1.f;
  std::vector<BurningTile> burning{{{2, 3}, params.baseDuration}, {{12, 3}, params.baseDuration}};
  uint32_t extinguished = 0, destroyed = 0;
  for (int tick = 0; tick < params.baseDuration; ++tick) {
    const auto result = FireSystem::step(city.map, city.store, burning, 1.f, 7u + tick, params,
      &city.roads, &city.cache);
    extinguished += result.extinguished;
    destroyed += result.buildingsDestroyed;
  }
  // Full response next to the station puts it out at once; no station reaches x = 12.
  EXPECT_EQ(extinguished, 1u);
  EXPECT_EQ(destroyed, 1u);
  EXPECT_NE(city.store.getBuilding(nearId), nullptr);
  EXPECT_EQ(city.store.getBuilding(farId), nullptr);
  EXPECT_TRUE(burning.empty());
}

TEST(FireSystemTests, NoStationsPreservesScalarFallbackExactly) {
  LocalFireCity a, b;
  ServiceCoverageCache empty;
  for (int x = 2; x <= 12; ++x) {
    placeBuilding(a.map, a.store, BuildingType::Residential, {x, 3}, 20);
    placeBuilding(b.map, b.store, BuildingType::Residential, {x, 3}, 20);
  }
  std::vector<BurningTile> firesA, firesB;
  auto params = zeroRiskParams();
  params.baseIgnitionChance = .4f;
  params.spreadChance = .5f;
  for (uint32_t seed = 0; seed < 8; ++seed) {
    const auto legacy = FireSystem::step(a.map, a.store, firesA, .6f, seed, params);
    const auto local = FireSystem::step(b.map, b.store, firesB, .6f, seed, params, &b.roads, &empty);
    EXPECT_EQ(legacy.newIgnitions, local.newIgnitions);
    ASSERT_EQ(firesA.size(), firesB.size());
    for (size_t i = 0; i < firesA.size(); ++i) {
      EXPECT_EQ(firesA[i].position, firesB[i].position);
      EXPECT_EQ(firesA[i].ticksRemaining, firesB[i].ticksRemaining);
    }
  }
}
