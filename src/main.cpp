#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
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

int main(int argc, char* argv[]) {
  // Parse arguments
  int mapSize = 64;
  int numTicks = 100;
  int gridSpacingOverride = -1;  // -1 = use SimOptions default
  uint32_t seed = 42;
  bool printMapFlag = false;
  int printTileX = -1, printTileY = -1;
  bool printZonesFlag = false;
  bool printDemandFlag = false;
  bool printGrowthSummaryFlag = false;
  bool printPopulationSummaryFlag = false;
  bool printPopulationGroupsFlag = false;
  bool printBuildingsFlag = false;
  bool printConnectivityMapFlag = false;
  bool runCommuteSimulationFlag = false;
  bool printTrafficSummaryFlag = false;
  bool runEconomyCalculationFlag = false;
  bool printBudgetSummaryFlag = false;
  bool runServiceEvaluationFlag = false;
  bool printServiceSummaryFlag = false;
  bool printCitySummaryFlag = false;
  std::string renderMapPath;
  int renderScale = 8;
  bool hasRenderView = false;
  int renderViewX = 0, renderViewY = 0, renderViewW = -1, renderViewH = -1;
  int verifyReplayGrowthSteps = -1;
  int simulateTicks = -1;
  bool simulateInfinite = false;
  std::string simulateReportPath;
  bool simulateNoTraffic = false;
  double simulateSpeed = 0.0;
  int simulateTrafficInterval = 1;
  int simulateServiceInterval = 1;
  int simulatePopulationInterval = 1;
  int simulateLandValueInterval = 1;
  float simulateInflationRate = 0.0f;
  bool simulateNoTransit = false;
  std::vector<SimulateDistrictRequest> simulateDistrictRequests;  // name, x1, y1, x2, y2
  int microTrafficGrowthTicks = -1;
  int microTrafficSteps = -1;
  int microTrafficIncidents = 0;
  int microTrafficLanes = -1;
  float microTrafficFollowingGap = -1.0f;
  int benchmarkPhase5Ticks = -1;
  BenchmarkFocus benchmarkPhase5Focus = BenchmarkFocus::All;
  std::string saveCityPath;
  std::string loadCityPath;
  std::string inspectSnapshotPath;
  std::vector<std::tuple<std::string, int, int, int>> serviceRequests;
  int printTopEdgesCount = -1;
  bool hasTrafficOriginFilter = false;
  int trafficOriginX = -1, trafficOriginY = -1;
  bool hasTrafficDestinationFilter = false;
  int trafficDestinationX = -1, trafficDestinationY = -1;
  std::vector<std::tuple<int, int, int, int, std::string>> zoneRequests;  // x1, y1, x2, y2, type
  bool generateTerrainFlag = false;
  float terrainWaterFraction = 0.18f;
  int runGrowthSteps = 0;
  int64_t districtPressurePool = -1;
  bool printGrowthPressureFlag = false;
  std::string exportGrowthPressurePath;
  std::string compareGrowthPressurePathA;
  std::string compareGrowthPressurePathB;
  std::string rankGrowthPressureBasePath;
  std::vector<std::string> rankGrowthPressureCandidatePaths;
  std::string runPolicySweepOutputDir;
  int sweepDistrictId = -1;
  std::vector<uint32_t> sweepSeeds;
  std::vector<int64_t> sweepCaps;
  std::vector<float> sweepAllocations;
  bool sweepManifestAllDistricts = false;
  int seedPopulation = -1;
  std::vector<std::tuple<int, int, int, int>> placeRoadRequests;  // x1, y1, x2, y2
  int findPathX1 = -1, findPathY1 = -1, findPathX2 = -1, findPathY2 = -1;
  bool listDistrictsFlag = false;
  int printDistrictSummaryId = -1;
  int printDistrictFacilitiesId = -1;
  int64_t printDistrictBalancingPool = -1;
  DistrictMutationRequests districtRequests;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help") {
      printHelp();
      return 0;
    } else if (arg == "--size" && i + 1 < argc) {
      mapSize = std::atoi(argv[++i]);
    } else if (arg == "--grid-spacing" && i + 1 < argc) {
      gridSpacingOverride = std::atoi(argv[++i]);
    } else if (arg == "--ticks" && i + 1 < argc) {
      numTicks = std::atoi(argv[++i]);
    } else if (arg == "--seed" && i + 1 < argc) {
      seed = std::atoi(argv[++i]);
    } else if (arg == "--print-map") {
      printMapFlag = true;
    } else if (arg == "--print-tile" && i + 2 < argc) {
      printTileX = std::atoi(argv[++i]);
      printTileY = std::atoi(argv[++i]);
    } else if (arg == "--zone-rect" && i + 5 < argc) {
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      std::string type = argv[++i];
      zoneRequests.emplace_back(x1, y1, x2, y2, type);
    } else if (arg == "--generate-terrain") {
      generateTerrainFlag = true;
    } else if (arg == "--terrain-water" && i + 1 < argc) {
      terrainWaterFraction = std::stof(argv[++i]);
      generateTerrainFlag = true;
    } else if (arg == "--print-zones") {
      printZonesFlag = true;
    } else if (arg == "--print-demand") {
      printDemandFlag = true;
    } else if (arg == "--print-growth-summary") {
      printGrowthSummaryFlag = true;
    } else if (arg == "--seed-population" && i + 1 < argc) {
      seedPopulation = std::atoi(argv[++i]);
    } else if (arg == "--print-population-summary") {
      printPopulationSummaryFlag = true;
    } else if (arg == "--print-population-groups") {
      printPopulationGroupsFlag = true;
    } else if (arg == "--run-growth" && i + 1 < argc) {
      runGrowthSteps = std::atoi(argv[++i]);
    } else if (arg == "--district-pressure-pool" && i + 1 < argc) {
      districtPressurePool = std::strtoll(argv[++i], nullptr, 10);
    } else if (arg == "--print-growth-pressure") {
      printGrowthPressureFlag = true;
    } else if (arg == "--export-growth-pressure" && i + 1 < argc) {
      exportGrowthPressurePath = argv[++i];
    } else if (arg == "--compare-growth-pressure" && i + 2 < argc) {
      compareGrowthPressurePathA = argv[++i];
      compareGrowthPressurePathB = argv[++i];
    } else if (arg == "--rank-growth-pressure" && i + 2 < argc) {
      const std::string basePath = argv[++i];
      const std::string candidatePath = argv[++i];
      if (rankGrowthPressureBasePath.empty()) {
        rankGrowthPressureBasePath = basePath;
      } else if (rankGrowthPressureBasePath != basePath) {
        std::cerr << "Error: --rank-growth-pressure must use the same BASE for all entries\n";
        return 1;
      }
      rankGrowthPressureCandidatePaths.push_back(candidatePath);
    } else if (arg == "--run-policy-sweep" && i + 1 < argc) {
      runPolicySweepOutputDir = argv[++i];
    } else if (arg == "--sweep-district" && i + 1 < argc) {
      sweepDistrictId = std::atoi(argv[++i]);
    } else if (arg == "--sweep-seeds" && i + 1 < argc) {
      if (!parseUint32List(argv[++i], sweepSeeds)) {
        std::cerr << "Error: --sweep-seeds expects a comma-separated list of integers\n";
        return 1;
      }
    } else if (arg == "--sweep-caps" && i + 1 < argc) {
      if (!parseInt64List(argv[++i], sweepCaps)) {
        std::cerr << "Error: --sweep-caps expects a comma-separated list of integers\n";
        return 1;
      }
    } else if (arg == "--sweep-allocations" && i + 1 < argc) {
      if (!parseFloatList(argv[++i], sweepAllocations)) {
        std::cerr << "Error: --sweep-allocations expects a comma-separated list of decimal values\n";
        return 1;
      }
    } else if (arg == "--sweep-manifest-all-districts") {
      sweepManifestAllDistricts = true;
    } else if (arg == "--print-buildings") {
      printBuildingsFlag = true;
    } else if (arg == "--connectivity-map") {
      printConnectivityMapFlag = true;
    } else if (arg == "--place-road" && i + 4 < argc) {
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      placeRoadRequests.emplace_back(x1, y1, x2, y2);
    } else if (arg == "--find-path" && i + 4 < argc) {
      findPathX1 = std::atoi(argv[++i]);
      findPathY1 = std::atoi(argv[++i]);
      findPathX2 = std::atoi(argv[++i]);
      findPathY2 = std::atoi(argv[++i]);
    } else if (arg == "--run-commute-simulation") {
      runCommuteSimulationFlag = true;
    } else if (arg == "--print-traffic-summary") {
      printTrafficSummaryFlag = true;
    } else if (arg == "--print-top-edges" && i + 1 < argc) {
      printTopEdgesCount = std::atoi(argv[++i]);
    } else if (arg == "--traffic-origin" && i + 2 < argc) {
      trafficOriginX = std::atoi(argv[++i]);
      trafficOriginY = std::atoi(argv[++i]);
      hasTrafficOriginFilter = true;
    } else if (arg == "--traffic-destination" && i + 2 < argc) {
      trafficDestinationX = std::atoi(argv[++i]);
      trafficDestinationY = std::atoi(argv[++i]);
      hasTrafficDestinationFilter = true;
    } else if (arg == "--create-district" && i + 5 < argc) {
      std::string name = argv[++i];
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      districtRequests.create.emplace_back(name, x1, y1, x2, y2);
    } else if (arg == "--list-districts") {
      listDistrictsFlag = true;
    } else if (arg == "--print-district-summary" && i + 1 < argc) {
      printDistrictSummaryId = std::atoi(argv[++i]);
    } else if (arg == "--print-district-facilities" && i + 1 < argc) {
      printDistrictFacilitiesId = std::atoi(argv[++i]);
    } else if (arg == "--print-district-balancing" && i + 1 < argc) {
      printDistrictBalancingPool = std::strtoll(argv[++i], nullptr, 10);
    } else if (arg == "--set-district-tax" && i + 3 < argc) {
      int districtId = std::atoi(argv[++i]);
      std::string buildingType = argv[++i];
      float rate = std::stof(argv[++i]);
      districtRequests.setTax.emplace_back(districtId, buildingType, rate);
    } else if (arg == "--set-district-service" && i + 5 < argc) {
      int districtId = std::atoi(argv[++i]);
      float fireWeight = std::stof(argv[++i]);
      float policeWeight = std::stof(argv[++i]);
      float healthWeight = std::stof(argv[++i]);
      float educationWeight = std::stof(argv[++i]);
      districtRequests.setServicePriorities.emplace_back(districtId, fireWeight, policeWeight, healthWeight, educationWeight);
    } else if (arg == "--set-district-allocation" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      float allocation = std::stof(argv[++i]);
      districtRequests.setAllocation.emplace_back(districtId, allocation);
    } else if (arg == "--set-district-budget-cap" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int64_t cap = std::strtoll(argv[++i], nullptr, 10);
      districtRequests.setBudgetCap.emplace_back(districtId, cap);
    } else if (arg == "--assign-facility" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int facilityId = std::atoi(argv[++i]);
      districtRequests.assignFacility.emplace_back(districtId, facilityId);
    } else if (arg == "--unassign-facility" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int facilityId = std::atoi(argv[++i]);
      districtRequests.unassignFacility.emplace_back(districtId, facilityId);
    } else if (arg == "--run-economy-calculation") {
      runEconomyCalculationFlag = true;
    } else if (arg == "--print-budget-summary") {
      printBudgetSummaryFlag = true;
    } else if (arg == "--add-service" && i + 4 < argc) {
      std::string serviceType = argv[++i];
      int x = std::atoi(argv[++i]);
      int y = std::atoi(argv[++i]);
      int dist = std::atoi(argv[++i]);
      serviceRequests.emplace_back(serviceType, x, y, dist);
    } else if (arg == "--run-service-evaluation") {
      runServiceEvaluationFlag = true;
    } else if (arg == "--print-service-summary") {
      printServiceSummaryFlag = true;
    } else if (arg == "--print-city-summary") {
      printCitySummaryFlag = true;
    } else if (arg == "--render-map" && i + 1 < argc) {
      renderMapPath = argv[++i];
    } else if (arg == "--render-scale" && i + 1 < argc) {
      renderScale = std::atoi(argv[++i]);
    } else if (arg == "--render-view" && i + 4 < argc) {
      renderViewX = std::atoi(argv[++i]);
      renderViewY = std::atoi(argv[++i]);
      renderViewW = std::atoi(argv[++i]);
      renderViewH = std::atoi(argv[++i]);
      hasRenderView = true;
    } else if (arg == "--save-city" && i + 1 < argc) {
      saveCityPath = argv[++i];
    } else if (arg == "--load-city" && i + 1 < argc) {
      loadCityPath = argv[++i];
    } else if (arg == "--inspect-snapshot" && i + 1 < argc) {
      inspectSnapshotPath = argv[++i];
    } else if (arg == "--benchmark-phase5" && i + 1 < argc) {
      benchmarkPhase5Ticks = std::atoi(argv[++i]);
    } else if (arg == "--benchmark-phase5-focus" && i + 1 < argc) {
      if (!parseBenchmarkFocus(argv[++i], benchmarkPhase5Focus)) {
        std::cerr << "Error: Unknown benchmark focus. Use ALL, GROWTH, POPULATION, TRAFFIC, ECONOMY, or SERVICE\n";
        return 1;
      }
    } else if (arg == "--verify-replay" && i + 1 < argc) {
      verifyReplayGrowthSteps = std::atoi(argv[++i]);
    } else if (arg == "--simulate" && i + 1 < argc) {
      simulateTicks = std::atoi(argv[++i]);
    } else if (arg == "--simulate-infinite") {
      simulateInfinite = true;
    } else if (arg == "--simulate-speed" && i + 1 < argc) {
      simulateSpeed = std::stod(argv[++i]);
    } else if (arg == "--simulate-traffic-interval" && i + 1 < argc) {
      simulateTrafficInterval = std::atoi(argv[++i]);
    } else if (arg == "--simulate-service-interval" && i + 1 < argc) {
      simulateServiceInterval = std::atoi(argv[++i]);
    } else if (arg == "--simulate-population-interval" && i + 1 < argc) {
      simulatePopulationInterval = std::atoi(argv[++i]);
    } else if (arg == "--simulate-land-value-interval" && i + 1 < argc) {
      simulateLandValueInterval = std::atoi(argv[++i]);
    } else if (arg == "--simulate-inflation-rate" && i + 1 < argc) {
      simulateInflationRate = std::stof(argv[++i]);
    } else if (arg == "--simulate-no-transit") {
      simulateNoTransit = true;
    } else if (arg == "--simulate-district" && i + 5 < argc) {
      std::string name = argv[++i];
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      simulateDistrictRequests.emplace_back(name, x1, y1, x2, y2);
    } else if (arg == "--simulate-report" && i + 1 < argc) {
      simulateReportPath = argv[++i];
    } else if (arg == "--simulate-no-traffic") {
      simulateNoTraffic = true;
    } else if (arg == "--micro-traffic" && i + 1 < argc) {
      microTrafficGrowthTicks = std::atoi(argv[++i]);
    } else if (arg == "--micro-traffic-steps" && i + 1 < argc) {
      microTrafficSteps = std::atoi(argv[++i]);
    } else if (arg == "--micro-traffic-incidents" && i + 1 < argc) {
      microTrafficIncidents = std::atoi(argv[++i]);
    } else if (arg == "--micro-traffic-lanes" && i + 1 < argc) {
      microTrafficLanes = std::atoi(argv[++i]);
    } else if (arg == "--micro-traffic-following-gap" && i + 1 < argc) {
      microTrafficFollowingGap = std::stof(argv[++i]);
    } else {
      std::cerr << "Error: Unknown argument '" << arg << "'\n";
      return 1;
    }
  }

  // Validate arguments
  if (mapSize <= 0 || mapSize > 512) {
    std::cerr << "Error: Map size must be between 1 and 512\n";
    return 1;
  }
  if (numTicks < 0) {
    std::cerr << "Error: Number of ticks must be non-negative\n";
    return 1;
  }
  if (benchmarkPhase5Ticks < -1) {
    std::cerr << "Error: benchmark ticks must be non-negative\n";
    return 1;
  }
  if (districtPressurePool >= 0 && runGrowthSteps <= 0) {
    std::cerr << "Error: --district-pressure-pool requires --run-growth N\n";
    return 1;
  }
  if (printGrowthPressureFlag && districtPressurePool < 0) {
    std::cerr << "Error: --print-growth-pressure requires --district-pressure-pool POOL\n";
    return 1;
  }
  if (!exportGrowthPressurePath.empty() && districtPressurePool < 0) {
    std::cerr << "Error: --export-growth-pressure requires --district-pressure-pool POOL\n";
    return 1;
  }
  if (!exportGrowthPressurePath.empty() && runGrowthSteps <= 0) {
    std::cerr << "Error: --export-growth-pressure requires --run-growth N\n";
    return 1;
  }
  if ((!compareGrowthPressurePathA.empty() && compareGrowthPressurePathB.empty()) ||
      (compareGrowthPressurePathA.empty() && !compareGrowthPressurePathB.empty())) {
    std::cerr << "Error: --compare-growth-pressure requires two file paths\n";
    return 1;
  }
  if (!rankGrowthPressureBasePath.empty() && rankGrowthPressureCandidatePaths.empty()) {
    std::cerr << "Error: --rank-growth-pressure requires at least one candidate\n";
    return 1;
  }
  if (!runPolicySweepOutputDir.empty()) {
    if (runGrowthSteps <= 0) {
      std::cerr << "Error: --run-policy-sweep requires --run-growth N\n";
      return 1;
    }
    if (districtPressurePool < 0) {
      std::cerr << "Error: --run-policy-sweep requires --district-pressure-pool POOL\n";
      return 1;
    }
    if (sweepDistrictId <= 0) {
      std::cerr << "Error: --run-policy-sweep requires --sweep-district DIST_ID\n";
      return 1;
    }
    if (sweepSeeds.empty()) {
      std::cerr << "Error: --run-policy-sweep requires --sweep-seeds A,B,C\n";
      return 1;
    }
  }
  if ((hasTrafficOriginFilter || hasTrafficDestinationFilter) && printTopEdgesCount <= 0) {
    std::cerr << "Error: --traffic-origin/--traffic-destination require --print-top-edges N\n";
    return 1;
  }

  try {
    if (!rankGrowthPressureBasePath.empty()) {
      std::vector<GrowthPressureReportRow> baselineRows;
      std::string baselineError;
      if (!loadGrowthPressureReportCSV(rankGrowthPressureBasePath, baselineRows, baselineError)) {
        std::cerr << "Error: Failed to load baseline pressure report ('" << rankGrowthPressureBasePath
                  << "'): " << baselineError << "\n";
        return 1;
      }

      std::vector<std::pair<std::string, std::vector<GrowthPressureReportRow>>> candidates;
      candidates.reserve(rankGrowthPressureCandidatePaths.size());

      for (const std::string& candidatePath : rankGrowthPressureCandidatePaths) {
        std::vector<GrowthPressureReportRow> candidateRows;
        std::string candidateError;
        if (!loadGrowthPressureReportCSV(candidatePath, candidateRows, candidateError)) {
          std::cerr << "Error: Failed to load candidate pressure report ('" << candidatePath
                    << "'): " << candidateError << "\n";
          return 1;
        }
        candidates.emplace_back(candidatePath, std::move(candidateRows));
      }

      printGrowthPressureRanking(rankGrowthPressureBasePath, baselineRows, candidates);
      return 0;
    }

    if (!compareGrowthPressurePathA.empty()) {
      std::vector<GrowthPressureReportRow> rowsA;
      std::vector<GrowthPressureReportRow> rowsB;
      std::string errorA;
      std::string errorB;

      if (!loadGrowthPressureReportCSV(compareGrowthPressurePathA, rowsA, errorA)) {
        std::cerr << "Error: Failed to load pressure report A ('" << compareGrowthPressurePathA
                  << "'): " << errorA << "\n";
        return 1;
      }
      if (!loadGrowthPressureReportCSV(compareGrowthPressurePathB, rowsB, errorB)) {
        std::cerr << "Error: Failed to load pressure report B ('" << compareGrowthPressurePathB
                  << "'): " << errorB << "\n";
        return 1;
      }

      printGrowthPressureComparison(
        compareGrowthPressurePathA,
        rowsA,
        compareGrowthPressurePathB,
        rowsB
      );
      return 0;
    }

    if (!inspectSnapshotPath.empty()) {
      CitySnapshot inspectedSnapshot;
      SnapshotLoadDiagnostics inspectDiagnostics;
      if (!SaveLoadSystem::loadSnapshotFromFile(inspectSnapshotPath, inspectedSnapshot, &inspectDiagnostics)) {
        std::cerr << "Error: Failed to inspect city snapshot from '" << inspectSnapshotPath << "'\n";
        return 1;
      }
      printSnapshotInspection(inspectedSnapshot, inspectDiagnostics);
      return 0;
    }

    if (benchmarkPhase5Ticks >= 0) {
      return runPhase5Benchmark(mapSize, seed, benchmarkPhase5Ticks, benchmarkPhase5Focus);
    }

    if (simulateTicks >= 0 || simulateInfinite) {
      const int gridSpacing = (gridSpacingOverride > 0) ? gridSpacingOverride : 4;
      return runCitySimulation(
        mapSize,
        seed,
        simulateInfinite ? -1 : simulateTicks,
        generateTerrainFlag,
        terrainWaterFraction,
        !simulateNoTraffic,
        simulateReportPath,
        gridSpacing,
        simulateSpeed,
        simulateTrafficInterval,
        simulateServiceInterval,
        simulatePopulationInterval,
        simulateLandValueInterval,
        simulateInflationRate,
        !simulateNoTransit,
        simulateDistrictRequests
      );
    }

    if (microTrafficGrowthTicks >= 0) {
      return runMicroTrafficDemo(
        mapSize,
        seed,
        microTrafficGrowthTicks,
        generateTerrainFlag,
        terrainWaterFraction,
        microTrafficSteps,
        microTrafficIncidents,
        microTrafficLanes,
        microTrafficFollowingGap
      );
    }

    CitySnapshot loadedSnapshot;
    SnapshotLoadDiagnostics snapshotDiagnostics;
    if (!loadCityPath.empty()) {
      if (!SaveLoadSystem::loadSnapshotFromFile(loadCityPath, loadedSnapshot, &snapshotDiagnostics)) {
        std::cerr << "Error: Failed to load city snapshot from '" << loadCityPath << "'\n";
        return 1;
      }
      std::cout << "Snapshot diagnostics: sourceVersion=" << snapshotDiagnostics.sourceVersion
                << ", targetVersion=" << snapshotDiagnostics.targetVersion
                << ", migrated=" << (snapshotDiagnostics.migrationApplied ? "yes" : "no")
                << ", path=" << snapshotDiagnostics.migrationPath
                << "\n";
      mapSize = loadedSnapshot.width;
    }

    if (verifyReplayGrowthSteps >= 0) {
      ReplayConfig config;
      config.mapSize = mapSize;
      config.seed = seed;
      config.growthSteps = verifyReplayGrowthSteps;
      config.seedPopulation = (seedPopulation >= 0) ? seedPopulation : 120;
      config.runCommutes = true;
      config.runEconomy = true;

      const ReplayResult replayResult = ReplayVerifier::verifyDeterministicRun(config);
      std::cout << "Replay Verification:\n";
      std::cout << "  First checksum:  " << replayResult.firstChecksum << "\n";
      std::cout << "  Second checksum: " << replayResult.secondChecksum << "\n";
      std::cout << "  Deterministic:   " << (replayResult.deterministic ? "Yes" : "No") << "\n";

      return replayResult.deterministic ? 0 : 1;
    }

    // Initialize city
    {
      const float sideKm = tileToKm(mapSize);
      const float areaKm2 = mapAreaKm2(mapSize, mapSize);
      const int blockMeters = ((gridSpacingOverride > 0) ? gridSpacingOverride : 4)
                              * static_cast<int>(kMetersPerTile);
      std::cout << "Initializing city (" << mapSize << "x" << mapSize
                << ", " << std::fixed << std::setprecision(1) << sideKm << " x " << sideKm
                << " km, " << std::setprecision(0) << areaKm2 << " km\xc2\xb2"
                << ", " << blockMeters << " m blocks)...\n";
    }
    CityMap map({mapSize, mapSize});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    DistrictSystem districtSystem;
    PopulationSummary populationSummary;
    TrafficSummary trafficSummary;
    EconomyState economyState;
    ServiceCoverageSummary serviceSummary;
    bool hasPopulationSummary = false;
    bool hasTrafficSummary = false;
    bool hasEconomyState = false;
    bool hasServiceSummary = false;

    std::vector<ServiceFacility> serviceFacilities;
    serviceFacilities.reserve(serviceRequests.size());

    auto saveIfRequested = [&]() -> bool {
      if (saveCityPath.empty()) {
        return true;
      }
      if (!SaveLoadSystem::saveToFile(saveCityPath, map, roads, store, population)) {
        std::cerr << "Error: Failed to save city snapshot to '" << saveCityPath << "'\n";
        return false;
      }
      std::cout << "City snapshot saved to " << saveCityPath << "\n";
      return true;
    };

    if (!loadCityPath.empty()) {
      if (!SaveLoadSystem::applySnapshot(loadedSnapshot, map, roads, store, population)) {
        std::cerr << "Error: Loaded snapshot dimensions do not match current city map\n";
        return 1;
      }
      populationSummary = buildPopulationSummaryFromState(store, population);
      hasPopulationSummary = true;
      std::cout << "Loaded city snapshot from " << loadCityPath << "\n";
    } else if (generateTerrainFlag) {
      // Generate terrain on a fresh map only; a loaded snapshot already carries its own.
      TerrainParams terrainParams;
      terrainParams.waterFraction = terrainWaterFraction;
      const TerrainStats terrainStats = TerrainGenerator::generate(map, seed, terrainParams);
      std::cout << "Generated terrain (seed " << seed << "): "
                << terrainStats.waterTiles << " water, "
                << terrainStats.terrainTiles << " terrain, "
                << terrainStats.buildableTiles << " buildable tiles\n";
    }

    for (const auto& [typeRaw, x, y, dist] : serviceRequests) {
      ServiceType type;
      if (!ServiceSystem::parseServiceType(typeRaw, type)) {
        std::cerr << "Error: Unknown service type '" << typeRaw
                  << "'. Use FIRE, POLICE, HEALTH/HOSPITAL, EDUCATION/SCHOOL.\n";
        return 1;
      }
      if (!map.isValid({x, y})) {
        std::cerr << "Error: Service location (" << x << "," << y << ") is out of bounds\n";
        return 1;
      }
      ServiceFacility facility;
      facility.type = type;
      facility.position = {x, y};
      facility.maxTravelDistance = std::max(0, dist);
      serviceFacilities.push_back(facility);
      std::cout << "Added service facility #" << serviceFacilities.size()
                << " (" << ServiceSystem::serviceTypeToString(type)
                << ") at (" << x << "," << y << ")"
                << " distance=" << facility.maxTravelDistance << "\n";
    }

    // Handle inspection commands
    if (printMapFlag) {
      printMap(map);
      if (!saveIfRequested()) return 1;
      return 0;
    }

    if (printTileX >= 0 && printTileY >= 0) {
      printTile(map, printTileX, printTileY);
      if (!saveIfRequested()) return 1;
      return 0;
    }

    // Apply district mutations before growth so growth-pressure uses current district state.
    if (districtRequests.any()) {
      if (!applyDistrictMutations(districtSystem, districtRequests)) {
        return 1;
      }
    }

    if (!zoneRequests.empty()) {
      for (const auto& [x1, y1, x2, y2, zoneTypeRaw] : zoneRequests) {
        ZoneType zoneType = ZoneType::None;
        if (!Zoning::parseZoneType(zoneTypeRaw, zoneType)) {
          std::cerr << "Error: Unknown zone type '" << zoneTypeRaw
                    << "'. Use NONE, RESIDENTIAL, COMMERCIAL, INDUSTRIAL, or PARK.\n";
          return 1;
        }

        int zonedCount = 0;
        if (!Zoning::applyZoneRect(map, {x1, y1}, {x2, y2}, zoneType, &zonedCount)) {
          std::cerr << "Error: Zone rectangle is out of bounds\n";
          return 1;
        }

        std::cout << "Applied zone " << Zoning::zoneToString(static_cast<int>(zoneType))
                  << " to " << zonedCount << " tiles.\n";
      }
    }

    if (!placeRoadRequests.empty()) {
      for (const auto& [x1, y1, x2, y2] : placeRoadRequests) {
        if (x1 == x2 && y1 == y2) {
          continue;
        }

        if (x1 == x2 || y1 == y2) {
          const int dx = (x2 > x1) ? 1 : (x2 < x1 ? -1 : 0);
          const int dy = (y2 > y1) ? 1 : (y2 < y1 ? -1 : 0);

          int cx = x1;
          int cy = y1;
          while (cx != x2 || cy != y2) {
            const int nx = cx + dx;
            const int ny = cy + dy;
            roads.buildRoad({cx, cy}, {nx, ny});
            cx = nx;
            cy = ny;
          }
        } else {
          roads.buildRoad({x1, y1}, {x2, y2});
        }

        roads.updateConnectivity({x1, y1});
        std::cout << "Road built from (" << x1 << "," << y1
                  << ") to (" << x2 << "," << y2 << ")\n";
      }
      std::cout << "Total roads: " << roads.getRoadCount() << "\n";
    }

    if (!runPolicySweepOutputDir.empty()) {
      PolicySweepOptions sweepOptions;
      sweepOptions.outputDir = runPolicySweepOutputDir;
      sweepOptions.districtId = sweepDistrictId;
      sweepOptions.seeds = sweepSeeds;
      sweepOptions.caps = sweepCaps;
      sweepOptions.allocations = sweepAllocations;
      sweepOptions.manifestAllDistricts = sweepManifestAllDistricts;
      sweepOptions.growthSteps = runGrowthSteps;
      sweepOptions.baseSeed = seed;
      sweepOptions.pressurePool = districtPressurePool;

      return runPolicySweep(
        std::move(sweepOptions),
        districtSystem,
        map,
        roads,
        store,
        population,
        serviceFacilities
      );
    }

    if (runGrowthSteps > 0) {
      std::vector<GrowthPressureReportRow> growthPressureRows;
      std::vector<GrowthPressureReportRow>* outRows = nullptr;
      if (!exportGrowthPressurePath.empty()) {
        outRows = &growthPressureRows;
      }

      runGrowthStepsWithPressure(
        districtSystem,
        map,
        roads,
        store,
        population,
        serviceFacilities,
        runGrowthSteps,
        seed,
        districtPressurePool,
        printGrowthPressureFlag,
        true,
        outRows
      );

      if (!exportGrowthPressurePath.empty()) {
        if (!writeGrowthPressureReportCSV(exportGrowthPressurePath, growthPressureRows)) {
          std::cerr << "Error: Failed to export growth pressure report to '"
                    << exportGrowthPressurePath << "'\n";
          return 1;
        }
        std::cout << "Exported growth pressure report to " << exportGrowthPressurePath
                  << " (rows=" << growthPressureRows.size() << ")\n";
      }
    }

    if (printDemandFlag) {
      printDemand(seed);
    }

    if (printConnectivityMapFlag) {
      roads.updateConnectivity({0, 0}); // Check connectivity from top-left
      printConnectivityMap(map, roads);
    }

    if (printZonesFlag) {
      printZones(map);
    }

    if (printBuildingsFlag) {
      printBuildings(store);
    }

    if (printGrowthSummaryFlag) {
      const GrowthMetrics growthMetrics = GrowthMetrics::collect(map, store);
      std::cout << growthMetrics.toString();
    }

    if (seedPopulation >= 0) {
      populationSummary = PopulationSystem::allocate(
        store,
        population,
        static_cast<uint32_t>(seedPopulation),
        seed
      );
      hasPopulationSummary = true;
    }

    if (printPopulationSummaryFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-population-summary requires --seed-population N\n";
        return 1;
      }
      printPopulationSummary(populationSummary);
    }

    if (printPopulationGroupsFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-population-groups requires --seed-population N\n";
        return 1;
      }
      printPopulationGroups(population);
    }

    if (runCommuteSimulationFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --run-commute-simulation requires --seed-population N\n";
        return 1;
      }
      trafficSummary = TrafficSystem::simulateCommutes(
        store, population, roads, seed
      );
      hasTrafficSummary = true;
      std::cout << "Commute simulation completed.\n";
    }

    if (printTrafficSummaryFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-traffic-summary requires --run-commute-simulation\n";
        return 1;
      }
      if (!hasTrafficSummary) {
        trafficSummary = TrafficSystem::simulateCommutes(
          store, population, roads, seed
        );
        hasTrafficSummary = true;
      }
      printTrafficSummary(trafficSummary);
    }

    if (printTopEdgesCount > 0) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-top-edges requires --seed-population N\n";
        return 1;
      }
      RouteDiagnosticsFilter routeFilter;
      if (hasTrafficOriginFilter) {
        if (!map.isValid({trafficOriginX, trafficOriginY})) {
          std::cerr << "Error: --traffic-origin coordinate is out of bounds\n";
          return 1;
        }
        routeFilter.hasOrigin = true;
        routeFilter.origin = {trafficOriginX, trafficOriginY};
      }
      if (hasTrafficDestinationFilter) {
        if (!map.isValid({trafficDestinationX, trafficDestinationY})) {
          std::cerr << "Error: --traffic-destination coordinate is out of bounds\n";
          return 1;
        }
        routeFilter.hasDestination = true;
        routeFilter.destination = {trafficDestinationX, trafficDestinationY};
      }

      printRouteDiagnosticsFilter(routeFilter);

      std::vector<EdgeTrafficData> topEdges;
      if (routeFilter.hasOrigin || routeFilter.hasDestination) {
        topEdges = TrafficSystem::getTopRouteDiagnosticEdges(
          store,
          population,
          roads,
          routeFilter,
          static_cast<size_t>(printTopEdgesCount),
          seed
        );
      } else {
        if (!hasTrafficSummary) {
          trafficSummary = TrafficSystem::simulateCommutes(
            store, population, roads, seed
          );
          hasTrafficSummary = true;
        }
        topEdges = TrafficSystem::getTopCongestedEdges(roads, printTopEdgesCount);
      }

      printTopCongestedEdges(topEdges);
    }

    if (runEconomyCalculationFlag) {
      economyState = EconomySystem::calculateEconomy(store, population);
      hasEconomyState = true;
      std::cout << "Economy calculation completed.\n";
    }

    if (printBudgetSummaryFlag) {
      if (!hasEconomyState) {
        economyState = EconomySystem::calculateEconomy(store, population);
        hasEconomyState = true;
      }
      printBudgetSummary(economyState);
    }

    if (runServiceEvaluationFlag) {
      serviceSummary = ServiceSystem::evaluateCoverage(store, roads, serviceFacilities);
      hasServiceSummary = true;
      std::cout << "Service evaluation completed.\n";
    }

    if (printServiceSummaryFlag) {
      if (!hasServiceSummary) {
        serviceSummary = ServiceSystem::evaluateCoverage(store, roads, serviceFacilities);
        hasServiceSummary = true;
      }
      printServiceSummary(serviceSummary);
    }

    if (printCitySummaryFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-city-summary requires --seed-population N\n";
        return 1;
      }
      if (!hasTrafficSummary) {
        trafficSummary = TrafficSystem::simulateCommutes(
          store, population, roads, seed
        );
        hasTrafficSummary = true;
      }
      if (!hasEconomyState) {
        economyState = EconomySystem::calculateEconomy(store, population);
        hasEconomyState = true;
      }
      if (!hasServiceSummary && !serviceFacilities.empty()) {
        serviceSummary = ServiceSystem::evaluateCoverage(store, roads, serviceFacilities);
        hasServiceSummary = true;
      }

      CityMetrics summaryMetrics = MetricsSystem::collectCityMetrics(
        populationSummary,
        trafficSummary,
        economyState,
        hasServiceSummary ? &serviceSummary : nullptr
      );
      std::cout << MetricsSystem::createCitySummaryReport(summaryMetrics);
    }

    // District reporting commands
    if (listDistrictsFlag) {
      printDistrictList(districtSystem);
    }

    if (printDistrictSummaryId >= 0) {
      if (!printDistrictSummaryReport(
            districtSystem,
            static_cast<DistrictId>(printDistrictSummaryId),
            map, store, population, roads, serviceFacilities)) {
        return 1;
      }
    }

    if (printDistrictBalancingPool >= 0) {
      printDistrictBalancing(
        districtSystem,
        printDistrictBalancingPool,
        map, store, population, roads, serviceFacilities
      );
    }

    if (printDistrictFacilitiesId >= 0) {
      if (!printDistrictFacilities(districtSystem, static_cast<DistrictId>(printDistrictFacilitiesId))) {
        return 1;
      }
    }

    if (!renderMapPath.empty()) {
      RenderOptions renderOptions;
      renderOptions.tilePixels = std::max(1, renderScale);
      if (hasRenderView) {
        renderOptions.viewX = renderViewX;
        renderOptions.viewY = renderViewY;
        renderOptions.viewWidth = renderViewW;
        renderOptions.viewHeight = renderViewH;
      }

      if (!MapRenderer::renderToPPM(renderMapPath, map, store, renderOptions)) {
        std::cerr << "Error: Failed to render map to '" << renderMapPath << "'\n";
        return 1;
      }
      std::cout << "Rendered map image to " << renderMapPath << "\n";
    }

    if (findPathX1 >= 0) {
      Pathfinding::Path path = Pathfinding::findShortestPath(
        roads, {findPathX1, findPathY1}, {findPathX2, findPathY2}
      );
      printPath(path);
      if (!saveIfRequested()) return 1;
      return 0;
    }

    if (!zoneRequests.empty() || generateTerrainFlag || !placeRoadRequests.empty() || runGrowthSteps > 0 || printZonesFlag ||
        printDemandFlag || printConnectivityMapFlag || printBuildingsFlag ||
        printGrowthSummaryFlag || seedPopulation >= 0 || printPopulationSummaryFlag ||
        printPopulationGroupsFlag || runCommuteSimulationFlag || printTrafficSummaryFlag ||
        printTopEdgesCount > 0 || runEconomyCalculationFlag || printBudgetSummaryFlag ||
        runServiceEvaluationFlag || printServiceSummaryFlag || !serviceRequests.empty() ||
        printCitySummaryFlag || !renderMapPath.empty() || !saveCityPath.empty() || !loadCityPath.empty() ||
        listDistrictsFlag || printDistrictSummaryId >= 0 ||
        printDistrictFacilitiesId >= 0 ||
        printDistrictBalancingPool >= 0 ||
        districtRequests.any()) {
      if (!saveIfRequested()) return 1;
      return 0;
    }

    // Normal simulation
    SimulationTime time;
    time.ticksPerDay = 24;
    time.ticksPerMonth = 720;

    CityMetrics metrics;

    if (numTicks > 0) {
      std::cout << "Running " << numTicks << " ticks with seed " << seed << "...\n\n";

      // Simulation loop
      for (int tick = 0; tick < numTicks; ++tick) {
        // Print status at intervals
        if (tick % 24 == 0) {
          std::cout << "Tick " << tick
                    << ": Time " << time.getCurrentHour() << "h "
                    << "(Day " << time.getCurrentDay() << ", Month " << time.getCurrentMonth() << ")\n";
        }

        // Advance time
        time.advance();
      }
    }

    std::cout << "\nSimulation complete.\n";
    std::cout << metrics.toString();

    if (!saveIfRequested()) {
      return 1;
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
