#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

#include "gtest/gtest.h"

#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"

TEST(GameplaySessionSystemTests, RoundTripPreservesCoreAndPlayableState) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  roads.buildRoad({1, 1}, {2, 1});
  map.setZone({2, 2}, 1);
  GameplaySessionState saved;
  saved.funds = 12345;
  saved.tick = 77;
  saved.paused = true;
  saved.tickIntervalMs = 120;
  saved.demand = {0.1f, 0.2f, 0.3f, 0.4f};
  saved.treasuryRevenue = 800;
  saved.treasuryExpenses = 300;
  saved.treasuryNet = 500;
  saved.treasuryDebt = 250;
  saved.treasuryInterestRemainder = 0.4;
  saved.simulationSeed = 123;
  saved.budgetPolicy.taxPercent = {{7, 9, 11, 13, 15}};
  saved.budgetPolicy.servicePercent[0] = 50;
  saved.budgetPolicy.servicePercent[4] = 0;
  saved.treasuryLedger.valid = true;
  saved.treasuryLedger.tick = 77;
  saved.treasuryLedger.breakdown.revenue[0] = 80;
  saved.treasuryLedger.breakdown.expenses[0] = 30;
  saved.treasuryLedger.breakdown.totalRevenue = 80;
  saved.treasuryLedger.breakdown.totalExpenses = 30;
  saved.treasuryLedger.flow.revenue = 80;
  saved.treasuryLedger.flow.expenses = 30;
  saved.treasuryLedger.flow.net = 50;
  saved.treasuryLedger.flow.outstandingDebt = 10;
  saved.treasuryLedger.fundsBefore = 100;
  saved.treasuryLedger.fundsAfter = 150;
  saved.treasuryLedger.debtBefore = 10;
  saved.enableDisasters = true;
  saved.disasters.burningTiles = {{{3, 3}, 4}};
  saved.disasters.fire = {1, 1, 1};
  saved.disasters.buildingsLostToFire = 8;
  saved.disasters.buildingsLostToDisaster = 3;
  saved.disasters.disaster = {true, false, 2};
  saved.populationTarget = 900;
  saved.migrationConditions = {0.2f, 0.3f, 0.7f, 0.1f, 0.2f, 0.8f};
  saved.populationChange = {12, 3, 1, 8, 0.6f};
  saved.growthModifiers.push_back({{1, 1}, {4, 4}, 0.8f});
  saved.laggedEducationCoverage = 0.75f;
  saved.fractionalDeaths = 0.75;
  saved.awaitingDisposition = 12;
  saved.autonomousGrowth = true;
  saved.autonomousExtent = 24;
  saved.emptyZonedCount = 17;
  saved.facilities.push_back({ServiceType::Fire, {2, 2}, 16, 0.8f});
  TransitRoute bus;
  bus.id = 3;
  bus.mode = TransitMode::Bus;
  bus.stops = {{1, 1}, {2, 1}};
  bus.vehicleCount = 4;
  bus.capacityPerVehicle = 40;
  bus.stopCoverageRadius = 6;
  saved.transitRoutes.push_back(bus);
  GameplaySessionState::DistrictRecord district;
  district.name = "FACTORY";
  district.x1 = 1;
  district.y1 = 1;
  district.x2 = 4;
  district.y2 = 5;
  district.archetype = 1;
  district.serviceBudgetCap = 1;
  saved.districts.push_back(district);

  const auto path = std::filesystem::temp_directory_path() / "urban_sim_gameplay_session.json";
  ASSERT_TRUE(GameplaySessionSystem::save(
    path.string(), map, roads, store, population, saved));

  int savedWidth = 0;
  int savedHeight = 0;
  ASSERT_TRUE(GameplaySessionSystem::readMapDimensions(
    path.string(), savedWidth, savedHeight));
  EXPECT_EQ(savedWidth, 8);
  EXPECT_EQ(savedHeight, 8);

  map.setZone({2, 2}, 0);
  roads.clear();
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(
    path.string(), map, roads, store, population, loaded));
  EXPECT_EQ(loaded.funds, 12345);
  EXPECT_EQ(loaded.tick, 77u);
  EXPECT_TRUE(loaded.paused);
  EXPECT_EQ(loaded.tickIntervalMs, 120u);
  EXPECT_FLOAT_EQ(loaded.demand.office, 0.4f);
  EXPECT_EQ(loaded.treasuryRevenue, 800);
  EXPECT_EQ(loaded.treasuryExpenses, 300);
  EXPECT_EQ(loaded.treasuryNet, 500);
  EXPECT_EQ(loaded.treasuryDebt, 250);
  EXPECT_DOUBLE_EQ(loaded.treasuryInterestRemainder, 0.4);
  EXPECT_EQ(loaded.simulationSeed, 123u);
  EXPECT_EQ(loaded.budgetPolicy.taxPercent, saved.budgetPolicy.taxPercent);
  EXPECT_EQ(loaded.budgetPolicy.servicePercent, saved.budgetPolicy.servicePercent);
  EXPECT_TRUE(loaded.treasuryLedger.valid);
  EXPECT_EQ(loaded.treasuryLedger.tick, 77u);
  EXPECT_EQ(loaded.treasuryLedger.fundsAfter, 150);
  EXPECT_EQ(loaded.treasuryLedger.breakdown.totalRevenue, 80);
  EXPECT_TRUE(loaded.enableDisasters);
  ASSERT_EQ(loaded.disasters.burningTiles.size(), 1u);
  EXPECT_EQ(loaded.disasters.burningTiles.front().ticksRemaining, 4);
  EXPECT_EQ(loaded.disasters.fire.activeFires, 1u);
  EXPECT_EQ(loaded.disasters.buildingsLostToFire, 8u);
  EXPECT_EQ(loaded.disasters.buildingsLostToDisaster, 3u);
  EXPECT_TRUE(loaded.disasters.disaster.earthquakeOccurred);
  EXPECT_EQ(loaded.populationTarget, 900u);
  EXPECT_FLOAT_EQ(loaded.migrationConditions.congestion, 0.2f);
  EXPECT_FLOAT_EQ(loaded.migrationConditions.utilityAvailability, 0.8f);
  EXPECT_EQ(loaded.populationChange.net, 8);
  ASSERT_EQ(loaded.growthModifiers.size(), 1u);
  EXPECT_FLOAT_EQ(loaded.growthModifiers[0].multiplier, 0.8f);
  EXPECT_EQ(loaded.populationChange.deaths, 1u);
  EXPECT_FLOAT_EQ(loaded.laggedEducationCoverage, 0.75f);
  EXPECT_DOUBLE_EQ(loaded.fractionalDeaths, 0.75);
  EXPECT_EQ(loaded.awaitingDisposition, 12u);
  EXPECT_TRUE(loaded.autonomousGrowth);
  EXPECT_EQ(loaded.autonomousExtent, 24);
  EXPECT_EQ(loaded.emptyZonedCount, 17);
  ASSERT_EQ(loaded.facilities.size(), 1u);
  ASSERT_EQ(loaded.transitRoutes.size(), 1u);
  EXPECT_EQ(loaded.transitRoutes.front().id, 3u);
  EXPECT_EQ(loaded.transitRoutes.front().mode, TransitMode::Bus);
  EXPECT_EQ(loaded.transitRoutes.front().stops.size(), 2u);
  EXPECT_EQ(loaded.transitRoutes.front().stops[1], Coord(2, 1));
  EXPECT_EQ(loaded.transitRoutes.front().vehicleCount, 4);
  ASSERT_EQ(loaded.districts.size(), 1u);
  EXPECT_EQ(loaded.districts.front().name, "FACTORY");
  EXPECT_EQ(loaded.districts.front().x1, 1);
  EXPECT_EQ(loaded.districts.front().y1, 1);
  EXPECT_EQ(loaded.districts.front().x2, 4);
  EXPECT_EQ(loaded.districts.front().y2, 5);
  EXPECT_EQ(loaded.districts.front().archetype, 1);
  EXPECT_EQ(loaded.districts.front().serviceBudgetCap, 1);
  EXPECT_EQ(loaded.transitRoutes.front().capacityPerVehicle, 40);
  EXPECT_EQ(loaded.transitRoutes.front().stopCoverageRadius, 6);
  EXPECT_EQ(loaded.facilities.front().position, Coord(2, 2));
  EXPECT_EQ(map.zone({2, 2}), 1);
  EXPECT_EQ(roads.getRoadCount(), 1u);

  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}

