#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

namespace {

void laySpine(CityMap& map, RoadNetwork& roads) {
  const int midY = map.getDimensions().y / 2;
  for (int x = 2; x < map.getDimensions().x - 2; ++x) {
    roads.buildRoad({x, midY}, {x + 1, midY});
  }
  roads.updateConnectivity({map.getDimensions().x / 2, midY});
}

}  // namespace

TEST(PlayableCityTickTests, IndustrialBuildingsEmitPollution) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);

  const Coord site{12, 11};
  Zoning::applyZoneRect(map, site, site, ZoneType::Industrial);
  const EntityId id = store.createBuilding(BuildingType::Industrial, site, 20);
  map.getTile(site).buildingId = id;

  PlayableCityTickState state;
  state.populationTarget = 0;
  int64_t funds = 50000;
  PlayableCityTickOptions options;
  options.requireUtilities = false;
  options.growthChance = 0.0f;
  options.enableTransit = false;

  playableCityTick(map, roads, store, population, {}, state, funds, options);

  EXPECT_GT(map.pollution(site), 0.0f);
}

TEST(PlayableCityTickTests, LandValueRecomputesFromJobsAndPollution) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);

  const Coord job{12, 11};
  const Coord house{8, 11};
  Zoning::applyZoneRect(map, job, job, ZoneType::Industrial);
  Zoning::applyZoneRect(map, house, house, ZoneType::Residential);
  map.getTile(job).buildingId = store.createBuilding(BuildingType::Industrial, job, 20);
  map.getTile(house).buildingId = store.createBuilding(BuildingType::Residential, house, 20);

  const float houseValueBefore = map.landValue(house);

  PlayableCityTickState state;
  state.populationTarget = 10;
  int64_t funds = 50000;
  PlayableCityTickOptions options;
  options.requireUtilities = false;
  options.growthChance = 0.0f;
  options.enableTransit = false;

  playableCityTick(map, roads, store, population, {}, state, funds, options);

  EXPECT_NE(map.landValue(house), houseValueBefore);
}

TEST(PlayableCityTickTests, PlayableTickReportsCrimeAndIllnessRates) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);

  const Coord house{10, 11};
  map.getTile(house).buildingId = store.createBuilding(BuildingType::Residential, house, 40);
  PlayableCityTickState state;
  state.populationTarget = 40;
  int64_t funds = 50000;
  PlayableCityTickOptions options;
  options.requireUtilities = false;
  options.growthChance = 0.0f;
  options.enableTransit = false;

  playableCityTick(map, roads, store, population, {}, state, funds, options);

  EXPECT_GE(state.illnessRate, 0.0f);
  EXPECT_LE(state.illnessRate, 1.0f);
  EXPECT_GE(state.crimeRate, 0.0f);
  EXPECT_LE(state.crimeRate, 1.0f);
  EXPECT_GT(state.crimeRate, 0.0f);
}

TEST(PlayableCityTickTests, RefreshDerivedStateUpdatesServicesWithoutAdvancingTick) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);

  const Coord house{7, 7};
  map.getTile(house).buildingId = store.createBuilding(BuildingType::Residential, house, 20);

  std::vector<ServiceFacility> facilities;
  int64_t funds = 20000;
  const ServicePlan plan = ServiceTool::plan(
    map, roads, facilities, ServiceType::Fire, {8, 7}, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));

  PlayableCityTickState state;
  state.tick = 4;
  DerivedCityRefreshOptions refresh;
  refresh.runTraffic = false;
  refresh.updateLandValues = true;
  refreshDerivedCityState(map, roads, store, population, facilities, state, refresh);

  EXPECT_EQ(state.tick, 4u);
  EXPECT_GT(state.serviceSummary.fireCoverage, 0.0f);
  EXPECT_EQ(state.serviceSummary.totalBuildings, 1u);
}

TEST(PlayableCityTickTests, DisablingEnvironmentPhasesLeavesFieldsUntouched) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);

  const Coord site{8, 7};
  Zoning::applyZoneRect(map, site, site, ZoneType::Industrial);
  map.getTile(site).buildingId = store.createBuilding(BuildingType::Industrial, site, 20);
  const float pollutionBefore = map.pollution(site);
  const float landBefore = map.landValue(site);

  PlayableCityTickState state;
  int64_t funds = 50000;
  PlayableCityTickOptions options;
  options.requireUtilities = false;
  options.growthChance = 0.0f;
  options.enableTransit = false;
  options.refreshPollution = false;
  options.updateLandValues = false;

  playableCityTick(map, roads, store, population, {}, state, funds, options);

  EXPECT_FLOAT_EQ(map.pollution(site), pollutionBefore);
  EXPECT_FLOAT_EQ(map.landValue(site), landBefore);
}
