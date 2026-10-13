#include "gtest/gtest.h"
#include <array>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/Zoning.hpp"

TEST(GrowthSystemTests, SpawnsWhenZonedAndRoadAccessible) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_EQ(roads.getRoadCount(), 1u);
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {2, 3}, ZoneType::Residential));

  ZoneDemand demand;
  demand.residential = 1.0f;
  demand.commercial = 0.0f;
  demand.industrial = 0.0f;

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 7, 1.0f);

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

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 11, 1.0f);

  EXPECT_EQ(stats.totalSpawned(), 0);
  EXPECT_EQ(store.getBuildingCount(), 0u);
  EXPECT_EQ(map.getTile({5, 5}).buildingId, EntityIdUtils::NullEntity);
}

TEST(GrowthSystemTests, NoSpawnForUnzonedTiles) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({1, 1}, {2, 1});
  EXPECT_EQ(roads.getRoadCount(), 1u);

  ZoneDemand demand;
  demand.residential = 1.0f;
  demand.commercial = 1.0f;
  demand.industrial = 1.0f;

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 13, 1.0f);

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
  EXPECT_EQ(roadsA.getRoadCount(), 2u);
  EXPECT_EQ(roadsB.getRoadCount(), 2u);

  EXPECT_TRUE(Zoning::applyZoneRect(mapA, {1, 2}, {3, 2}, ZoneType::Industrial));
  EXPECT_TRUE(Zoning::applyZoneRect(mapB, {1, 2}, {3, 2}, ZoneType::Industrial));

  ZoneDemand demand;
  demand.residential = 0.0f;
  demand.commercial = 0.0f;
  demand.industrial = 0.6f;

  const GrowthStats first = GrowthSystem::runStep(mapA, storeA, demand, 99, 0.8f);
  const GrowthStats second = GrowthSystem::runStep(mapB, storeB, demand, 99, 0.8f);

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

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 5, 1.0f);

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

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 9, 1.0f);

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
      map, store, demand, static_cast<uint32_t>(100 + step), 1.0f
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
      mapA, storeA, demand, static_cast<uint32_t>(200 + step), 1.0f
    ).totalSpawned();
    spawnedB += GrowthSystem::runStep(
      mapB, storeB, demand, static_cast<uint32_t>(200 + step), 1.0f
    ).totalSpawned();
  }

  EXPECT_EQ(spawnedA, spawnedB);
  EXPECT_EQ(storeA.getBuildingCount(), storeB.getBuildingCount());
}

TEST(GrowthSystemTests, ChanceModifiersCanSuppressGrowthInRegion) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {2, 3}, ZoneType::Residential));

  ZoneDemand demand;
  demand.residential = 1.0f;
  demand.commercial = 0.0f;
  demand.industrial = 0.0f;

  std::vector<GrowthChanceModifier> modifiers;
  modifiers.push_back(GrowthChanceModifier{{0, 0}, {7, 7}, 0.0f});

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 7, 1.0f, &modifiers);

  EXPECT_EQ(stats.totalSpawned(), 0);
  EXPECT_EQ(store.getBuildingCount(), 0u);
}

TEST(GrowthSystemTests, MultiZoneDemandBalancingBiasesHigherDemandZone) {
  CityMap map({18, 18});
  RoadNetwork roads(map);
  EntityStore store;

  for (int x = 2; x < 15; ++x) {
    roads.buildRoad({x, 2}, {x + 1, 2});
    roads.buildRoad({x, 9}, {x + 1, 9});
  }

  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {15, 3}, ZoneType::Residential));
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 10}, {15, 10}, ZoneType::Commercial));

  ZoneDemand demand;
  demand.residential = 0.95f;
  demand.commercial = 0.20f;
  demand.industrial = 0.05f;

  int spawnedResidential = 0;
  int spawnedCommercial = 0;
  for (int step = 0; step < 50; ++step) {
    const GrowthStats stats = GrowthSystem::runStep(
      map,
      store,
      demand,
      static_cast<uint32_t>(700 + step),
      0.9f
    );
    spawnedResidential += stats.spawnedResidential;
    spawnedCommercial += stats.spawnedCommercial;
  }

  EXPECT_GT(spawnedResidential, spawnedCommercial);
}