TEST(GameplaySessionSystemTests, RejectsNegativeTreasuryDebt) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  GameplaySessionState saved;
  const auto path = std::filesystem::temp_directory_path() / "urban_sim_negative_debt.json";
  ASSERT_TRUE(GameplaySessionSystem::save(
    path.string(), map, roads, store, population, saved));

  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  const std::string needle = "\"treasuryDebt\": 0";
  const auto pos = text.find(needle);
  ASSERT_NE(pos, std::string::npos);
  text.replace(pos, needle.size(), "\"treasuryDebt\": -1");
  std::ofstream out(path);
  out << text;
  ASSERT_TRUE(out.good());
  out.close();

  GameplaySessionState loaded;
  std::string error;
  EXPECT_FALSE(GameplaySessionSystem::load(
    path.string(), map, roads, store, population, loaded, &error));
  EXPECT_NE(error.find("treasury debt"), std::string::npos);

  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}

TEST(GameplaySessionSystemTests, MissingSessionReturnsUsefulError) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  GameplaySessionState session;
  std::string error;
  const auto path = std::filesystem::temp_directory_path() / "urban_sim_missing_gameplay.json";
  std::filesystem::remove(path);
  EXPECT_FALSE(GameplaySessionSystem::load(
    path.string(), map, roads, store, population, session, &error));
  EXPECT_FALSE(error.empty());
}

