#include <gtest/gtest.h>

#include <array>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/systems/PopulationSystem.hpp"

namespace {
uint32_t occupancyByType(const EntityStore& store, BuildingType type) {
  uint32_t total = 0;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    if (building.type == type) {
      total += static_cast<uint32_t>(building.occupancy);
    }
  }
  return total;
}

std::array<uint32_t, 3> populationByBand(const PopulationStore& store) {
  std::array<uint32_t, 3> totals{0u, 0u, 0u};
  for (const auto& [id, group] : store.getGroups()) {
    (void)id;
    if (group.band == IncomeBand::Low) {
      totals[0] += group.size;
    } else if (group.band == IncomeBand::Middle) {
      totals[1] += group.size;
    } else if (group.band == IncomeBand::High) {
      totals[2] += group.size;
    }
  }
  return totals;
}

std::array<uint32_t, 3> employedByBand(const PopulationStore& store) {
  std::array<uint32_t, 3> totals{0u, 0u, 0u};
  for (const auto& [id, group] : store.getGroups()) {
    (void)id;
    if (group.band == IncomeBand::Low) {
      totals[0] += group.employed;
    } else if (group.band == IncomeBand::Middle) {
      totals[1] += group.employed;
    } else if (group.band == IncomeBand::High) {
      totals[2] += group.employed;
    }
  }
  return totals;
}
} // namespace

TEST(PopulationSystemTests, AllocationRespectsHousingAndJobs) {
  EntityStore buildings;
  PopulationStore people;

  buildings.createBuilding(BuildingType::Residential, {1, 1}, 10);
  buildings.createBuilding(BuildingType::Residential, {2, 1}, 10);
  buildings.createBuilding(BuildingType::Commercial, {3, 1}, 5);
  buildings.createBuilding(BuildingType::Industrial, {4, 1}, 3);

  const PopulationSummary summary = PopulationSystem::allocate(buildings, people, 15, 42);

  EXPECT_EQ(summary.requestedPopulation, 15u);
  EXPECT_EQ(summary.housedPopulation, 15u);
  EXPECT_EQ(summary.employedPopulation, 8u);
  EXPECT_EQ(summary.unemployedPopulation, 7u);
  EXPECT_EQ(summary.availableHousing, 5u);
  EXPECT_EQ(summary.availableJobs, 0u);
  EXPECT_NEAR(summary.unemploymentRate, 7.0f / 15.0f, 0.0001f);

  EXPECT_EQ(people.getTotalPopulation(), 15u);
  EXPECT_EQ(people.getTotalEmployed(), 8u);

  EXPECT_EQ(summary.lowIncomePopulation + summary.middleIncomePopulation + summary.highIncomePopulation, 15u);
  EXPECT_EQ(summary.lowIncomeEmployed + summary.middleIncomeEmployed + summary.highIncomeEmployed, 8u);

  const auto popBands = populationByBand(people);
  const auto empBands = employedByBand(people);
  EXPECT_EQ(popBands[0], summary.lowIncomePopulation);
  EXPECT_EQ(popBands[1], summary.middleIncomePopulation);
  EXPECT_EQ(popBands[2], summary.highIncomePopulation);
  EXPECT_EQ(empBands[0], summary.lowIncomeEmployed);
  EXPECT_EQ(empBands[1], summary.middleIncomeEmployed);
  EXPECT_EQ(empBands[2], summary.highIncomeEmployed);

  EXPECT_EQ(occupancyByType(buildings, BuildingType::Residential), 15u);
  EXPECT_EQ(
    occupancyByType(buildings, BuildingType::Commercial) +
    occupancyByType(buildings, BuildingType::Industrial),
    8u
  );
}

