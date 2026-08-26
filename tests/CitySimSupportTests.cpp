#include "gtest/gtest.h"

#include "src/core/ThreadPool.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimSupport.hpp"
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

TEST(CitySimSupportTests, ApplyEmptyZonedDeltaClampsAtZero) {
  city_sim::ConstructionState state;
  state.emptyZonedCount = 3;
  city_sim::applyEmptyZonedDelta(state, -10);
  EXPECT_EQ(state.emptyZonedCount, 0);
  city_sim::applyEmptyZonedDelta(state, 4);
  EXPECT_EQ(state.emptyZonedCount, 4);
}
