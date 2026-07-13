#pragma once

#include <cstdint>
#include <vector>

#include "src/systems/ServiceSystem.hpp"

class PopulationStore;

struct DeathcareState {
  double fractionalDeaths = 0.0;
  uint32_t awaitingDisposition = 0;
};

struct DeathcareSummary {
  uint32_t deaths = 0;
  uint32_t processed = 0;
  uint32_t awaitingDisposition = 0;
  uint32_t capacity = 0;
  float mortalityRate = 0.0f;
  float happinessPenalty = 0.0f;
  float healthPenalty = 0.0f;
};

class DeathcareSystem {
public:
  static DeathcareSummary step(
    PopulationStore& population,
    const std::vector<ServiceFacility>& facilities,
    const ServiceCoverageSummary& coverage,
    float illnessRate,
    float pollution,
    DeathcareState& state
  );
};
