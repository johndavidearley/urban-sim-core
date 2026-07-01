#include "src/cli/MicroTraffic.hpp"

#include <iomanip>
#include <iostream>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/systems/TrafficMicroSim.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"

int runMicroTrafficDemo(
  int mapSize,
  uint32_t seed,
  int growthTicks,
  bool generateTerrain,
  float waterFraction,
  int maxSteps
) {
  std::cout << "Traffic micro-simulation on a " << mapSize << "x" << mapSize
            << " city (seed " << seed << ", grown " << growthTicks << " ticks)...\n";

  CityMap map({mapSize, mapSize});
  if (generateTerrain) {
    TerrainParams terrainParams;
    terrainParams.waterFraction = waterFraction;
    TerrainGenerator::generate(map, seed, terrainParams);
  }

  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  // Grow a realistic city first (aggregate traffic off - the micro-sim is the point).
  SimOptions simOptions;
  simOptions.runTraffic = false;
  CitySimulator::run(map, roads, store, population, seed, growthTicks, simOptions);

  TrafficMicroSim::Options options;
  if (maxSteps > 0) {
    options.maxSteps = maxSteps;
  }

  const MicroTrafficSummary summary = TrafficMicroSim::simulate(store, population, roads, seed + 1u, options);

  std::cout << "\nVehicle agents:\n";
  std::cout << "  Commuting population: " << summary.commutingPopulation << "\n";
  std::cout << "  Vehicles spawned:     " << summary.vehicles << "\n";
  std::cout << "  Vehicles arrived:     " << summary.arrived
            << " (" << std::fixed << std::setprecision(1)
            << (summary.vehicles > 0 ? 100.0 * summary.arrived / summary.vehicles : 0.0) << "%)\n";
  std::cout << "  Steps simulated:      " << summary.stepsSimulated << "\n";
  std::cout << "  Avg trip length:      " << std::setprecision(1) << summary.averageTripSteps << " steps\n";
  std::cout << "  Peak edge occupancy:  " << std::setprecision(0) << summary.peakEdgeOccupancy << " vehicles\n";
  std::cout << "  Avg edge occupancy:   " << std::setprecision(2) << summary.averageEdgeOccupancy << " vehicles\n";

  return 0;
}