TEST(GameplaySessionSystemTests, LegacySaveLoadsWithoutMigrationFields) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore people;
  GameplaySessionState saved;
  saved.populationTarget = 272;
  const auto path =
      std::filesystem::temp_directory_path() / "urban_migration_legacy.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store,
                                          people, saved));
  nlohmann::json document;
  {
    std::ifstream in(path);
    in >> document;
  }
  document.erase("migrationConditions");
  document.erase("populationChange");
  document.erase("laggedEducationCoverage");
  document.erase("growthModifiers");
  document.erase("disasters");
  document.erase("simulationSeed");
  document.erase("budgetTaxPercent");
  document.erase("budgetServicePercent");
  document.erase("treasuryLedger");
  {
    std::ofstream out(path);
    out << document;
  }
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store,
                                          people, loaded));
  EXPECT_EQ(loaded.populationTarget, 272u);
  EXPECT_FALSE(loaded.enableDisasters);
  EXPECT_TRUE(loaded.disasters.burningTiles.empty());
  EXPECT_EQ(loaded.simulationSeed, 98765u);
  EXPECT_EQ(loaded.budgetPolicy.taxPercent, BudgetPolicy{}.taxPercent);
  EXPECT_EQ(loaded.budgetPolicy.servicePercent, BudgetPolicy{}.servicePercent);
  EXPECT_FALSE(loaded.treasuryLedger.valid);
  EXPECT_EQ(loaded.populationChange.net, 0);
  EXPECT_FLOAT_EQ(loaded.migrationConditions.utilityAvailability, 1.0f);
  EXPECT_FLOAT_EQ(loaded.laggedEducationCoverage, 0.0f);
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}

TEST(GameplaySessionSystemTests,
     RejectsInvalidMigrationConditionsWithoutReplacingState) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore people;
  GameplaySessionState saved;
  const auto path =
      std::filesystem::temp_directory_path() / "urban_migration_invalid.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store,
                                          people, saved));
  nlohmann::json document;
  {
    std::ifstream in(path);
    in >> document;
  }
  document["migrationConditions"]["utilityAvailability"] = -0.5;
  {
    std::ofstream out(path);
    out << document;
  }
  GameplaySessionState loaded;
  loaded.funds = 123;
  std::string error;
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(), map, roads, store,
                                           people, loaded, &error));
  EXPECT_NE(error.find("migration conditions"), std::string::npos);
  EXPECT_EQ(loaded.funds, 123);
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}

