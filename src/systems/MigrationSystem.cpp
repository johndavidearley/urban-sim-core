#include "src/systems/MigrationSystem.hpp"

#include <algorithm>

#include "src/systems/CitySimSupport.hpp"

MigrationDemand MigrationSystem::evaluate(uint32_t population,
                                          uint32_t employed,
                                          uint32_t housingCapacity,
                                          const MigrationConditions &c) {
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

  const float headroom = housingCapacity > population
                             ? static_cast<float>(housingCapacity - population)
                             : 0.0f;
  const float requested = desirability < 0.05f && population > 0
                              ? static_cast<float>(population) * 0.98f
                              : static_cast<float>(population) +
                                    0.25f * desirability * headroom +
                                    3.0f * desirability;
  // Saturate before conversion, including capacities that round upward in
  // float.
  const uint32_t target =
      requested >= static_cast<float>(housingCapacity)
          ? housingCapacity
          : static_cast<uint32_t>(std::max(0.0f, requested));
  return {target, desirability};
}
