#include "gtest/gtest.h"

#include "src/systems/ServiceSystem.hpp"
#include "src/systems/PlayableCityTick.hpp"

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
  for (const char* alias : {"SANITATION", "sewage", "WASTEWATER"}) {
    ASSERT_TRUE(ServiceSystem::parseServiceType(alias, type));
    EXPECT_EQ(type, ServiceType::Sanitation);
  }
  EXPECT_STREQ(ServiceSystem::serviceTypeToString(ServiceType::Sanitation), "Sanitation");
}

TEST(ServiceSystemTests, ParsesDistinctGarbageAndRecyclingTypes) {
  ServiceType type = ServiceType::Fire;
  ASSERT_TRUE(ServiceSystem::parseServiceType("garbage", type));
  EXPECT_EQ(type, ServiceType::Garbage);
  ASSERT_TRUE(ServiceSystem::parseServiceType("recycling", type));
  EXPECT_EQ(type, ServiceType::Recycling);
  EXPECT_STREQ(ServiceSystem::serviceTypeToString(ServiceType::Garbage), "Garbage");
  EXPECT_STREQ(ServiceSystem::serviceTypeToString(ServiceType::Recycling), "Recycling");
}

TEST(ServiceSystemTests, ParsesDistinctDeathcareTypes) {
  ServiceType type = ServiceType::Fire;
  ASSERT_TRUE(ServiceSystem::parseServiceType("cemetery", type));
  EXPECT_EQ(type, ServiceType::Cemetery);
  ASSERT_TRUE(ServiceSystem::parseServiceType("crematorium", type));
  EXPECT_EQ(type, ServiceType::Crematorium);
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

TEST(ServiceSystemTests, ParsesPowerSourceTypesAndProvidesDefaultEmissions) {
  PowerSourceType type = PowerSourceType::Generic;
  ASSERT_TRUE(ServiceSystem::parsePowerSourceType("natural_gas", type));
  EXPECT_EQ(type, PowerSourceType::NaturalGas);
  EXPECT_GT(ServiceSystem::defaultPowerEmissions(PowerSourceType::Coal),
            ServiceSystem::defaultPowerEmissions(PowerSourceType::Solar));
  EXPECT_STREQ(ServiceSystem::powerSourceTypeToString(PowerSourceType::Nuclear), "Nuclear");
}

TEST(ServiceSystemTests, PowerSupplyTracksCapacityDemandAndGenerationEmissions) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;
  const EntityId homeId = store.createBuilding(BuildingType::Residential, {2, 2}, 20);
  const EntityId jobId = store.createBuilding(BuildingType::Commercial, {3, 2}, 20);
  store.getBuilding(homeId)->occupancy = 10;
  store.getBuilding(jobId)->occupancy = 10;
  roads.buildRoad({2, 2}, {3, 2});

  ServiceFacility coal;
  coal.type = ServiceType::Power;
  coal.position = {3, 2};
  coal.maxTravelDistance = 6;
  coal.powerSource = PowerSourceType::Coal;
  coal.powerCapacityMW = 0.03f;
  coal.emissionsKgPerMWh = ServiceSystem::defaultPowerEmissions(coal.powerSource);

  const ServiceCoverageSummary summary = ServiceSystem::evaluateCoverage(store, roads, {coal});
  EXPECT_NEAR(summary.powerDemandMW, 0.06f, 0.0001f);
  EXPECT_NEAR(summary.powerGenerationMW, 0.03f, 0.0001f);
  EXPECT_NEAR(summary.powerSupplyRatio, 0.5f, 0.0001f);
  EXPECT_FLOAT_EQ(summary.powerEmissionsKgPerMWh, 1000.0f);
  // No water plant. The same occupancy is water demand, so the ratio is 0.
  EXPECT_NEAR(summary.waterDemand, 0.06f, 0.0001f);
  EXPECT_FLOAT_EQ(summary.waterSupply, 0.0f);
  EXPECT_FLOAT_EQ(summary.waterSupplyRatio, 0.0f);
}

TEST(ServiceSystemTests, WaterSupplyTracksPlantCapacity) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  const EntityId homeId = store.createBuilding(BuildingType::Residential, {2, 2}, 20);
  store.getBuilding(homeId)->occupancy = 10;
  roads.buildRoad({2, 2}, {3, 2});

  ServiceFacility water;
  water.type = ServiceType::Water;
  water.position = {3, 2};
  water.maxTravelDistance = 6;
  water.waterSupplyUnits = 0.01f;

  const ServiceCoverageSummary summary = ServiceSystem::evaluateCoverage(store, roads, {water});
  EXPECT_NEAR(summary.waterDemand, 0.02f, 0.0001f);
  EXPECT_NEAR(summary.waterSupply, 0.01f, 0.0001f);
  EXPECT_NEAR(summary.waterSupplyRatio, 0.5f, 0.0001f);
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

