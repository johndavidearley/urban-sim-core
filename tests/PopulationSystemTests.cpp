#include <gtest/gtest.h>

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
