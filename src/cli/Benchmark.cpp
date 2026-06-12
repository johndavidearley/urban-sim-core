#include "src/cli/Benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

namespace {
void seedBenchmarkScenario(CityMap& map, RoadNetwork& roads) {
  const glm::ivec2 dims = map.getDimensions();
  const int midX = dims.x / 2;
  const int midY = dims.y / 2;

  Zoning::applyZoneRect(map, {2, 2}, {midX - 2, dims.y - 3}, ZoneType::Residential);
  Zoning::applyZoneRect(map, {midX + 1, 2}, {dims.x - 3, midY - 2}, ZoneType::Commercial);
  Zoning::applyZoneRect(map, {midX + 1, midY + 1}, {dims.x - 3, dims.y - 3}, ZoneType::Industrial);

  for (int x = 1; x < dims.x - 1; ++x) {
    roads.buildRoad({x, midY}, {x + 1, midY});
  }
  for (int y = 1; y < dims.y - 1; ++y) {
    roads.buildRoad({midX, y}, {midX, y + 1});
  }

  for (int x = 6; x < dims.x - 6; x += 8) {
    roads.buildRoad({x, 6}, {x, 7});
    roads.buildRoad({x, dims.y - 7}, {x, dims.y - 8});
  }

  roads.updateConnectivity({midX, midY});
}

void printBenchmarkResults(
  int benchmarkTicks,
  BenchmarkFocus focus,
  double growthMs,
  double populationMs,
  double trafficMs,
  double economyMs,
  double serviceMs,
  size_t buildingCount,
  uint32_t finalPopulation
) {
  const double totalMs = growthMs + populationMs + trafficMs + economyMs + serviceMs;
  const auto printPhaseTime = [](const char* label, double value, bool included) {
    std::cout << "    " << label << ": ";
    if (included) {
      std::cout << value << " ms\n";
    } else {
      std::cout << "excluded by focus\n";
    }
  };

  const bool includeGrowth = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Growth);
  const bool includePopulation = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Population);
  const bool includeTraffic = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Traffic);
  const bool includeEconomy = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Economy);
  const bool includeService = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Service);

  std::cout << "Phase 5 Performance Benchmark:\n";
  std::cout << "  Ticks: " << benchmarkTicks << "\n";
  std::cout << "  Focus: " << benchmarkFocusToString(focus) << "\n";
  std::cout << "  Final Buildings: " << buildingCount << "\n";
  std::cout << "  Final Population: " << finalPopulation << "\n";
  std::cout << "  Total Time: " << std::fixed << std::setprecision(2) << totalMs << " ms\n";
  std::cout << "  Avg/Tick: " << (benchmarkTicks > 0 ? (totalMs / benchmarkTicks) : 0.0) << " ms\n";
  std::cout << "  Breakdown:\n";
  printPhaseTime("Growth", growthMs, includeGrowth);
  printPhaseTime("Population", populationMs, includePopulation);
  printPhaseTime("Traffic", trafficMs, includeTraffic);
  printPhaseTime("Economy", economyMs, includeEconomy);
  printPhaseTime("Service", serviceMs, includeService);
}
} // namespace

int runPhase5Benchmark(int mapSize, uint32_t seed, int benchmarkTicks, BenchmarkFocus focus) {
  std::cout << "Running Phase 5 benchmark on " << mapSize << "x" << mapSize
            << " map for " << benchmarkTicks << " ticks"
            << " (focus=" << benchmarkFocusToString(focus) << ")...\n";

  CityMap benchmarkMap({mapSize, mapSize});
  RoadNetwork benchmarkRoads(benchmarkMap);
  EntityStore benchmarkStore;
  PopulationStore benchmarkPopulation;

  seedBenchmarkScenario(benchmarkMap, benchmarkRoads);

  std::vector<ServiceFacility> benchmarkFacilities;
  const int midX = mapSize / 2;
  const int midY = mapSize / 2;
  benchmarkFacilities.push_back(ServiceFacility{ServiceType::Fire, {midX - 6, midY}, 20, 1.0f});
  benchmarkFacilities.push_back(ServiceFacility{ServiceType::Police, {midX + 6, midY}, 20, 1.0f});
  benchmarkFacilities.push_back(ServiceFacility{ServiceType::Health, {midX, midY - 6}, 22, 1.0f});
  benchmarkFacilities.push_back(ServiceFacility{ServiceType::Education, {midX, midY + 6}, 24, 1.0f});

  double growthMs = 0.0;
  double populationMs = 0.0;
  double trafficMs = 0.0;
  double economyMs = 0.0;
  double serviceMs = 0.0;

  for (int tick = 0; tick < benchmarkTicks; ++tick) {
    const uint32_t tickSeed = seed + static_cast<uint32_t>(tick * 13);
    const ZoneDemand demand = Zoning::calculateDemand(tickSeed);

    if (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Growth) {
      auto t0 = std::chrono::steady_clock::now();
      (void)GrowthSystem::runStep(benchmarkMap, benchmarkRoads, benchmarkStore, demand, tickSeed, 0.30f);
      auto t1 = std::chrono::steady_clock::now();
      growthMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    } else {
      (void)GrowthSystem::runStep(benchmarkMap, benchmarkRoads, benchmarkStore, demand, tickSeed, 0.30f);
    }

    const uint32_t requestedPopulation = static_cast<uint32_t>(std::max(1000, mapSize * mapSize / 2))
      + static_cast<uint32_t>((tick % 12) * 100);
    if (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Population) {
      auto t0 = std::chrono::steady_clock::now();
      (void)PopulationSystem::allocate(benchmarkStore, benchmarkPopulation, requestedPopulation, tickSeed + 1);
      auto t1 = std::chrono::steady_clock::now();
      populationMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    } else {
      (void)PopulationSystem::allocate(benchmarkStore, benchmarkPopulation, requestedPopulation, tickSeed + 1);
    }

    if (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Traffic) {
      auto t0 = std::chrono::steady_clock::now();
      (void)TrafficSystem::simulateCommutes(benchmarkStore, benchmarkPopulation, benchmarkRoads, tickSeed + 2);
      auto t1 = std::chrono::steady_clock::now();
      trafficMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    } else {
      (void)TrafficSystem::simulateCommutes(benchmarkStore, benchmarkPopulation, benchmarkRoads, tickSeed + 2);
    }

    if (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Economy) {
      auto t0 = std::chrono::steady_clock::now();
      (void)EconomySystem::calculateEconomy(benchmarkStore, benchmarkPopulation);
      auto t1 = std::chrono::steady_clock::now();
      economyMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    } else {
      (void)EconomySystem::calculateEconomy(benchmarkStore, benchmarkPopulation);
    }

    if (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Service) {
      auto t0 = std::chrono::steady_clock::now();
      (void)ServiceSystem::evaluateCoverage(benchmarkStore, benchmarkRoads, benchmarkFacilities);
      auto t1 = std::chrono::steady_clock::now();
      serviceMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    } else {
      (void)ServiceSystem::evaluateCoverage(benchmarkStore, benchmarkRoads, benchmarkFacilities);
    }
  }

  printBenchmarkResults(
    benchmarkTicks,
    focus,
    growthMs,
    populationMs,
    trafficMs,
    economyMs,
    serviceMs,
    benchmarkStore.getBuildingCount(),
    benchmarkPopulation.getTotalPopulation()
  );
  return 0;
}