TEST(ServiceSystemTests, ResultCacheInvalidatesOnStructuralStoreMutation) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;
  const EntityId id = store.createBuilding(BuildingType::Residential, {1, 1}, 10);

  ServiceCoverageCache cache;
  ServiceCoverageSummary result;
  result.totalBuildings = 1;
  ServiceSystem::storeCachedResult(store, result, cache);
  ASSERT_TRUE(ServiceSystem::isResultCacheValid(store, cache));

  // Bare non-const pointer edits do not advance mutationVersion; structural
  // APIs (create/remove/upsert) do. Coverage-relevant position changes must
  // go through upsertBuilding so the result cache invalidates in O(1).
  Building* building = store.getBuilding(id);
  ASSERT_NE(building, nullptr);
  building->position = {9, 9};
  EXPECT_TRUE(ServiceSystem::isResultCacheValid(store, cache));

  store.upsertBuilding(*building);
  EXPECT_FALSE(ServiceSystem::isResultCacheValid(store, cache));

  ServiceSystem::storeCachedResult(store, result, cache);
  ASSERT_TRUE(ServiceSystem::isResultCacheValid(store, cache));
  store.createBuilding(BuildingType::Commercial, {2, 2}, 5);
  EXPECT_FALSE(ServiceSystem::isResultCacheValid(store, cache));
}

TEST(ServiceSystemTests, RawOutletsReduceIntakeSupplyOnlyAlongConnectedWater) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  for (int y = 5; y < 12; ++y) {
    roads.buildRoad({10, y}, {10, y + 1});
    roads.buildRoad({16, y}, {16, y + 1});
  }
  for (int y = 0; y < 24; ++y) {
    map.getTile({12, y}).type = 2;
    map.getTile({14, y}).type = 2;
  }
  ServiceFacility intake{ServiceType::Water, {11, 8}, 20, 1.f};
  intake.surfaceWater = true;
  ServiceFacility outlet{ServiceType::Sanitation, {11, 10}, 18, 1.f};
  outlet.surfaceWater = true;
  std::vector<ServiceFacility> facilities{intake, outlet};
  const float polluted =
      ServiceSystem::intakeCleanFraction(roads, facilities, intake.position);
  EXPECT_LT(polluted, 0.5f);
  EXPECT_GE(polluted, 0.1f);
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);
  EXPECT_NEAR(cache.waterSupplyUnits, 100 * polluted, 0.001f);
  facilities[1].position = {15, 10};
  EXPECT_FALSE(ServiceSystem::isCacheValid(roads, facilities, cache));
  EXPECT_FLOAT_EQ(
      ServiceSystem::intakeCleanFraction(roads, facilities, intake.position),
      1.f);
  facilities[1].position = {11, 10};
  facilities[1].surfaceWater = false;
  EXPECT_FLOAT_EQ(
      ServiceSystem::intakeCleanFraction(roads, facilities, intake.position),
      1.f);
  facilities[1].surfaceWater = true;
  facilities[1].quality = 0;
  EXPECT_FLOAT_EQ(
      ServiceSystem::intakeCleanFraction(roads, facilities, intake.position),
      1.f);
  EXPECT_FLOAT_EQ(ServiceSystem::intakeCleanFraction(roads, facilities, {3, 3}),
                  0.f);
}

