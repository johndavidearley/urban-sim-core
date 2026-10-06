#include "gtest/gtest.h"

#include "src/core/ThreadPool.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimSupport.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

namespace {

city_sim::ConstructionRegion runOnce(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  city_sim::ConstructionState& state,
  ThreadPool& pool,
  const ZoneDemand& demand,
  const city_sim::ConstructionOptions& options
) {
  std::vector<ServiceFacility> facilities;
  std::vector<TransitRoute> routes;
  return city_sim::expandConstruction(
    map, roads, store, population, facilities, routes, state, pool, demand, options);
}

}  // namespace

TEST(CitySimSupportTests, ExpandConstructionSeedsRoadsAndZones) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  city_sim::ConstructionState state;
  ThreadPool pool(1);

  ZoneDemand demand;
  demand.residential = 1.0f;

  city_sim::ConstructionOptions options;
  options.gridSpacing = 4;
  options.zoneBatchPerTick = 16;
  const city_sim::ConstructionRegion region =
    runOnce(map, roads, store, population, state, pool, demand, options);

  EXPECT_GT(state.extent, 0);
  EXPECT_EQ(region.extent, state.extent);
  EXPECT_GT(roads.getRoadCount(), 0u);
  EXPECT_GT(state.emptyZonedCount, 0);
  EXPECT_GT(state.zoningCandidatesExtent, 0);

  int zoned = 0;
  const glm::ivec2 dims = map.getDimensions();
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      if (map.zone({x, y}) != 0) {
        ++zoned;
      }
    }
  }
  EXPECT_GT(zoned, 0);
  EXPECT_LE(zoned, options.zoneBatchPerTick);
}

TEST(CitySimSupportTests, EmptyZonedCountPacesRoadExpansion) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  city_sim::ConstructionState state;
  ThreadPool pool(1);

  ZoneDemand demand;
  demand.residential = 1.0f;
  city_sim::ConstructionOptions options;
  options.gridSpacing = 4;
  options.zoneBatchPerTick = 8;

  runOnce(map, roads, store, population, state, pool, demand, options);
  const int extentAfterFirst = state.extent;
  const size_t roadsAfterFirst = roads.getRoadCount();
  ASSERT_GT(extentAfterFirst, 0);

  state.emptyZonedCount = static_cast<int64_t>(3 * options.zoneBatchPerTick);
  runOnce(map, roads, store, population, state, pool, demand, options);

  EXPECT_EQ(state.extent, extentAfterFirst);
  EXPECT_EQ(roads.getRoadCount(), roadsAfterFirst);
}

TEST(CitySimSupportTests, GModeOptionsPlaceUtilities) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  city_sim::ConstructionState state;
  ThreadPool pool(1);
  std::vector<ServiceFacility> facilities;
  std::vector<TransitRoute> routes;

  ZoneDemand demand;
  demand.residential = 1.0f;
  city_sim::ConstructionOptions options;
  options.placeFacilities = true;
  options.includeUtilities = true;
  options.includeWasteDeathcare = true;

  city_sim::expandConstruction(
    map, roads, store, population, facilities, routes, state, pool, demand, options);

  bool hasPower = false;
  bool hasWater = false;
  for (const ServiceFacility& facility : facilities) {
    hasPower = hasPower || facility.type == ServiceType::Power;
    hasWater = hasWater || facility.type == ServiceType::Water;
  }
  EXPECT_TRUE(hasPower);
  EXPECT_TRUE(hasWater);
}

TEST(CitySimSupportTests, ExpandConstructionHonorsIndustrialOrdinance) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  city_sim::ConstructionState state;
  ThreadPool pool(1);

  DistrictSystem districts;
  const DistrictId id = districts.createDistrict("Factory", {0, 0}, {31, 31});
  ASSERT_TRUE(districts.setDistrictArchetype(id, DistrictArchetype::Industrial));

  ZoneDemand demand;
  demand.residential = 1.0f;
  city_sim::ConstructionOptions options;
  options.gridSpacing = 4;
  options.zoneBatchPerTick = 16;
  options.districts = &districts;

  for (int i = 0; i < 4; ++i) {
    runOnce(map, roads, store, population, state, pool, demand, options);
  }

  int residential = 0;
  int other = 0;
  const glm::ivec2 dims = map.getDimensions();
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      const int zone = map.zone({x, y});
      if (zone == static_cast<int>(ZoneType::Residential)
          || zone == static_cast<int>(ZoneType::Office)) {
        ++residential;
      } else if (zone != 0) {
        ++other;
      }
    }
  }
  EXPECT_EQ(residential, 0);
  EXPECT_GT(other, 0);
}

