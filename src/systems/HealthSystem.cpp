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

  const float coverage = clamp01(healthCoverage);
  const float rawRate = unmitigatedRate(housingDensity, averagePollution, sanitationCoverage, params);

  summary.illnessRate = clamp01(rawRate * (1.0f - coverage * params.healthCoverageReduction));
  return summary;
}

float HealthSystem::unmitigatedRate(float housingDensity, float pollution,
  float sanitationCoverage, const HealthParams& params) {
  return params.baseIllnessRate + params.densityWeight * clamp01(housingDensity)
    + params.pollutionWeight * clamp01(pollution)
    + params.sanitationWeight * (1.0f - clamp01(sanitationCoverage));
}
