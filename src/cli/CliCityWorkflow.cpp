#include "src/cli/CliCityWorkflow.hpp"

#include "src/cli/CliOptions.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
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
#include "src/systems/CitySimulator.hpp"
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
#include "src/gameplay/RoadTool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
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


namespace {

// True when the user asked only for a bulk autonomous sim (same engine as
// --simulate), not an inspect/mutate city session.
bool isPureAutonomousSimulation(const CliOptions& o) {
  if (o.numTicks <= 0) return false;
  if (!o.loadCityPath.empty() || o.verifyReplayGrowthSteps >= 0) return false;
  if (o.printMapFlag || o.printTileX >= 0 || o.printZonesFlag || o.printDemandFlag) return false;
  if (o.printGrowthSummaryFlag || o.printPopulationSummaryFlag || o.printPopulationGroupsFlag) return false;
  if (o.printBuildingsFlag || o.printConnectivityMapFlag) return false;
  if (o.runCommuteSimulationFlag || o.printTrafficSummaryFlag || o.printTopEdgesCount > 0) return false;
  if (o.runEconomyCalculationFlag || o.printBudgetSummaryFlag) return false;
  if (o.runServiceEvaluationFlag || o.printServiceSummaryFlag || o.printCitySummaryFlag) return false;
  if (!o.renderMapPath.empty() || !o.saveCityPath.empty()) return false;
  if (!o.zoneRequests.empty() || !o.placeRoadRequests.empty() || o.runGrowthSteps > 0) return false;
  if (!o.serviceRequests.empty() || !o.powerSourceRequests.empty()) return false;
  if (o.listDistrictsFlag || o.printDistrictSummaryId >= 0 || o.printDistrictFacilitiesId >= 0) return false;
  if (o.printDistrictBalancingPool >= 0 || o.districtRequests.any()) return false;
  if (!o.runPolicySweepOutputDir.empty() || !o.commuteSweepOutputDir.empty()) return false;
  if (o.districtPressurePool >= 0 || o.printGrowthPressureFlag) return false;
  if (!o.exportGrowthPressurePath.empty()) return false;
  if (o.seedPopulation >= 0 || o.findPathX1 >= 0) return false;
  // generateTerrain / terrain water / grid spacing / seed / size / ticks are OK
  return true;
}

} // namespace

