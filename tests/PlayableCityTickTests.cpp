#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/persistence/ReplayVerifier.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <filesystem>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/systems/PopulationSystem.hpp"
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
  PopulationSystem::allocate(schooledStore, schooledPeople, 200, 1u);

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
  PopulationSystem::allocate(plainStore, plainPeople, 200, 1u);
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

TEST(PlayableCityTickTests, RedevelopmentBillsMoreThanANewLowBuilding) {
  PlayableCityTickOptions options;
  options.requireUtilities = false;
  options.growthChance = 1.0f;
  options.enableTransit = false;
  options.refreshPollution = false;
  options.updateLandValues = false;
  options.treasuryInterestRate = 0.0;

  CityMap spawnMap({16, 12});
  RoadNetwork spawnRoads(spawnMap);
  EntityStore spawnStore;
  PopulationStore spawnPeople;
  laySpine(spawnMap, spawnRoads);
  const int midY = spawnMap.getDimensions().y / 2;
  const Coord empty{4, midY - 1};
  Zoning::applyZoneRect(spawnMap, empty, empty, ZoneType::Residential);

  PlayableCityTickState spawnState;
  spawnState.populationTarget = 0;
  int64_t spawnFunds = 80000;
  for (int step = 0; step < 8 && spawnState.buildingsSpawned == 0; ++step) {
    playableCityTick(
      spawnMap, spawnRoads, spawnStore, spawnPeople, {}, spawnState, spawnFunds, options);
  }
  ASSERT_EQ(spawnState.buildingsSpawned, 1);
  EXPECT_EQ(spawnState.constructionSpend, kLowBuildingConstructionCost);
  EXPECT_GE(spawnState.treasuryExpenses, spawnState.constructionSpend);

  CityMap map({16, 12});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);
  const Coord site{8, midY - 1};
  Zoning::applyZoneRect(map, site, site, ZoneType::Residential);
  map.getTile(site).buildingId = store.createBuilding(BuildingType::Residential, site, 8);
  map.landValue(site) = kRedevelopmentLandValueCeiling;

  std::vector<ServiceFacility> facilities;
  for (ServiceType type : {ServiceType::Fire, ServiceType::Police, ServiceType::Health, ServiceType::Education}) {
    facilities.push_back(ServiceFacility{type, {8, midY}, 12, 1.0f});
  }

  PlayableCityTickState state;
  state.populationTarget = 0;
  int64_t funds = 80000;
  int64_t redevelopSpend = 0;
  int newCapacity = 8;
  for (int step = 0; step < 400 && newCapacity == 8; ++step) {
    playableCityTick(map, roads, store, population, facilities, state, funds, options);
    const Building* building = store.getBuilding(map.getTile(site).buildingId);
    ASSERT_NE(building, nullptr);
    if (building->capacity > 8) {
      newCapacity = building->capacity;
      redevelopSpend = state.constructionSpend;
    }
  }

  EXPECT_GT(newCapacity, 8);
  EXPECT_EQ(redevelopSpend, buildingConstructionCost(BuildingType::Residential, newCapacity));
  EXPECT_GT(redevelopSpend, spawnState.constructionSpend);
  EXPECT_GE(state.treasuryExpenses, redevelopSpend);
}

namespace {
void migrationCity(CityMap &map, RoadNetwork &roads, EntityStore &store,
                   std::vector<ServiceFacility> &facilities) {
  laySpine(map, roads);
  const int y = map.getDimensions().y / 2 - 1;
  for (int x : {6, 9, 12}) {
    map.getTile({x, y}).buildingId =
        store.createBuilding(BuildingType::Residential, {x, y}, 512);
  }
  map.getTile({15, y}).buildingId =
      store.createBuilding(BuildingType::Commercial, {15, y}, 1280);
  map.getTile({18, y}).buildingId =
      store.createBuilding(BuildingType::Industrial, {18, y}, 1536);
  map.getTile({21, y}).buildingId =
      store.createBuilding(BuildingType::Office, {21, y}, 1152);
  for (ServiceType type :
       {ServiceType::Fire, ServiceType::Police, ServiceType::Health,
        ServiceType::Education, ServiceType::Power, ServiceType::Water,
        ServiceType::Sanitation, ServiceType::Garbage, ServiceType::Cemetery}) {
    facilities.push_back({type, {16, y}, 64, 1.0f});
  }
}
PlayableCityTickOptions migrationOptions() {
  PlayableCityTickOptions options;
  options.growthChance = 0.0f;
  options.refreshPollution = false;
  options.updateLandValues = false;
  return options;
}
} // namespace

TEST(PlayableCityTickTests,
     HealthyCityExceedsLegacyPopulationTargetAndUnlocksBus) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore people;
  std::vector<ServiceFacility> facilities;
  migrationCity(map, roads, store, facilities);
  PlayableCityTickState state;
  state.populationTarget =
      272; // A legacy hour-playtest save must not cap arrivals.
  int64_t funds = 50000;
  const auto options = migrationOptions();
  for (int i = 0; i < 80; ++i) {
    const auto before = people.getTotalPopulation();
    playableCityTick(map, roads, store, people, facilities, state, funds,
                     options);
    EXPECT_EQ(state.populationChange.net,
              static_cast<int64_t>(state.populationChange.arrivals) -
                  state.populationChange.departures -
                  state.populationChange.deaths);
    EXPECT_EQ(static_cast<int64_t>(people.getTotalPopulation()) - before,
              state.populationChange.net);
  }
  EXPECT_GT(people.getTotalPopulation(), 1200u);
  EXPECT_LE(people.getTotalPopulation(), 1536u);
  EXPECT_GT(state.deathcareState.fractionalDeaths, 0.0);
  EXPECT_FALSE(state.transitRoutes.empty());
}

