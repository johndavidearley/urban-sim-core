#include "src/systems/MigrationSystem.hpp"
#include "gtest/gtest.h"
#include <limits>

TEST(MigrationSystemTests, AttractiveCityGrowsWithinHousingCapacity) {
  MigrationConditions c;
  c.serviceSatisfaction = 1.0f;
  const auto result = MigrationSystem::evaluate(100, 100, 200, c);
  EXPECT_EQ(result.requestedPopulation, 128u);
  EXPECT_FLOAT_EQ(result.desirability, 1.0f);
  EXPECT_EQ(MigrationSystem::evaluate(200, 200, 200, c).requestedPopulation,
            200u);
  EXPECT_EQ(MigrationSystem::evaluate(100, 100, 0, c).requestedPopulation, 0u);
}

TEST(MigrationSystemTests, UnemploymentAndUtilityLossCauseDepartures) {
  MigrationConditions c;
  EXPECT_EQ(MigrationSystem::evaluate(100, 0, 200, c).requestedPopulation, 98u);
  c.utilityAvailability = 0.0f;
  EXPECT_EQ(MigrationSystem::evaluate(100, 100, 200, c).requestedPopulation,
            98u);
  EXPECT_EQ(MigrationSystem::evaluate(0, 0, 200, c).requestedPopulation, 0u);
}

TEST(MigrationSystemTests, QualityOfLifeAndShortagesSlowArrivals) {
  MigrationConditions good;
  good.serviceSatisfaction = 1.0f;
  const auto baseline = MigrationSystem::evaluate(100, 100, 1000, good);
  for (float MigrationConditions::*field :
       {&MigrationConditions::congestion, &MigrationConditions::pollution,
        &MigrationConditions::crimeRate, &MigrationConditions::illnessRate}) {
    auto poor = good;
    poor.*field = 1.0f;
    EXPECT_LT(
        MigrationSystem::evaluate(100, 100, 1000, poor).requestedPopulation,
        baseline.requestedPopulation);
  }
  good.serviceSatisfaction = 0.0f;
  EXPECT_LT(MigrationSystem::evaluate(100, 100, 1000, good).requestedPopulation,
            baseline.requestedPopulation);
  good.utilityAvailability = 0.5f;
  EXPECT_LT(MigrationSystem::evaluate(100, 100, 1000, good).requestedPopulation,
            baseline.requestedPopulation);
}

TEST(MigrationSystemTests, SaturatesBeforeUnsignedConversion) {
  const auto maximum = std::numeric_limits<uint32_t>::max();
  EXPECT_EQ(MigrationSystem::evaluate(maximum, maximum, maximum, {})
                .requestedPopulation,
            maximum);
  EXPECT_LT(MigrationSystem::evaluate(10, maximum, 100, {}).requestedPopulation,
            100u);
}
