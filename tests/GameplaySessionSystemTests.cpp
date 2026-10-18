#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

#include "gtest/gtest.h"

#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/persistence/CityHistoryJson.hpp"
#include "src/persistence/TransitReportJson.hpp"
#include "src/persistence/RegionalConnectionsJson.hpp"
#include "src/persistence/RegionalSettlementJson.hpp"
#include "src/persistence/CityAlertsJson.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"

TEST(GameplaySessionSystemTests, RegionalConfigurationSurvivesContinuationAndRejectsBeforeMutation) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  GameplaySessionState session;
  regional::Connection airport; airport.id=8; airport.kind=regional::Kind::Airport;
  airport.origin={3,3}; airport.entrance={3,4}; airport.passengersPerTick=60;
  session.regionalConnections={airport};
  session.tick=3;
  const auto allocation=regional::allocate(3,{10,0,0},{{airport,regional::Access::Ready}});
  ASSERT_TRUE(regional::settle(allocation,{{8,{5,0,0}}},{2,0,0,1},session.regionalSettlement));
  const auto path=std::filesystem::temp_directory_path()/"urban_regional_session.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),map,roads,store,people,session));
  nlohmann::json valid; { std::ifstream input(path); input>>valid; }
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  ASSERT_EQ(loaded.regionalConnections.size(),1u);
  EXPECT_EQ(loaded.regionalConnections[0].passengersPerTick,60u);
  EXPECT_EQ(loaded.regionalSettlement.revenue,10);
  EXPECT_FALSE(regional::settle(allocation,{{8,{5,0,0}}},{2,0,0,1},loaded.regionalSettlement));
  GameplaySessionSystem::advance(map,roads,store,people,loaded);
  EXPECT_EQ(loaded.regionalConnections[0].id,8u);
  const auto tick=loaded.tick;
  store.createBuilding(BuildingType::Residential,{1,1},10);
  auto invalid=valid; invalid["regionalConnections"][0]["width"]=100;
  { std::ofstream output(path); output<<invalid; }
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_EQ(store.getBuildingCount(),1u);
  EXPECT_EQ(loaded.tick,tick);
  EXPECT_EQ(loaded.regionalConnections[0].id,8u);
  valid.erase("regionalConnections");
  valid.erase("regionalSettlement");
  { std::ofstream output(path); output<<valid; }
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_TRUE(loaded.regionalConnections.empty());
  EXPECT_FALSE(loaded.regionalSettlement.valid);
  std::filesystem::remove(path);
  std::filesystem::remove(path.string()+".city.json");
}

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
  saved.transitReport = {true,77,200,{{3,160,200,140}}};
  CityAlertInputs alertInputs; alertInputs.expenses = 50;
  for (uint32_t tick = 1; tick <= 77; ++tick) saved.alerts.update(tick, alertInputs);
  saved.history.append({76, 100, {}, 40, 30, 12000, 250, {}});
  saved.history.append({77, 100, {}, 50, 40, 12345, 250, {}});
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
  EXPECT_EQ(cityAlertsJson(loaded.alerts), cityAlertsJson(saved.alerts));
  EXPECT_EQ(transitReportJson(loaded.transitReport),transitReportJson(saved.transitReport));
  EXPECT_EQ(cityHistoryJson(loaded.history), cityHistoryJson(saved.history));
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
  document.erase("cityHistory");
  document.erase("cityAlerts");
  document.erase("transitReport");
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
  EXPECT_TRUE(loaded.history.samples.empty());
  EXPECT_EQ(loaded.alerts.checkedTick, 0u);
  EXPECT_FALSE(loaded.transitReport.valid);
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
    if (invalid == 0) { root["budgetServicePercent"].erase(0); root["budgetServicePercent"].erase(0); }
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

