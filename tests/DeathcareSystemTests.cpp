#include "gtest/gtest.h"

#include "src/entities/PopulationStore.hpp"
#include "src/systems/DeathcareSystem.hpp"

TEST(DeathcareSystemTests, MortalityAccumulatesDeterministicallyAndReducesPopulation) {
  PopulationStore population;
  population.createGroup(10000, 8000);
  DeathcareState state;
  ServiceCoverageSummary coverage;

  const DeathcareSummary summary = DeathcareSystem::step(
    population, {}, coverage, 0.2f, 0.2f, state);
  EXPECT_GT(summary.deaths, 0u);
  EXPECT_EQ(population.getTotalPopulation(), 10000u - summary.deaths);
  EXPECT_EQ(summary.awaitingDisposition, summary.deaths);
}

TEST(DeathcareSystemTests, CrematoriumProcessesRoadCoveredBacklog) {
  PopulationStore population;
  population.createGroup(10000, 8000);
  DeathcareState state;
  state.awaitingDisposition = 80;
  ServiceCoverageSummary coverage;
  coverage.crematoriumCoverage = 1.0f;
  std::vector<ServiceFacility> facilities{
    {ServiceType::Crematorium, {1, 1}, 10, 1.0f}
  };

  const DeathcareSummary summary = DeathcareSystem::step(
    population, facilities, coverage, 0.0f, 0.0f, state);
  EXPECT_GE(summary.processed, 80u);
  EXPECT_EQ(summary.awaitingDisposition, 0u);
}
