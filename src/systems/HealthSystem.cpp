#include "HealthSystem.hpp"

#include <algorithm>

namespace {
float clamp01(float v) {
  return std::max(0.0f, std::min(1.0f, v));
}
} // namespace

HealthSummary HealthSystem::evaluate(
  float housingDensity,
  float averagePollution,
  float healthCoverage,
  float sanitationCoverage,
  const HealthParams& params
) {
  HealthSummary summary;

  const float density = clamp01(housingDensity);
  const float pollution = clamp01(averagePollution);
  const float coverage = clamp01(healthCoverage);
  const float sewage = 1.0f - clamp01(sanitationCoverage);

  const float rawRate = params.baseIllnessRate +
    params.densityWeight * density +
    params.pollutionWeight * pollution +
    params.sanitationWeight * sewage;

  summary.illnessRate = clamp01(rawRate * (1.0f - coverage * params.healthCoverageReduction));
  return summary;
}