TEST(CityHistoryTests, RetainsLatestTicksAndMarksAppliedPolicyChanges) {
  CityHistory history;
  BudgetPolicy policy;
  for (uint32_t tick = 1; tick <= 300; ++tick) {
    if (tick == 100) policy.taxPercent[0] = 8;
    if (tick == 200) policy.servicePercent[0] = 50;
    history.append({tick, 100, {}, 20, 30, 1000, 0, policy});
  }
  ASSERT_EQ(history.samples.size(), 240u);
  EXPECT_EQ(history.samples.front().tick, 61u);
  EXPECT_EQ(history.samples.back().tick, 300u);
  EXPECT_FALSE(history.policyChanged(0));
  size_t changes = 0;
  for (size_t i = 0; i < history.samples.size(); ++i) {
    if (history.policyChanged(i)) {
      ++changes;
      EXPECT_TRUE(history.samples[i].tick == 100 || history.samples[i].tick == 200);
    }
  }
  EXPECT_EQ(changes, 2u);
  EXPECT_EQ(cityHistoryJson(parseCityHistory(cityHistoryJson(history), 300)), cityHistoryJson(history));
}

TEST(CityHistoryTests, RejectsInvalidAndUnboundedImportedHistory) {
  CityHistory history;
  history.append({1,100,{},20,30,1000,0,{}});
  history.append({2,100,{},20,30,1000,0,{}});
  const auto good = cityHistoryJson(history);
  for (int invalid = 0; invalid < 10; ++invalid) {
    auto rows = good;
    if (invalid == 0) rows[1]["tick"] = 1;
    if (invalid == 1) rows[1]["tick"] = 3;
    if (invalid == 2) rows[1]["population"] = -1;
    if (invalid == 3) rows[1]["revenue"] = 0.5;
    if (invalid == 4) rows[1]["net_migration"] = 5;
    if (invalid == 5) rows[1]["tax_percent"][0] = 21;
    if (invalid == 6) rows[1]["service_percent"][0] = -1;
    if (invalid == 7) rows[1]["debt"] = std::numeric_limits<uint64_t>::max();
    if (invalid == 8) rows[1]["tax_percent"].erase(0);
    if (invalid == 9) for (int i = 0; i < 240; ++i) rows.push_back(good[0]);
    EXPECT_THROW(parseCityHistory(rows, 2), std::exception);
  }
  EXPECT_TRUE(parseCityHistory(nlohmann::json::array(), 500).samples.empty());
}

TEST(GameplaySessionSystemTests, InvalidHistoryDoesNotMutateCityOrSession) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore population;
  GameplaySessionState saved;
  saved.tick = 1;
  saved.history.append({1,0,{},20,30,1000,0,{}});
  const auto path = std::filesystem::temp_directory_path() / "urban_invalid_history.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),map,roads,store,population,saved));
  nlohmann::json root;
  { std::ifstream input(path); input >> root; }
  root["cityHistory"][0]["tick"] = 2;
  { std::ofstream output(path); output << root; }
  GameplaySessionState loaded; loaded.funds = 123;
  map.setZone({1,1},static_cast<int>(ZoneType::Park));
  std::string error;
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,population,loaded,&error));
  EXPECT_EQ(loaded.funds,123);
  EXPECT_TRUE(loaded.history.samples.empty());
  EXPECT_EQ(map.zone({1,1}),static_cast<int>(ZoneType::Park));
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".city.json");
}

TEST(CityAlertsTests, DebounceRecoveryHysteresisAndCooldownSurviveSave) {
  CityAlerts alerts;
  CityAlertInputs inputs;
  inputs.hasBuildings = true;
  inputs.powerCoverage = 0.f;
  alerts.update(1, inputs); alerts.update(2, inputs);
  EXPECT_FALSE(alerts.records[0].active);
  alerts.update(3, inputs);
  EXPECT_TRUE(alerts.records[0].active);
  EXPECT_EQ(alerts.records[0].sinceTick, 1u);
  EXPECT_TRUE(alerts.hasNotice());
  alerts = parseCityAlerts(cityAlertsJson(alerts), 3);
  alerts.update(4, inputs);
  EXPECT_FALSE(alerts.hasNotice());
  inputs.powerCoverage = .97f;
  alerts.update(5, inputs); alerts.update(6, inputs);
  EXPECT_TRUE(alerts.records[0].active);
  inputs.powerCoverage = 1.f;
  alerts.update(7, inputs); alerts.update(8, inputs);
  EXPECT_FALSE(alerts.records[0].active);
  inputs.powerCoverage = 0.f;
  for (uint32_t tick = 9; tick < 63; ++tick) {
    alerts.update(tick, inputs);
    EXPECT_FALSE(alerts.hasNotice());
  }
  alerts.update(63, inputs);
  EXPECT_TRUE(alerts.hasNotice());
  EXPECT_EQ(alerts.records[0].lastNoticeTick,63u);
  alerts.update(63, inputs);
  EXPECT_EQ(alerts.records[0].badTicks,3u);
}