TEST(GameplaySessionSystemTests, InvalidBurningTilesDoNotMutateTheCity) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  GameplaySessionState saved;
  const auto path = std::filesystem::temp_directory_path() / "urban_invalid_fires.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store, population, saved));
  nlohmann::json original;
  { std::ifstream in(path); in >> original; }
  const auto burn = nlohmann::json{{"x", 3}, {"y", 3}, {"ticksRemaining", 4}};
  for (int invalid = 0; invalid < 4; ++invalid) {
    auto root = original;
    root["disasters"]["enabled"] = true;
    root["disasters"]["burningTiles"] = nlohmann::json::array({burn});
    if (invalid == 0) root["disasters"]["burningTiles"][0]["x"] = 99;
    if (invalid == 1) root["disasters"]["burningTiles"][0]["ticksRemaining"] = 0;
    if (invalid == 2) root["disasters"]["burningTiles"].push_back(burn);
    if (invalid == 3) root["disasters"]["buildingsLostToFire"] = -1;
    { std::ofstream out(path); out << root; }
    GameplaySessionState loaded;
    loaded.funds = 123;
    map.setZone({1, 1}, static_cast<int>(ZoneType::Park));
    std::string error;
    EXPECT_FALSE(GameplaySessionSystem::load(path.string(), map, roads, store, population, loaded, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(loaded.funds, 123);
    EXPECT_EQ(map.zone({1, 1}), static_cast<int>(ZoneType::Park));
  }
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}

TEST(GameplaySessionSystemTests, RejectsInvalidTaxPolicyWithoutMutatingTheCity) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  GameplaySessionState saved;
  const auto path = std::filesystem::temp_directory_path() / "urban_invalid_budget.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store, population, saved));
  nlohmann::json original;
  { std::ifstream in(path); in >> original; }
  for (const auto& values : {nlohmann::json::array({5, 8, 6, 9}),
                            nlohmann::json::array({-1, 8, 6, 9, 10}),
                            nlohmann::json::array({21, 8, 6, 9, 10}),
                            nlohmann::json::array({5.5, 8, 6, 9, 10})}) {
    auto root = original;
    root["budgetTaxPercent"] = values;
    { std::ofstream out(path); out << root; }
    GameplaySessionState loaded;
    loaded.funds = 123;
    map.setZone({1, 1}, static_cast<int>(ZoneType::Park));
    std::string error;
    EXPECT_FALSE(GameplaySessionSystem::load(path.string(), map, roads, store, population, loaded, &error));
    EXPECT_EQ(loaded.funds, 123);
    EXPECT_EQ(map.zone({1, 1}), static_cast<int>(ZoneType::Park));
  }
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}

TEST(GameplaySessionSystemTests, RejectsCorruptTreasuryLedgerWithoutMutatingTheCity) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  GameplaySessionState saved;
  GameplaySessionSystem::advance(map, roads, store, population, saved);
  const auto path = std::filesystem::temp_directory_path() / "urban_invalid_ledger.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store, population, saved));
  nlohmann::json original;
  { std::ifstream in(path); in >> original; }
  for (int invalid = 0; invalid < 3; ++invalid) {
    auto root = original;
    if (invalid == 0) root["treasuryLedger"]["totalRevenue"] = 1;
    if (invalid == 1) root["treasuryLedger"]["fundsAfter"] = 0;
    if (invalid == 2) root["treasuryLedger"]["revenue"].push_back(0);
    { std::ofstream out(path); out << root; }
    GameplaySessionState loaded;
    loaded.funds = 123;
    map.setZone({1, 1}, static_cast<int>(ZoneType::Park));
    std::string error;
    EXPECT_FALSE(GameplaySessionSystem::load(path.string(), map, roads, store, population, loaded, &error));
    EXPECT_NE(error.find("ledger"), std::string::npos);
    EXPECT_EQ(loaded.funds, 123);
    EXPECT_EQ(map.zone({1, 1}), static_cast<int>(ZoneType::Park));
  }
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}

TEST(GameplaySessionSystemTests, InvalidServiceFundingDoesNotMutateCityOrPolicy) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore population;
  GameplaySessionState saved;
  const auto path = std::filesystem::temp_directory_path() / "urban_invalid_funding.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),map,roads,store,population,saved));
  nlohmann::json original;
  { std::ifstream input(path); input >> original; }
  for (int invalid = 0; invalid < 4; ++invalid) {
    auto root = original;
    if (invalid == 0) root["budgetServicePercent"].erase(0);
    if (invalid == 1) root["budgetServicePercent"][0] = -1;
    if (invalid == 2) root["budgetServicePercent"][0] = 101;
    if (invalid == 3) root["budgetServicePercent"][0] = 50.5;
    { std::ofstream output(path); output << root; }
    GameplaySessionState loaded; loaded.funds = 123;
    map.setZone({1,1},static_cast<int>(ZoneType::Park));
    std::string error;
    EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,population,loaded,&error));
    EXPECT_EQ(loaded.funds,123);
    EXPECT_EQ(loaded.budgetPolicy.servicePercent,BudgetPolicy{}.servicePercent);
    EXPECT_EQ(map.zone({1,1}),static_cast<int>(ZoneType::Park));
  }
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".city.json");
}
