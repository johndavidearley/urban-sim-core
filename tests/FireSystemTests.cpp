#include <gtest/gtest.h>

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

TEST(FireSystemTests, GuaranteedIgnitionDestroysTheBuildingAndStartsBurning) {
  CityMap map({10, 10});
  EntityStore store;
  const EntityId id = placeBuilding(map, store, BuildingType::Industrial, {2, 2}, 20);

  FireParams params;
  params.baseIgnitionChance = 1.0f;   // guaranteed ignition
  params.industrialRiskMultiplier = 1.0f;
  params.spreadChance = 0.0f;

  std::vector<BurningTile> burning;
  const FireSummary summary = FireSystem::step(map, store, burning, /*fireCoverage=*/0.0f, 7, params);

  EXPECT_EQ(summary.newIgnitions, 1u);
  EXPECT_EQ(summary.buildingsDestroyed, 1u);
  ASSERT_EQ(burning.size(), 1u);
  EXPECT_EQ(burning[0].position.x, 2);
  EXPECT_EQ(burning[0].position.y, 2);
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
  EXPECT_EQ(map.getTile({3, 2}).buildingId, EntityIdUtils::NullEntity);
  EXPECT_EQ(store.getBuildingCount(), 0u);

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