TEST(CityAlertsTests, DiagnosticsRequireRealDemandAndTrackAllProblemTypes) {
  CityAlerts alerts;
  CityAlertInputs inputs;
  inputs.requireUtilities = false;
  inputs.powerCoverage = inputs.waterCoverage = 0;
  for (uint32_t tick = 1; tick <= 3; ++tick) alerts.update(tick,inputs);
  EXPECT_EQ(alerts.activeCount(),0u);
  inputs.hasBuildings = inputs.hasResidents = inputs.requireUtilities = true;
  inputs.unemployment = .5f;
  inputs.serviceCoverage = .25f;
  inputs.revenue = 50; inputs.expenses = 100;
  for (uint32_t tick = 4; tick <= 6; ++tick) alerts.update(tick,inputs);
  EXPECT_EQ(alerts.activeCount(),5u);
  const auto saved = cityAlertsJson(alerts);
  EXPECT_EQ(cityAlertsJson(parseCityAlerts(saved,6)),saved);
  for (int invalid = 0; invalid < 7; ++invalid) {
    auto bad = saved;
    if (invalid == 0) bad["checked_tick"] = 7;
    if (invalid == 1) bad["records"][0]["last_notice_tick"] = 7;
    if (invalid == 2) bad["records"][0]["bad_ticks"] = 4;
    if (invalid == 3) bad["records"][0]["since_tick"] = 0;
    if (invalid == 4) bad["inputs"]["power_supply"] = -1;
    if (invalid == 5) bad["inputs"]["revenue"] = -1;
    if (invalid == 6) bad["records"].erase(0);
    EXPECT_THROW(parseCityAlerts(bad,6),std::exception);
  }
}

TEST(GameplaySessionSystemTests, InvalidAlertStateDoesNotMutateCityOrSession) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore population;
  GameplaySessionState saved;
  const auto path = std::filesystem::temp_directory_path() / "urban_invalid_alerts.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),map,roads,store,population,saved));
  nlohmann::json root;
  { std::ifstream input(path); input >> root; }
  root["cityAlerts"]["records"][0]["last_notice_tick"] = 1;
  { std::ofstream output(path); output << root; }
  GameplaySessionState loaded; loaded.funds = 123;
  map.setZone({1,1},static_cast<int>(ZoneType::Park));
  std::string error;
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,population,loaded,&error));
  EXPECT_EQ(loaded.funds,123);
  EXPECT_EQ(loaded.alerts.checkedTick,0u);
  EXPECT_EQ(map.zone({1,1}),static_cast<int>(ZoneType::Park));
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".city.json");
}

TEST(GameplaySessionSystemTests, InvalidTransitImportsDoNotMutateCity) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore population;
  GameplaySessionState saved;
  TransitRoute bus;bus.id=1;bus.stops={{1,1},{2,1}};saved.transitRoutes.push_back(bus);
  const auto path=std::filesystem::temp_directory_path()/"urban_invalid_transit_management.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),map,roads,store,population,saved));
  nlohmann::json original;
  {std::ifstream input(path);input>>original;}
  for (int invalid=0;invalid<4;++invalid) {
    auto root=original;
    if (invalid==0) root["transitRoutes"][0]["vehicleCount"]=1.5;
    if (invalid==1) root["transitRoutes"][0]["capacityPerVehicle"]=INT32_MAX;
    if (invalid==2) root["transitRoutes"].push_back(root["transitRoutes"][0]);
    if (invalid==3) {root["transitReport"]["valid"]=true;root["transitReport"]["tick"]=1;}
    {std::ofstream output(path);output<<root;}
    GameplaySessionState loaded;loaded.funds=123;
    map.setZone({1,1},static_cast<int>(ZoneType::Park));
    std::string error;
    EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,population,loaded,&error));
    EXPECT_EQ(loaded.funds,123);
    EXPECT_EQ(map.zone({1,1}),static_cast<int>(ZoneType::Park));
  }
  std::filesystem::remove(path);std::filesystem::remove(path.string()+".city.json");
}

