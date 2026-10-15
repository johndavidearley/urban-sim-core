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

TEST(PlayableCityTickTests, DerivedPlayableEconomyDoesNotTaxVacantCapacity) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  const Coord site{5, 5};
  map.getTile(site).zone = static_cast<int>(ZoneType::Residential);
  map.getTile(site).buildingId = store.createBuilding(BuildingType::Residential, site, 512);
  PlayableCityTickState state;
  DerivedCityRefreshOptions options;
  options.preserveCheckpoint = true;
  options.runTraffic = false;
  options.enableTransit = false;
  options.updateLandValues = false;
  refreshDerivedCityState(map, roads, store, population, {}, state, options);
  EXPECT_EQ(state.economy.residentialTaxRevenue, 0);
  EXPECT_EQ(state.economy.residentialMaintenance, 3200);
  EXPECT_LT(state.economy.balance, 0);
}

TEST(PlayableCityTickTests, ParksRaiseNearbyValueAndPayUpkeepWithoutGrowingBuildings) {
  CityMap map({12, 12}); RoadNetwork roads(map); EntityStore store; PopulationStore population;
  map.setZone({3, 3}, static_cast<int>(ZoneType::Residential));
  map.setZone({9, 9}, static_cast<int>(ZoneType::Residential));
  PlayableCityTickState state;
  DerivedCityRefreshOptions refresh; refresh.runTraffic = false; refresh.enableTransit = false;
  refreshDerivedCityState(map, roads, store, population, {}, state, refresh);
  const float before = map.landValue({3, 3});
  map.setZone({4, 3}, static_cast<int>(ZoneType::Park));
  refreshDerivedCityState(map, roads, store, population, {}, state, refresh);
  EXPECT_FLOAT_EQ(map.landValue({3, 3}), before + 22.5f);
  EXPECT_FLOAT_EQ(map.landValue({9, 9}), before);
  EXPECT_EQ(state.economy.parkTiles, 1);
  EXPECT_EQ(state.economy.parkMaintenance, 200);
  PlayableCityTickOptions options; options.growthChance = 0.f;
  int64_t funds = 1000;
  playableCityTick(map, roads, store, population, {}, state, funds, options);
  EXPECT_EQ(state.treasuryExpenses, 2);
  EXPECT_EQ(funds, 998);
  EXPECT_EQ(map.getTile({4, 3}).buildingId, 0u);
  map.setZone({4, 3}, static_cast<int>(ZoneType::None));
  playableCityTick(map, roads, store, population, {}, state, funds, options);
  EXPECT_EQ(state.treasuryExpenses, 0);
  EXPECT_EQ(state.economy.parkTiles, 0);
  EXPECT_FLOAT_EQ(map.landValue({3, 3}), before);
}

TEST(PlayableCityTickTests, DisastersAreOptInAndFireLossesRefreshTheCity) {
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);
  for (int x = 5; x <= 8; ++x) {
    auto& tile = map.getTile({x, 13});
    tile.zone = static_cast<int>(ZoneType::Residential);
    tile.buildingId = store.createBuilding(BuildingType::Residential, {x, 13}, 16);
  }
  PopulationSystem::allocate(store, population, 64, 1);
  PlayableCityTickState state;
  PlayableCityTickOptions options;
  options.growthChance = 0.f;
  options.requireUtilities = false;
  options.fireParams.baseIgnitionChance = 1.f;
  options.disasterParams.earthquakeChancePerTick = 0.f;
  options.disasterParams.floodChancePerTick = 0.f;
  int64_t funds = 50000;
  playableCityTick(map, roads, store, population, {}, state, funds, options);
  EXPECT_EQ(store.getBuildingCount(), 4u);
  EXPECT_EQ(state.disasters.fire.activeFires, 0u);
  options.enableDisasters = true;
  playableCityTick(map, roads, store, population, {}, state, funds, options);
  EXPECT_EQ(state.disasters.fire.activeFires, 4u);
  EXPECT_EQ(state.disasters.buildingsLostToFire, 4u);
  EXPECT_EQ(store.getBuildingCount(), 0u);
  EXPECT_EQ(population.getTotalPopulation(), 0u);
  EXPECT_EQ(state.economy.totalRevenue, 0);
  EXPECT_EQ(state.buildingsDemolished, 4);
  EXPECT_EQ(state.populationChange.net,
            static_cast<int64_t>(state.populationChange.arrivals) -
                state.populationChange.departures - state.populationChange.deaths);
  options.growthChance = 1.f;
  options.fireParams.baseIgnitionChance = 0.f;
  playableCityTick(map, roads, store, population, {}, state, funds, options);
  EXPECT_EQ(store.getBuildingCount(), 0u); // Burning lots cannot rebuild.
  EXPECT_EQ(map.getTile({5, 13}).zone, static_cast<int>(ZoneType::Residential));
  options.enableDisasters = false;
  options.growthChance = 0.f;
  playableCityTick(map, roads, store, population, {}, state, funds, options);
  EXPECT_TRUE(state.disasters.burningTiles.empty());
  EXPECT_EQ(state.disasters.buildingsLostToFire, 4u);
}

