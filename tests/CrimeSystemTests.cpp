#include "gtest/gtest.h"

#include "src/systems/CrimeSystem.hpp"

TEST(CrimeSystemTests, NoFactorsYieldsBaseRate) {
  CrimeParams params;
  const CrimeSummary summary = CrimeSystem::evaluate(
    /*unemploymentRate=*/0.0f, /*policeCoverage=*/0.0f, /*averageLandValue=*/params.referenceLandValue, params);

  EXPECT_FLOAT_EQ(summary.overallRate, params.baseCrimeRate);
}

TEST(CrimeSystemTests, HigherUnemploymentIncreasesCrime) {
  CrimeParams params;
  const CrimeSummary low = CrimeSystem::evaluate(0.05f, 0.0f, params.referenceLandValue, params);
  const CrimeSummary high = CrimeSystem::evaluate(0.5f, 0.0f, params.referenceLandValue, params);

  EXPECT_GT(high.overallRate, low.overallRate);
}

TEST(CrimeSystemTests, LowerLandValueIncreasesCrime) {
  CrimeParams params;
  const CrimeSummary wealthy = CrimeSystem::evaluate(0.1f, 0.0f, /*averageLandValue=*/300.0f, params);
  const CrimeSummary poor = CrimeSystem::evaluate(0.1f, 0.0f, /*averageLandValue=*/20.0f, params);

  EXPECT_GT(poor.overallRate, wealthy.overallRate);
}

TEST(CrimeSystemTests, HigherPoliceCoverageReducesCrime) {
  CrimeParams params;
  const CrimeSummary uncovered = CrimeSystem::evaluate(0.2f, 0.0f, 100.0f, params);
  const CrimeSummary covered = CrimeSystem::evaluate(0.2f, 1.0f, 100.0f, params);

  EXPECT_LT(covered.overallRate, uncovered.overallRate);
  EXPECT_GT(covered.overallRate, 0.0f);  // policeCoverageReduction < 1.0, so coverage mitigates but doesn't eliminate
}

TEST(CrimeSystemTests, RateStaysWithinZeroOneUnderExtremeInputs) {
  CrimeParams params;
  const CrimeSummary worstCase = CrimeSystem::evaluate(1.0f, 0.0f, 0.0f, params);
  const CrimeSummary bestCase = CrimeSystem::evaluate(0.0f, 1.0f, 1000.0f, params);

  EXPECT_GE(worstCase.overallRate, 0.0f);
  EXPECT_LE(worstCase.overallRate, 1.0f);
  EXPECT_GE(bestCase.overallRate, 0.0f);
  EXPECT_LE(bestCase.overallRate, 1.0f);
  EXPECT_GT(worstCase.overallRate, bestCase.overallRate);
}

TEST(CrimeSystemTests, FullPoliceCoverageStillLeavesBaseRateReduced) {
  CrimeParams params;
  const CrimeSummary summary = CrimeSystem::evaluate(0.0f, 1.0f, params.referenceLandValue, params);

  // At full coverage, only policeCoverageReduction of the base rate is cut.
  EXPECT_FLOAT_EQ(summary.overallRate, params.baseCrimeRate * (1.0f - params.policeCoverageReduction));
}