int runCityCliWorkflow(CliOptions opts) {
    // Default --ticks N path: same full RCI engine as --simulate (not an empty clock loop).
    if (isPureAutonomousSimulation(opts)) {
      const int gridSpacing = (opts.gridSpacingOverride > 0) ? opts.gridSpacingOverride : 4;
      return runCitySimulation(
        opts.mapSize,
        opts.seed,
        opts.numTicks,
        opts.generateTerrainFlag,
        opts.terrainWaterFraction,
        /*runTraffic=*/true,
        /*reportPath=*/"",
        gridSpacing,
        /*ticksPerSecond=*/0.0,
        /*trafficInterval=*/1,
        /*serviceInterval=*/1,
        /*populationInterval=*/1,
        /*landValueInterval=*/1,
        /*inflationRate=*/0.0f,
        /*enableTransit=*/true,
        /*districtRequests=*/{},
        /*districtArchetypeRequests=*/{},
        /*enableDisasters=*/false,
        /*fireRiskMultiplier=*/1.0f,
        /*earthquakeRiskMultiplier=*/1.0f,
        /*floodRiskMultiplier=*/1.0f,
        /*enableUtilities=*/false
      );
    }

    CitySnapshot loadedSnapshot;
    SnapshotLoadDiagnostics snapshotDiagnostics;
    if (!opts.loadCityPath.empty()) {
      if (!SaveLoadSystem::loadSnapshotFromFile(opts.loadCityPath, loadedSnapshot, &snapshotDiagnostics)) {
        std::cerr << "Error: Failed to load city snapshot from '" << opts.loadCityPath
                  << "': " << snapshotDiagnostics.errorMessage << "\n";
        return 1;
      }
      std::cout << "Snapshot diagnostics: sourceVersion=" << snapshotDiagnostics.sourceVersion
                << ", targetVersion=" << snapshotDiagnostics.targetVersion
                << ", migrated=" << (snapshotDiagnostics.migrationApplied ? "yes" : "no")
                << ", path=" << snapshotDiagnostics.migrationPath
                << "\n";
      opts.mapSize = loadedSnapshot.width;
    }

    if (opts.verifyReplayGrowthSteps >= 0) {
      ReplayConfig config;
      config.mapSize = opts.mapSize;
      config.seed = opts.seed;
      config.growthSteps = opts.verifyReplayGrowthSteps;
      config.seedPopulation = (opts.seedPopulation >= 0) ? opts.seedPopulation : 120;
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
      const float sideKm = tileToKm(opts.mapSize);
      const float areaKm2 = mapAreaKm2(opts.mapSize, opts.mapSize);
      const int blockMeters = ((opts.gridSpacingOverride > 0) ? opts.gridSpacingOverride : 4)
                              * static_cast<int>(kMetersPerTile);
      std::cout << "Initializing city (" << opts.mapSize << "x" << opts.mapSize
                << ", " << std::fixed << std::setprecision(1) << sideKm << " x " << sideKm
                << " km, " << std::setprecision(0) << areaKm2 << " km\xc2\xb2"
                << ", " << blockMeters << " m blocks)...\n";
    }
    CityMap map({opts.mapSize, opts.mapSize});
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
    serviceFacilities.reserve(opts.serviceRequests.size() + opts.powerSourceRequests.size());

    auto saveIfRequested = [&]() -> bool {
      if (opts.saveCityPath.empty()) {
        return true;
      }
      if (!SaveLoadSystem::saveToFile(opts.saveCityPath, map, roads, store, population)) {
        std::cerr << "Error: Failed to save city snapshot to '" << opts.saveCityPath << "'\n";
        return false;
      }
      std::cout << "City snapshot saved to " << opts.saveCityPath << "\n";
      return true;
    };

    if (!opts.loadCityPath.empty()) {
      std::string applyError;
      if (!SaveLoadSystem::applySnapshot(loadedSnapshot, map, roads, store, population, &applyError)) {
        std::cerr << "Error: Failed to apply loaded snapshot: " << applyError << "\n";
        return 1;
      }
      populationSummary = buildPopulationSummaryFromState(store, population);
      hasPopulationSummary = true;
      std::cout << "Loaded city snapshot from " << opts.loadCityPath << "\n";
    } else if (opts.generateTerrainFlag) {
      // Generate terrain on a fresh map only; a loaded snapshot already carries its own.
      TerrainParams terrainParams;
      terrainParams.waterFraction = opts.terrainWaterFraction;
      const TerrainStats terrainStats = TerrainGenerator::generate(map, opts.seed, terrainParams);
      std::cout << "Generated terrain (seed " << opts.seed << "): "
                << terrainStats.waterTiles << " water, "
                << terrainStats.terrainTiles << " terrain, "
                << terrainStats.buildableTiles << " buildable tiles\n";
    }

    // CLI inspect/setup has no treasury. Tools still run so placement rules
    // match the visualizer; cost is computed then waived.
    int64_t sandboxFunds = std::numeric_limits<int64_t>::max();

    for (const auto& [typeRaw, x, y, dist] : opts.serviceRequests) {
      ServiceType type;
      if (!ServiceSystem::parseServiceType(typeRaw, type)) {
        std::cerr << "Error: Unknown service type '" << typeRaw
                  << "'. Use FIRE, POLICE, HEALTH/HOSPITAL, EDUCATION/SCHOOL, "
                     "POWER, WATER, or SANITATION/WASTE.\n";
        return 1;
      }
      // Coverage fixtures for --run-service-evaluation: skip the player road
      // gate so DIST/MW experiments can sit on an empty inspect map.
      ServicePlacementOptions placement;
      placement.requireRoadAccess = false;
      placement.coverageDistanceOverride = std::max(0, dist);
      const ServicePlan plan = ServiceTool::plan(
        map, roads, serviceFacilities, type, {x, y}, sandboxFunds, placement);
      if (!plan.valid || !ServiceTool::build(map, roads, serviceFacilities, plan, sandboxFunds)) {
        std::cerr << "Error: Cannot add service at (" << x << "," << y << "): "
                  << (plan.error.empty() ? "placement failed" : plan.error) << "\n";
        return 1;
      }
      std::cout << "Added service facility #" << serviceFacilities.size()
                << " (" << ServiceSystem::serviceTypeToString(type)
                << ") at (" << x << "," << y << ")"
                << " distance=" << serviceFacilities.back().maxTravelDistance << "\n";
    }

    for (const auto& [sourceRaw, x, y, dist, capacityMW] : opts.powerSourceRequests) {
      PowerSourceType sourceType;
      if (!ServiceSystem::parsePowerSourceType(sourceRaw, sourceType)) {
        std::cerr << "Error: Unknown power source '" << sourceRaw
                  << "'. Use GENERIC, COAL, GAS, NUCLEAR, SOLAR, WIND, or HYDRO.\n";
        return 1;
      }
      if (capacityMW < 0.0f) {
        std::cerr << "Error: Invalid power source location or negative capacity\n";
        return 1;
      }
      ServicePlacementOptions placement;
      placement.requireRoadAccess = false;
      placement.coverageDistanceOverride = std::max(0, dist);
      const ServicePlan plan = ServiceTool::plan(
        map, roads, serviceFacilities, ServiceType::Power, {x, y}, sandboxFunds, placement);
      if (!plan.valid || !ServiceTool::build(map, roads, serviceFacilities, plan, sandboxFunds)) {
        std::cerr << "Error: Cannot add power source at (" << x << "," << y << "): "
                  << (plan.error.empty() ? "placement failed" : plan.error) << "\n";
        return 1;
      }
      ServiceFacility& facility = serviceFacilities.back();
      facility.powerSource = sourceType;
      facility.powerCapacityMW = capacityMW;
      facility.emissionsKgPerMWh = ServiceSystem::defaultPowerEmissions(sourceType);
      std::cout << "Added " << ServiceSystem::powerSourceTypeToString(sourceType)
                << " power source #" << serviceFacilities.size() << " at ("
                << x << "," << y << "), capacity=" << capacityMW << " MW\n";
    }

    // Handle inspection commands
    if (opts.printMapFlag) {
      printMap(map);
      if (!saveIfRequested()) return 1;
      return 0;
    }

    if (opts.printTileX >= 0 && opts.printTileY >= 0) {
      printTile(map, opts.printTileX, opts.printTileY);
      if (!saveIfRequested()) return 1;
      return 0;
    }

    // Apply district mutations before growth so growth-pressure uses current district state.
    if (opts.districtRequests.any()) {
      if (!applyDistrictMutations(districtSystem, opts.districtRequests)) {
        return 1;
      }
    }

    if (!opts.zoneRequests.empty()) {
      for (const auto& [x1, y1, x2, y2, zoneTypeRaw] : opts.zoneRequests) {
        ZoneType zoneType = ZoneType::None;
        if (!Zoning::parseZoneType(zoneTypeRaw, zoneType)) {
          std::cerr << "Error: Unknown zone type '" << zoneTypeRaw
                    << "'. Use NONE, RESIDENTIAL, COMMERCIAL, INDUSTRIAL, PARK, or OFFICE.\n";
          return 1;
        }

        // PARK/NONE are inspect-only types the player ZoneTool does not paint.
        if (zoneType == ZoneType::None || zoneType == ZoneType::Park) {
          int zonedCount = 0;
          if (!Zoning::applyZoneRect(map, {x1, y1}, {x2, y2}, zoneType, &zonedCount)) {
            std::cerr << "Error: Zone rectangle is out of bounds\n";
            return 1;
          }
          std::cout << "Applied zone " << Zoning::zoneToString(static_cast<int>(zoneType))
                    << " to " << zonedCount << " tiles.\n";
          continue;
        }

        const ZonePlan plan = ZoneTool::plan(map, {x1, y1}, {x2, y2}, zoneType, sandboxFunds);
        if (!plan.valid && plan.error == "area already has this zone") {
          std::cout << "Applied zone " << Zoning::zoneToString(static_cast<int>(zoneType))
                    << " to 0 tiles.\n";
          continue;
        }
        if (!plan.valid || !ZoneTool::apply(map, plan, sandboxFunds)) {
          std::cerr << "Error: Cannot zone (" << x1 << "," << y1 << ")-("
                    << x2 << "," << y2 << "): "
                    << (plan.error.empty() ? "apply failed" : plan.error) << "\n";
          return 1;
        }
        std::cout << "Applied zone " << Zoning::zoneToString(static_cast<int>(zoneType))
                  << " to " << plan.changedTiles << " tiles.\n";
      }
    }

    if (!opts.placeRoadRequests.empty()) {
      for (const auto& [x1, y1, x2, y2] : opts.placeRoadRequests) {
        if (x1 == x2 && y1 == y2) {
          continue;
        }

        const RoadPlan plan = RoadTool::plan(map, roads, {x1, y1}, {x2, y2}, sandboxFunds);
        if (!plan.valid || !RoadTool::build(map, roads, plan, sandboxFunds)) {
          std::cerr << "Error: Cannot place road (" << x1 << "," << y1 << ")-("
                    << x2 << "," << y2 << "): "
                    << (plan.error.empty() ? "build failed" : plan.error) << "\n";
          return 1;
        }
        std::cout << "Road built from (" << x1 << "," << y1
                  << ") to (" << x2 << "," << y2 << ") ("
                  << plan.newSegments << " new segments)\n";
      }
      std::cout << "Total roads: " << roads.getRoadCount() << "\n";
    }

    if (!opts.runPolicySweepOutputDir.empty()) {
      PolicySweepOptions sweepOptions;
      sweepOptions.outputDir = opts.runPolicySweepOutputDir;
      sweepOptions.districtId = opts.sweepDistrictId;
      sweepOptions.seeds = opts.sweepSeeds;
      sweepOptions.caps = opts.sweepCaps;
      sweepOptions.allocations = opts.sweepAllocations;
      sweepOptions.manifestAllDistricts = opts.sweepManifestAllDistricts;
      sweepOptions.growthSteps = opts.runGrowthSteps;
      sweepOptions.baseSeed = opts.seed;
      sweepOptions.pressurePool = opts.districtPressurePool;

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

    if (!opts.commuteSweepOutputDir.empty()) {
      CommutePolicySweepOptions commuteSweepOptions;
      commuteSweepOptions.outputDir = opts.commuteSweepOutputDir;
      commuteSweepOptions.seeds = opts.commuteSweepSeeds;
      commuteSweepOptions.transitCapacityMultipliers = opts.commuteSweepMultipliers;
      commuteSweepOptions.includeTransitDisabledScenario = !opts.commuteSweepNoTransitDisabled;
      commuteSweepOptions.ticks = opts.commuteSweepTicks;

      return runCommutePolicySweep(
        std::move(commuteSweepOptions),
        map,
        roads,
        store,
        population
      );
    }

    if (opts.runGrowthSteps > 0) {
      std::vector<GrowthPressureReportRow> growthPressureRows;
      std::vector<GrowthPressureReportRow>* outRows = nullptr;
      if (!opts.exportGrowthPressurePath.empty()) {
        outRows = &growthPressureRows;
      }

      runGrowthStepsWithPressure(
        districtSystem,
        map,
        roads,
        store,
        population,
        serviceFacilities,
        opts.runGrowthSteps,
        opts.seed,
        opts.districtPressurePool,
        opts.printGrowthPressureFlag,
        true,
        outRows
      );

      if (!opts.exportGrowthPressurePath.empty()) {
        if (!writeGrowthPressureReportCSV(opts.exportGrowthPressurePath, growthPressureRows)) {
          std::cerr << "Error: Failed to export growth pressure report to '"
                    << opts.exportGrowthPressurePath << "'\n";
          return 1;
        }
        std::cout << "Exported growth pressure report to " << opts.exportGrowthPressurePath
                  << " (rows=" << growthPressureRows.size() << ")\n";
      }
    }

    if (opts.printDemandFlag) {
      printDemand(opts.seed);
    }

    if (opts.printConnectivityMapFlag) {
      roads.updateConnectivity({0, 0}); // Check connectivity from top-left
      printConnectivityMap(map, roads);
    }

    if (opts.printZonesFlag) {
      printZones(map);
    }

    if (opts.printBuildingsFlag) {
      printBuildings(store);
    }

    if (opts.printGrowthSummaryFlag) {
      const GrowthMetrics growthMetrics = GrowthMetrics::collect(map, store);
      std::cout << growthMetrics.toString();
    }

    if (opts.seedPopulation >= 0) {
      populationSummary = PopulationSystem::allocate(
        store,
        population,
        static_cast<uint32_t>(opts.seedPopulation),
        opts.seed
      );
      hasPopulationSummary = true;
    }

    if (opts.printPopulationSummaryFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-population-summary requires --seed-population N\n";
        return 1;
      }
      printPopulationSummary(populationSummary);
    }

    if (opts.printPopulationGroupsFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-population-groups requires --seed-population N\n";
        return 1;
      }
      printPopulationGroups(population);
    }

    if (opts.runCommuteSimulationFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --run-commute-simulation requires --seed-population N\n";
        return 1;
      }
      trafficSummary = TrafficSystem::simulateCommutes(
        store, population, roads, opts.seed
      );
      hasTrafficSummary = true;
      std::cout << "Commute simulation completed.\n";
    }

    if (opts.printTrafficSummaryFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-traffic-summary requires --run-commute-simulation\n";
        return 1;
      }
      if (!hasTrafficSummary) {
        trafficSummary = TrafficSystem::simulateCommutes(
          store, population, roads, opts.seed
        );
        hasTrafficSummary = true;
      }
      printTrafficSummary(trafficSummary);
    }

    if (opts.printTopEdgesCount > 0) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-top-edges requires --seed-population N\n";
        return 1;
      }
      RouteDiagnosticsFilter routeFilter;
      if (opts.hasTrafficOriginFilter) {
        if (!map.isValid({opts.trafficOriginX, opts.trafficOriginY})) {
          std::cerr << "Error: --traffic-origin coordinate is out of bounds\n";
          return 1;
        }
        routeFilter.hasOrigin = true;
        routeFilter.origin = {opts.trafficOriginX, opts.trafficOriginY};
      }
      if (opts.hasTrafficDestinationFilter) {
        if (!map.isValid({opts.trafficDestinationX, opts.trafficDestinationY})) {
          std::cerr << "Error: --traffic-destination coordinate is out of bounds\n";
          return 1;
        }
        routeFilter.hasDestination = true;
        routeFilter.destination = {opts.trafficDestinationX, opts.trafficDestinationY};
      }

      printRouteDiagnosticsFilter(routeFilter);

      std::vector<EdgeTrafficData> topEdges;
      if (routeFilter.hasOrigin || routeFilter.hasDestination) {
        topEdges = TrafficSystem::getTopRouteDiagnosticEdges(
          store,
          population,
          roads,
          routeFilter,
          static_cast<size_t>(opts.printTopEdgesCount),
          opts.seed
        );
      } else {
        if (!hasTrafficSummary) {
          trafficSummary = TrafficSystem::simulateCommutes(
            store, population, roads, opts.seed
          );
          hasTrafficSummary = true;
        }
        topEdges = TrafficSystem::getTopCongestedEdges(roads, opts.printTopEdgesCount);
      }

      printTopCongestedEdges(topEdges);

      if (!opts.topEdgesExportPath.empty()) {
        if (!writeTopEdgesCSV(opts.topEdgesExportPath, topEdges)) {
          std::cerr << "Error: Failed to write route diagnostics to '" << opts.topEdgesExportPath << "'\n";
          return 1;
        }
        std::cout << "Wrote route diagnostics to " << opts.topEdgesExportPath << "\n";
      }
    }

    if (opts.runEconomyCalculationFlag) {
      economyState = EconomySystem::calculateEconomy(store, population, TaxRates{}, &map);
      hasEconomyState = true;
      std::cout << "Economy calculation completed.\n";
    }

    if (opts.printBudgetSummaryFlag) {
      if (!hasEconomyState) {
        economyState = EconomySystem::calculateEconomy(store, population, TaxRates{}, &map);
        hasEconomyState = true;
      }
      printBudgetSummary(economyState);
    }

    if (opts.runServiceEvaluationFlag) {
      serviceSummary = ServiceSystem::evaluateCoverage(store, roads, serviceFacilities);
      hasServiceSummary = true;
      std::cout << "Service evaluation completed.\n";
    }

    if (opts.printServiceSummaryFlag) {
      if (!hasServiceSummary) {
        serviceSummary = ServiceSystem::evaluateCoverage(store, roads, serviceFacilities);
        hasServiceSummary = true;
      }
      printServiceSummary(serviceSummary);
    }

    if (opts.printCitySummaryFlag) {
      if (!hasPopulationSummary) {
        populationSummary = PopulationSystem::summarize(store, population);
        hasPopulationSummary = true;
      }
      if (!hasTrafficSummary) {
        trafficSummary = TrafficSystem::simulateCommutes(
          store, population, roads, opts.seed
        );
        hasTrafficSummary = true;
      }
      if (!hasEconomyState) {
        economyState = EconomySystem::calculateEconomy(store, population, TaxRates{}, &map);
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
    if (opts.listDistrictsFlag) {
      printDistrictList(districtSystem);
    }

    if (opts.printDistrictSummaryId >= 0) {
      if (!printDistrictSummaryReport(
            districtSystem,
            static_cast<DistrictId>(opts.printDistrictSummaryId),
            map, store, population, roads, serviceFacilities)) {
        return 1;
      }
    }

    if (opts.printDistrictBalancingPool >= 0) {
      printDistrictBalancing(
        districtSystem,
        opts.printDistrictBalancingPool,
        map, store, population, roads, serviceFacilities
      );
    }

    if (opts.printDistrictFacilitiesId >= 0) {
      if (!printDistrictFacilities(districtSystem, static_cast<DistrictId>(opts.printDistrictFacilitiesId))) {
        return 1;
      }
    }

    if (!opts.renderMapPath.empty()) {
      RenderOptions renderOptions;
      renderOptions.tilePixels = std::max(1, opts.renderScale);
      if (opts.hasRenderView) {
        renderOptions.viewX = opts.renderViewX;
        renderOptions.viewY = opts.renderViewY;
        renderOptions.viewWidth = opts.renderViewW;
        renderOptions.viewHeight = opts.renderViewH;
      }

      if (!MapRenderer::renderToPPM(opts.renderMapPath, map, store, renderOptions)) {
        std::cerr << "Error: Failed to render map to '" << opts.renderMapPath << "'\n";
        return 1;
      }
      std::cout << "Rendered map image to " << opts.renderMapPath << "\n";
    }

    if (opts.findPathX1 >= 0) {
      Pathfinding::Path path = Pathfinding::findShortestPath(
        roads, {opts.findPathX1, opts.findPathY1}, {opts.findPathX2, opts.findPathY2}
      );
      printPath(path);
      if (!saveIfRequested()) return 1;
      return 0;
    }

    if (!opts.zoneRequests.empty() || opts.generateTerrainFlag || !opts.placeRoadRequests.empty() || opts.runGrowthSteps > 0 || opts.printZonesFlag ||
        opts.printDemandFlag || opts.printConnectivityMapFlag || opts.printBuildingsFlag ||
        opts.printGrowthSummaryFlag || opts.seedPopulation >= 0 || opts.printPopulationSummaryFlag ||
        opts.printPopulationGroupsFlag || opts.runCommuteSimulationFlag || opts.printTrafficSummaryFlag ||
        opts.printTopEdgesCount > 0 || opts.runEconomyCalculationFlag || opts.printBudgetSummaryFlag ||
        opts.runServiceEvaluationFlag || opts.printServiceSummaryFlag || !opts.serviceRequests.empty() || !opts.powerSourceRequests.empty() ||
        opts.printCitySummaryFlag || !opts.renderMapPath.empty() || !opts.saveCityPath.empty() || !opts.loadCityPath.empty() ||
        opts.listDistrictsFlag || opts.printDistrictSummaryId >= 0 ||
        opts.printDistrictFacilitiesId >= 0 ||
        opts.printDistrictBalancingPool >= 0 ||
        opts.districtRequests.any()) {
      if (!saveIfRequested()) return 1;
      return 0;
    }

    // Default path: full autonomous RCI city (same engine as --simulate).
    // Previously this only advanced SimulationTime and printed empty metrics.
    if (opts.numTicks > 0) {
      // Prefer a clean autonomous run when the map was not customized by
      // session commands (load/zone/road/growth/etc. already returned above).
      // Reaching here means no inspection/mutation flags were set, so either
      // the city is still empty (fresh) or was only loaded for replay-adjacent
      // work. For a fresh map, runCitySimulation builds its own world.
      const bool mapIsPristine = opts.loadCityPath.empty()
        && opts.zoneRequests.empty()
        && opts.placeRoadRequests.empty()
        && opts.runGrowthSteps <= 0
        && opts.serviceRequests.empty()
        && opts.powerSourceRequests.empty()
        && !opts.districtRequests.any();

      if (mapIsPristine) {
        const int gridSpacing = (opts.gridSpacingOverride > 0) ? opts.gridSpacingOverride : 4;
        return runCitySimulation(
          opts.mapSize,
          opts.seed,
          opts.numTicks,
          opts.generateTerrainFlag,
          opts.terrainWaterFraction,
          /*runTraffic=*/true,
          /*reportPath=*/"",
          gridSpacing,
          /*ticksPerSecond=*/0.0,
          /*trafficInterval=*/1,
          /*serviceInterval=*/1,
          /*populationInterval=*/1,
          /*landValueInterval=*/1,
          /*inflationRate=*/0.0f,
          /*enableTransit=*/true,
          /*districtRequests=*/{},
          /*districtArchetypeRequests=*/{},
          /*enableDisasters=*/false,
          /*fireRiskMultiplier=*/1.0f,
          /*earthquakeRiskMultiplier=*/1.0f,
          /*floodRiskMultiplier=*/1.0f,
          /*enableUtilities=*/false
        );
      }

      // Loaded / partially built session: continue with CitySimulator on this map.
      SimOptions simOptions;
      simOptions.runTraffic = true;
      simOptions.gridSpacing = (opts.gridSpacingOverride > 0) ? opts.gridSpacingOverride : 4;
      simOptions.enableTransit = true;
      std::cout << "Running full city simulation for " << opts.numTicks
                << " ticks with seed " << opts.seed << " on existing session...\n";
      const SimResult result = CitySimulator::run(
        map, roads, store, population, opts.seed, opts.numTicks, simOptions, &districtSystem
      );
      if (!result.rows.empty()) {
        const SimTickMetrics& last = result.rows.back();
        std::cout << "\nSimulation complete.\n";
        std::cout << "  population=" << last.population
                  << " employed=" << last.employed
                  << " buildings=" << (last.residentialBuildings + last.commercialBuildings +
                                       last.industrialBuildings + last.officeBuildings)
                  << " roads=" << last.roadTiles
                  << " balance=" << last.budgetBalance
                  << " congestion=" << std::fixed << std::setprecision(2) << last.trafficCongestion
                  << "\n";
        std::cout << "\n" << MetricsSystem::createCitySummaryReport(result.finalMetrics);
      } else {
        std::cout << "\nSimulation complete (no metrics rows).\n";
      }
    } else {
      std::cout << "\nNo ticks requested; city session ready.\n";
    }

    if (!saveIfRequested()) {
      return 1;
    }

    return 0;
}