TEST(PlayableCityTickTests, EarthquakeAndFloodUseTheSameOptInSwitch) {
  for (bool flood : {false, true}) {
    CityMap map({24, 24});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    laySpine(map, roads);
    auto& tile = map.getTile({7, 13});
    tile.zone = static_cast<int>(ZoneType::Residential);
    tile.buildingId = store.createBuilding(BuildingType::Residential, {7, 13}, 16);
    map.getTile({7, 14}).type = 2;
    PlayableCityTickState state;
    PlayableCityTickOptions options;
    options.growthChance = 0.f;
    options.requireUtilities = false;
    options.enableDisasters = true;
    options.fireParams.baseIgnitionChance = 0.f;
    options.disasterParams.earthquakeChancePerTick = flood ? 0.f : 1.f;
    options.disasterParams.earthquakeDestructionChance = 1.f;
    options.disasterParams.floodChancePerTick = flood ? 1.f : 0.f;
    options.disasterParams.floodDestructionChance = 1.f;
    int64_t funds = 50000;
    playableCityTick(map, roads, store, population, {}, state, funds, options);
    EXPECT_EQ(state.disasters.disaster.earthquakeOccurred, !flood);
    EXPECT_EQ(state.disasters.disaster.floodOccurred, flood);
    EXPECT_EQ(state.disasters.buildingsLostToDisaster, 1u);
    EXPECT_EQ(store.getBuildingCount(), 0u);
  }
}

TEST(PlayableCityTickTests, TransitCoverageRefreshesWhenRoadsExpand) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  roads.buildRoad({1, 5}, {2, 5});
  PlayableCityTickState state;
  TransitRoute route;
  route.stops = {{1, 5}};
  route.stopCoverageRadius = 10;
  state.transitRoutes.push_back(route);
  refreshDerivedCityState(map, roads, store, population, {}, state);
  ASSERT_EQ(state.transitCache.entries.size(), 1u);
  EXPECT_EQ(state.transitCache.entries.front().distanceField.count({4, 5}), 0u);
  roads.buildRoad({2, 5}, {3, 5});
  roads.buildRoad({3, 5}, {4, 5});
  refreshDerivedCityState(map, roads, store, population, {}, state);
  EXPECT_EQ(state.transitCache.entries.front().distanceField.count({4, 5}), 1u);
  EXPECT_EQ(state.transitCache.topologyVersion, roads.getTopologyVersion());
}

TEST(PlayableCityTickTests, CitywideTaxesChangeRevenueAndLedgerWithoutChangingDefaultRates) {
  const TaxRates defaults;
  const auto policyDefaults = BudgetPolicy{}.rates();
  EXPECT_FLOAT_EQ(defaults.residentialRate, policyDefaults.residentialRate);
  EXPECT_FLOAT_EQ(defaults.commercialRate, policyDefaults.commercialRate);
  EXPECT_FLOAT_EQ(defaults.industrialRate, policyDefaults.industrialRate);
  EXPECT_FLOAT_EQ(defaults.officeRate, policyDefaults.officeRate);
  EXPECT_FLOAT_EQ(defaults.incomeRate, policyDefaults.incomeRate);
  CityMap map({24, 24});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);
  auto& tile = map.getTile({7, 13});
  tile.zone = static_cast<int>(ZoneType::Residential);
  tile.buildingId = store.createBuilding(BuildingType::Residential, {7, 13}, 80);
  PopulationSystem::allocate(store, population, 80, 1);
  PlayableCityTickState state;
  refreshDerivedCityState(map, roads, store, population, {}, state);
  const auto originalRevenue = state.economy.residentialTaxRevenue;
  state.budgetPolicy.taxPercent[0] = 20;
  refreshDerivedCityState(map, roads, store, population, {}, state);
  EXPECT_GE(state.economy.residentialTaxRevenue, originalRevenue * 4);
  PlayableCityTickOptions options;
  options.growthChance = 0.f;
  options.requireUtilities = false;
  int64_t funds = 0;
  state.treasuryDebt = 1000;
  playableCityTick(map, roads, store, population, {}, state, funds, options);
  const auto& ledger = state.treasuryLedger;
  EXPECT_TRUE(ledger.valid);
  EXPECT_EQ(ledger.tick, state.tick);
  EXPECT_EQ(ledger.breakdown.totalRevenue, state.treasuryRevenue);
  EXPECT_EQ(ledger.breakdown.totalExpenses, state.treasuryExpenses);
  EXPECT_EQ(ledger.fundsAfter, funds);
  EXPECT_EQ(ledger.debtBefore + ledger.flow.debtIssued + ledger.flow.interestCharged -
            ledger.flow.debtRepaid, state.treasuryDebt);
}

