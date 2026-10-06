#include "gtest/gtest.h"

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

  const PopulationSummary live = PopulationSystem::summarize(buildings, people);
  EXPECT_EQ(live.housedPopulation, summary.housedPopulation);
  EXPECT_EQ(live.employedPopulation, summary.employedPopulation);
  EXPECT_EQ(live.availableHousing, summary.availableHousing);
  EXPECT_EQ(live.availableJobs, summary.availableJobs);
  EXPECT_EQ(live.lowIncomePopulation, summary.lowIncomePopulation);
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

TEST(PopulationSystemTests, IncomeBandJobPreferencesAffectJobTypeMix) {
  EntityStore buildings;
  PopulationStore people;

  buildings.createBuilding(BuildingType::Residential, {1, 1}, 20);
  buildings.createBuilding(BuildingType::Commercial, {3, 1}, 20);
  buildings.createBuilding(BuildingType::Industrial, {4, 1}, 20);

  const PopulationSummary summary = PopulationSystem::allocate(buildings, people, 20, 5);

  uint32_t commercialOccupancy = 0;
  uint32_t industrialOccupancy = 0;
  for (const auto& [id, building] : buildings.getBuildings()) {
    (void)id;
    if (building.type == BuildingType::Commercial) {
      commercialOccupancy += static_cast<uint32_t>(building.occupancy);
    } else if (building.type == BuildingType::Industrial) {
      industrialOccupancy += static_cast<uint32_t>(building.occupancy);
    }
  }

  EXPECT_EQ(summary.employedPopulation, 20u);
  EXPECT_EQ(commercialOccupancy + industrialOccupancy, 20u);
  EXPECT_GT(industrialOccupancy, commercialOccupancy);
}

// Office buildings are a third job type, distinct from commercial/industrial,
// and jobCapacity/employment allocation must account for them - a city with
// only office jobs available should still fully employ up to office capacity.
TEST(PopulationSystemTests, OfficeBuildingsProvideJobs) {
  EntityStore buildings;
  PopulationStore people;

  buildings.createBuilding(BuildingType::Residential, {1, 1}, 20);
  buildings.createBuilding(BuildingType::Office, {5, 1}, 10);

  const PopulationSummary summary = PopulationSystem::allocate(buildings, people, 10, 11);

  EXPECT_EQ(summary.housedPopulation, 10u);
  EXPECT_EQ(summary.employedPopulation, 10u);
  EXPECT_EQ(summary.availableJobs, 0u);

  const uint32_t officeOccupancy = occupancyByType(buildings, BuildingType::Office);
  EXPECT_EQ(officeOccupancy, 10u);
}

// With all three job types present, total employment must equal the sum of
// commercial + industrial + office occupancy - no jobs lost or double-counted
// across the three-way split.
TEST(PopulationSystemTests, EmploymentConservedAcrossAllThreeJobTypes) {
  EntityStore buildings;
  PopulationStore people;

  buildings.createBuilding(BuildingType::Residential, {1, 1}, 30);
  buildings.createBuilding(BuildingType::Commercial, {2, 1}, 10);
  buildings.createBuilding(BuildingType::Industrial, {3, 1}, 10);
  buildings.createBuilding(BuildingType::Office, {4, 1}, 10);

  const PopulationSummary summary = PopulationSystem::allocate(buildings, people, 30, 13);

  EXPECT_EQ(summary.employedPopulation, 30u);
  const uint32_t total =
    occupancyByType(buildings, BuildingType::Commercial) +
    occupancyByType(buildings, BuildingType::Industrial) +
    occupancyByType(buildings, BuildingType::Office);
  EXPECT_EQ(total, 30u);
}

TEST(PopulationSystemTests, SummaryAppliesToCityMetrics) {
  CityMetrics metrics;
  PopulationSummary summary;
  summary.housedPopulation = 20;
  summary.availableHousing = 5;
  summary.availableJobs = 3;
  summary.unemploymentRate = 0.2f;
  summary.lowIncomePopulation = 10;
  summary.middleIncomePopulation = 7;
  summary.highIncomePopulation = 3;

  PopulationSystem::applyToMetrics(summary, metrics);

  EXPECT_EQ(metrics.population, 20u);
  EXPECT_EQ(metrics.availableHousing, 5u);
  EXPECT_EQ(metrics.availableJobs, 3u);
  EXPECT_FLOAT_EQ(metrics.unemployment, 0.2f);
  EXPECT_EQ(metrics.lowIncomePopulation, 10u);
  EXPECT_EQ(metrics.middleIncomePopulation, 7u);
  EXPECT_EQ(metrics.highIncomePopulation, 3u);
}

namespace {

struct LaborAllocation {
  PopulationSummary summary;
  uint32_t commercial = 0;
  uint32_t industrial = 0;
  uint32_t office = 0;
};

void addLaborCity(EntityStore& buildings, uint32_t residential, uint32_t commercial,
                  uint32_t industrial, uint32_t office) {
  buildings.createBuilding(BuildingType::Residential, {1, 1}, static_cast<int>(residential));
  buildings.createBuilding(BuildingType::Commercial, {2, 1}, static_cast<int>(commercial));
  buildings.createBuilding(BuildingType::Industrial, {3, 1}, static_cast<int>(industrial));
  buildings.createBuilding(BuildingType::Office, {4, 1}, static_cast<int>(office));
}

LaborAllocation allocateLaborCity(uint32_t residential, uint32_t commercial, uint32_t industrial,
                                  uint32_t office, uint32_t requested, uint32_t seed, float coverage) {
  EntityStore buildings;
  PopulationStore people;
  addLaborCity(buildings, residential, commercial, industrial, office);
  LaborAllocation result;
  result.summary = PopulationSystem::allocate(buildings, people, requested, seed, coverage);
  result.commercial = occupancyByType(buildings, BuildingType::Commercial);
  result.industrial = occupancyByType(buildings, BuildingType::Industrial);
  result.office = occupancyByType(buildings, BuildingType::Office);
  return result;
}

}  // namespace

