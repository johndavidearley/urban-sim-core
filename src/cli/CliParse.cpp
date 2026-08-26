#include "src/cli/CliParse.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#include "src/cli/CityPrinters.hpp"
#include "src/cli/CliUtils.hpp"

std::optional<int> parseCliArgs(int argc, char* argv[], CliOptions& opts) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help") {
      printHelp();
      return 0;
    } else if (arg == "--size" && i + 1 < argc) {
      opts.mapSize = std::atoi(argv[++i]);
    } else if (arg == "--grid-spacing" && i + 1 < argc) {
      opts.gridSpacingOverride = std::atoi(argv[++i]);
    } else if (arg == "--ticks" && i + 1 < argc) {
      opts.numTicks = std::atoi(argv[++i]);
    } else if (arg == "--seed" && i + 1 < argc) {
      opts.seed = std::atoi(argv[++i]);
    } else if (arg == "--print-map") {
      opts.printMapFlag = true;
    } else if (arg == "--print-tile" && i + 2 < argc) {
      opts.printTileX = std::atoi(argv[++i]);
      opts.printTileY = std::atoi(argv[++i]);
    } else if (arg == "--zone-rect" && i + 5 < argc) {
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      std::string type = argv[++i];
      opts.zoneRequests.emplace_back(x1, y1, x2, y2, type);
    } else if (arg == "--generate-terrain") {
      opts.generateTerrainFlag = true;
    } else if (arg == "--terrain-water" && i + 1 < argc) {
      opts.terrainWaterFraction = std::stof(argv[++i]);
      opts.generateTerrainFlag = true;
    } else if (arg == "--print-zones") {
      opts.printZonesFlag = true;
    } else if (arg == "--print-demand") {
      opts.printDemandFlag = true;
    } else if (arg == "--print-growth-summary") {
      opts.printGrowthSummaryFlag = true;
    } else if (arg == "--seed-population" && i + 1 < argc) {
      opts.seedPopulation = std::atoi(argv[++i]);
    } else if (arg == "--print-population-summary") {
      opts.printPopulationSummaryFlag = true;
    } else if (arg == "--print-population-groups") {
      opts.printPopulationGroupsFlag = true;
    } else if (arg == "--run-growth" && i + 1 < argc) {
      opts.runGrowthSteps = std::atoi(argv[++i]);
    } else if (arg == "--district-pressure-pool" && i + 1 < argc) {
      opts.districtPressurePool = std::strtoll(argv[++i], nullptr, 10);
    } else if (arg == "--print-growth-pressure") {
      opts.printGrowthPressureFlag = true;
    } else if (arg == "--export-growth-pressure" && i + 1 < argc) {
      opts.exportGrowthPressurePath = argv[++i];
    } else if (arg == "--compare-growth-pressure" && i + 2 < argc) {
      opts.compareGrowthPressurePathA = argv[++i];
      opts.compareGrowthPressurePathB = argv[++i];
    } else if (arg == "--rank-growth-pressure" && i + 2 < argc) {
      const std::string basePath = argv[++i];
      const std::string candidatePath = argv[++i];
      if (opts.rankGrowthPressureBasePath.empty()) {
        opts.rankGrowthPressureBasePath = basePath;
      } else if (opts.rankGrowthPressureBasePath != basePath) {
        std::cerr << "Error: --rank-growth-pressure must use the same BASE for all entries\n";
        return 1;
      }
      opts.rankGrowthPressureCandidatePaths.push_back(candidatePath);
    } else if (arg == "--run-policy-sweep" && i + 1 < argc) {
      opts.runPolicySweepOutputDir = argv[++i];
    } else if (arg == "--sweep-district" && i + 1 < argc) {
      opts.sweepDistrictId = std::atoi(argv[++i]);
    } else if (arg == "--sweep-seeds" && i + 1 < argc) {
      if (!parseUint32List(argv[++i], opts.sweepSeeds)) {
        std::cerr << "Error: --sweep-seeds expects a comma-separated list of integers\n";
        return 1;
      }
    } else if (arg == "--sweep-caps" && i + 1 < argc) {
      if (!parseInt64List(argv[++i], opts.sweepCaps)) {
        std::cerr << "Error: --sweep-caps expects a comma-separated list of integers\n";
        return 1;
      }
    } else if (arg == "--sweep-allocations" && i + 1 < argc) {
      if (!parseFloatList(argv[++i], opts.sweepAllocations)) {
        std::cerr << "Error: --sweep-allocations expects a comma-separated list of decimal values\n";
        return 1;
      }
    } else if (arg == "--sweep-manifest-all-districts") {
      opts.sweepManifestAllDistricts = true;
    } else if (arg == "--print-buildings") {
      opts.printBuildingsFlag = true;
    } else if (arg == "--connectivity-map") {
      opts.printConnectivityMapFlag = true;
    } else if (arg == "--place-road" && i + 4 < argc) {
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      opts.placeRoadRequests.emplace_back(x1, y1, x2, y2);
    } else if (arg == "--find-path" && i + 4 < argc) {
      opts.findPathX1 = std::atoi(argv[++i]);
      opts.findPathY1 = std::atoi(argv[++i]);
      opts.findPathX2 = std::atoi(argv[++i]);
      opts.findPathY2 = std::atoi(argv[++i]);
    } else if (arg == "--run-commute-simulation") {
      opts.runCommuteSimulationFlag = true;
    } else if (arg == "--print-traffic-summary") {
      opts.printTrafficSummaryFlag = true;
    } else if (arg == "--print-top-edges" && i + 1 < argc) {
      opts.printTopEdgesCount = std::atoi(argv[++i]);
    } else if (arg == "--top-edges-export" && i + 1 < argc) {
      opts.topEdgesExportPath = argv[++i];
    } else if (arg == "--traffic-origin" && i + 2 < argc) {
      opts.trafficOriginX = std::atoi(argv[++i]);
      opts.trafficOriginY = std::atoi(argv[++i]);
      opts.hasTrafficOriginFilter = true;
    } else if (arg == "--traffic-destination" && i + 2 < argc) {
      opts.trafficDestinationX = std::atoi(argv[++i]);
      opts.trafficDestinationY = std::atoi(argv[++i]);
      opts.hasTrafficDestinationFilter = true;
    } else if (arg == "--create-district" && i + 5 < argc) {
      std::string name = argv[++i];
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      opts.districtRequests.create.emplace_back(name, x1, y1, x2, y2);
    } else if (arg == "--list-districts") {
      opts.listDistrictsFlag = true;
    } else if (arg == "--print-district-summary" && i + 1 < argc) {
      opts.printDistrictSummaryId = std::atoi(argv[++i]);
    } else if (arg == "--print-district-facilities" && i + 1 < argc) {
      opts.printDistrictFacilitiesId = std::atoi(argv[++i]);
    } else if (arg == "--print-district-balancing" && i + 1 < argc) {
      opts.printDistrictBalancingPool = std::strtoll(argv[++i], nullptr, 10);
    } else if (arg == "--set-district-tax" && i + 3 < argc) {
      int districtId = std::atoi(argv[++i]);
      std::string buildingType = argv[++i];
      float rate = std::stof(argv[++i]);
      opts.districtRequests.setTax.emplace_back(districtId, buildingType, rate);
    } else if (arg == "--set-district-service" && i + 5 < argc) {
      int districtId = std::atoi(argv[++i]);
      float fireWeight = std::stof(argv[++i]);
      float policeWeight = std::stof(argv[++i]);
      float healthWeight = std::stof(argv[++i]);
      float educationWeight = std::stof(argv[++i]);
      opts.districtRequests.setServicePriorities.emplace_back(districtId, fireWeight, policeWeight, healthWeight, educationWeight);
    } else if (arg == "--set-district-allocation" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      float allocation = std::stof(argv[++i]);
      opts.districtRequests.setAllocation.emplace_back(districtId, allocation);
    } else if (arg == "--set-district-budget-cap" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int64_t cap = std::strtoll(argv[++i], nullptr, 10);
      opts.districtRequests.setBudgetCap.emplace_back(districtId, cap);
    } else if (arg == "--assign-facility" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int facilityId = std::atoi(argv[++i]);
      opts.districtRequests.assignFacility.emplace_back(districtId, facilityId);
    } else if (arg == "--unassign-facility" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int facilityId = std::atoi(argv[++i]);
      opts.districtRequests.unassignFacility.emplace_back(districtId, facilityId);
    } else if (arg == "--run-economy-calculation") {
      opts.runEconomyCalculationFlag = true;
    } else if (arg == "--print-budget-summary") {
      opts.printBudgetSummaryFlag = true;
    } else if (arg == "--add-service" && i + 4 < argc) {
      std::string serviceType = argv[++i];
      int x = std::atoi(argv[++i]);
      int y = std::atoi(argv[++i]);
      int dist = std::atoi(argv[++i]);
      opts.serviceRequests.emplace_back(serviceType, x, y, dist);
    } else if (arg == "--add-power-source" && i + 5 < argc) {
      std::string sourceType = argv[++i];
      int x = std::atoi(argv[++i]);
      int y = std::atoi(argv[++i]);
      int dist = std::atoi(argv[++i]);
      float capacityMW = std::stof(argv[++i]);
      opts.powerSourceRequests.emplace_back(sourceType, x, y, dist, capacityMW);
    } else if (arg == "--run-service-evaluation") {
      opts.runServiceEvaluationFlag = true;
    } else if (arg == "--print-service-summary") {
      opts.printServiceSummaryFlag = true;
    } else if (arg == "--print-city-summary") {
      opts.printCitySummaryFlag = true;
    } else if (arg == "--render-map" && i + 1 < argc) {
      opts.renderMapPath = argv[++i];
    } else if (arg == "--render-scale" && i + 1 < argc) {
      opts.renderScale = std::atoi(argv[++i]);
    } else if (arg == "--render-view" && i + 4 < argc) {
      opts.renderViewX = std::atoi(argv[++i]);
      opts.renderViewY = std::atoi(argv[++i]);
      opts.renderViewW = std::atoi(argv[++i]);
      opts.renderViewH = std::atoi(argv[++i]);
      opts.hasRenderView = true;
    } else if (arg == "--save-city" && i + 1 < argc) {
      opts.saveCityPath = argv[++i];
    } else if (arg == "--load-city" && i + 1 < argc) {
      opts.loadCityPath = argv[++i];
    } else if (arg == "--inspect-snapshot" && i + 1 < argc) {
      opts.inspectSnapshotPath = argv[++i];
    } else if (arg == "--audit-snapshots" && i + 1 < argc) {
      opts.auditSnapshotsDir = argv[++i];
    } else if (arg == "--commute-sweep" && i + 1 < argc) {
      opts.commuteSweepOutputDir = argv[++i];
    } else if (arg == "--commute-sweep-seeds" && i + 1 < argc) {
      if (!parseUint32List(argv[++i], opts.commuteSweepSeeds)) {
        std::cerr << "Error: --commute-sweep-seeds expects a comma-separated list of integers\n";
        return 1;
      }
    } else if (arg == "--commute-sweep-transit-multipliers" && i + 1 < argc) {
      if (!parseFloatList(argv[++i], opts.commuteSweepMultipliers)) {
        std::cerr << "Error: --commute-sweep-transit-multipliers expects a comma-separated list of decimal values\n";
        return 1;
      }
    } else if (arg == "--commute-sweep-ticks" && i + 1 < argc) {
      opts.commuteSweepTicks = std::atoi(argv[++i]);
    } else if (arg == "--commute-sweep-no-transit-disabled") {
      opts.commuteSweepNoTransitDisabled = true;
    } else if (arg == "--benchmark-phase5" && i + 1 < argc) {
      opts.benchmarkPhase5Ticks = std::atoi(argv[++i]);
    } else if (arg == "--benchmark-phase5-focus" && i + 1 < argc) {
      if (!parseBenchmarkFocus(argv[++i], opts.benchmarkPhase5Focus)) {
        std::cerr << "Error: Unknown benchmark focus. Use ALL, GROWTH, POPULATION, TRAFFIC, ECONOMY, or SERVICE\n";
        return 1;
      }
    } else if (arg == "--verify-replay" && i + 1 < argc) {
      opts.verifyReplayGrowthSteps = std::atoi(argv[++i]);
    } else if (arg == "--simulate" && i + 1 < argc) {
      opts.simulateTicks = std::atoi(argv[++i]);
    } else if (arg == "--simulate-infinite") {
      opts.simulateInfinite = true;
    } else if (arg == "--simulate-benchmark-trials" && i + 1 < argc) {
      opts.simulateBenchmarkTrials = std::atoi(argv[++i]);
    } else if (arg == "--simulate-speed" && i + 1 < argc) {
      opts.simulateSpeed = std::stod(argv[++i]);
    } else if (arg == "--simulate-traffic-interval" && i + 1 < argc) {
      opts.simulateTrafficInterval = std::atoi(argv[++i]);
    } else if (arg == "--simulate-service-interval" && i + 1 < argc) {
      opts.simulateServiceInterval = std::atoi(argv[++i]);
    } else if (arg == "--simulate-population-interval" && i + 1 < argc) {
      opts.simulatePopulationInterval = std::atoi(argv[++i]);
    } else if (arg == "--simulate-land-value-interval" && i + 1 < argc) {
      opts.simulateLandValueInterval = std::atoi(argv[++i]);
    } else if (arg == "--simulate-inflation-rate" && i + 1 < argc) {
      opts.simulateInflationRate = std::stof(argv[++i]);
    } else if (arg == "--simulate-no-transit") {
      opts.simulateNoTransit = true;
    } else if (arg == "--simulate-district" && i + 5 < argc) {
      std::string name = argv[++i];
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      opts.simulateDistrictRequests.emplace_back(name, x1, y1, x2, y2);
    } else if (arg == "--simulate-district-archetype" && i + 2 < argc) {
      std::string name = argv[++i];
      std::string archetype = argv[++i];
      opts.simulateDistrictArchetypeRequests.emplace_back(name, archetype);
    } else if (arg == "--simulate-disasters") {
      opts.simulateDisasters = true;
    } else if (arg == "--simulate-utilities") {
      opts.simulateUtilities = true;
    } else if (arg == "--simulate-fire-risk" && i + 1 < argc) {
      opts.simulateFireRisk = std::stof(argv[++i]);
    } else if (arg == "--simulate-earthquake-risk" && i + 1 < argc) {
      opts.simulateEarthquakeRisk = std::stof(argv[++i]);
    } else if (arg == "--simulate-flood-risk" && i + 1 < argc) {
      opts.simulateFloodRisk = std::stof(argv[++i]);
    } else if (arg == "--simulate-report" && i + 1 < argc) {
      opts.simulateReportPath = argv[++i];
    } else if (arg == "--simulate-no-traffic") {
      opts.simulateNoTraffic = true;
    } else if (arg == "--micro-traffic" && i + 1 < argc) {
      opts.microTrafficGrowthTicks = std::atoi(argv[++i]);
    } else if (arg == "--micro-traffic-steps" && i + 1 < argc) {
      opts.microTrafficSteps = std::atoi(argv[++i]);
    } else if (arg == "--micro-traffic-incidents" && i + 1 < argc) {
      opts.microTrafficIncidents = std::atoi(argv[++i]);
    } else if (arg == "--micro-traffic-lanes" && i + 1 < argc) {
      opts.microTrafficLanes = std::atoi(argv[++i]);
    } else if (arg == "--micro-traffic-following-gap" && i + 1 < argc) {
      opts.microTrafficFollowingGap = std::stof(argv[++i]);
    } else if (arg == "--micro-traffic-vehicle-length" && i + 1 < argc) {
      opts.microTrafficVehicleLength = std::stof(argv[++i]);
    } else {
      std::cerr << "Error: Unknown argument '" << arg << "'\n";
      return 1;
    }
  }

  // Validate arguments
  if (opts.mapSize <= 0 || opts.mapSize > 512) {
    std::cerr << "Error: Map size must be between 1 and 512\n";
    return 1;
  }
  if (opts.numTicks < 0) {
    std::cerr << "Error: Number of ticks must be non-negative\n";
    return 1;
  }
  if (opts.benchmarkPhase5Ticks < -1) {
    std::cerr << "Error: benchmark ticks must be non-negative\n";
    return 1;
  }
  if (opts.districtPressurePool >= 0 && opts.runGrowthSteps <= 0) {
    std::cerr << "Error: --district-pressure-pool requires --run-growth N\n";
    return 1;
  }
  if (opts.printGrowthPressureFlag && opts.districtPressurePool < 0) {
    std::cerr << "Error: --print-growth-pressure requires --district-pressure-pool POOL\n";
    return 1;
  }
  if (!opts.exportGrowthPressurePath.empty() && opts.districtPressurePool < 0) {
    std::cerr << "Error: --export-growth-pressure requires --district-pressure-pool POOL\n";
    return 1;
  }
  if (!opts.exportGrowthPressurePath.empty() && opts.runGrowthSteps <= 0) {
    std::cerr << "Error: --export-growth-pressure requires --run-growth N\n";
    return 1;
  }
  if ((!opts.compareGrowthPressurePathA.empty() && opts.compareGrowthPressurePathB.empty()) ||
      (opts.compareGrowthPressurePathA.empty() && !opts.compareGrowthPressurePathB.empty())) {
    std::cerr << "Error: --compare-growth-pressure requires two file paths\n";
    return 1;
  }
  if (!opts.rankGrowthPressureBasePath.empty() && opts.rankGrowthPressureCandidatePaths.empty()) {
    std::cerr << "Error: --rank-growth-pressure requires at least one candidate\n";
    return 1;
  }
  if (!opts.runPolicySweepOutputDir.empty()) {
    if (opts.runGrowthSteps <= 0) {
      std::cerr << "Error: --run-policy-sweep requires --run-growth N\n";
      return 1;
    }
    if (opts.districtPressurePool < 0) {
      std::cerr << "Error: --run-policy-sweep requires --district-pressure-pool POOL\n";
      return 1;
    }
    if (opts.sweepDistrictId <= 0) {
      std::cerr << "Error: --run-policy-sweep requires --sweep-district DIST_ID\n";
      return 1;
    }
    if (opts.sweepSeeds.empty()) {
      std::cerr << "Error: --run-policy-sweep requires --sweep-seeds A,B,C\n";
      return 1;
    }
  }
  if ((opts.hasTrafficOriginFilter || opts.hasTrafficDestinationFilter) && opts.printTopEdgesCount <= 0) {
    std::cerr << "Error: --traffic-origin/--traffic-destination require --print-top-edges N\n";
    return 1;
  }
  if (!opts.commuteSweepOutputDir.empty()) {
    if (opts.commuteSweepTicks <= 0) {
      std::cerr << "Error: --commute-sweep requires --commute-sweep-ticks N\n";
      return 1;
    }
    if (opts.commuteSweepSeeds.empty()) {
      std::cerr << "Error: --commute-sweep requires --commute-sweep-seeds A,B,C\n";
      return 1;
    }
  }

  return std::nullopt;
}