TEST(GrowthSystemTests, LowDemandOverbuiltZoneCanDemolishBuildings) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  EntityStore store;

  for (int x = 2; x < 13; ++x) {
    roads.buildRoad({x, 2}, {x + 1, 2});
  }

  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {13, 3}, ZoneType::Residential));

  ZoneDemand highDemand;
  highDemand.residential = 1.0f;
  highDemand.commercial = 0.0f;
  highDemand.industrial = 0.0f;

  for (int step = 0; step < 6; ++step) {
    (void)GrowthSystem::runStep(
      map,
      store,
      highDemand,
      static_cast<uint32_t>(900 + step),
      1.0f
    );
  }

  const size_t beforeDemolition = store.getBuildingCount();
  ASSERT_GT(beforeDemolition, 0u);

  ZoneDemand lowDemand;
  lowDemand.residential = 0.01f;
  lowDemand.commercial = 0.0f;
  lowDemand.industrial = 0.0f;

  int totalDemolished = 0;
  for (int step = 0; step < 30; ++step) {
    const GrowthStats stats = GrowthSystem::runStep(
      map,
      store,
      lowDemand,
      static_cast<uint32_t>(1000 + step),
      0.3f
    );
    totalDemolished += stats.totalDemolished();
  }

  EXPECT_GT(totalDemolished, 0);
  EXPECT_LT(store.getBuildingCount(), beforeDemolition);
}

// Office demand should spawn genuine BuildingType::Office buildings on
// Office-zoned, road-accessible land, tracked separately from the other
// three zone types throughout GrowthStats.
TEST(GrowthSystemTests, SpawnsOfficeWhenZonedAndRoadAccessible) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {2, 3}, ZoneType::Office));

  ZoneDemand demand;
  demand.office = 1.0f;

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 7, 1.0f);

  EXPECT_EQ(stats.spawnedOffice, 1);
  EXPECT_EQ(stats.totalSpawned(), 1);
  ASSERT_EQ(store.getBuildingCount(), 1u);
  const EntityId id = map.getTile({2, 3}).buildingId;
  ASSERT_TRUE(EntityIdUtils::isValid(id));
  EXPECT_EQ(store.getBuilding(id)->type, BuildingType::Office);
}

// Zero office demand (the default) must not spawn any office buildings even
// when office-zoned land with road access exists - preserving prior behavior
// for every caller that doesn't set demand.office.
TEST(GrowthSystemTests, ZeroOfficeDemandSpawnsNoOffice) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {2, 3}, ZoneType::Office));

  ZoneDemand demand;  // office defaults to 0.0f

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 7, 1.0f);

  EXPECT_EQ(stats.spawnedOffice, 0);
  EXPECT_EQ(stats.totalSpawned(), 0);
  EXPECT_EQ(store.getBuildingCount(), 0u);
}

// requireUtilities=true must block growth on a tile whose Tile::connectedToPower/
// connectedToWater haven't been set true, even though it's zoned and road-accessible.
TEST(GrowthSystemTests, RequireUtilitiesBlocksGrowthWithoutPowerAndWater) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {2, 3}, ZoneType::Office));
  Tile& tile = map.getTile({2, 3});
  tile.connectedToPower = false;
  tile.connectedToWater = false;

  ZoneDemand demand;
  demand.office = 1.0f;

  const GrowthStats stats = GrowthSystem::runStep(
    map, store, demand, 7, 1.0f, nullptr, {-1, -1}, {-1, -1}, nullptr,
    /*requireUtilities=*/true);

  EXPECT_EQ(stats.totalSpawned(), 0);
  EXPECT_EQ(store.getBuildingCount(), 0u);
}

