#include "src/cli/MicroTraffic.hpp"

#include <iomanip>
#include <iostream>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficMicroSim.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"

namespace {
// Spreads a handful of demo facility sites across the grown road network
// (evenly sampled from the road-tile list) so emergency dispatch has somewhere
// to originate from without depending on CitySimulator's internal placement.
std::vector<ServiceFacility> sampleDemoFacilities(const RoadNetwork& roads, int count) {
  std::vector<ServiceFacility> facilities;
  const std::vector<glm::ivec2> roadTiles = roads.getAllRoadTiles();
  if (roadTiles.empty() || count <= 0) {
    return facilities;
  }
  const size_t stride = std::max<size_t>(1, roadTiles.size() / static_cast<size_t>(count));
  for (size_t i = 0; i < roadTiles.size() && static_cast<int>(facilities.size()) < count; i += stride) {
    facilities.push_back(ServiceFacility{ServiceType::Fire, roadTiles[i], 10, 1.0f});
  }
  return facilities;
}
} // namespace

int runMicroTrafficDemo(
  int mapSize,
  uint32_t seed,
  int growthTicks,
  bool generateTerrain,
  float waterFraction,
  int maxSteps,
  int emergencyIncidents
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
  options.emergencyIncidents = std::max(0, emergencyIncidents);

  const std::vector<ServiceFacility> facilities = sampleDemoFacilities(roads, 4);
  const MicroTrafficSummary summary =
    TrafficMicroSim::simulate(store, population, roads, seed + 1u, options, &facilities);

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
  std::cout << "  Signalized junctions: " << summary.signalizedIntersections << "\n";
  std::cout << "  Avg signal wait:      " << std::setprecision(1) << summary.averageSignalWaitSteps << " steps/vehicle\n";

  if (options.emergencyIncidents > 0) {
    std::cout << "\nEmergency dispatch (from " << facilities.size() << " facilities):\n";
    std::cout << "  Vehicles dispatched:  " << summary.emergencyVehicles << "\n";
    std::cout << "  Vehicles arrived:     " << summary.emergencyArrived << "\n";
    std::cout << "  Avg response time:    " << std::setprecision(1)
              << summary.averageEmergencyResponseSteps << " steps\n";
  }

  return 0;
}
