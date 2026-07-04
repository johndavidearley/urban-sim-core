#include <gtest/gtest.h>

#include "src/systems/HealthSystem.hpp"

TEST(HealthSystemTests, NoFactorsYieldsBaseRate) {
  HealthParams params;
  const HealthSummary summary = HealthSystem::evaluate(
    /*housingDensity=*/0.0f, /*averagePollution=*/0.0f, /*healthCoverage=*/0.0f, params);

  EXPECT_FLOAT_EQ(summary.illnessRate, params.baseIllnessRate);
}

TEST(HealthSystemTests, HigherDensityIncreasesIllness) {
  HealthParams params;
  const HealthSummary low = HealthSystem::evaluate(0.1f, 0.0f, 0.0f, params);
  const HealthSummary high = HealthSystem::evaluate(0.9f, 0.0f, 0.0f, params);

  EXPECT_GT(high.illnessRate, low.illnessRate);
}

TEST(HealthSystemTests, HigherPollutionIncreasesIllness) {
  HealthParams params;
  const HealthSummary clean = HealthSystem::evaluate(0.0f, 0.05f, 0.0f, params);
  const HealthSummary dirty = HealthSystem::evaluate(0.0f, 0.9f, 0.0f, params);

  EXPECT_GT(dirty.illnessRate, clean.illnessRate);
}

TEST(HealthSystemTests, HigherHealthCoverageReducesIllness) {
  HealthParams params;
  const HealthSummary uncovered = HealthSystem::evaluate(0.5f, 0.5f, 0.0f, params);
  const HealthSummary covered = HealthSystem::evaluate(0.5f, 0.5f, 1.0f, params);

  EXPECT_LT(covered.illnessRate, uncovered.illnessRate);
  EXPECT_GT(covered.illnessRate, 0.0f);  // healthCoverageReduction < 1.0, so coverage mitigates but doesn't eliminate
}

TEST(HealthSystemTests, RateStaysWithinZeroOneUnderExtremeInputs) {
  HealthParams params;
  const HealthSummary worstCase = HealthSystem::evaluate(1.0f, 1.0f, 0.0f, params);
  const HealthSummary bestCase = HealthSystem::evaluate(0.0f, 0.0f, 1.0f, params);

  EXPECT_GE(worstCase.illnessRate, 0.0f);
  EXPECT_LE(worstCase.illnessRate, 1.0f);
  EXPECT_GE(bestCase.illnessRate, 0.0f);
  EXPECT_LE(bestCase.illnessRate, 1.0f);
  EXPECT_GT(worstCase.illnessRate, bestCase.illnessRate);
}

TEST(HealthSystemTests, FullHealthCoverageStillLeavesBaseRateReduced) {
  HealthParams params;
  const HealthSummary summary = HealthSystem::evaluate(0.0f, 0.0f, 1.0f, params);

  // At full coverage, only healthCoverageReduction of the base rate is cut.
  EXPECT_FLOAT_EQ(summary.illnessRate, params.baseIllnessRate * (1.0f - params.healthCoverageReduction));
}
