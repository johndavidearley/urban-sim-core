#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/systems/NaturalDisasterSystem.hpp"
#include "src/world/CityMap.hpp"

namespace {
DisasterParams zeroRiskParams() {
  DisasterParams params;
  params.earthquakeChancePerTick = 0.0f;
  params.floodChancePerTick = 0.0f;
  return params;
}

EntityId placeBuilding(CityMap& map, EntityStore& store, BuildingType type, Coord pos, int capacity) {
  const EntityId id = store.createBuilding(type, pos, capacity);
  map.getTile(pos).buildingId = static_cast<uint32_t>(id);
  return id;
}
} // namespace

TEST(NaturalDisasterSystemTests, ZeroChanceNeverTriggersAnything) {
  CityMap map({20, 20});
  EntityStore store;
  placeBuilding(map, store, BuildingType::Residential, {5, 5}, 20);

  const DisasterSummary summary = NaturalDisasterSystem::step(map, store, 42, zeroRiskParams());

  EXPECT_FALSE(summary.earthquakeOccurred);
  EXPECT_FALSE(summary.floodOccurred);
  EXPECT_EQ(summary.buildingsDestroyed, 0u);
  EXPECT_EQ(store.getBuildingCount(), 1u);
}

TEST(NaturalDisasterSystemTests, GuaranteedEarthquakeDestroysOnlyTheEpicenterWhenBuildingsAreFarApart) {
  // Two buildings farther apart than the radius: whichever one the epicenter
  // lands on is destroyed with certainty (distance 0, destructionChance
  // 1.0), but the other is out of range regardless of which was picked.
  CityMap map({20, 20});
  EntityStore store;
  placeBuilding(map, store, BuildingType::Residential, {1, 1}, 20);
  placeBuilding(map, store, BuildingType::Residential, {18, 18}, 20);

  DisasterParams params = zeroRiskParams();
  params.earthquakeChancePerTick = 1.0f;
  params.earthquakeRadius = 3;
  params.earthquakeDestructionChance = 1.0f;

  const DisasterSummary summary = NaturalDisasterSystem::step(map, store, 7, params);

  EXPECT_TRUE(summary.earthquakeOccurred);
  EXPECT_FALSE(summary.floodOccurred);
  EXPECT_EQ(summary.buildingsDestroyed, 1u);
  EXPECT_EQ(store.getBuildingCount(), 1u);
}

TEST(NaturalDisasterSystemTests, NoEarthquakeWithoutAnyBuildings) {
  CityMap map({10, 10});
  EntityStore store;

  DisasterParams params = zeroRiskParams();
  params.earthquakeChancePerTick = 1.0f;

  const DisasterSummary summary = NaturalDisasterSystem::step(map, store, 1, params);

  EXPECT_FALSE(summary.earthquakeOccurred);
  EXPECT_EQ(summary.buildingsDestroyed, 0u);
}

TEST(NaturalDisasterSystemTests, GuaranteedFloodOnlyAffectsBuildingsNearWater) {
  CityMap map({20, 20});
  for (int y = 0; y < 20; ++y) {
    map.getTile({0, y}).type = 2;  // a column of water down the west edge
  }
  EntityStore store;
  const EntityId coastal = placeBuilding(map, store, BuildingType::Residential, {1, 10}, 20);
  const EntityId inland = placeBuilding(map, store, BuildingType::Residential, {15, 10}, 20);

  DisasterParams params = zeroRiskParams();
  params.floodChancePerTick = 1.0f;
  params.floodProximity = 2;
  params.floodRadius = 3;
  params.floodDestructionChance = 1.0f;

  const DisasterSummary summary = NaturalDisasterSystem::step(map, store, 3, params);

  EXPECT_TRUE(summary.floodOccurred);
  EXPECT_FALSE(summary.earthquakeOccurred);
  EXPECT_EQ(store.getBuilding(coastal), nullptr);   // within flood radius of its own epicenter, destroyed for certain
  EXPECT_NE(store.getBuilding(inland), nullptr);    // far from any water, never eligible as epicenter or in range
}

TEST(NaturalDisasterSystemTests, NoFloodWhenNoBuildingIsNearWater) {
  CityMap map({20, 20});
  map.getTile({0, 0}).type = 2;  // water exists, but far from the only building
  EntityStore store;
  placeBuilding(map, store, BuildingType::Residential, {15, 15}, 20);

  DisasterParams params = zeroRiskParams();
  params.floodChancePerTick = 1.0f;
  params.floodProximity = 2;

  const DisasterSummary summary = NaturalDisasterSystem::step(map, store, 5, params);

  EXPECT_FALSE(summary.floodOccurred);
  EXPECT_EQ(store.getBuildingCount(), 1u);
}

TEST(NaturalDisasterSystemTests, DeterministicForSameSeed) {
  auto runOnce = [](uint32_t seed) {
    CityMap map({20, 20});
    for (int y = 0; y < 20; ++y) {
      map.getTile({0, y}).type = 2;
    }
    EntityStore store;
    for (int i = 0; i < 15; ++i) {
      placeBuilding(map, store, BuildingType::Residential, {i, 5}, 20);
    }
    DisasterParams params;
    params.earthquakeChancePerTick = 0.3f;
    params.floodChancePerTick = 0.3f;
    uint32_t destroyed = 0;
    for (int tick = 0; tick < 20; ++tick) {
      destroyed += NaturalDisasterSystem::step(map, store, seed + static_cast<uint32_t>(tick), params).buildingsDestroyed;
    }
    return destroyed;
  };

  EXPECT_EQ(runOnce(99), runOnce(99));
}