TEST(PlayableCityTickTests, FundingChangesReachCapacityAndBillsWithoutMutatingFacilities) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  laySpine(map, roads);
  for (const Coord c : {Coord{3,17}, Coord{23,17}}) {
    map.getTile(c).zone = static_cast<int>(ZoneType::Residential);
    map.getTile(c).buildingId = store.createBuilding(BuildingType::Residential, c, 40);
  }
  PopulationSystem::allocate(store, population, 40, 1);
  const std::vector<ServiceFacility> facilities{
    {ServiceType::Fire, {4,15}, 24, 1.f}, {ServiceType::Power, {14,15}, 30, 1.f},
    {ServiceType::Water, {15,15}, 30, 1.f}, {ServiceType::Garbage, {6,15}, 24, 1.f},
    {ServiceType::Cemetery, {7,15}, 24, 1.f}};
  PlayableCityTickState state;
  DerivedCityRefreshOptions refresh;
  refresh.runTraffic = false;
  refreshDerivedCityState(map, roads, store, population, facilities, state, refresh);
  EXPECT_FLOAT_EQ(state.serviceSummary.fireCoverage, 1.f);
  EXPECT_FLOAT_EQ(state.serviceSummary.powerGenerationMW, 100.f);
  state.budgetPolicy.servicePercent[0] = 50;
  state.budgetPolicy.servicePercent[4] = 25;
  state.budgetPolicy.servicePercent[5] = 50;
  state.budgetPolicy.servicePercent[7] = 50;
  state.budgetPolicy.servicePercent[9] = 50;
  refreshDerivedCityState(map, roads, store, population, facilities, state, refresh);
  EXPECT_FLOAT_EQ(state.serviceSummary.fireCoverage, .5f);
  EXPECT_FLOAT_EQ(state.serviceSummary.powerGenerationMW, 25.f);
  EXPECT_FLOAT_EQ(state.serviceSummary.waterSupply, 50.f);
  const auto funded = state.budgetPolicy.fundedFacilities(facilities);
  EXPECT_FLOAT_EQ(funded.front().quality, .5f);
  EXPECT_EQ(funded.front().maxTravelDistance, 12);
  EXPECT_FLOAT_EQ(facilities.front().quality, 1.f);
  EXPECT_EQ(facilities.front().maxTravelDistance, 24);
  const auto waste = WasteSystem::evaluate(store, funded, state.serviceSummary);
  EXPECT_EQ(waste.collectionCapacity, 200);
  DeathcareState backlog;
  backlog.awaitingDisposition = 100;
  const auto deathcare = DeathcareSystem::step(population, funded, state.serviceSummary, 0, 0, backlog);
  EXPECT_EQ(deathcare.capacity, 25u);
  EXPECT_LT(deathcare.processed, 50u);
  int64_t funds = 50000;
  PlayableCityTickOptions options;
  options.growthChance = 0;
  options.requireUtilities = false;
  playableCityTick(map, roads, store, population, facilities, state, funds, options);
  EXPECT_EQ(state.treasuryLedger.breakdown.expenses[3], state.budgetPolicy.serviceCost(facilities));
  state.budgetPolicy.servicePercent.fill(0);
  refreshDerivedCityState(map, roads, store, population, facilities, state, refresh);
  EXPECT_FLOAT_EQ(state.serviceSummary.fireCoverage, 0.f);
  EXPECT_FLOAT_EQ(state.serviceSummary.powerGenerationMW, 0.f);
  EXPECT_FALSE(map.getTile({3,17}).connectedToPower);
  EXPECT_TRUE(state.budgetPolicy.fundedFacilities(facilities).empty());
  EXPECT_EQ(state.budgetPolicy.serviceCost(facilities), 0);
  state.budgetPolicy.servicePercent.fill(100);
  refreshDerivedCityState(map, roads, store, population, facilities, state, refresh);
  EXPECT_FLOAT_EQ(state.serviceSummary.fireCoverage, 1.f);
  EXPECT_FLOAT_EQ(state.serviceSummary.powerGenerationMW, 100.f);
  EXPECT_TRUE(map.getTile({3,17}).connectedToPower);
}
