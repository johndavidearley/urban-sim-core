#pragma once

#include <cstdint>
#include <string>

// Runs the autonomous RCI-demand-driven city simulation on a fresh map and
// prints an evolution table plus per-phase timing. Optionally writes a per-tick
// CSV. If ticks < 0, runs indefinitely until SIGINT (Ctrl+C). Returns the
// process exit code.
int runCitySimulation(
  int mapSize,
  uint32_t seed,
  int ticks,
  bool generateTerrain,
  float waterFraction,
  bool runTraffic,
  const std::string& reportPath,
  int gridSpacing = 4,
  double ticksPerSecond = 0.0,  // 0 = unlimited
  int trafficInterval = 1,      // run traffic every N ticks
  int serviceInterval = 1,      // run service evaluation every N ticks
  int populationInterval = 1    // run full population allocation every N ticks
);
