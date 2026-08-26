#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "src/cli/CliUtils.hpp"
#include "src/cli/CommutePolicySweep.hpp"
#include "src/cli/DistrictCommands.hpp"
#include "src/cli/Simulate.hpp"

// Aggregated CLI flags/options for UrbanSimCore-cli. Parsed by parseCliArgs
// and consumed by runCliApp.
struct CliOptions {
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
  int simulateBenchmarkTrials = 0;  // 0 = off; >=2 dispatches to runCitySimulationBenchmark instead
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
  std::vector<SimulateDistrictArchetypeRequest> simulateDistrictArchetypeRequests;  // name, archetype
  bool simulateDisasters = false;
  bool simulateUtilities = false;
  float simulateFireRisk = 1.0f;
  float simulateEarthquakeRisk = 1.0f;
  float simulateFloodRisk = 1.0f;
  int microTrafficGrowthTicks = -1;
  int microTrafficSteps = -1;
  int microTrafficIncidents = 0;
  int microTrafficLanes = -1;
  float microTrafficFollowingGap = -1.0f;
  float microTrafficVehicleLength = -1.0f;
  int benchmarkPhase5Ticks = -1;
  BenchmarkFocus benchmarkPhase5Focus = BenchmarkFocus::All;
  std::string saveCityPath;
  std::string loadCityPath;
  std::string inspectSnapshotPath;
  std::string auditSnapshotsDir;
  std::string commuteSweepOutputDir;
  std::vector<uint32_t> commuteSweepSeeds;
  std::vector<float> commuteSweepMultipliers;
  int commuteSweepTicks = 0;
  bool commuteSweepNoTransitDisabled = false;
  std::vector<std::tuple<std::string, int, int, int>> serviceRequests;
  std::vector<std::tuple<std::string, int, int, int, float>> powerSourceRequests;
  int printTopEdgesCount = -1;
  std::string topEdgesExportPath;
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
};
