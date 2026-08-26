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

  uint32_t lowIncomePopulation = 0;
  uint32_t middleIncomePopulation = 0;
  uint32_t highIncomePopulation = 0;

  uint32_t lowIncomeEmployed = 0;
  uint32_t middleIncomeEmployed = 0;
  uint32_t highIncomeEmployed = 0;
};

class PopulationSystem {
public:
  static PopulationSummary allocate(
    EntityStore& store,
    PopulationStore& population,
    uint32_t requestedPopulation,
    uint32_t seed = 0
  );

  // Read-only snapshot of current groups and building capacities (no reallocation).
  static PopulationSummary summarize(const EntityStore& store, const PopulationStore& population);

  static void applyToMetrics(const PopulationSummary& summary, CityMetrics& metrics);
};