TEST(PopulationSystemTests, AllocationWithNoBuildingsProducesZeroPopulation) {
  EntityStore buildings;
  PopulationStore people;

  const PopulationSummary summary = PopulationSystem::allocate(buildings, people, 50, 7);

  EXPECT_EQ(summary.housedPopulation, 0u);
  EXPECT_EQ(summary.employedPopulation, 0u);
  EXPECT_EQ(summary.unemployedPopulation, 0u);
  EXPECT_EQ(summary.lowIncomePopulation, 0u);
  EXPECT_EQ(summary.middleIncomePopulation, 0u);
  EXPECT_EQ(summary.highIncomePopulation, 0u);
  EXPECT_EQ(summary.availableHousing, 0u);
  EXPECT_EQ(summary.availableJobs, 0u);
  EXPECT_FLOAT_EQ(summary.unemploymentRate, 0.0f);
  EXPECT_EQ(people.getGroupCount(), 0u);
}

TEST(PopulationSystemTests, AllocationIsDeterministicForSameSeed) {
  EntityStore buildings;
  PopulationStore people;

  buildings.createBuilding(BuildingType::Residential, {1, 1}, 6);
  buildings.createBuilding(BuildingType::Residential, {2, 1}, 6);
  buildings.createBuilding(BuildingType::Commercial, {3, 1}, 4);
  buildings.createBuilding(BuildingType::Industrial, {4, 1}, 4);

  const PopulationSummary first = PopulationSystem::allocate(buildings, people, 10, 99);
  const uint32_t resA = occupancyByType(buildings, BuildingType::Residential);
  const uint32_t comA = occupancyByType(buildings, BuildingType::Commercial);
  const uint32_t indA = occupancyByType(buildings, BuildingType::Industrial);

  const PopulationSummary second = PopulationSystem::allocate(buildings, people, 10, 99);
  const uint32_t resB = occupancyByType(buildings, BuildingType::Residential);
  const uint32_t comB = occupancyByType(buildings, BuildingType::Commercial);
  const uint32_t indB = occupancyByType(buildings, BuildingType::Industrial);

  EXPECT_EQ(first.housedPopulation, second.housedPopulation);
  EXPECT_EQ(first.employedPopulation, second.employedPopulation);
  EXPECT_EQ(resA, resB);
  EXPECT_EQ(comA, comB);
  EXPECT_EQ(indA, indB);
}

TEST(PopulationSystemTests, CompositionIsDeterministicForSameSeed) {
  EntityStore buildingsA;
  EntityStore buildingsB;
  PopulationStore peopleA;
  PopulationStore peopleB;

  buildingsA.createBuilding(BuildingType::Residential, {1, 1}, 30);
  buildingsA.createBuilding(BuildingType::Commercial, {3, 1}, 8);
  buildingsA.createBuilding(BuildingType::Industrial, {4, 1}, 8);

  buildingsB.createBuilding(BuildingType::Residential, {1, 1}, 30);
  buildingsB.createBuilding(BuildingType::Commercial, {3, 1}, 8);
  buildingsB.createBuilding(BuildingType::Industrial, {4, 1}, 8);

  const PopulationSummary a = PopulationSystem::allocate(buildingsA, peopleA, 24, 123);
  const PopulationSummary b = PopulationSystem::allocate(buildingsB, peopleB, 24, 123);

  EXPECT_EQ(a.lowIncomePopulation, b.lowIncomePopulation);
  EXPECT_EQ(a.middleIncomePopulation, b.middleIncomePopulation);
  EXPECT_EQ(a.highIncomePopulation, b.highIncomePopulation);
  EXPECT_EQ(a.lowIncomeEmployed, b.lowIncomeEmployed);
  EXPECT_EQ(a.middleIncomeEmployed, b.middleIncomeEmployed);
  EXPECT_EQ(a.highIncomeEmployed, b.highIncomeEmployed);
}

TEST(PopulationSystemTests, SummaryAppliesToCityMetrics) {
  CityMetrics metrics;
  PopulationSummary summary;
  summary.housedPopulation = 20;
  summary.availableHousing = 5;
  summary.availableJobs = 3;
  summary.unemploymentRate = 0.2f;

  PopulationSystem::applyToMetrics(summary, metrics);

  EXPECT_EQ(metrics.population, 20u);
  EXPECT_EQ(metrics.availableHousing, 5u);
  EXPECT_EQ(metrics.availableJobs, 3u);
  EXPECT_FLOAT_EQ(metrics.unemployment, 0.2f);
}
