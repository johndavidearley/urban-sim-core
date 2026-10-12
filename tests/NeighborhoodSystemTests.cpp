#include <gtest/gtest.h>

#include "src/systems/LandValueSystem.hpp"
#include "src/systems/NeighborhoodSystem.hpp"
#include "src/world/Zoning.hpp"

namespace {
struct NeighborhoodCity {
  CityMap map{{32, 8}};
  RoadNetwork roads{map};
  EntityStore store;
  ServiceCoverageCache cache;
  EntityId near, far;
  NeighborhoodCity() {
    for (int x = 1; x < 28; ++x)
      roads.buildRoad({x, 3}, {x + 1, 3});
    near = store.createBuilding(BuildingType::Residential, {3, 2}, 100);
    far = store.createBuilding(BuildingType::Residential, {24, 2}, 100);
    store.getBuilding(near)->occupancy = 25;
    store.getBuilding(far)->occupancy = 75;
    for (auto id : {near, far}) {
      auto &tile = map.getTile(store.getBuilding(id)->position);
      tile.buildingId = id;
      map.setZone(store.getBuilding(id)->position,
                  static_cast<int>(ZoneType::Residential));
    }
  }
};
} // namespace

TEST(NeighborhoodSystem,
     PoliceHospitalAndSewageImproveOnlyReachableNeighborhood) {
  NeighborhoodCity city;
  ServiceSystem::buildCache(city.roads,
                            {{ServiceType::Police, {2, 3}, 8, 1.0f},
                             {ServiceType::Health, {2, 3}, 8, 1.0f},
                             {ServiceType::Sanitation, {2, 3}, 8, 1.0f}},
                            city.cache);
  const auto summary = NeighborhoodSystem::evaluate(
      city.map, city.roads, city.store, city.cache, 0.1f, 0.5f, 100.0f, 0.0f);
  const auto &near = *city.store.getBuilding(city.near);
  const auto &far = *city.store.getBuilding(city.far);
  EXPECT_LT(near.crimeRate, far.crimeRate);
  EXPECT_LT(near.illnessRate, far.illnessRate);
  EXPECT_FLOAT_EQ(summary.crimeRate,
                  near.crimeRate * 0.25f + far.crimeRate * 0.75f);
  EXPECT_FLOAT_EQ(summary.illnessRate,
                  near.illnessRate * 0.25f + far.illnessRate * 0.75f);
  LandValueSystem::updateLandValues(city.map, city.roads, city.store, nullptr,
                                    0, 0, 31, 7);
  EXPECT_GT(city.map.landValue(near.position),
            city.map.landValue(far.position));
  EXPECT_EQ(near.occupancy, 25);
  EXPECT_EQ(far.occupancy, 75);
}

TEST(NeighborhoodSystem, NoFacilitiesCollapsesToLegacyCityFormula) {
  NeighborhoodCity city;
  city.map.pollution({3, 2}) = 0.0f;
  city.map.pollution({24, 2}) = 0.8f;
  const auto summary = NeighborhoodSystem::evaluate(
      city.map, city.roads, city.store, city.cache, 0.2f, 0.75f, 120.0f, 0.6f);
  EXPECT_FLOAT_EQ(summary.crimeRate,
                  CrimeSystem::evaluate(0.2f, 0.0f, 120.0f).overallRate);
  EXPECT_FLOAT_EQ(summary.illnessRate,
                  HealthSystem::evaluate(0.75f, 0.6f, 0.0f, 0.0f).illnessRate);
  EXPECT_FLOAT_EQ(city.store.getBuilding(city.near)->illnessRate,
                  city.store.getBuilding(city.far)->illnessRate);
}

TEST(NeighborhoodSystem,
     LocalPollutionRaisesIllnessAndEmptyHomesDoNotWeightMean) {
  NeighborhoodCity city;
  ServiceSystem::buildCache(
      city.roads, {{ServiceType::Health, {15, 3}, 32, 1.0f}}, city.cache);
  city.map.pollution({24, 2}) = 0.9f;
  city.store.getBuilding(city.near)->occupancy = 0;
  const auto summary = NeighborhoodSystem::evaluate(
      city.map, city.roads, city.store, city.cache, 0.0f, 0.5f, 200.0f, 0.0f);
  EXPECT_GT(city.store.getBuilding(city.far)->illnessRate,
            city.store.getBuilding(city.near)->illnessRate);
  EXPECT_FLOAT_EQ(summary.illnessRate,
                  city.store.getBuilding(city.far)->illnessRate);
  city.store.getBuilding(city.far)->occupancy = 0;
  EXPECT_FLOAT_EQ(NeighborhoodSystem::summarize(city.store).illnessRate, 0.0f);
}

TEST(NeighborhoodSystem,
     NearerServiceHasStrongerEffectAndDisconnectedRoadDoesNotCover) {
  NeighborhoodCity city;
  ServiceSystem::buildCache(
      city.roads, {{ServiceType::Police, {2, 3}, 32, 1.0f}}, city.cache);
  NeighborhoodSystem::evaluate(city.map, city.roads, city.store, city.cache,
                               0.0f, 0.5f, 200.0f, 0.0f);
  EXPECT_LT(city.store.getBuilding(city.near)->crimeRate,
            city.store.getBuilding(city.far)->crimeRate);
  city.roads.removeRoad({12, 3}, {13, 3});
  ServiceSystem::buildCache(
      city.roads, {{ServiceType::Police, {2, 3}, 32, 1.0f}}, city.cache);
  NeighborhoodSystem::evaluate(city.map, city.roads, city.store, city.cache,
                               0.0f, 0.5f, 200.0f, 0.0f);
  EXPECT_FLOAT_EQ(city.store.getBuilding(city.far)->crimeRate,
                  CrimeSystem::evaluate(0.0f, 0.0f, 200.0f).overallRate);
}
