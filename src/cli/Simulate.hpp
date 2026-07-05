#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "src/systems/CitySimulator.hpp"

// name, x1, y1, x2, y2 - mirrors main.cpp's zoneRequests tuple shape.
using SimulateDistrictRequest = std::tuple<std::string, int, int, int, int>;

// district name, archetype name ("GENERAL"/"INDUSTRIAL"/"TECHHUB") - applied
// after all districtRequests are created, matched by name.
using SimulateDistrictArchetypeRequest = std::tuple<std::string, std::string>;

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
  int populationInterval = 1,   // run full population allocation every N ticks
  int landValueInterval = 1,    // run land-value recompute every N ticks
  float inflationRate = 0.0f,   // compounding per-tick inflation rate applied to costs/trade prices, not tax revenue
  bool enableTransit = true,    // auto-place bus routes that offload commuters from the road network
  const std::vector<SimulateDistrictRequest>& districtRequests = {},  // user-defined districts (default tax rates/service allocation) that shape growth pressure
  const std::vector<SimulateDistrictArchetypeRequest>& districtArchetypeRequests = {},  // apply a named archetype (zoning ordinance + service priorities) to a district by name
  bool enableDisasters = false,  // opt-in: buildings can be destroyed by fire/earthquake/flood (see FireSystem/NaturalDisasterSystem)
  float fireRiskMultiplier = 1.0f,     // scales FireParams::baseIgnitionChance; only relevant when enableDisasters is set
  float earthquakeRiskMultiplier = 1.0f,  // scales DisasterParams::earthquakeChancePerTick; only relevant when enableDisasters is set
  float floodRiskMultiplier = 1.0f     // scales DisasterParams::floodChancePerTick; only relevant when enableDisasters is set
);

// Runs the same autonomous simulation scenario `trials` times - same seed
// every trial, so run-to-run differences reflect measurement noise
// (scheduling, cache/thermal state), not different simulated cities - and
// reports min/median/max per-phase timing across trials instead of a
// single (noisy) run's numbers. Building "Next Targets" backlog item 3
// ("Extend benchmark reports with percentile timings over repeated runs").
// Finite ticks only (no infinite-mode benchmarking). Returns the process
// exit code.
int runCitySimulationBenchmark(
  int mapSize,
  uint32_t seed,
  int ticks,
  bool generateTerrain,
  float waterFraction,
  bool runTraffic,
  int trials,
  int gridSpacing = 4,
  int trafficInterval = 1,
  int serviceInterval = 1,
  int populationInterval = 1,
  int landValueInterval = 1,
  float inflationRate = 0.0f,
  bool enableTransit = true,
  bool enableDisasters = false,
  float fireRiskMultiplier = 1.0f,
  float earthquakeRiskMultiplier = 1.0f,
  float floodRiskMultiplier = 1.0f
);

// Writes a vector of per-tick simulation metrics to CSV in the same format
// --simulate-report uses. Exposed for reuse by other CLI tools (e.g.
// CommutePolicySweep) that produce SimTickMetrics rows of their own.
// Returns false if filePath couldn't be opened for writing.
bool writeSimulationReportCSV(const std::string& path, const std::vector<SimTickMetrics>& rows);
