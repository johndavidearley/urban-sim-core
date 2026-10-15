#pragma once

#include <cstdint>

// Previous completed tick's conditions. Tool refreshes do not advance this lag.
struct MigrationConditions {
  float congestion = 0.0f;
  float pollution = 0.0f;
  float serviceSatisfaction = 0.5f;
  float crimeRate = 0.0f;
  float illnessRate = 0.0f;
  // 1 preserves the autonomous simulator's existing migration behavior.
  float utilityAvailability = 1.0f;
};

struct MigrationDemand {
  uint32_t requestedPopulation = 0;
  float desirability = 0.0f;
};

struct PopulationChange {
  uint32_t arrivals = 0;
  // Includes residents displaced by lost housing capacity.
  uint32_t departures = 0;
  uint32_t deaths = 0;
  int64_t net = 0;
  float desirability = 0.0f;
};

class MigrationSystem {
public:
  static MigrationDemand evaluate(uint32_t population, uint32_t employed,
                                  uint32_t housingCapacity,
                                  const MigrationConditions &conditions,
                                  float taxAttraction = 1.0f);
};
