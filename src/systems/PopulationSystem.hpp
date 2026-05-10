#pragma once

#include <cstdint>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/metrics/CityMetrics.hpp"

struct PopulationSummary {
  uint32_t requestedPopulation = 0;
  uint32_t housedPopulation = 0;
  uint32_t employedPopulation = 0;
  uint32_t unemployedPopulation = 0;
  uint32_t availableHousing = 0;
  uint32_t availableJobs = 0;
  float unemploymentRate = 0.0f;
};

class PopulationSystem {
public:
  static PopulationSummary allocate(
    EntityStore& store,
    PopulationStore& population,
    uint32_t requestedPopulation,
    uint32_t seed = 0
  );

  static void applyToMetrics(const PopulationSummary& summary, CityMetrics& metrics);
};