// Once both connectedToPower and connectedToWater are set on the tile, growth
// proceeds normally even with requireUtilities=true.
TEST(GrowthSystemTests, RequireUtilitiesAllowsGrowthOnceConnected) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {2, 3}, ZoneType::Office));
  Tile& tile = map.getTile({2, 3});
  tile.connectedToPower = true;
  tile.connectedToWater = true;

  ZoneDemand demand;
  demand.office = 1.0f;

  const GrowthStats stats = GrowthSystem::runStep(
    map, store, demand, 7, 1.0f, nullptr, {-1, -1}, {-1, -1}, nullptr,
    /*requireUtilities=*/true);

  EXPECT_EQ(stats.spawnedOffice, 1);
  EXPECT_EQ(stats.totalSpawned(), 1);
  ASSERT_EQ(store.getBuildingCount(), 1u);
}

// requireUtilities defaults to false, so existing callers that never pass it
// must be completely unaffected by Tile::connectedToPower/connectedToWater,
// which default to true anyway (the M7 stub) but shouldn't matter either way.
TEST(GrowthSystemTests, RequireUtilitiesDefaultsFalseAndDoesNotGateGrowth) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;

  roads.buildRoad({2, 2}, {3, 2});
  EXPECT_TRUE(Zoning::applyZoneRect(map, {2, 3}, {2, 3}, ZoneType::Office));

  ZoneDemand demand;
  demand.office = 1.0f;

  const GrowthStats stats = GrowthSystem::runStep(map, store, demand, 7, 1.0f);

  EXPECT_EQ(stats.spawnedOffice, 1);
  EXPECT_EQ(stats.totalSpawned(), 1);
}