TEST(GameplaySessionSystemTests, ExplicitLogisticsModePersistsAfterLastPortAndRejectsMalformedMode) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  GameplaySessionState session; session.regionalLogisticsEnabled=true;
  const auto path=std::filesystem::temp_directory_path()/"urban_logistics_mode.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),map,roads,store,people,session));
  nlohmann::json valid; {std::ifstream input(path); input>>valid;}
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_TRUE(loaded.regionalLogisticsEnabled);
  auto state=GameplaySessionSystem::advance(map,roads,store,people,loaded);
  EXPECT_TRUE(state.regionalLogisticsEnabled);
  ASSERT_EQ(state.trafficSummary.freight.gateways.size(),1u);
  EXPECT_EQ(state.trafficSummary.freight.gateways[0].gateway.goodsPerTick,100);
  for (auto bad:{nlohmann::json(1),nlohmann::json("true"),nlohmann::json(nullptr)}) {
    auto root=valid; root["regionalLogisticsEnabled"]=bad;
    {std::ofstream output(path); output<<root;}
    map.setZone({2,2},4); loaded.funds=123;
    EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
    EXPECT_EQ(loaded.funds,123); EXPECT_TRUE(loaded.regionalLogisticsEnabled);
    EXPECT_EQ(map.zone({2,2}),4);
  }
  valid.erase("regionalLogisticsEnabled");
  {std::ofstream output(path); output<<valid;}
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_FALSE(loaded.regionalLogisticsEnabled);
  EXPECT_TRUE(loaded.regionalConnections.empty());
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".city.json");
}

TEST(GameplaySessionSystemTests, RegionalIDCounterSurvivesEmptyRegistryAndRejectsMalformedValues) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  GameplaySessionState session; session.nextRegionalConnectionId=42;
  const auto path=std::filesystem::temp_directory_path()/"urban_regional_ids.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),map,roads,store,people,session));
  nlohmann::json valid; {std::ifstream input(path); input>>valid;}
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_EQ(loaded.nextRegionalConnectionId,42u);
  for (const auto& bad:{nlohmann::json(0),nlohmann::json(-1),nlohmann::json(1.5),
       nlohmann::json(true),nlohmann::json(4294967297ULL)}) {
    auto root=valid;root["nextRegionalConnectionId"]=bad;
    {std::ofstream output(path);output<<root;}
    loaded.funds=123;
    EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
    EXPECT_EQ(loaded.funds,123);EXPECT_EQ(loaded.nextRegionalConnectionId,42u);
  }
  valid.erase("nextRegionalConnectionId");
  {std::ofstream output(path);output<<valid;}
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_EQ(loaded.nextRegionalConnectionId,1u);
  std::filesystem::remove(path);std::filesystem::remove(path.string()+".city.json");
}

TEST(GameplaySessionSystemTests,
     ShoreFacilitiesPersistAndInvalidSitesRejectAtomically) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore people;
  for (int y = 0; y < 16; ++y)
    map.getTile({8, y}).type = 2;
  for (int y = 4; y < 10; ++y)
    roads.buildRoad({6, y}, {6, y + 1});
  GameplaySessionState session;
  ServiceFacility intake{ServiceType::Water, {7, 5}, 20, 1.f};
  intake.surfaceWater = true;
  ServiceFacility outlet{ServiceType::Sanitation, {7, 8}, 18, 1.f};
  outlet.surfaceWater = true;
  session.facilities = {intake, outlet};
  const auto path =
      std::filesystem::temp_directory_path() / "urban_shore_session.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store,
                                          people, session));
  nlohmann::json valid;
  {
    std::ifstream input(path);
    input >> valid;
  }
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store,
                                          people, loaded));
  ASSERT_EQ(loaded.facilities.size(), 2u);
  EXPECT_TRUE(loaded.facilities[0].surfaceWater);
  EXPECT_TRUE(loaded.facilities[1].surfaceWater);
  BudgetPolicy policy;
  EXPECT_EQ(policy.serviceCost(loaded.facilities), 20);
  policy.servicePercent[static_cast<size_t>(ServiceType::Sanitation)] = 0;
  EXPECT_FLOAT_EQ(
      ServiceSystem::intakeCleanFraction(
          roads, policy.fundedFacilities(loaded.facilities), {7, 5}),
      1.f);
  auto invalid = valid;
  invalid["facilities"][0]["x"] = 3;
  {
    std::ofstream output(path);
    output << invalid;
  }
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(), map, roads, store,
                                           people, loaded));
  EXPECT_EQ(loaded.facilities[0].position, Coord(7, 5));
  EXPECT_EQ(map.getTile({8, 5}).type, 2);
  // Existing sessions without the added field retain their inland facilities.
  valid["facilities"][0].erase("surfaceWater");
  valid["facilities"][1].erase("surfaceWater");
  {
    std::ofstream output(path);
    output << valid;
  }
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store,
                                          people, loaded));
  EXPECT_FALSE(loaded.facilities[0].surfaceWater);
  EXPECT_FALSE(loaded.facilities[1].surfaceWater);
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}

