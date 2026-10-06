#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/DistrictSystem.hpp"
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

TEST(PlayableCityTickTests, UnpaidDeficitBecomesTreasuryDebt) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);

  std::vector<ServiceFacility> facilities;
  facilities.push_back({ServiceType::Fire, {11, 11}, 16, 1.0f});

  PlayableCityTickState state;
  state.populationTarget = 0;
  int64_t funds = 5;
  PlayableCityTickOptions options;
  options.requireUtilities = false;
  options.growthChance = 0.0f;
  options.enableTransit = false;
  options.treasuryInterestRate = 0.0;

  playableCityTick(map, roads, store, population, facilities, state, funds, options);

  EXPECT_EQ(funds, 0);
  EXPECT_EQ(state.treasuryDebt, 20);
  EXPECT_EQ(state.treasuryDebtIssued, 20);
  EXPECT_TRUE(state.bankrupt);
}

TEST(PlayableCityTickTests, SanitationCoverageLowersIllnessRate) {
  auto tickCity = [](bool placeSanitation) {
    CityMap map({24, 24});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    laySpine(map, roads);

    const Coord house{10, 11};
    map.getTile(house).buildingId = store.createBuilding(BuildingType::Residential, house, 40);

    std::vector<ServiceFacility> facilities;
    int64_t funds = 50000;
    if (placeSanitation) {
      const ServicePlan plan = ServiceTool::plan(
        map, roads, facilities, ServiceType::Sanitation, {11, 11}, funds);
      EXPECT_TRUE(plan.valid) << plan.error;
      EXPECT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
    }

    PlayableCityTickState state;
    state.populationTarget = 40;
    PlayableCityTickOptions options;
    options.requireUtilities = false;
    options.growthChance = 0.0f;
    options.enableTransit = false;
    playableCityTick(map, roads, store, population, facilities, state, funds, options);
    return state;
  };

  const PlayableCityTickState uncovered = tickCity(false);
  const PlayableCityTickState covered = tickCity(true);

  EXPECT_FLOAT_EQ(uncovered.serviceSummary.sanitationCoverage, 0.0f);
  EXPECT_GT(covered.serviceSummary.sanitationCoverage, uncovered.serviceSummary.sanitationCoverage);
  EXPECT_LT(covered.illnessRate, uncovered.illnessRate);
}

TEST(PlayableCityTickTests, DistrictsProduceLaggedGrowthModifiers) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);

  const Coord house{10, 11};
  map.getTile(house).buildingId = store.createBuilding(BuildingType::Residential, house, 40);

  DistrictSystem districts;
  districts.createDistrict("Downtown", {8, 8}, {14, 14});

  PlayableCityTickState state;
  state.populationTarget = 40;
  int64_t funds = 50000;
  PlayableCityTickOptions options;
  options.requireUtilities = false;
  options.growthChance = 0.0f;
  options.enableTransit = false;
  options.districts = &districts;

  playableCityTick(map, roads, store, population, {}, state, funds, options);

  ASSERT_EQ(state.districtGrowthModifiers.size(), 1u);
  EXPECT_EQ(state.districtGrowthModifiers[0].minCorner, Coord(8, 8));
  EXPECT_EQ(state.districtGrowthModifiers[0].maxCorner, Coord(14, 14));
  EXPECT_GT(state.districtGrowthModifiers[0].multiplier, 0.0f);
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

namespace {

uint32_t occupancyOf(const EntityStore& store, BuildingType type) {
  uint32_t total = 0;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    if (building.type == type) {
      total += static_cast<uint32_t>(building.occupancy);
    }
  }
  return total;
}

uint32_t bandSize(const PopulationStore& population, IncomeBand band) {
  uint32_t total = 0;
  for (const auto& [id, group] : population.getGroups()) {
    (void)id;
    if (group.band == band) {
      total += group.size;
    }
  }
  return total;
}