TEST(PlayableCityTickTests,
     UtilityOutageReversesMigrationWithoutDoubleCountingDeaths) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore people;
  std::vector<ServiceFacility> facilities;
  migrationCity(map, roads, store, facilities);
  PlayableCityTickState state;
  int64_t funds = 50000;
  const auto options = migrationOptions();
  for (int i = 0; i < 20; ++i)
    playableCityTick(map, roads, store, people, facilities, state, funds,
                     options);
  const auto healthyPopulation = people.getTotalPopulation();
  facilities.erase(std::remove_if(facilities.begin(), facilities.end(),
                                  [](const ServiceFacility &f) {
                                    return f.type == ServiceType::Power ||
                                           f.type == ServiceType::Water;
                                  }),
                   facilities.end());
  for (int i = 0; i < 10; ++i) {
    playableCityTick(map, roads, store, people, facilities, state, funds,
                     options);
    if (i > 0) {
      EXPECT_EQ(state.populationChange.arrivals, 0u);
      EXPECT_GT(state.populationChange.departures, 0u);
    }
    EXPECT_EQ(state.populationChange.net,
              static_cast<int64_t>(state.populationChange.arrivals) -
                  state.populationChange.departures -
                  state.populationChange.deaths);
  }
  EXPECT_LT(people.getTotalPopulation(), healthyPopulation);
}

TEST(PlayableCityTickTests,
     SaveLoadContinuesMigrationEducationAndMortalityDeterministically) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore people;
  std::vector<ServiceFacility> facilities;
  migrationCity(map, roads, store, facilities);
  PlayableCityTickState state;
  int64_t funds = 50000;
  const auto options = migrationOptions();
  for (int i = 0; i < 13; ++i)
    playableCityTick(map, roads, store, people, facilities, state, funds,
                     options);
  GameplaySessionState saved;
  saved.tick = state.tick;
  saved.funds = funds;
  saved.populationTarget = state.populationTarget;
  saved.migrationConditions = state.migrationConditions;
  saved.populationChange = state.populationChange;
  saved.laggedEducationCoverage = state.laggedEducationCoverage;
  saved.fractionalDeaths = state.deathcareState.fractionalDeaths;
  saved.awaitingDisposition = state.deathcareState.awaitingDisposition;
  saved.growthModifiers = state.districtGrowthModifiers;
  saved.transitRoutes = state.transitRoutes;
  saved.facilities = facilities;
  const auto path = std::filesystem::temp_directory_path() /
                    "urban_migration_continuation.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store,
                                          people, saved));
  CityMap resumedMap({32, 32});
  RoadNetwork resumedRoads(resumedMap);
  EntityStore resumedStore;
  PopulationStore resumedPeople;
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), resumedMap,
                                          resumedRoads, resumedStore,
                                          resumedPeople, loaded));
  PlayableCityTickState resumed;
  resumed.tick = loaded.tick;
  resumed.populationTarget = loaded.populationTarget;
  resumed.migrationConditions = loaded.migrationConditions;
  resumed.populationChange = loaded.populationChange;
  resumed.laggedEducationCoverage = loaded.laggedEducationCoverage;
  resumed.deathcareState = {loaded.fractionalDeaths,
                            loaded.awaitingDisposition};
  resumed.districtGrowthModifiers = loaded.growthModifiers;
  resumed.transitRoutes = loaded.transitRoutes;
  // The host refreshes HUD summaries on load without advancing simulation.
  const auto checkpoint = ReplayVerifier::calculateChecksum(
      resumedMap, resumedRoads, resumedStore, resumedPeople);
  DerivedCityRefreshOptions refresh;
  refresh.preserveCheckpoint = true;
  refresh.runTraffic = true;
  refresh.enableTransit = true;
  refresh.seed = 777u;
  refreshDerivedCityState(resumedMap, resumedRoads, resumedStore, resumedPeople,
                          loaded.facilities, resumed, refresh);
  EXPECT_EQ(checkpoint,
            ReplayVerifier::calculateChecksum(resumedMap, resumedRoads,
                                              resumedStore, resumedPeople));
  EXPECT_FLOAT_EQ(state.trafficSummary.averageEdgeCongestion,
                  resumed.trafficSummary.averageEdgeCongestion);
  EXPECT_FLOAT_EQ(state.trafficSummary.maxEdgeCongestion,
                  resumed.trafficSummary.maxEdgeCongestion);
  int64_t resumedFunds = loaded.funds;
  for (int i = 0; i < 30; ++i) {
    playableCityTick(map, roads, store, people, facilities, state, funds,
                     options);
    playableCityTick(resumedMap, resumedRoads, resumedStore, resumedPeople,
                     loaded.facilities, resumed, resumedFunds, options);
    EXPECT_EQ(people.getTotalPopulation(), resumedPeople.getTotalPopulation());
    EXPECT_EQ(funds, resumedFunds);
    EXPECT_EQ(ReplayVerifier::calculateChecksum(map, roads, store, people),
              ReplayVerifier::calculateChecksum(resumedMap, resumedRoads,
                                                resumedStore, resumedPeople));
    EXPECT_EQ(state.populationChange.net, resumed.populationChange.net);
    EXPECT_EQ(state.populationChange.deaths, resumed.populationChange.deaths);
    EXPECT_FLOAT_EQ(state.populationChange.desirability,
                    resumed.populationChange.desirability);
    EXPECT_DOUBLE_EQ(state.deathcareState.fractionalDeaths,
                     resumed.deathcareState.fractionalDeaths);
    EXPECT_EQ(state.transitRoutes.size(), resumed.transitRoutes.size());
    EXPECT_EQ(occupancyOf(store, BuildingType::Office),
              occupancyOf(resumedStore, BuildingType::Office));
  }
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}
