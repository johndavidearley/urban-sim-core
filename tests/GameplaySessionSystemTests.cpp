#include <filesystem>

#include "gtest/gtest.h"

#include "src/persistence/GameplaySessionSystem.hpp"
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
  saved.populationTarget = 900;
  saved.fractionalDeaths = 0.75;
  saved.awaitingDisposition = 12;
  saved.autonomousGrowth = true;
  saved.autonomousExtent = 24;
  saved.facilities.push_back({ServiceType::Fire, {2, 2}, 16, 0.8f});

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
  EXPECT_EQ(loaded.populationTarget, 900u);
  EXPECT_DOUBLE_EQ(loaded.fractionalDeaths, 0.75);
  EXPECT_EQ(loaded.awaitingDisposition, 12u);
  EXPECT_TRUE(loaded.autonomousGrowth);
  EXPECT_EQ(loaded.autonomousExtent, 24);
  ASSERT_EQ(loaded.facilities.size(), 1u);
  EXPECT_EQ(loaded.facilities.front().position, Coord(2, 2));
  EXPECT_EQ(map.zone({2, 2}), 1);
  EXPECT_EQ(roads.getRoadCount(), 1u);

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
