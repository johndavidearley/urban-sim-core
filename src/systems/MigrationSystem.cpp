#include "src/systems/MigrationSystem.hpp"

#include <algorithm>

#include "src/systems/CitySimSupport.hpp"

MigrationDemand MigrationSystem::evaluate(uint32_t population,
                                          uint32_t employed,
                                          uint32_t housingCapacity,
                                          const MigrationConditions &c,
                                          float taxAttraction) {
  const float unemployment =
      population > 0
          ? city_sim::clamp01(static_cast<float>(
                                  population - std::min(population, employed)) /
                              population)
          : 0.0f;
  float desirability = city_sim::attractivenessFromUnemployment(unemployment);
  desirability *= city_sim::clamp01(1.0f - 0.4f * c.congestion);
  desirability *= city_sim::clamp01(1.0f - 0.5f * c.pollution);
  desirability *= city_sim::clamp01(0.6f + 0.4f * c.serviceSatisfaction);
  desirability *= city_sim::clamp01(1.0f - 0.25f * c.crimeRate);
  desirability *= city_sim::clamp01(1.0f - 0.3f * c.illnessRate);
  desirability *= city_sim::clamp01(c.utilityAvailability);

  taxAttraction = std::clamp(taxAttraction, 0.4f, 1.4f);
  desirability *= taxAttraction;

  const float headroom = housingCapacity > population
                             ? static_cast<float>(housingCapacity - population)
                             : 0.0f;
  float requested = desirability < 0.05f && population > 0
                              ? static_cast<float>(population) * 0.98f
                              : static_cast<float>(population) +
                                    0.25f * desirability * headroom +
                                    3.0f * desirability;
  // Tax-related departures are at most 0.3% per tick, independent of city
  // size and housing vacancy. Never evict the city instantly after a policy edit.
  requested -= static_cast<float>(population) * 0.005f *
               std::max(0.0f, 1.0f - taxAttraction);
  // Saturate before conversion, including capacities that round upward in
  // float.
  const uint32_t target =
      requested >= static_cast<float>(housingCapacity)
          ? housingCapacity
          : static_cast<uint32_t>(std::max(0.0f, requested));
  // Persisted desirability is normalized; the attraction multiplier may
  // still accelerate arrivals beyond the default rate.
  return {target, city_sim::clamp01(desirability)};
}