TEST(ServiceSystemTests, IncineratorGenerationTracksFuelAndRoadConnectivity) {
  CityMap map({16,16}); RoadNetwork roads(map); EntityStore store;
  roads.buildRoad({2,3}, {3,3});
  const auto id = store.createBuilding(BuildingType::Residential, {2,2}, 10000);
  map.getTile({2,2}).buildingId = static_cast<uint32_t>(id);
  store.getBuilding(id)->occupancy = 1000;
  std::vector<ServiceFacility> facilities{{ServiceType::Incinerator, {3,2}, 18, 1.f}};
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);
  auto coverage = ServiceSystem::evaluateFromCache(store, roads, cache);
  EXPECT_FLOAT_EQ(coverage.incineratorCoverage, 1.f);
  EXPECT_FLOAT_EQ(coverage.powerCoverage, 1.f);
  EXPECT_FLOAT_EQ(coverage.powerGenerationMW, 5.f);
  EXPECT_FLOAT_EQ(coverage.powerSupplyRatio, 1.f);
  EXPECT_FLOAT_EQ(coverage.powerEmissionsKgPerMWh, 650.f);
  updateUtilityConnectivityFromFacilities(map, roads, store, facilities);
  EXPECT_TRUE(map.getTile({2,2}).connectedToPower);
  store.getBuilding(id)->occupancy = 10000; // Demand exceeds the 6 MW fuel-processing cap.
  updateUtilityConnectivityFromFacilities(map, roads, store, facilities);
  EXPECT_FALSE(map.getTile({2,2}).connectedToPower);
  store.getBuilding(id)->occupancy = 1000;
  ServiceSystem::storeCachedResult(store, coverage, cache);
  store.getBuilding(id)->occupancy = 0; // In-place changes must not retain old fuel output.
  EXPECT_FALSE(ServiceSystem::isResultCacheValid(store, cache));
  EXPECT_FLOAT_EQ(ServiceSystem::evaluateFromCache(store, roads, cache).powerGenerationMW, 0.f);
  store.getBuilding(id)->occupancy = 1000;
  facilities[0].position = {14,14};
  ServiceSystem::buildCache(roads, facilities, cache);
  EXPECT_FLOAT_EQ(ServiceSystem::evaluateFromCache(store, roads, cache).powerGenerationMW, 0.f);
}

TEST(ServiceSystemTests, LandfillStorageRefreshPreservesCoverageCacheAndPowersEmptyCity) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store;
  roads.buildRoad({1,1},{2,1});
  std::vector<ServiceFacility> facilities{{ServiceType::Garbage,{1,2},10,1.f},
    {ServiceType::Incinerator,{2,2},10,1.f}};
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);
  facilities[0].landfillRemaining = 0;
  EXPECT_TRUE(ServiceSystem::isCacheValid(roads, facilities, cache));
  ServiceSystem::refreshWasteStorage(facilities, cache);
  EXPECT_FLOAT_EQ(ServiceSystem::evaluateFromCache(store, roads, cache).powerGenerationMW, 6.f);
  EXPECT_EQ(facilities[0].landfillRemaining, 0);
}

TEST(ServiceSystemTests, UnfundedLandfillRetainsFuelWithoutAcceptingDeposits) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store;
  roads.buildRoad({1,1},{2,1});
  std::vector<ServiceFacility> facilities{{ServiceType::Garbage,{1,2},10,1.f},
    {ServiceType::Incinerator,{2,2},10,1.f}};
  facilities[0].landfillRemaining=0;
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads,{facilities[1]},cache);
  ServiceSystem::refreshWasteStorage(facilities,cache);
  const auto summary=ServiceSystem::evaluateFromCache(store,roads,cache);
  EXPECT_FLOAT_EQ(summary.powerGenerationMW,6.f);
  const auto waste=WasteSystem::step(store,facilities,summary,&cache.wasteFacilities);
  EXPECT_EQ(waste.landfilled,0);
  EXPECT_EQ(waste.storedIncinerated,300);
  EXPECT_EQ(facilities[0].landfillRemaining,300);
}

TEST(ServiceSystemTests, SchoolAndHospitalCapacityScalesWithFundingAndReach) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store;
  roads.buildRoad({1,1},{2,1});
  std::vector<ServiceFacility> facilities{{ServiceType::Education,{1,2},10,1.f},
    {ServiceType::Health,{2,2},10,1.f}};
  BudgetPolicy policy; policy.servicePercent[3]=50; policy.servicePercent[2]=25;
  const auto funded=policy.fundedFacilities(facilities);
  ServiceCoverageCache cache; ServiceSystem::buildCache(roads,funded,cache);
  auto summary=ServiceSystem::evaluateFromCache(store,roads,cache);
  EXPECT_FLOAT_EQ(summary.schoolSeats,400.f);
  EXPECT_FLOAT_EQ(summary.hospitalBeds,100.f);
  summary.educationCoverage=1.f; summary.healthCoverage=1.f;
  ServiceSystem::applyCivicCapacity(summary,5000,.2f);
  EXPECT_FLOAT_EQ(summary.schoolDemand,1000.f);
  EXPECT_FLOAT_EQ(summary.effectiveEducationCoverage,.4f);
  EXPECT_FLOAT_EQ(summary.schoolSeatsUsed,400.f);
  EXPECT_FLOAT_EQ(summary.patientDemand,1000.f);
  EXPECT_FLOAT_EQ(summary.hospitalCapacityFactor,.1f);
  EXPECT_FLOAT_EQ(summary.hospitalBedsUsed,100.f);
  summary.educationCoverage=.1f; summary.healthCoverage=.05f;
  ServiceSystem::applyCivicCapacity(summary,5000,.2f);
  EXPECT_FLOAT_EQ(summary.effectiveEducationCoverage,.1f);
  EXPECT_FLOAT_EQ(summary.schoolSeatsUsed,100.f);
  EXPECT_FLOAT_EQ(summary.hospitalBedsUsed,100.f);
  policy.servicePercent[3]=policy.servicePercent[2]=0;
  ServiceSystem::buildCache(roads,policy.fundedFacilities(facilities),cache);
  summary=ServiceSystem::evaluateFromCache(store,roads,cache);
  ServiceSystem::applyCivicCapacity(summary,100,.01f);
  EXPECT_FLOAT_EQ(summary.schoolSeats,0.f);
  EXPECT_FLOAT_EQ(summary.hospitalBeds,0.f);
  EXPECT_FLOAT_EQ(summary.effectiveEducationCoverage,0.f);
  EXPECT_FLOAT_EQ(summary.patientDemand,1.f);
  EXPECT_FLOAT_EQ(summary.hospitalCapacityFactor,0.f);
}