TEST(GameplaySessionSystemTests, IncineratorFundingAndLegacyServicePolicyRoundTrip) {
  CityMap map({16,16}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  roads.buildRoad({2,3}, {3,3});
  GameplaySessionState session;
  session.facilities = {{ServiceType::Incinerator, {3,2}, 18, 1.f}};
  session.budgetPolicy.servicePercent[11] = 50;
  const auto path = std::filesystem::temp_directory_path()/"urban_incinerator_session.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store, people, session));
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store, people, loaded));
  ASSERT_EQ(loaded.facilities.size(), 1u);
  EXPECT_EQ(loaded.facilities[0].type, ServiceType::Incinerator);
  EXPECT_EQ(loaded.budgetPolicy.serviceCost(loaded.facilities), 30);
  auto funded = loaded.budgetPolicy.fundedFacilities(loaded.facilities);
  EXPECT_FLOAT_EQ(funded[0].quality, .5f);
  loaded.budgetPolicy.servicePercent[11] = 0;
  EXPECT_TRUE(loaded.budgetPolicy.fundedFacilities(loaded.facilities).empty());
  nlohmann::json json; {std::ifstream input(path); input >> json;}
  json["facilities"] = nlohmann::json::array();
  json["budgetServicePercent"].erase(11);
  {std::ofstream output(path); output << json;}
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store, people, loaded));
  EXPECT_EQ(loaded.budgetPolicy.servicePercent[11], 100);
  std::filesystem::remove(path);
  std::filesystem::remove(path.string()+".city.json");
}

TEST(GameplaySessionSystemTests, LegacyBudgetHistoryDefaultsNewDepartmentToFullFunding) {
  CityHistory history;
  CityHistorySample sample;
  sample.tick = 1;
  history.append(sample);
  auto rows = cityHistoryJson(history);
  rows[0]["service_percent"].erase(11);
  const auto parsed = parseCityHistory(rows,1);
  ASSERT_EQ(parsed.samples.size(), 1u);
  EXPECT_EQ(parsed.samples[0].policy.servicePercent[11], 100);
}

TEST(CityAlertsTests, FullLandfillUsesExistingDebounceAndLegacyAlertsLoad) {
  CityAlerts alerts; CityAlertInputs inputs;
  inputs.fullLandfills = 1; inputs.landfillX = 3; inputs.landfillY = 4;
  for (uint32_t t = 1; t <= 2; ++t) alerts.update(t, inputs);
  EXPECT_FALSE(alerts.records[5].active);
  alerts.update(3, inputs);
  EXPECT_TRUE(alerts.records[5].active);
  auto saved = cityAlertsJson(alerts);
  EXPECT_EQ(cityAlertsJson(parseCityAlerts(saved,3)), saved);
  inputs.fullLandfills = 0;
  alerts.update(4, inputs); EXPECT_TRUE(alerts.records[5].active);
  alerts.update(5, inputs); EXPECT_FALSE(alerts.records[5].active);
  saved["records"].erase(5);
  EXPECT_FALSE(parseCityAlerts(saved,3).records[5].active);
}

