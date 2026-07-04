#include "CrimeSystem.hpp"

#include <algorithm>

namespace {
float clamp01(float v) {
  return std::max(0.0f, std::min(1.0f, v));
}
} // namespace

CrimeSummary CrimeSystem::evaluate(
  float unemploymentRate,
  float policeCoverage,
  float averageLandValue,
  const CrimeParams& params
) {
  CrimeSummary summary;

  const float unemployment = clamp01(unemploymentRate);
  const float poverty = params.referenceLandValue > 0.0f
    ? clamp01(1.0f - averageLandValue / params.referenceLandValue)
    : 0.0f;
  const float coverage = clamp01(policeCoverage);

  const float rawRate = params.baseCrimeRate +
    params.unemploymentWeight * unemployment +
    params.povertyWeight * poverty;

  summary.overallRate = clamp01(rawRate * (1.0f - coverage * params.policeCoverageReduction));
  return summary;
}
