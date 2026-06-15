#include "src/cli/Simulate.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"

namespace {
void printRow(const SimTickMetrics& row) {
  std::cout << "  " << std::setw(5) << row.tick
            << " | " << std::fixed << std::setprecision(2)
            << row.demandResidential << " " << row.demandCommercial << " " << row.demandIndustrial
            << " | " << std::setw(7) << row.population
            << " " << std::setw(7) << row.employed
            << " | " << std::setw(4) << row.residentialBuildings
            << " " << std::setw(4) << row.commercialBuildings
            << " " << std::setw(4) << row.industrialBuildings
            << " | " << std::setw(5) << row.roadTiles
            << " | " << std::setw(11) << row.budgetBalance
            << " | " << std::setprecision(2) << row.trafficCongestion << "\n";
}

bool writeReportCSV(const std::string& path, const std::vector<SimTickMetrics>& rows) {
  std::ofstream out(path);
  if (!out.is_open()) {
    return false;
  }
  out << "tick,demand_residential,demand_commercial,demand_industrial,"
         "population,employed,residential_buildings,commercial_buildings,industrial_buildings,"
         "road_tiles,budget_balance,traffic_congestion,avg_pollution,"
         "service_coverage,service_facilities\n";
  for (const SimTickMetrics& row : rows) {
    out << row.tick << ","
        << std::fixed << std::setprecision(4)
        << row.demandResidential << "," << row.demandCommercial << "," << row.demandIndustrial << ","
        << row.population << "," << row.employed << ","
        << row.residentialBuildings << "," << row.commercialBuildings << "," << row.industrialBuildings << ","
        << row.roadTiles << "," << row.budgetBalance << ","
        << row.trafficCongestion << "," << row.avgPollution << ","
        << row.serviceCoverage << "," << row.serviceFacilities << "\n";
  }
  return static_cast<bool>(out);
}
} // namespace

int runCitySimulation(
  int mapSize,
  uint32_t seed,
  int ticks,
  bool generateTerrain,
  float waterFraction,
  bool runTraffic,
  const std::string& reportPath
) {
  std::cout << "Running autonomous RCI simulation on " << mapSize << "x" << mapSize
            << " map for " << ticks << " ticks (seed " << seed << ")...\n";

  CityMap map({mapSize, mapSize});
  if (generateTerrain) {
    TerrainParams terrainParams;
    terrainParams.waterFraction = waterFraction;
    const TerrainStats terrainStats = TerrainGenerator::generate(map, seed, terrainParams);
    std::cout << "Terrain: " << terrainStats.waterTiles << " water, "
              << terrainStats.terrainTiles << " terrain, "
              << terrainStats.buildableTiles << " buildable tiles\n";
  }

  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  SimOptions options;
  options.runTraffic = runTraffic;

  const SimResult result = CitySimulator::run(map, roads, store, population, seed, ticks, options);

  // Evolution table: print a bounded number of sampled rows plus the final tick.
  std::cout << "\n  tick |  R    C    I  |     pop    empl |  res  com  ind | roads |     balance | cong\n";
  std::cout << "  -----+---------------+-----------------+----------------+-------+-------------+-----\n";
  if (!result.rows.empty()) {
    const size_t maxRows = 20;
    const size_t step = std::max<size_t>(1, result.rows.size() / maxRows);
    for (size_t i = 0; i < result.rows.size(); i += step) {
      printRow(result.rows[i]);
    }
    if ((result.rows.size() - 1) % step != 0) {
      printRow(result.rows.back());
    }
  }

  if (!result.rows.empty()) {
    const SimTickMetrics& last = result.rows.back();
    std::cout << "\nFinal city: population=" << last.population
              << " employed=" << last.employed
              << " buildings=" << (last.residentialBuildings + last.commercialBuildings + last.industrialBuildings)
              << " (R=" << last.residentialBuildings
              << " C=" << last.commercialBuildings
              << " I=" << last.industrialBuildings << ")"
              << " roadTiles=" << last.roadTiles
              << " residentialPollution=" << std::fixed << std::setprecision(2) << last.avgPollution
              << " serviceCoverage=" << last.serviceCoverage
              << " (" << last.serviceFacilities << " facilities)\n";
  }

  const SimPhaseTimings& t = result.timings;
  const double totalMs = t.roadMs + t.zoningMs + t.growthMs + t.populationMs + t.trafficMs + t.economyMs;
  std::cout << "\nPhase timing over " << ticks << " ticks (total "
            << std::fixed << std::setprecision(2) << totalMs << " ms, "
            << (ticks > 0 ? totalMs / ticks : 0.0) << " ms/tick):\n";
  std::cout << "    Roads:      " << t.roadMs << " ms\n";
  std::cout << "    Zoning:     " << t.zoningMs << " ms\n";
  std::cout << "    Growth:     " << t.growthMs << " ms\n";
  std::cout << "    Population: " << t.populationMs << " ms\n";
  std::cout << "    Traffic:    " << t.trafficMs << " ms\n";
  std::cout << "    Economy:    " << t.economyMs << " ms\n";
  std::cout << "    Services:   " << t.serviceMs << " ms\n";

  if (!reportPath.empty()) {
    if (!writeReportCSV(reportPath, result.rows)) {
      std::cerr << "Error: Failed to write simulation report to '" << reportPath << "'\n";
      return 1;
    }
    std::cout << "\nWrote per-tick report to " << reportPath << "\n";
  }

  return 0;
}