TEST(GameplaySessionSystemTests, LandfillVolumeRoundTripsAndLegacyStartsEmpty) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  GameplaySessionState state;
  state.facilities.push_back({ServiceType::Garbage,{2,2},10,1.f});
  state.facilities[0].landfillRemaining = 123;
  state.facilities[0].landfillLastFill = -20;
  const auto path = std::filesystem::temp_directory_path()/"urban_landfill_session.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store, people, state));
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store, people, loaded));
  ASSERT_EQ(loaded.facilities.size(), 1u);
  EXPECT_EQ(loaded.facilities[0].landfillRemaining, 123);
  EXPECT_EQ(loaded.facilities[0].landfillLastFill, -20);
  nlohmann::json root; {std::ifstream in(path); in >> root;}
  root["facilities"][0]["landfillRemaining"] = -1;
  {std::ofstream out(path); out << root;}
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(), map, roads, store, people, loaded));
  EXPECT_EQ(loaded.facilities[0].landfillRemaining, 123);
  root["facilities"][0].erase("landfillRemaining");
  root["facilities"][0].erase("landfillLastFill");
  {std::ofstream out(path); out << root;}
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store, people, loaded));
  EXPECT_EQ(loaded.facilities[0].landfillRemaining, 80000);
  EXPECT_EQ(loaded.facilities[0].landfillLastFill, 0);
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".city.json");
}

TEST(GameplaySessionSystemTests, TreatmentRoundTripLegacyDefaultAndInvalidModeAreAtomic) {
  CityMap map({16,16}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  map.getTile({8,6}).type=2; roads.buildRoad({6,6},{6,7});
  GameplaySessionState session;
  ServiceFacility treatment{ServiceType::Sanitation,{7,6},18,1.f};
  treatment.surfaceWater=true; treatment.sewageTreatment=true; session.facilities={treatment};
  const auto path=std::filesystem::temp_directory_path()/"urban_treatment_session.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),map,roads,store,people,session));
  nlohmann::json valid; {std::ifstream input(path); input>>valid;}
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  ASSERT_EQ(loaded.facilities.size(),1u);
  EXPECT_TRUE(loaded.facilities[0].sewageTreatment);
  auto invalid=valid; invalid["facilities"][0]["surfaceWater"]=false;
  {std::ofstream output(path);output<<invalid;}
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_TRUE(loaded.facilities[0].sewageTreatment);
  EXPECT_EQ(map.getTile({8,6}).type,2);
  invalid=valid; invalid["facilities"][0]["type"]=5;
  {std::ofstream output(path);output<<invalid;}
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_TRUE(loaded.facilities[0].sewageTreatment);
  valid["facilities"][0].erase("sewageTreatment");
  {std::ofstream output(path);output<<valid;}
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),map,roads,store,people,loaded));
  EXPECT_FALSE(loaded.facilities[0].sewageTreatment);
  EXPECT_EQ(BudgetPolicy{}.serviceCost(loaded.facilities),8);
  std::filesystem::remove(path);std::filesystem::remove(path.string()+".city.json");
}

TEST(GameplaySessionSystemTests, DistrictHeightCapRoundTripsAndLegacyLoadsUncapped) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore people;
  GameplaySessionState saved;
  GameplaySessionState::DistrictRecord district;
  district.name = "LOW";
  district.x1 = 1;
  district.y1 = 1;
  district.x2 = 3;
  district.y2 = 3;
  district.heightCap = 2;
  saved.districts.push_back(district);
  const auto path = std::filesystem::temp_directory_path() / "urban_height_cap_session.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(), map, roads, store, people, saved));
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store, people, loaded));
  ASSERT_EQ(loaded.districts.size(), 1u);
  EXPECT_EQ(loaded.districts.front().heightCap, 2);
  EXPECT_EQ(loaded.districts.front().serviceBudgetCap, -1);

  nlohmann::json root;
  {
    std::ifstream in(path);
    in >> root;
  }
  root["districts"][0]["heightCap"] = 3;
  {
    std::ofstream out(path);
    out << root;
  }
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(), map, roads, store, people, loaded));
  EXPECT_EQ(loaded.districts.front().heightCap, 2);
  EXPECT_EQ(map.zone({1, 1}), 0);

  root["districts"][0].erase("heightCap");
  {
    std::ofstream out(path);
    out << root;
  }
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(), map, roads, store, people, loaded));
  EXPECT_EQ(loaded.districts.front().heightCap, 0);
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".city.json");
}
