#include "src/systems/DeathcareSystem.hpp"

#include <algorithm>
#include <cmath>

#include "src/entities/PopulationStore.hpp"

DeathcareSummary DeathcareSystem::step(
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  const ServiceCoverageSummary& coverage,
  float illnessRate,
  float pollution,
  DeathcareState& state
) {
  DeathcareSummary summary;
  const uint32_t populationBefore = population.getTotalPopulation();
  summary.mortalityRate = std::max(0.0f, 0.0001f + illnessRate * 0.001f + pollution * 0.0005f);
  state.fractionalDeaths += static_cast<double>(populationBefore) * summary.mortalityRate;
  const uint32_t requestedDeaths = static_cast<uint32_t>(std::floor(state.fractionalDeaths));
  state.fractionalDeaths -= requestedDeaths;
  summary.deaths = population.applyDeaths(requestedDeaths);
  state.awaitingDisposition += summary.deaths;

  for (const ServiceFacility& facility : facilities) {
    if (facility.type == ServiceType::Cemetery) {
      summary.capacity += static_cast<uint32_t>(50.0f * std::max(0.0f, facility.quality));
    } else if (facility.type == ServiceType::Crematorium) {
      summary.capacity += static_cast<uint32_t>(100.0f * std::max(0.0f, facility.quality));
    }
  }
  const float reach = std::max(coverage.cemeteryCoverage, coverage.crematoriumCoverage);
  const uint32_t reachableCapacity = static_cast<uint32_t>(summary.capacity * reach);
  summary.processed = std::min(state.awaitingDisposition, reachableCapacity);
  state.awaitingDisposition -= summary.processed;
  summary.awaitingDisposition = state.awaitingDisposition;
  if (populationBefore > 0) {
    const float backlogRatio = std::min(1.0f,
      static_cast<float>(state.awaitingDisposition) / static_cast<float>(populationBefore));
    summary.happinessPenalty = backlogRatio * 0.35f;
    summary.healthPenalty = backlogRatio * 0.15f;
  }
  return summary;
}
