#include "src/cli/Simulate.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

#include "src/core/TileScale.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"

namespace {

volatile sig_atomic_t g_simulationStop = 0;
void onSigInt(int) { g_simulationStop = 1; }

static const char* kCSVHeader =
  "tick,demand_residential,demand_commercial,demand_industrial,demand_office,"
  "population,employed,residential_buildings,commercial_buildings,industrial_buildings,office_buildings,"
  "road_tiles,budget_balance,traffic_congestion,avg_pollution,"
  "service_coverage,service_facilities,avg_land_value,trade_balance,inflation_multiplier,"
  "transit_routes,transit_ridership,transit_demand,transit_modal_share\n";

void printRow(const SimTickMetrics& row) {
  std::cout << "  " << std::setw(5) << row.tick
            << " | " << std::fixed << std::setprecision(2)
            << row.demandResidential << " " << row.demandCommercial << " " << row.demandIndustrial
            << " " << row.demandOffice
            << " | " << std::setw(7) << row.population
            << " " << std::setw(7) << row.employed
            << " | " << std::setw(4) << row.residentialBuildings
            << " " << std::setw(4) << row.commercialBuildings
            << " " << std::setw(4) << row.industrialBuildings
            << " " << std::setw(4) << row.officeBuildings
            << " | " << std::setw(5) << row.roadTiles
            << " | " << std::setw(11) << row.budgetBalance
            << " | " << std::setprecision(2) << row.trafficCongestion
            << " | " << std::setw(3) << row.transitRoutes
            << " " << std::setprecision(2) << (row.transitModalShare * 100.0f) << "%\n";
}

void writeCSVRow(std::ostream& out, const SimTickMetrics& row) {
  out << row.tick << ","
      << std::fixed << std::setprecision(4)
      << row.demandResidential << "," << row.demandCommercial << "," << row.demandIndustrial << ","
      << row.demandOffice << ","
      << row.population << "," << row.employed << ","
      << row.residentialBuildings << "," << row.commercialBuildings << "," << row.industrialBuildings << ","
      << row.officeBuildings << ","
      << row.roadTiles << "," << row.budgetBalance << ","
      << row.trafficCongestion << "," << row.avgPollution << ","
      << row.serviceCoverage << "," << row.serviceFacilities << "," << row.avgLandValue << ","
      << row.tradeBalance << "," << row.inflationMultiplier << ","
      << row.transitRoutes << "," << row.transitRidership << "," << row.transitDemand << ","
      << row.transitModalShare << "\n";
}

bool writeReportCSV(const std::string& path, const std::vector<SimTickMetrics>& rows) {
  std::ofstream out(path);
  if (!out.is_open()) {
    return false;
  }
  out << kCSVHeader;
  for (const SimTickMetrics& row : rows) {
    writeCSVRow(out, row);
  }
  return static_cast<bool>(out);
}

void printTimings(const SimPhaseTimings& t, int ranTicks) {
  const double totalMs = t.roadMs + t.zoningMs + t.growthMs + t.populationMs + t.trafficMs +
                         t.economyMs + t.serviceMs + t.landValueMs + t.transitMs;
  std::cout << "\nPhase timing over " << ranTicks << " ticks (total "
            << std::fixed << std::setprecision(2) << totalMs << " ms, "
            << (ranTicks > 0 ? totalMs / ranTicks : 0.0) << " ms/tick):\n";
  std::cout << "    Roads:      " << t.roadMs << " ms\n";
  std::cout << "    Zoning:     " << t.zoningMs << " ms\n";
  std::cout << "    Growth:     " << t.growthMs << " ms\n";
  std::cout << "    Population: " << t.populationMs << " ms\n";
  std::cout << "    Traffic:    " << t.trafficMs << " ms\n";
  std::cout << "    Economy:    " << t.economyMs << " ms\n";
  std::cout << "    Services:   " << t.serviceMs << " ms\n";
  std::cout << "    LandValue:  " << t.landValueMs << " ms\n";
  std::cout << "    Transit:    " << t.transitMs << " ms\n";
}

} // namespace