TEST(CitySimSupportTests, ApplyEmptyZonedDeltaClampsAtZero) {
  city_sim::ConstructionState state;
  state.emptyZonedCount = 3;
  city_sim::applyEmptyZonedDelta(state, -10);
  EXPECT_EQ(state.emptyZonedCount, 0);
  city_sim::applyEmptyZonedDelta(state, 4);
  EXPECT_EQ(state.emptyZonedCount, 4);
}

namespace {

void layRoadRow(RoadNetwork& roads, int y, int x0, int x1) {
  for (int x = x0; x < x1; ++x) {
    roads.buildRoad({x, y}, {x + 1, y});
  }
}

void applyUtilityLoad(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  const std::vector<ServiceFacility>& facilities
) {
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);
  ThreadPool pool(2);
  const glm::ivec2 dims = map.getDimensions();
  city_sim::updateUtilityConnectivity(
    map, roads, store, cache, 0, 0, dims.x - 1, dims.y - 1, pool);
}

ServiceFacility plantAt(ServiceType type, Coord position, float capacity) {
  ServiceFacility facility;
  facility.type = type;
  facility.position = position;
  facility.maxTravelDistance = 12;
  if (type == ServiceType::Power) {
    facility.powerCapacityMW = capacity;
  } else if (type == ServiceType::Water) {
    facility.waterSupplyUnits = capacity;
  }
  return facility;
}

}  // namespace

// Enough plant capacity leaves every graph-covered tile connected, including
// an empty tile past the buildings. The BFS itself is unchanged.
TEST(CitySimSupportTests, FullUtilitySupplyKeepsGraphCoverage) {
  CityMap map({12, 8});
  RoadNetwork roads(map);
  EntityStore store;
  layRoadRow(roads, 2, 0, 6);

  const EntityId nearId = store.createBuilding(BuildingType::Commercial, {1, 3}, 20);
  store.getBuilding(nearId)->occupancy = 10;
  map.getTile({1, 3}).buildingId = nearId;
  const EntityId farId = store.createBuilding(BuildingType::Commercial, {6, 3}, 20);
  store.getBuilding(farId)->occupancy = 10;
  map.getTile({6, 3}).buildingId = farId;

  applyUtilityLoad(map, roads, store, {
    plantAt(ServiceType::Power, {0, 2}, 100.0f),
    plantAt(ServiceType::Water, {0, 2}, 100.0f)
  });

  EXPECT_TRUE(map.getTile({1, 3}).connectedToPower);
  EXPECT_TRUE(map.getTile({6, 3}).connectedToPower);
  EXPECT_TRUE(map.getTile({6, 1}).connectedToPower);
  EXPECT_TRUE(map.getTile({1, 3}).connectedToWater);
  EXPECT_TRUE(map.getTile({6, 1}).connectedToWater);

  // A building with no road anchor is still uncovered. Shedding does not
  // invent coverage the BFS never had.
  const EntityId stranded = store.createBuilding(BuildingType::Commercial, {0, 0}, 20);
  store.getBuilding(stranded)->occupancy = 10;
  map.getTile({0, 0}).buildingId = stranded;
  applyUtilityLoad(map, roads, store, {
    plantAt(ServiceType::Power, {0, 2}, 100.0f),
    plantAt(ServiceType::Water, {0, 2}, 100.0f)
  });
  EXPECT_FALSE(map.getTile({0, 0}).connectedToPower);
  EXPECT_FALSE(map.getTile({0, 0}).connectedToWater);
}