TEST(GrowthSystemTests, RedevelopmentLocationFactorTracksValueCoverageAndCongestion) {
  EXPECT_FLOAT_EQ(redevelopmentLocationFactor(kRedevelopmentLandValueFloor, 1.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(redevelopmentLocationFactor(kRedevelopmentLandValueCeiling, 1.0f, 0.0f), 1.0f);
  EXPECT_FLOAT_EQ(redevelopmentLocationFactor(kRedevelopmentLandValueCeiling, 0.0f, 0.0f), 0.25f);
  EXPECT_FLOAT_EQ(redevelopmentLocationFactor(kRedevelopmentLandValueCeiling, 1.0f, 1.0f), 0.0f);
  EXPECT_FLOAT_EQ(redevelopmentLocationFactor(kRedevelopmentLandValueCeiling, 1.0f, 0.5f), 0.5f);

  const float mid = (kRedevelopmentLandValueFloor + kRedevelopmentLandValueCeiling) * 0.5f;
  EXPECT_FLOAT_EQ(redevelopmentLocationFactor(mid, 1.0f, 0.0f), 0.5f);
}

TEST(GrowthSystemTests, DensityTierAndConstructionCostSeparateLowAndRedevelopedBuildings) {
  EXPECT_EQ(buildingDensityTier(BuildingType::Residential, 8), 0);
  EXPECT_EQ(buildingDensityTier(BuildingType::Residential, 16), 1);
  EXPECT_EQ(buildingDensityTier(BuildingType::Residential, 128), 4);
  EXPECT_EQ(buildingDensityTier(BuildingType::Residential, 512), 6);
  EXPECT_EQ(buildingDensityTier(BuildingType::Residential, 4096), 6);
  EXPECT_EQ(defaultBuildingCapacity(BuildingType::Commercial), 20);
  EXPECT_EQ(buildingDensityTier(BuildingType::Commercial, 40), 1);

  const int64_t low = buildingConstructionCost(BuildingType::Residential, 8);
  const int64_t doubled = buildingConstructionCost(BuildingType::Residential, 16);
  const int64_t sixteen = buildingConstructionCost(BuildingType::Residential, 128);
  EXPECT_EQ(low, kLowBuildingConstructionCost);
  EXPECT_GT(doubled, low);
  EXPECT_GT(sixteen, doubled);
  EXPECT_EQ(doubled, low * 2);
  EXPECT_EQ(sixteen, low * 16);
}

// A saturated zone still densifies on the valuable, served, clear tile.
// The fringe tile and the jammed high-value tile stay at the low capacity.
TEST(GrowthSystemTests, HighestCapacityFollowsLandValueNotTheSaturatedFringe) {
  CityMap map({12, 6});
  RoadNetwork roads(map);
  EntityStore store;

  for (int x = 1; x < 9; ++x) {
    roads.buildRoad({x, 2}, {x + 1, 2});
  }

  const Coord fringe{2, 2};
  const Coord jammed{4, 2};
  const Coord prime{7, 2};
  const Coord sites[] = {fringe, jammed, prime};
  for (const Coord site : sites) {
    map.getTile(site).zone = static_cast<int>(ZoneType::Residential);
    map.getTile(site).buildingId = store.createBuilding(BuildingType::Residential, site, 8);
  }
  map.landValue(fringe) = kRedevelopmentLandValueFloor;
  map.landValue(jammed) = kRedevelopmentLandValueCeiling;
  map.landValue(prime) = kRedevelopmentLandValueCeiling;
  roads.updateCongestion({3, 2}, {4, 2}, kLocalRoadCapacity);
  roads.updateCongestion({4, 2}, {5, 2}, kLocalRoadCapacity);

  std::vector<ServiceFacility> facilities;
  for (ServiceType type : {ServiceType::Fire, ServiceType::Police, ServiceType::Health, ServiceType::Education}) {
    ServiceFacility facility;
    facility.type = type;
    facility.position = prime;
    facility.maxTravelDistance = 20;
    facilities.push_back(facility);
  }
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);

  ZoneDemand demand;
  demand.residential = 1.0f;

  for (int step = 0; step < 300; ++step) {
    GrowthSystem::runStep(
      map, store, demand, static_cast<uint32_t>(40 + step), 0.0f,
      nullptr, {-1, -1}, {-1, -1}, nullptr, false, &roads, &cache);
  }

  const Building* fringeBuilding = store.getBuilding(map.getTile(fringe).buildingId);
  const Building* jammedBuilding = store.getBuilding(map.getTile(jammed).buildingId);
  const Building* primeBuilding = store.getBuilding(map.getTile(prime).buildingId);
  ASSERT_NE(fringeBuilding, nullptr);
  ASSERT_NE(jammedBuilding, nullptr);
  ASSERT_NE(primeBuilding, nullptr);
  EXPECT_EQ(fringeBuilding->capacity, 8);
  EXPECT_EQ(jammedBuilding->capacity, 8);
  EXPECT_GT(primeBuilding->capacity, jammedBuilding->capacity);
}

TEST(GrowthSystemTests, LowDemandDoesNotDemolishOccupiedHousingOrJobs) {
  CityMap map({8, 8});
  EntityStore store;
  const std::array<BuildingType, 4> types = {BuildingType::Residential,
      BuildingType::Commercial, BuildingType::Industrial, BuildingType::Office};
  std::vector<EntityId> ids;
  for (size_t i = 0; i < types.size(); ++i) {
    const Coord site{2 + static_cast<int>(i), 2};
    map.getTile(site).zone = static_cast<int>(types[i]);
    const auto id = store.createBuilding(types[i], site, 20);
    store.getBuilding(id)->occupancy = 10;
    map.getTile(site).buildingId = id;
    ids.push_back(id);
  }
  const ZoneDemand noDemand{0.f, 0.f, 0.f, 0.f};
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    const auto stats = GrowthSystem::runStep(map, store, noDemand, seed, 0.f);
    EXPECT_EQ(stats.totalDemolished(), 0);
  }
  for (const auto id : ids) ASSERT_NE(store.getBuilding(id), nullptr);
}

TEST(GrowthSystemTests, LowDemandCanStillRetireVacantStock) {
  CityMap map({8, 8});
  EntityStore store;
  const Coord site{2, 2};
  map.getTile(site).zone = static_cast<int>(ZoneType::Residential);
  const auto id = store.createBuilding(BuildingType::Residential, site, 8);
  map.getTile(site).buildingId = id;
  const ZoneDemand noDemand{0.f, 0.f, 0.f, 0.f};
  for (uint32_t seed = 1; seed <= 200 && store.getBuilding(id); ++seed)
    GrowthSystem::runStep(map, store, noDemand, seed, 0.f);
  EXPECT_EQ(store.getBuilding(id), nullptr);
}