int runCitySimulation(
  int mapSize,
  uint32_t seed,
  int ticks,
  bool generateTerrain,
  float waterFraction,
  bool runTraffic,
  const std::string& reportPath,
  int gridSpacing,
  double ticksPerSecond,
  int trafficInterval,
  int serviceInterval,
  int populationInterval,
  int landValueInterval,
  float inflationRate,
  bool enableTransit
) {
  const bool infinite = (ticks < 0);
  const float sideKm = tileToKm(mapSize);
  const float areaKm2 = mapAreaKm2(mapSize, mapSize);
  const int blockMeters = gridSpacing * static_cast<int>(kMetersPerTile);

  if (infinite) {
    std::cout << "Running infinite RCI simulation on " << mapSize << "x" << mapSize
              << " map (" << std::fixed << std::setprecision(1) << sideKm << " x " << sideKm
              << " km, " << std::setprecision(0) << areaKm2 << " km\xc2\xb2"
              << ", " << blockMeters << " m blocks"
              << ", seed " << seed << ")... Press Ctrl+C to stop.\n";
  } else {
    std::cout << "Running autonomous RCI simulation on " << mapSize << "x" << mapSize
              << " map (" << std::fixed << std::setprecision(1) << sideKm << " x " << sideKm
              << " km, " << std::setprecision(0) << areaKm2 << " km\xc2\xb2"
              << ", " << blockMeters << " m blocks"
              << ") for " << ticks << " ticks (seed " << seed << ")...\n";
  }

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
  options.gridSpacing = gridSpacing;
  options.trafficInterval = std::max(1, trafficInterval);
  options.serviceInterval = std::max(1, serviceInterval);
  options.populationInterval = std::max(1, populationInterval);
  options.landValueInterval = std::max(1, landValueInterval);
  options.inflationRatePerTick = inflationRate;
  options.enableTransit = enableTransit;

  if (!infinite) {
    const SimResult result = CitySimulator::run(map, roads, store, population, seed, ticks, options);

    // Evolution table: print a bounded number of sampled rows plus the final tick.
    std::cout << "\n  tick |  R    C    I    O  |     pop    empl |  res  com  ind  off | roads |     balance | cong | trn modal\n";
    std::cout << "  -----+--------------------+-----------------+---------------------+-------+-------------+------+----------\n";
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
                << " buildings=" << (last.residentialBuildings + last.commercialBuildings +
                                     last.industrialBuildings + last.officeBuildings)
                << " (R=" << last.residentialBuildings
                << " C=" << last.commercialBuildings
                << " I=" << last.industrialBuildings
                << " O=" << last.officeBuildings << ")"
                << " roadTiles=" << last.roadTiles
                << " residentialPollution=" << std::fixed << std::setprecision(2) << last.avgPollution
                << " serviceCoverage=" << last.serviceCoverage
                << " (" << last.serviceFacilities << " facilities)"
                << " avgLandValue=" << std::setprecision(1) << last.avgLandValue
                << " tradeBalance=" << last.tradeBalance
                << " inflationMultiplier=" << std::setprecision(3) << last.inflationMultiplier
                << " transitRoutes=" << last.transitRoutes
                << " transitRidership=" << last.transitRidership
                << " transitModalShare=" << std::setprecision(1) << (last.transitModalShare * 100.0f) << "%\n";
    }

    printTimings(result.timings, ticks);

    if (!reportPath.empty()) {
      if (!writeReportCSV(reportPath, result.rows)) {
        std::cerr << "Error: Failed to write simulation report to '" << reportPath << "'\n";
        return 1;
      }
      std::cout << "\nWrote per-tick report to " << reportPath << "\n";
    }

    return 0;
  }

  // ---- Infinite mode ----
  g_simulationStop = 0;
  const auto prevHandler = std::signal(SIGINT, onSigInt);

  std::ofstream csvOut;
  if (!reportPath.empty()) {
    csvOut.open(reportPath);
    if (!csvOut.is_open()) {
      std::cerr << "Error: Failed to open report file '" << reportPath << "'\n";
      std::signal(SIGINT, prevHandler);
      return 1;
    }
    csvOut << kCSVHeader;
  }

  std::cout << "\n  tick |  R    C    I    O  |     pop    empl |  res  com  ind  off | roads |     balance | cong | trn modal\n";
  std::cout << "  -----+--------------------+-----------------+---------------------+-------+-------------+------+----------\n";

  const int printEvery = 10;
  SimTickMetrics lastRow;
  bool hasLastRow = false;

  using Clock = std::chrono::steady_clock;
  const double targetTickMs = (ticksPerSecond > 0.0) ? 1000.0 / ticksPerSecond : 0.0;
  auto lastTickTime = Clock::now();

  options.tickCallback = [&](const SimTickMetrics& row) -> bool {
    lastRow = row;
    hasLastRow = true;
    if (row.tick % printEvery == 0) {
      printRow(row);
      std::cout.flush();
    }
    if (csvOut.is_open()) {
      writeCSVRow(csvOut, row);
      if (row.tick % 100 == 0) {
        csvOut.flush();
      }
    }
    if (targetTickMs > 0.0) {
      const auto now = Clock::now();
      const double elapsedMs = std::chrono::duration<double, std::milli>(now - lastTickTime).count();
      if (elapsedMs < targetTickMs) {
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(targetTickMs - elapsedMs));
      }
      lastTickTime = Clock::now();
    }
    return g_simulationStop == 0;
  };

  const SimResult result = CitySimulator::run(map, roads, store, population, seed, -1, options);
  std::signal(SIGINT, prevHandler);

  if (hasLastRow) {
    if (lastRow.tick % printEvery != 0) {
      printRow(lastRow);
    }
    std::cout << "\nStopped at tick " << lastRow.tick << ": population=" << lastRow.population
              << " employed=" << lastRow.employed
              << " buildings=" << (lastRow.residentialBuildings + lastRow.commercialBuildings +
                                   lastRow.industrialBuildings + lastRow.officeBuildings)
              << " (R=" << lastRow.residentialBuildings
              << " C=" << lastRow.commercialBuildings
              << " I=" << lastRow.industrialBuildings
              << " O=" << lastRow.officeBuildings << ")"
              << " roadTiles=" << lastRow.roadTiles
              << " serviceCoverage=" << std::fixed << std::setprecision(2) << lastRow.serviceCoverage
              << " (" << lastRow.serviceFacilities << " facilities)"
              << " avgLandValue=" << std::setprecision(1) << lastRow.avgLandValue
              << " tradeBalance=" << lastRow.tradeBalance
              << " inflationMultiplier=" << std::setprecision(3) << lastRow.inflationMultiplier
              << " transitRoutes=" << lastRow.transitRoutes
              << " transitRidership=" << lastRow.transitRidership
              << " transitModalShare=" << std::setprecision(1) << (lastRow.transitModalShare * 100.0f) << "%\n";
  }

  if (!reportPath.empty()) {
    std::cout << "Wrote per-tick report to " << reportPath << "\n";
  }

  const int ranTicks = hasLastRow ? lastRow.tick + 1 : 0;
  printTimings(result.timings, ranTicks);

  return 0;
}
