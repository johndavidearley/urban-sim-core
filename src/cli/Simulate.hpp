#pragma once

#include <cstdint>
#include <string>

// Runs the autonomous RCI-demand-driven city simulation on a fresh map and
// prints an evolution table plus per-phase timing. Optionally writes a per-tick
// CSV. Returns the process exit code.
int runCitySimulation(
  int mapSize,
  uint32_t seed,
  int ticks,
  bool generateTerrain,
  float waterFraction,
  bool runTraffic,
  const std::string& reportPath
);
