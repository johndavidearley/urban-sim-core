#include <gtest/gtest.h>

#include "src/systems/ServiceSystem.hpp"

TEST(ServiceSystemTests, CoverageUsesRoadReachability) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;

  EntityId connectedId = store.createBuilding(BuildingType::Residential, {2, 2}, 10);
  EntityId disconnectedId = store.createBuilding(BuildingType::Residential, {9, 9}, 10);
  map.getTile({2, 2}).buildingId = static_cast<uint32_t>(connectedId);
  map.getTile({9, 9}).buildingId = static_cast<uint32_t>(disconnectedId);

  roads.buildRoad({2, 2}, {3, 2});
  roads.buildRoad({3, 2}, {4, 2});

  ServiceFacility fire;
  fire.type = ServiceType::Fire;
  fire.position = {4, 2};
  fire.maxTravelDistance = 6;

  ServiceCoverageSummary summary = ServiceSystem::evaluateCoverage(store, roads, {fire});

  EXPECT_EQ(summary.totalBuildings, 2u);
  EXPECT_EQ(summary.servicedBuildings, 1u);
  EXPECT_FLOAT_EQ(summary.fireCoverage, 0.5f);
}

TEST(ServiceSystemTests, ServiceSatisfactionBoostsHappinessWhenCoverageHigh) {
  CityMap map({10, 10});
  RoadNetwork roads(map);
  EntityStore store;

  EntityId id = store.createBuilding(BuildingType::Residential, {5, 5}, 8);
  map.getTile({5, 5}).buildingId = static_cast<uint32_t>(id);

  roads.buildRoad({5, 5}, {5, 6});

  std::vector<ServiceFacility> facilities;
  for (ServiceType type : {ServiceType::Fire, ServiceType::Police, ServiceType::Health, ServiceType::Education}) {
    ServiceFacility facility;
    facility.type = type;
    facility.position = {5, 6};
    facility.maxTravelDistance = 4;
    facilities.push_back(facility);
  }

  ServiceCoverageSummary summary = ServiceSystem::evaluateCoverage(store, roads, facilities);

  CityMetrics metrics;
  metrics.happiness = 0.5f;
  ServiceSystem::applyToMetrics(summary, metrics);

  EXPECT_GT(metrics.serviceCoverage, 0.9f);
  EXPECT_GT(metrics.serviceSatisfaction, 0.9f);
  EXPECT_GT(metrics.happiness, 0.5f);
}

// buildCache's nearestAnyDistance merge must report, for each reachable
// coordinate, the minimum distance across every entry that reaches it - not
// just the last entry processed - so LandValueSystem's "nearest facility of
// any type" lookup stays correct now that it reads this precomputed map
// instead of scanning cache.entries itself.
TEST(ServiceSystemTests, NearestAnyDistanceMergesTheClosestFacilityAcrossEntries) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  for (int x = 0; x < 11; ++x) {
    roads.buildRoad({x, 5}, {x + 1, 5});
  }

  ServiceFacility near;
  near.type = ServiceType::Fire;
  near.position = {8, 5};
  near.maxTravelDistance = 10;

  ServiceFacility far;
  far.type = ServiceType::Police;
  far.position = {0, 5};
  far.maxTravelDistance = 10;

  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, {near, far}, cache);

  // (5,5) is 3 hops from the fire station and 5 hops from the police
  // station - nearestAnyDistance must pick the closer one regardless of
  // which entry was built first.
  const auto it = cache.nearestAnyDistance.find({5, 5});
  ASSERT_NE(it, cache.nearestAnyDistance.end());
  EXPECT_EQ(it->second, 3);

  // A coordinate unreachable by any facility must be absent entirely.
  EXPECT_EQ(cache.nearestAnyDistance.find({11, 11}), cache.nearestAnyDistance.end());
}