// Supply for one commercial building of occupancy 10 (0.04). The farther
// building sheds. An empty tile at that same distance goes dark, so growth
// stops there. A closer empty tile stays connected and can still build.
TEST(CitySimSupportTests, UtilityShortfallShedsFarthestAndBlocksGrowthThere) {
  CityMap map({12, 8});
  RoadNetwork roads(map);
  EntityStore store;
  layRoadRow(roads, 2, 0, 6);

  const EntityId nearId = store.createBuilding(BuildingType::Commercial, {1, 3}, 20);
  store.getBuilding(nearId)->occupancy = 10;
  map.getTile({1, 3}).buildingId = nearId;
  const EntityId farId = store.createBuilding(BuildingType::Commercial, {6, 3}, 20);
  store.getBuilding(farId)->occupancy = 10;
  map.getTile({6, 3}).buildingId = farId;
  // Draws nothing, so it keeps power even though it is the farthest building.
  const EntityId emptyFarId = store.createBuilding(BuildingType::Commercial, {5, 3}, 20);
  store.getBuilding(emptyFarId)->occupancy = 0;
  map.getTile({5, 3}).buildingId = emptyFarId;

  applyUtilityLoad(map, roads, store, {
    plantAt(ServiceType::Power, {0, 2}, 0.04f),
    plantAt(ServiceType::Water, {0, 2}, 100.0f)
  });

  EXPECT_TRUE(map.getTile({1, 3}).connectedToPower);
  EXPECT_FALSE(map.getTile({6, 3}).connectedToPower);
  EXPECT_TRUE(map.getTile({5, 3}).connectedToPower);
  EXPECT_FALSE(map.getTile({6, 1}).connectedToPower);
  EXPECT_TRUE(map.getTile({1, 1}).connectedToPower);
  EXPECT_TRUE(map.getTile({6, 3}).connectedToWater);
  EXPECT_TRUE(map.getTile({1, 1}).connectedToWater);

  EXPECT_TRUE(Zoning::applyZoneRect(map, {6, 1}, {6, 1}, ZoneType::Office));
  ZoneDemand demand;
  demand.office = 1.0f;
  const GrowthStats blocked = GrowthSystem::runStep(
    map, store, demand, 7, 1.0f, nullptr, {-1, -1}, {-1, -1}, nullptr,
    /*requireUtilities=*/true);
  EXPECT_EQ(blocked.totalSpawned(), 0);
  EXPECT_FALSE(EntityIdUtils::isValid(map.getTile({6, 1}).buildingId));

  map.getTile({6, 1}).zone = static_cast<int>(ZoneType::None);
  EXPECT_TRUE(Zoning::applyZoneRect(map, {1, 1}, {1, 1}, ZoneType::Office));
  const GrowthStats allowed = GrowthSystem::runStep(
    map, store, demand, 7, 1.0f, nullptr, {-1, -1}, {-1, -1}, nullptr,
    /*requireUtilities=*/true);
  EXPECT_EQ(allowed.spawnedOffice, 1);
  EXPECT_TRUE(EntityIdUtils::isValid(map.getTile({1, 1}).buildingId));
}

// Same distance: the higher id sheds first. Water shortage is independent
// of a surplus of power.
TEST(CitySimSupportTests, UtilityShedOrderBreaksTiesByEntityIdAndWaterIsSeparate) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  roads.buildRoad({2, 2}, {2, 1});
  roads.buildRoad({2, 2}, {2, 3});

  const EntityId lowId = store.createBuilding(BuildingType::Commercial, {2, 1}, 20);
  store.getBuilding(lowId)->occupancy = 10;
  map.getTile({2, 1}).buildingId = lowId;
  const EntityId highId = store.createBuilding(BuildingType::Commercial, {2, 3}, 20);
  store.getBuilding(highId)->occupancy = 10;
  map.getTile({2, 3}).buildingId = highId;
  ASSERT_LT(lowId, highId);

  applyUtilityLoad(map, roads, store, {
    plantAt(ServiceType::Power, {2, 2}, 0.04f),
    plantAt(ServiceType::Water, {2, 2}, 0.04f)
  });

  EXPECT_TRUE(map.getTile({2, 1}).connectedToPower);
  EXPECT_FALSE(map.getTile({2, 3}).connectedToPower);
  EXPECT_TRUE(map.getTile({2, 1}).connectedToWater);
  EXPECT_FALSE(map.getTile({2, 3}).connectedToWater);

  // A surplus of power does not keep a building that water had to shed.
  applyUtilityLoad(map, roads, store, {
    plantAt(ServiceType::Power, {2, 2}, 100.0f),
    plantAt(ServiceType::Water, {2, 2}, 0.04f)
  });
  EXPECT_TRUE(map.getTile({2, 1}).connectedToPower);
  EXPECT_TRUE(map.getTile({2, 3}).connectedToPower);
  EXPECT_TRUE(map.getTile({2, 1}).connectedToWater);
  EXPECT_FALSE(map.getTile({2, 3}).connectedToWater);
}