// Coverage 0 is the historical split. Default allocate() and an explicit 0
// must agree, including office occupancy filled by low-band overflow.
TEST(PopulationSystemTests, ZeroEducationCoverageKeepsUneducatedSplit) {
  EntityStore baselineBuildings;
  EntityStore explicitBuildings;
  PopulationStore baselinePeople;
  PopulationStore explicitPeople;
  addLaborCity(baselineBuildings, 200, 200, 200, 200);
  addLaborCity(explicitBuildings, 200, 200, 200, 200);

  const PopulationSummary baseline = PopulationSystem::allocate(baselineBuildings, baselinePeople, 200, 17);
  const PopulationSummary explicitZero =
    PopulationSystem::allocate(explicitBuildings, explicitPeople, 200, 17, 0.0f);

  EXPECT_EQ(baseline.housedPopulation, 200u);
  EXPECT_EQ(baseline.employedPopulation, 200u);
  EXPECT_EQ(baseline.unemployedPopulation, 0u);
  EXPECT_EQ(baseline.lowIncomePopulation, 100u);
  EXPECT_EQ(baseline.middleIncomePopulation, 70u);
  EXPECT_EQ(baseline.highIncomePopulation, 30u);
  EXPECT_EQ(occupancyByType(baselineBuildings, BuildingType::Commercial), 75u);
  EXPECT_EQ(occupancyByType(baselineBuildings, BuildingType::Industrial), 80u);
  EXPECT_EQ(occupancyByType(baselineBuildings, BuildingType::Office), 45u);

  EXPECT_EQ(explicitZero.lowIncomePopulation, baseline.lowIncomePopulation);
  EXPECT_EQ(explicitZero.middleIncomePopulation, baseline.middleIncomePopulation);
  EXPECT_EQ(explicitZero.highIncomePopulation, baseline.highIncomePopulation);
  EXPECT_EQ(explicitZero.employedPopulation, baseline.employedPopulation);
  EXPECT_EQ(occupancyByType(explicitBuildings, BuildingType::Office), 45u);
  EXPECT_EQ(occupancyByType(explicitBuildings, BuildingType::Industrial), 80u);
  EXPECT_EQ(occupancyByType(explicitBuildings, BuildingType::Commercial), 75u);
}

// Ample jobs: full coverage raises the office share and the high-income band.
// Half coverage lands between the two, so the shift is not a cliff at 100%.
TEST(PopulationSystemTests, FullEducationCoverageRaisesOfficeShare) {
  const LaborAllocation uneducated = allocateLaborCity(200, 200, 200, 200, 200, 17, 0.0f);
  const LaborAllocation halfway = allocateLaborCity(200, 200, 200, 200, 200, 17, 0.5f);
  const LaborAllocation educated = allocateLaborCity(200, 200, 200, 200, 200, 17, 1.0f);

  EXPECT_EQ(uneducated.office, 45u);
  EXPECT_EQ(halfway.office, 69u);
  EXPECT_EQ(educated.office, 96u);
  EXPECT_GT(halfway.office, uneducated.office);
  EXPECT_LT(halfway.office, educated.office);
  EXPECT_EQ(educated.industrial, 56u);
  EXPECT_EQ(educated.commercial, 48u);
  EXPECT_EQ(educated.summary.highIncomePopulation, 60u);
  EXPECT_EQ(educated.summary.middleIncomePopulation, 80u);
  EXPECT_EQ(educated.summary.lowIncomePopulation, 60u);
  EXPECT_EQ(educated.summary.employedPopulation, 200u);
  EXPECT_EQ(educated.summary.unemployedPopulation, 0u);
}

// Office seats past the educated preference share stay empty. The low band
// takes the industrial work instead of overflowing into office, and the
// workers who still cannot be placed show up as unemployment.
TEST(PopulationSystemTests, EducatedOfficeSpareStaysEmpty) {
  const LaborAllocation uneducated = allocateLaborCity(100, 0, 20, 100, 100, 3, 0.0f);
  const LaborAllocation educated = allocateLaborCity(100, 0, 20, 100, 100, 3, 1.0f);

  EXPECT_EQ(uneducated.summary.unemployedPopulation, 0u);
  EXPECT_EQ(uneducated.summary.employedPopulation, 100u);
  EXPECT_EQ(uneducated.office, 80u);
  EXPECT_EQ(uneducated.industrial, 20u);

  EXPECT_EQ(educated.office, 48u);
  EXPECT_EQ(educated.industrial, 20u);
  EXPECT_LT(educated.office, 100u);
  EXPECT_EQ(educated.summary.employedPopulation, 68u);
  EXPECT_EQ(educated.summary.unemployedPopulation, 32u);
  EXPECT_EQ(educated.summary.availableJobs, 52u);
  EXPECT_EQ(educated.summary.highIncomePopulation, 30u);
  EXPECT_GT(educated.summary.unemploymentRate, 0.0f);
}