TEST(ServiceSystemTests, CivicDemandIsDerivedWithoutInvalidatingRoadFields) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store;
  std::vector<ServiceFacility> facilities{{ServiceType::Education,{1,2},10,1.f}};
  ServiceCoverageCache cache; ServiceSystem::buildCache(roads,facilities,cache);
  ServiceCoverageSummary summary; summary.schoolSeats=800.f; summary.educationCoverage=.75f;
  ServiceSystem::applyCivicCapacity(summary,1000,0.f);
  EXPECT_FLOAT_EQ(summary.effectiveEducationCoverage,.75f);
  EXPECT_FLOAT_EQ(summary.schoolSeatsUsed,150.f);
  EXPECT_FLOAT_EQ(summary.hospitalCapacityFactor,1.f);
  EXPECT_TRUE(ServiceSystem::isCacheValid(roads,facilities,cache));
  facilities[0].quality=.5f;
  EXPECT_FALSE(ServiceSystem::isCacheValid(roads,facilities,cache));
}

TEST(ServiceSystemTests, TreatmentRestoresIntakeOnNextPassWithoutAffectingSeparateWaterBody) {
  CityMap map({24,24}); RoadNetwork roads(map); EntityStore store;
  for (int y=0;y<24;++y) { map.getTile({12,y}).type=2; map.getTile({14,y}).type=2; }
  for (int y=5;y<12;++y) {
    roads.buildRoad({10,y},{10,y+1}); roads.buildRoad({16,y},{16,y+1});
  }
  store.createBuilding(BuildingType::Residential,{9,8},20);
  ServiceFacility intake{ServiceType::Water,{11,8},20,1.f}; intake.surfaceWater=true;
  ServiceFacility other{ServiceType::Water,{15,8},20,1.f}; other.surfaceWater=true;
  ServiceFacility raw{ServiceType::Sanitation,{11,10},18,1.f}; raw.surfaceWater=true;
  std::vector<ServiceFacility> facilities{intake,other,raw};
  ServiceCoverageCache cache; ServiceSystem::buildCache(roads,facilities,cache);
  EXPECT_LT(cache.waterSupplyUnits,200.f);
  EXPECT_FLOAT_EQ(ServiceSystem::intakeCleanFraction(roads,facilities,other.position),1.f);
  facilities[2].sewageTreatment=true;
  EXPECT_FALSE(ServiceSystem::isCacheValid(roads,facilities,cache));
  ServiceSystem::buildCache(roads,facilities,cache);
  EXPECT_FLOAT_EQ(cache.waterSupplyUnits,200.f);
  EXPECT_FLOAT_EQ(ServiceSystem::evaluateFromCache(store,roads,cache).sanitationCoverage,1.f);
  BudgetPolicy policy; policy.servicePercent[6]=0;
  const auto closed=policy.fundedFacilities(facilities);
  ServiceSystem::buildCache(roads,closed,cache);
  EXPECT_FLOAT_EQ(cache.waterSupplyUnits,200.f);
  EXPECT_FLOAT_EQ(ServiceSystem::evaluateFromCache(store,roads,cache).sanitationCoverage,0.f);
  EXPECT_FLOAT_EQ(ServiceSystem::intakeCleanFraction(roads,closed,intake.position),1.f);
}
