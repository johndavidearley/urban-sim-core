#include "gtest/gtest.h"

#include "src/systems/LandValueSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"

namespace {
// A straight 10-tile road along y=0, x=0..9.
void buildStraightRoad(RoadNetwork& roads, int length) {
  for (int x = 0; x < length - 1; ++x) {
    roads.buildRoad({x, 0}, {x + 1, 0});
  }
}
} // namespace

TEST(LandValueSystemTests, JobProximityRaisesLandValue) {
  CityMap map({10, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 10);

  EntityStore store;
  // Job (commercial) building anchored at the road's left end.
  store.createBuilding(BuildingType::Commercial, {0, 1}, 20);

  // Zone two residential tiles on the road: one near the job, one far.
  Zoning::applyZoneRect(map, {1, 0}, {1, 0}, ZoneType::Residential);
  Zoning::applyZoneRect(map, {8, 0}, {8, 0}, ZoneType::Residential);

  LandValueSystem::updateLandValues(map, roads, store, nullptr, 0, 0, 9, 2);

  const float nearValue = map.landValue({1, 0});
  const float farValue = map.landValue({8, 0});
  EXPECT_GT(nearValue, farValue);
}

TEST(LandValueSystemTests, PollutionLowersLandValue) {
  CityMap map({10, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 10);

  EntityStore store;
  store.createBuilding(BuildingType::Commercial, {0, 1}, 20);

  // Two identical residential tiles, equidistant from the job.
  Zoning::applyZoneRect(map, {4, 0}, {4, 0}, ZoneType::Residential);
  Zoning::applyZoneRect(map, {5, 0}, {5, 0}, ZoneType::Residential);
  map.pollution({5, 0}) = 1.0f;  // maximally polluted

  LandValueSystem::updateLandValues(map, roads, store, nullptr, 0, 0, 9, 2);

  EXPECT_LT(map.landValue({5, 0}), map.landValue({4, 0}));
}

TEST(LandValueSystemTests, ServiceProximityRaisesLandValue) {
  CityMap map({10, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 10);

  EntityStore store;

  std::vector<ServiceFacility> facilities;
  facilities.push_back(ServiceFacility{ServiceType::Fire, {0, 1}, 10, 1.0f});
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);

  Zoning::applyZoneRect(map, {1, 0}, {1, 0}, ZoneType::Residential);
  Zoning::applyZoneRect(map, {8, 0}, {8, 0}, ZoneType::Residential);

  LandValueSystem::updateLandValues(map, roads, store, &cache, 0, 0, 9, 2);

  EXPECT_GT(map.landValue({1, 0}), map.landValue({8, 0}));

  // Without a cache at all, the service factor contributes nothing, so the
  // near/far difference collapses to whatever the (absent) job factor gives -
  // here, zero jobs too, so both tiles land on the same base value.
  LandValueSystem::updateLandValues(map, roads, store, nullptr, 0, 0, 9, 2);
  EXPECT_FLOAT_EQ(map.landValue({1, 0}), map.landValue({8, 0}));
}

TEST(LandValueSystemTests, UnreachableTileFallsBackToBaseValue) {
  CityMap map({10, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 10);

  EntityStore store;
  store.createBuilding(BuildingType::Commercial, {0, 1}, 20);

  // Zone a tile with no road adjacency at all (row 2, far from the road on row 0).
  Zoning::applyZoneRect(map, {5, 2}, {5, 2}, ZoneType::Residential);

  LandValueSystem::updateLandValues(map, roads, store, nullptr, 0, 0, 9, 2);

  EXPECT_FLOAT_EQ(map.landValue({5, 2}), Zoning::defaultLandValueForZone(ZoneType::Residential));
}

TEST(LandValueSystemTests, WaterTilesAreUntouched) {
  CityMap map({5, 5});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 5);

  EntityStore store;
  Tile& water = map.getTile({2, 2});
  water.type = 2;
  map.landValue({2, 2}) = 42.0f;

  LandValueSystem::updateLandValues(map, roads, store, nullptr, 0, 0, 4, 4);

  EXPECT_FLOAT_EQ(map.landValue({2, 2}), 42.0f);
}

TEST(LandValueSystemTests, AverageLandValueMeansOverZonedTilesOnly) {
  CityMap map({4, 1});
  EntityStore store;

  // Tile 0: unzoned (excluded). Tile 1 and 2: zoned, known values. Tile 3: water (excluded).
  map.getTile({1, 0}).zone = static_cast<int>(ZoneType::Residential);
  map.landValue({1, 0}) = 100.0f;
  map.getTile({2, 0}).zone = static_cast<int>(ZoneType::Commercial);
  map.landValue({2, 0}) = 200.0f;
  map.getTile({3, 0}).zone = static_cast<int>(ZoneType::Residential);
  map.getTile({3, 0}).type = 2;  // water
  map.landValue({3, 0}) = 999.0f;

  EXPECT_FLOAT_EQ(LandValueSystem::averageLandValue(map), 150.0f);
  // Bounded sample that includes the zoned tile matches full-map mean.
  EXPECT_FLOAT_EQ(LandValueSystem::averageLandValue(map, 0, 0, 5, 5), 150.0f);
  // Region with no zoned tiles returns the neutral default.
  EXPECT_FLOAT_EQ(LandValueSystem::averageLandValue(map, 10, 10, 12, 12), 100.0f);
}

TEST(LandValueSystemTests, AverageLandValueDefaultsWhenNothingZoned) {
  CityMap map({4, 1});
  EXPECT_FLOAT_EQ(LandValueSystem::averageLandValue(map), 100.0f);
}
