#include "gtest/gtest.h"

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

// Power/Water coverage is computed and reported like the original four
// types, but must never affect servicedBuildings/overallCoverage/
// satisfaction - those stay defined as exactly
// (fire+police+health+education)/4, so a caller that places a Power
// facility without otherwise opting into utilities sees no change to the
// coverage numbers it already depends on.
TEST(ServiceSystemTests, PowerAndWaterCoverageAreTrackedButExcludedFromOverallCoverage) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;

  EntityId id = store.createBuilding(BuildingType::Residential, {2, 2}, 10);
  map.getTile({2, 2}).buildingId = static_cast<uint32_t>(id);
  roads.buildRoad({2, 2}, {3, 2});

  ServiceFacility power;
  power.type = ServiceType::Power;
  power.position = {3, 2};
  power.maxTravelDistance = 6;

  const ServiceCoverageSummary withPowerOnly = ServiceSystem::evaluateCoverage(store, roads, {power});

  EXPECT_FLOAT_EQ(withPowerOnly.powerCoverage, 1.0f);
  EXPECT_FLOAT_EQ(withPowerOnly.waterCoverage, 0.0f);
  // No Fire/Police/Health/Education facility exists, so this building is
  // not "serviced" and overall coverage is 0 - power alone doesn't count.
  EXPECT_EQ(withPowerOnly.servicedBuildings, 0u);
  EXPECT_FLOAT_EQ(withPowerOnly.overallCoverage, 0.0f);
}

TEST(ServiceSystemTests, ParsesSanitationAliases) {
  ServiceType type = ServiceType::Fire;
  for (const char* alias : {"SANITATION", "waste", "Garbage", "RECYCLING"}) {
    ASSERT_TRUE(ServiceSystem::parseServiceType(alias, type));
    EXPECT_EQ(type, ServiceType::Sanitation);
  }
  EXPECT_STREQ(ServiceSystem::serviceTypeToString(ServiceType::Sanitation), "Sanitation");
}

TEST(ServiceSystemTests, SanitationCoverageIsTrackedButExcludedFromOverallCoverage) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;
  store.createBuilding(BuildingType::Residential, {2, 2}, 10);
  roads.buildRoad({2, 2}, {3, 2});

  ServiceFacility sanitation;
  sanitation.type = ServiceType::Sanitation;
  sanitation.position = {3, 2};
  sanitation.maxTravelDistance = 6;

  const ServiceCoverageSummary summary =
    ServiceSystem::evaluateCoverage(store, roads, {sanitation});
  EXPECT_FLOAT_EQ(summary.sanitationCoverage, 1.0f);
  EXPECT_EQ(summary.servicedBuildings, 0u);
  EXPECT_FLOAT_EQ(summary.overallCoverage, 0.0f);
}

// nearestPowerDistance/nearestWaterDistance must only merge entries of
// their own type, unlike nearestAnyDistance which merges everything -
// a nearby Police station must not make a tile look power-covered.
TEST(ServiceSystemTests, NearestPowerAndWaterDistanceOnlyMergeTheirOwnType) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  for (int x = 0; x < 11; ++x) {
    roads.buildRoad({x, 5}, {x + 1, 5});
  }

  ServiceFacility policeStation;
  policeStation.type = ServiceType::Police;
  policeStation.position = {5, 5};
  policeStation.maxTravelDistance = 10;

  ServiceFacility powerPlant;
  powerPlant.type = ServiceType::Power;
  powerPlant.position = {2, 5};
  powerPlant.maxTravelDistance = 10;

  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, {policeStation, powerPlant}, cache);

  // (5,5) is covered by the police station (distance 0) but is 3 hops from
  // the power plant - nearestPowerDistance must reflect only the plant.
  EXPECT_EQ(cache.nearestAnyDistance.count({5, 5}), 1u);
  const auto powerIt = cache.nearestPowerDistance.find({5, 5});
  ASSERT_NE(powerIt, cache.nearestPowerDistance.end());
  EXPECT_EQ(powerIt->second, 3);
  EXPECT_EQ(cache.nearestWaterDistance.count({5, 5}), 0u);
}

TEST(ServiceSystemTests, CacheRecordsRoadTopologyVersion) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  roads.buildRoad({1, 1}, {2, 1});

  ServiceFacility fire;
  fire.type = ServiceType::Fire;
  fire.position = {1, 1};

  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, {fire}, cache);
  EXPECT_TRUE(ServiceSystem::isCacheValid(roads, {fire}, cache));
  EXPECT_EQ(cache.builtForTopologyVersion, roads.getTopologyVersion());

  roads.buildRoad({2, 1}, {3, 1});
  EXPECT_FALSE(ServiceSystem::isCacheValid(roads, {fire}, cache));
  EXPECT_NE(cache.builtForTopologyVersion, roads.getTopologyVersion());
}

TEST(ServiceSystemTests, CacheInvalidatesWhenFacilityChangesWithoutChangingCount) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  roads.buildRoad({1, 1}, {2, 1});
  roads.buildRoad({2, 1}, {3, 1});

  std::vector<ServiceFacility> facilities(1);
  facilities[0].type = ServiceType::Fire;
  facilities[0].position = {1, 1};
  facilities[0].maxTravelDistance = 4;

  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);
  ASSERT_TRUE(ServiceSystem::isCacheValid(roads, facilities, cache));

  facilities[0].position = {3, 1};
  EXPECT_FALSE(ServiceSystem::isCacheValid(roads, facilities, cache));

  facilities[0].position = {1, 1};
  facilities[0].maxTravelDistance = 1;
  EXPECT_FALSE(ServiceSystem::isCacheValid(roads, facilities, cache));

  facilities[0].maxTravelDistance = 4;
  facilities[0].type = ServiceType::Police;
  EXPECT_FALSE(ServiceSystem::isCacheValid(roads, facilities, cache));
}

TEST(ServiceSystemTests, ResultCacheInvalidatesAfterMutableBuildingPositionChange) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;
  const EntityId id = store.createBuilding(BuildingType::Residential, {1, 1}, 10);

  ServiceCoverageCache cache;
  ServiceCoverageSummary result;
  result.totalBuildings = 1;
  ServiceSystem::storeCachedResult(store, result, cache);
  ASSERT_TRUE(ServiceSystem::isResultCacheValid(store, cache));

  Building* building = store.getBuilding(id);
  ASSERT_NE(building, nullptr);
  building->position = {9, 9};

  // Mutable access deliberately does not advance EntityStore's structural
  // version, so the position signature must detect this change.
  EXPECT_FALSE(ServiceSystem::isResultCacheValid(store, cache));
}
