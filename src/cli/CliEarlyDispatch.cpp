#include "src/cli/CliEarlyDispatch.hpp"

#include "src/cli/CliOptions.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "src/cli/Benchmark.hpp"
#include "src/cli/CityPrinters.hpp"
#include "src/cli/CliUtils.hpp"
#include "src/cli/DistrictCommands.hpp"
#include "src/cli/GrowthPressureReport.hpp"
#include "src/cli/PolicySweep.hpp"
#include "src/cli/Simulate.hpp"
#include "src/cli/MicroTraffic.hpp"
#include "src/cli/SnapshotAudit.hpp"
#include "src/cli/CommutePolicySweep.hpp"
#include "src/core/SimulationTime.hpp"
#include "src/core/TileScale.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/metrics/CityMetrics.hpp"
#include "src/metrics/GrowthMetrics.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/persistence/ReplayVerifier.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/MetricsSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/visualization/MapRenderer.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"


std::optional<int> tryEarlyCliDispatch(CliOptions& opts) {
    if (!opts.rankGrowthPressureBasePath.empty()) {
      std::vector<GrowthPressureReportRow> baselineRows;
      std::string baselineError;
      if (!loadGrowthPressureReportCSV(opts.rankGrowthPressureBasePath, baselineRows, baselineError)) {
        std::cerr << "Error: Failed to load baseline pressure report ('" << opts.rankGrowthPressureBasePath
                  << "'): " << baselineError << "\n";
        return 1;
      }

      std::vector<std::pair<std::string, std::vector<GrowthPressureReportRow>>> candidates;
      candidates.reserve(opts.rankGrowthPressureCandidatePaths.size());

      for (const std::string& candidatePath : opts.rankGrowthPressureCandidatePaths) {
        std::vector<GrowthPressureReportRow> candidateRows;
        std::string candidateError;
        if (!loadGrowthPressureReportCSV(candidatePath, candidateRows, candidateError)) {
          std::cerr << "Error: Failed to load candidate pressure report ('" << candidatePath
                    << "'): " << candidateError << "\n";
          return 1;
        }
        candidates.emplace_back(candidatePath, std::move(candidateRows));
      }

      printGrowthPressureRanking(opts.rankGrowthPressureBasePath, baselineRows, candidates);
      return 0;
    }

    if (!opts.compareGrowthPressurePathA.empty()) {
      std::vector<GrowthPressureReportRow> rowsA;
      std::vector<GrowthPressureReportRow> rowsB;
      std::string errorA;
      std::string errorB;

      if (!loadGrowthPressureReportCSV(opts.compareGrowthPressurePathA, rowsA, errorA)) {
        std::cerr << "Error: Failed to load pressure report A ('" << opts.compareGrowthPressurePathA
                  << "'): " << errorA << "\n";
        return 1;
      }
      if (!loadGrowthPressureReportCSV(opts.compareGrowthPressurePathB, rowsB, errorB)) {
        std::cerr << "Error: Failed to load pressure report B ('" << opts.compareGrowthPressurePathB
                  << "'): " << errorB << "\n";
        return 1;
      }

      printGrowthPressureComparison(
        opts.compareGrowthPressurePathA,
        rowsA,
        opts.compareGrowthPressurePathB,
        rowsB
      );
      return 0;
    }

    if (!opts.inspectSnapshotPath.empty()) {
      CitySnapshot inspectedSnapshot;
      SnapshotLoadDiagnostics inspectDiagnostics;
      if (!SaveLoadSystem::loadSnapshotFromFile(opts.inspectSnapshotPath, inspectedSnapshot, &inspectDiagnostics)) {
        std::cerr << "Error: Failed to inspect city snapshot from '" << opts.inspectSnapshotPath
                  << "': " << inspectDiagnostics.errorMessage << "\n";
        return 1;
      }
      printSnapshotInspection(inspectedSnapshot, inspectDiagnostics);
      return 0;
    }

    if (!opts.auditSnapshotsDir.empty()) {
      return runSnapshotAudit(opts.auditSnapshotsDir);
    }

    if (opts.benchmarkPhase5Ticks >= 0) {
      return runPhase5Benchmark(opts.mapSize, opts.seed, opts.benchmarkPhase5Ticks, opts.benchmarkPhase5Focus);
    }

    if (opts.simulateTicks >= 0 && opts.simulateBenchmarkTrials >= 2) {
      const int gridSpacing = (opts.gridSpacingOverride > 0) ? opts.gridSpacingOverride : 4;
      return runCitySimulationBenchmark(
        opts.mapSize,
        opts.seed,
        opts.simulateTicks,
        opts.generateTerrainFlag,
        opts.terrainWaterFraction,
        !opts.simulateNoTraffic,
        opts.simulateBenchmarkTrials,
        gridSpacing,
        opts.simulateTrafficInterval,
        opts.simulateServiceInterval,
        opts.simulatePopulationInterval,
        opts.simulateLandValueInterval,
        opts.simulateInflationRate,
        !opts.simulateNoTransit,
        opts.simulateDisasters,
        opts.simulateFireRisk,
        opts.simulateEarthquakeRisk,
        opts.simulateFloodRisk,
        opts.simulateUtilities
      );
    }

    if (opts.simulateTicks >= 0 || opts.simulateInfinite) {
      const int gridSpacing = (opts.gridSpacingOverride > 0) ? opts.gridSpacingOverride : 4;
      return runCitySimulation(
        opts.mapSize,
        opts.seed,
        opts.simulateInfinite ? -1 : opts.simulateTicks,
        opts.generateTerrainFlag,
        opts.terrainWaterFraction,
        !opts.simulateNoTraffic,
        opts.simulateReportPath,
        gridSpacing,
        opts.simulateSpeed,
        opts.simulateTrafficInterval,
        opts.simulateServiceInterval,
        opts.simulatePopulationInterval,
        opts.simulateLandValueInterval,
        opts.simulateInflationRate,
        !opts.simulateNoTransit,
        opts.simulateDistrictRequests,
        opts.simulateDistrictArchetypeRequests,
        opts.simulateDisasters,
        opts.simulateFireRisk,
        opts.simulateEarthquakeRisk,
        opts.simulateFloodRisk,
        opts.simulateUtilities
      );
    }

    if (opts.microTrafficGrowthTicks >= 0) {
      return runMicroTrafficDemo(
        opts.mapSize,
        opts.seed,
        opts.microTrafficGrowthTicks,
        opts.generateTerrainFlag,
        opts.terrainWaterFraction,
        opts.microTrafficSteps,
        opts.microTrafficIncidents,
        opts.microTrafficLanes,
        opts.microTrafficFollowingGap,
        opts.microTrafficVehicleLength
      );
    }

  return std::nullopt;
}
