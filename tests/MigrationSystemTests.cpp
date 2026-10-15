#include "src/systems/MigrationSystem.hpp"
#include "src/gameplay/BudgetPolicy.hpp"
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

TEST(MigrationSystemTests, TaxPressureChangesArrivalsAndDeparturesGradually) {
  MigrationConditions good;
  good.serviceSatisfaction = 1.f;
  const auto normal = MigrationSystem::evaluate(1000, 1000, 2000, good);
  const auto high = MigrationSystem::evaluate(1000, 1000, 2000, good, 0.5f);
  const auto low = MigrationSystem::evaluate(1000, 1000, 2000, good, 1.3f);
  EXPECT_LT(high.requestedPopulation, normal.requestedPopulation);
  EXPECT_GT(low.requestedPopulation, normal.requestedPopulation);
  EXPECT_LE(low.desirability, 1.f);
  const auto full = MigrationSystem::evaluate(10000, 10000, 10000, good, 0.5f);
  EXPECT_LT(full.requestedPopulation, 10000u);
  EXPECT_GT(full.requestedPopulation, 9900u);
  EXPECT_EQ(MigrationSystem::evaluate(10000, 10000, 10000, good).requestedPopulation, 10000u);
  EXPECT_EQ(MigrationSystem::evaluate(0, 0, 0, good, 0.4f).requestedPopulation, 0u);
}

TEST(BudgetPolicyTests, DefaultTaxesAreNeutralAndResponsesAreMonotonic) {
  BudgetPolicy policy;
  const ZoneDemand input{0.5f, 0.5f, 0.5f, 0.5f};
  for (const auto value : policy.taxAttraction()) EXPECT_FLOAT_EQ(value, 1.f);
  EXPECT_FLOAT_EQ(policy.adjustedDemand(input).residential, input.residential);
  for (size_t tax = 0; tax < 5; ++tax) {
    policy = BudgetPolicy{};
    auto previous = policy.taxAttraction();
    policy.taxPercent[tax] = 0;
    previous = policy.taxAttraction();
    for (int rate = 1; rate <= 20; ++rate) {
      policy.taxPercent[tax] = rate;
      const auto next = policy.taxAttraction();
      for (size_t zone = 0; zone < next.size(); ++zone) {
        EXPECT_LE(next[zone], previous[zone]);
        EXPECT_GE(next[zone], 0.4f);
        EXPECT_LE(next[zone], 1.4f);
        if (zone != tax && !(zone == 0 && tax == 4)) {
          EXPECT_FLOAT_EQ(next[zone], 1.f);
        }
      }
      previous = next;
    }
  }
  policy.taxPercent.fill(0);
  const auto low = policy.adjustedDemand(input);
  policy.taxPercent.fill(20);
  const auto high = policy.adjustedDemand(input);
  EXPECT_GT(low.residential, high.residential);
  EXPECT_GT(low.commercial, high.commercial);
  EXPECT_GT(low.industrial, high.industrial);
  EXPECT_GT(low.office, high.office);
  EXPECT_LE(policy.adjustedDemand({1,1,1,1}).residential, 1.f);
}
