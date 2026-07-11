#include "gtest/gtest.h"

#include "src/persistence/ReplayVerifier.hpp"

TEST(ReplayVerifierTests, DeterministicScenarioProducesMatchingChecksums) {
  ReplayConfig config;
  config.mapSize = 32;
  config.seed = 101;
  config.growthSteps = 8;
  config.seedPopulation = 120;
  config.runCommutes = true;
  config.runEconomy = true;

  const ReplayResult result = ReplayVerifier::verifyDeterministicRun(config);

  EXPECT_TRUE(result.deterministic);
  EXPECT_EQ(result.firstChecksum, result.secondChecksum);
}

TEST(ReplayVerifierTests, ChecksumChangesWhenStateChanges) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const uint64_t before = ReplayVerifier::calculateChecksum(map, roads, store, population);

  map.getTile({3, 3}).zone = 1;
  roads.buildRoad({3, 3}, {4, 3});
  store.createBuilding(BuildingType::Residential, {3, 3}, 8);
  population.createGroup(IncomeBand::Middle, 10, 5);

  const uint64_t after = ReplayVerifier::calculateChecksum(map, roads, store, population);

  EXPECT_NE(before, after);
}