void placeLaborBuildings(CityMap& map, EntityStore& store) {
  const Coord sites[] = {{6, 11}, {9, 11}, {12, 11}, {15, 11}};
  const BuildingType types[] = {
    BuildingType::Residential, BuildingType::Commercial,
    BuildingType::Industrial, BuildingType::Office
  };
  for (size_t i = 0; i < 4; ++i) {
    map.getTile(sites[i]).buildingId = store.createBuilding(types[i], sites[i], 200);
  }
}

}  // namespace

// A school does not change this tick's labor split. The next tick, lagged
// coverage moves office employment up. The same seed with the school removed
// stays on the uneducated split. A tool refresh can change the live coverage
// number without advancing that lag.
TEST(PlayableCityTickTests, SchoolsRaiseOfficeShareOnTheNextTick) {
  CityMap schooledMap({24, 24});
  RoadNetwork schooledRoads(schooledMap);
  EntityStore schooledStore;
  PopulationStore schooledPeople;
  laySpine(schooledMap, schooledRoads);
  placeLaborBuildings(schooledMap, schooledStore);

  std::vector<ServiceFacility> schools;
  schools.push_back({ServiceType::Education, {12, 12}, 16, 1.0f});

  PlayableCityTickState schooled;
  schooled.populationTarget = 200;
  int64_t schooledFunds = 50000;
  PlayableCityTickOptions options;
  options.requireUtilities = false;
  options.growthChance = 0.0f;
  options.enableTransit = false;
  options.refreshPollution = false;
  options.updateLandValues = false;

  playableCityTick(schooledMap, schooledRoads, schooledStore, schooledPeople, schools, schooled, schooledFunds, options);

  EXPECT_EQ(schooled.deathcare.deaths, 0u);
  EXPECT_FLOAT_EQ(schooled.serviceSummary.educationCoverage, 1.0f);
  EXPECT_FLOAT_EQ(schooled.laggedEducationCoverage, 1.0f);
  EXPECT_EQ(occupancyOf(schooledStore, BuildingType::Office), 45u);
  EXPECT_EQ(bandSize(schooledPeople, IncomeBand::High), 30u);

  DerivedCityRefreshOptions toolRefresh;
  toolRefresh.runTraffic = false;
  toolRefresh.enableTransit = false;
  toolRefresh.updateLandValues = false;
  refreshDerivedCityState(
    schooledMap, schooledRoads, schooledStore, schooledPeople, {}, schooled, toolRefresh);
  EXPECT_FLOAT_EQ(schooled.serviceSummary.educationCoverage, 0.0f);
  EXPECT_FLOAT_EQ(schooled.laggedEducationCoverage, 1.0f);

  playableCityTick(schooledMap, schooledRoads, schooledStore, schooledPeople, schools, schooled, schooledFunds, options);

  EXPECT_EQ(schooled.deathcare.deaths, 0u);
  EXPECT_EQ(occupancyOf(schooledStore, BuildingType::Office), 96u);
  EXPECT_EQ(bandSize(schooledPeople, IncomeBand::High), 60u);
  EXPECT_FLOAT_EQ(schooled.laggedEducationCoverage, 1.0f);

  CityMap plainMap({24, 24});
  RoadNetwork plainRoads(plainMap);
  EntityStore plainStore;
  PopulationStore plainPeople;
  laySpine(plainMap, plainRoads);
  placeLaborBuildings(plainMap, plainStore);
  PlayableCityTickState plain;
  plain.populationTarget = 200;
  int64_t plainFunds = 50000;
  playableCityTick(plainMap, plainRoads, plainStore, plainPeople, {}, plain, plainFunds, options);
  playableCityTick(plainMap, plainRoads, plainStore, plainPeople, {}, plain, plainFunds, options);

  EXPECT_FLOAT_EQ(plain.serviceSummary.educationCoverage, 0.0f);
  EXPECT_FLOAT_EQ(plain.laggedEducationCoverage, 0.0f);
  EXPECT_EQ(occupancyOf(plainStore, BuildingType::Office), 45u);
  EXPECT_EQ(bandSize(plainPeople, IncomeBand::High), 30u);
  EXPECT_GT(occupancyOf(schooledStore, BuildingType::Office), occupancyOf(plainStore, BuildingType::Office));
}
