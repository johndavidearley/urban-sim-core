#pragma once

#include <cstdint>

// Grows a city (autonomous simulator, traffic off) then runs the vehicle-agent
// micro-simulation on the result, reporting agent-level statistics. Returns the
// process exit code.
int runMicroTrafficDemo(
  int mapSize,
  uint32_t seed,
  int growthTicks,
  bool generateTerrain,
  float waterFraction,
  int maxSteps,
  int emergencyIncidents = 0,
  int lanesPerRoad = -1,       // -1 keeps TrafficMicroSim::Options' default
  float minFollowingGap = -1.0f  // negative keeps TrafficMicroSim::Options' default
);
