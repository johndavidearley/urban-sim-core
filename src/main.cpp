#include <iostream>
#include <string>
#include <cstdlib>
#include <iomanip>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include "src/core/SimulationTime.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/MetricsSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/persistence/ReplayVerifier.hpp"
#include "src/visualization/MapRenderer.hpp"
#include "src/metrics/CityMetrics.hpp"
#include "src/metrics/GrowthMetrics.hpp"

void printHelp() {
  std::cout << "UrbanSimCore CLI v0.1.0\n"
            << "Usage: UrbanSimCore-cli [options]\n"
            << "Options:\n"
            << "  --size SIZE              Map size (default: 64)\n"
            << "  --ticks N                Number of ticks to simulate (default: 100)\n"
            << "  --seed SEED              Random seed (default: 42)\n"
            << "  --print-map              Print ASCII map representation and exit\n"
            << "  --print-tile X Y         Print detailed info for tile at (X,Y)\n"
            << "  --zone-rect X1 Y1 X2 Y2 TYPE  Apply zoning to rectangle\n"
            << "  --print-zones            Print zoning map and exit\n"
            << "  --print-demand           Print zoning demand stub and exit\n"
            << "  --run-growth N           Run N growth steps and print summary\n"
            << "  --print-growth-summary   Print growth fill-rate summary\n"
            << "  --seed-population N      Allocate N residents to housing/jobs\n"
            << "  --print-population-summary  Print population/job summary\n"
            << "  --print-population-groups   Print grouped population composition\n"
            << "  --print-buildings        Print all spawned buildings\n"
            << "  --place-road X1 Y1 X2 Y2  Build a road segment between tiles\n"
            << "  --connectivity-map       Print connectivity status and exit\n"
            << "  --find-path X1 Y1 X2 Y2  Find shortest path from (X1,Y1) to (X2,Y2)\n"
            << "  --run-commute-simulation Run commute simulation for all employed\n"
            << "  --print-traffic-summary  Print traffic congestion and commute metrics\n"
            << "  --print-top-edges N      Print top N most congested edges\n"
            << "  --traffic-origin X Y     Filter route diagnostics by origin coordinate\n"
            << "  --traffic-destination X Y Filter route diagnostics by destination coordinate\n"
            << "  --run-economy-calculation Run economy/tax calculation\n"
            << "  --print-budget-summary    Print revenue/expense/economic health summary\n"
            << "  --add-service TYPE X Y DIST  Add service facility and max road distance\n"
            << "  --run-service-evaluation  Evaluate service coverage from facilities\n"
            << "  --print-service-summary   Print service coverage and satisfaction\n"
            << "  --print-city-summary      Print consolidated city metrics summary\n"
            << "  --create-district NAME X1 Y1 X2 Y2  Create a district (min to max corners)\n"
            << "  --list-districts         List all districts\n"
            << "  --print-district-summary DIST_ID  Print metrics for a district\n"
            << "  --render-map FILE         Render top-down city snapshot to PPM file\n"
            << "  --render-scale N          Pixel size per tile when rendering (default: 8)\n"
            << "  --render-view X Y W H     Render viewport rectangle in tiles\n"
            << "  --save-city FILE          Save city snapshot JSON to FILE\n"
            << "  --load-city FILE          Load city snapshot JSON from FILE (prints migration diagnostics)\n"
            << "  --inspect-snapshot FILE   Inspect snapshot schema and structural summary\n"
            << "  --benchmark-phase5 N      Run N-tick Phase 5 performance benchmark\n"
            << "  --benchmark-phase5-focus PHASE  Time only one phase: ALL|GROWTH|POPULATION|TRAFFIC|ECONOMY|SERVICE\n"
            << "  --verify-replay N         Run deterministic replay check using N growth steps\n"
            << "  --help                   Show this help message\n";
}

enum class BenchmarkFocus {
  All,
  Growth,
  Population,
  Traffic,
  Economy,
  Service
};

bool parseBenchmarkFocus(const std::string& raw, BenchmarkFocus& out) {
  std::string value = raw;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });

  if (value == "ALL") {
    out = BenchmarkFocus::All;
    return true;
  }
  if (value == "GROWTH") {
    out = BenchmarkFocus::Growth;
    return true;
  }
  if (value == "POPULATION") {
    out = BenchmarkFocus::Population;
    return true;
  }
  if (value == "TRAFFIC") {
    out = BenchmarkFocus::Traffic;
    return true;
  }
  if (value == "ECONOMY") {
    out = BenchmarkFocus::Economy;
    return true;
  }
  if (value == "SERVICE") {
    out = BenchmarkFocus::Service;
    return true;
  }
  return false;
}

const char* benchmarkFocusToString(BenchmarkFocus focus) {
  switch (focus) {
    case BenchmarkFocus::All:
      return "ALL";
    case BenchmarkFocus::Growth:
      return "GROWTH";
    case BenchmarkFocus::Population:
      return "POPULATION";
    case BenchmarkFocus::Traffic:
      return "TRAFFIC";
    case BenchmarkFocus::Economy:
      return "ECONOMY";
    case BenchmarkFocus::Service:
      return "SERVICE";
    default:
      return "ALL";
  }
}

void printServiceSummary(const ServiceCoverageSummary& summary) {
  std::cout << "Service Summary:\n";
  std::cout << "  Total Buildings: " << summary.totalBuildings << "\n";
  std::cout << "  Serviced Buildings: " << summary.servicedBuildings << "\n";
  std::cout << "  Fire Coverage: " << std::fixed << std::setprecision(1)
            << (summary.fireCoverage * 100.0f) << "%\n";
  std::cout << "  Police Coverage: " << std::fixed << std::setprecision(1)
            << (summary.policeCoverage * 100.0f) << "%\n";
  std::cout << "  Health Coverage: " << std::fixed << std::setprecision(1)
            << (summary.healthCoverage * 100.0f) << "%\n";
  std::cout << "  Education Coverage: " << std::fixed << std::setprecision(1)
            << (summary.educationCoverage * 100.0f) << "%\n";
  std::cout << "  Overall Coverage: " << std::fixed << std::setprecision(1)
            << (summary.overallCoverage * 100.0f) << "%\n";
  std::cout << "  Satisfaction: " << std::fixed << std::setprecision(1)
            << (summary.satisfaction * 100.0f) << "%\n";
}

PopulationSummary buildPopulationSummaryFromState(
  const EntityStore& store,
  const PopulationStore& population
) {
  PopulationSummary summary;
  summary.requestedPopulation = population.getTotalPopulation();
  summary.housedPopulation = summary.requestedPopulation;
  summary.employedPopulation = population.getTotalEmployed();
  summary.unemployedPopulation = summary.housedPopulation - summary.employedPopulation;

  uint32_t housingCapacity = 0;
  uint32_t jobCapacity = 0;
  uint32_t occupiedHousing = 0;
  uint32_t occupiedJobs = 0;

  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    const uint32_t cap = static_cast<uint32_t>(std::max(0, building.capacity));
    const uint32_t occ = static_cast<uint32_t>(std::max(0, building.occupancy));
    if (building.type == BuildingType::Residential) {
      housingCapacity += cap;
      occupiedHousing += std::min(cap, occ);
    } else {
      jobCapacity += cap;
      occupiedJobs += std::min(cap, occ);
    }
  }

  summary.availableHousing = (housingCapacity > occupiedHousing) ? (housingCapacity - occupiedHousing) : 0;
  summary.availableJobs = (jobCapacity > occupiedJobs) ? (jobCapacity - occupiedJobs) : 0;
  summary.unemploymentRate = summary.housedPopulation > 0
    ? (static_cast<float>(summary.unemployedPopulation) / static_cast<float>(summary.housedPopulation))
    : 0.0f;

  for (const auto& [id, group] : population.getGroups()) {
    (void)id;
    switch (group.band) {
      case IncomeBand::Low:
        summary.lowIncomePopulation += group.size;
        summary.lowIncomeEmployed += group.employed;
        break;
      case IncomeBand::Middle:
        summary.middleIncomePopulation += group.size;
        summary.middleIncomeEmployed += group.employed;
        break;
      case IncomeBand::High:
        summary.highIncomePopulation += group.size;
        summary.highIncomeEmployed += group.employed;
        break;
    }
  }

  return summary;
}

void printMap(const CityMap& map) {
  glm::ivec2 dims = map.getDimensions();
  
  std::cout << "Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = empty, # = has road, ~ = water, T = terrain\n\n";
  
  // Print column headers
  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";
  
  // Print rows
  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      char symbol = '.';
      if (tile.hasRoad) symbol = '#';
      else if (tile.type == 1) symbol = 'T';
      else if (tile.type == 2) symbol = '~';
      std::cout << symbol;
    }
    std::cout << "\n";
  }
}

void printTile(const CityMap& map, int x, int y) {
  if (!map.isValid({x, y})) {
    std::cout << "Error: Tile (" << x << "," << y << ") is out of bounds\n";
    return;
  }
  
  const Tile& tile = map.getTile({x, y});
  
  std::cout << "Tile (" << x << "," << y << "):\n";
  std::cout << "  Position: (" << tile.position.x << "," << tile.position.y << ")\n";
  std::cout << "  Zone: " << Zoning::zoneToString(tile.zone) << " (" << tile.zone << ")\n";
  std::cout << "  Type: " << static_cast<int>(tile.type) << "\n";
  std::cout << "  Land Value: " << tile.landValue << "\n";
  std::cout << "  Pollution: " << tile.pollution << "\n";
  std::cout << "  Has Road: " << (tile.hasRoad ? "Yes" : "No") << "\n";
  std::cout << "  Connected to Road: " << (tile.connectedToRoad ? "Yes" : "No") << "\n";
  if (tile.buildingId != 0) {
    std::cout << "  Building ID: " << tile.buildingId << "\n";
  }
}

void printZones(const CityMap& map) {
  glm::ivec2 dims = map.getDimensions();

  std::cout << "Zone Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = none, R = residential, C = commercial, I = industrial, P = park\n\n";

  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";

  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      std::cout << Zoning::zoneToSymbol(tile.zone);
    }
    std::cout << "\n";
  }
}

void printDemand(uint32_t seed) {
  ZoneDemand demand = Zoning::calculateDemand(seed);
  std::cout << "Zone demand (stub):\n";
  std::cout << "  Residential: " << std::fixed << std::setprecision(3) << demand.residential << "\n";
  std::cout << "  Commercial:  " << std::fixed << std::setprecision(3) << demand.commercial << "\n";
  std::cout << "  Industrial:  " << std::fixed << std::setprecision(3) << demand.industrial << "\n";
}

const char* buildingTypeToString(BuildingType type) {
  switch (type) {
    case BuildingType::Residential:
      return "Residential";
    case BuildingType::Commercial:
      return "Commercial";
    case BuildingType::Industrial:
      return "Industrial";
    default:
      return "Unknown";
  }
}

void printBuildings(const EntityStore& store) {
  const auto& buildings = store.getBuildings();
  std::cout << "Buildings: " << buildings.size() << "\n";

  for (const auto& [id, building] : buildings) {
    std::cout << "  #" << id << " " << buildingTypeToString(building.type)
              << " at (" << building.position.x << "," << building.position.y << ")"
              << " cap=" << building.capacity << " occ=" << building.occupancy << "\n";
  }
}

void printConnectivityMap(const CityMap& map, const RoadNetwork& network) {
  glm::ivec2 dims = map.getDimensions();
  
  std::cout << "Connectivity Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = disconnected, X = connected, # = road only\n\n";
  
  // Print column headers
  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";
  
  // Print rows
  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      char symbol = '.';
      if (network.isConnected({x, y})) {
        symbol = 'X';
      } else if (map.getTile({x, y}).hasRoad) {
        symbol = '#';
      }
      std::cout << symbol;
    }
    std::cout << "\n";
  }
}

void printPath(const Pathfinding::Path& path) {
  if (!path.found) {
    std::cout << "No path found between points.\n";
    return;
  }
  
  std::cout << "Path found (distance: " << std::fixed << std::setprecision(1) 
            << path.totalDistance << "):\n";
  for (size_t i = 0; i < path.waypoints.size(); ++i) {
    if (i > 0) std::cout << " -> ";
    std::cout << "(" << path.waypoints[i].x << "," << path.waypoints[i].y << ")";
  }
  std::cout << "\n";
}

const char* incomeBandToString(IncomeBand band) {
  switch (band) {
    case IncomeBand::Low:
      return "Low";
    case IncomeBand::Middle:
      return "Middle";
    case IncomeBand::High:
      return "High";
    default:
      return "Unknown";
  }
}

void printPopulationSummary(const PopulationSummary& summary) {
  std::cout << "Population Summary:\n";
  std::cout << "  Requested: " << summary.requestedPopulation << "\n";
  std::cout << "  Housed: " << summary.housedPopulation << "\n";
  std::cout << "  Employed: " << summary.employedPopulation << "\n";
  std::cout << "  Unemployed: " << summary.unemployedPopulation << "\n";
  std::cout << "  Available Housing: " << summary.availableHousing << "\n";
  std::cout << "  Available Jobs: " << summary.availableJobs << "\n";
  std::cout << "  Unemployment: " << std::fixed << std::setprecision(1)
            << (summary.unemploymentRate * 100.0f) << "%\n";
  std::cout << "  Composition (pop): Low=" << summary.lowIncomePopulation
            << ", Middle=" << summary.middleIncomePopulation
            << ", High=" << summary.highIncomePopulation << "\n";
  std::cout << "  Composition (employed): Low=" << summary.lowIncomeEmployed
            << ", Middle=" << summary.middleIncomeEmployed
            << ", High=" << summary.highIncomeEmployed << "\n";
}

void printPopulationGroups(const PopulationStore& population) {
  std::cout << "Population Groups: " << population.getGroupCount() << "\n";
  for (const auto& [id, group] : population.getGroups()) {
    std::cout << "  #" << id << " " << incomeBandToString(group.band)
              << " size=" << group.size
              << " employed=" << group.employed << "\n";
  }
}

void printTrafficSummary(const TrafficSummary& summary) {
  std::cout << "Traffic Summary:\n";
  std::cout << "  Commuting Population: " << summary.commutingPopulation << "\n";
  std::cout << "  Average Commute Time: " << std::fixed << std::setprecision(2)
            << summary.averageCommuteTime << "\n";
  std::cout << "  Total Commute Burden: " << summary.totalCommuteBurden << "\n";
  std::cout << "  Max Edge Congestion: " << std::fixed << std::setprecision(3)
            << (summary.maxEdgeCongestion * 100.0f) << "%\n";
  std::cout << "  Average Edge Congestion: " << std::fixed << std::setprecision(3)
            << (summary.averageEdgeCongestion * 100.0f) << "%\n";
  std::cout << "  Congested Edges: " << summary.congestionDetectedEdges << "\n";
}

void printTopCongestedEdges(const std::vector<EdgeTrafficData>& edges) {
  std::cout << "Top Congested Edges:\n";
  for (size_t i = 0; i < edges.size(); ++i) {
    const auto& edge = edges[i];
    std::cout << "  #" << (i + 1) << " (" << edge.from.x << "," << edge.from.y
              << ")->(" << edge.to.x << "," << edge.to.y << ")"
              << " congestion=" << std::fixed << std::setprecision(1)
              << (edge.congestion * 100.0f) << "%"
              << " commuters=" << edge.totalCommuters << "\n";
  }
}

void printRouteDiagnosticsFilter(const RouteDiagnosticsFilter& filter) {
  if (!filter.hasOrigin && !filter.hasDestination) {
    return;
  }

  std::cout << "Route Diagnostics Filter:";
  if (filter.hasOrigin) {
    std::cout << " origin=(" << filter.origin.x << "," << filter.origin.y << ")";
  }
  if (filter.hasDestination) {
    std::cout << " destination=(" << filter.destination.x << "," << filter.destination.y << ")";
  }
  std::cout << "\n";
}

void printBudgetSummary(const EconomyState& economy) {
  std::cout << "Budget Summary:\n";
  std::cout << "  Residential Tax Revenue: " << economy.residentialTaxRevenue << "\n";
  std::cout << "  Commercial Tax Revenue: " << economy.commercialTaxRevenue << "\n";
  std::cout << "  Industrial Tax Revenue: " << economy.industrialTaxRevenue << "\n";
  std::cout << "  Total Revenue: " << economy.totalRevenue << "\n";
  std::cout << "  Residential Maintenance: " << economy.residentialMaintenance << "\n";
  std::cout << "  Commercial Maintenance: " << economy.commercialMaintenance << "\n";
  std::cout << "  Industrial Maintenance: " << economy.industrialMaintenance << "\n";
  std::cout << "  Total Expenses: " << economy.totalExpenses << "\n";
  std::cout << "  Balance: " << economy.balance << "\n";
  std::cout << "  Average Land Value: " << std::fixed << std::setprecision(2)
            << economy.averageLandValue << "\n";
  std::cout << "  Economic Health: " << std::fixed << std::setprecision(1)
            << (economy.economicHealth * 100.0f) << "%\n";
}

void printSnapshotInspection(const CitySnapshot& snapshot, const SnapshotLoadDiagnostics& diagnostics) {
  size_t roadTileCount = 0;
  size_t zonedTileCount = 0;
  std::array<size_t, 5> zoneHistogram = {0, 0, 0, 0, 0};

  for (const SerializedTile& tile : snapshot.tiles) {
    if (tile.hasRoad) {
      ++roadTileCount;
    }
    if (tile.zone > 0 && tile.zone < static_cast<int>(zoneHistogram.size())) {
      ++zonedTileCount;
      ++zoneHistogram[static_cast<size_t>(tile.zone)];
    }
  }

  size_t residentialBuildings = 0;
  size_t commercialBuildings = 0;
  size_t industrialBuildings = 0;
  for (const Building& building : snapshot.buildings) {
    switch (building.type) {
      case BuildingType::Residential:
        ++residentialBuildings;
        break;
      case BuildingType::Commercial:
        ++commercialBuildings;
        break;
      case BuildingType::Industrial:
        ++industrialBuildings;
        break;
    }
  }

  std::cout << "Snapshot Inspection:\n";
  std::cout << "  Schema: sourceVersion=" << diagnostics.sourceVersion
            << ", targetVersion=" << diagnostics.targetVersion
            << ", migrated=" << (diagnostics.migrationApplied ? "yes" : "no")
            << ", path=" << diagnostics.migrationPath << "\n";
  std::cout << "  Map: " << snapshot.width << "x" << snapshot.height
            << " tiles=" << snapshot.tiles.size()
            << " roads=" << snapshot.roads.size()
            << " roadTiles=" << roadTileCount << "\n";
  std::cout << "  Zoning: zonedTiles=" << zonedTileCount
            << " (R=" << zoneHistogram[1]
            << " C=" << zoneHistogram[2]
            << " I=" << zoneHistogram[3]
            << " P=" << zoneHistogram[4] << ")\n";
  std::cout << "  Buildings: total=" << snapshot.buildings.size()
            << " (R=" << residentialBuildings
            << " C=" << commercialBuildings
            << " I=" << industrialBuildings << ")\n";
  std::cout << "  Population Groups: " << snapshot.populationGroups.size() << "\n";
}

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

int main(int argc, char* argv[]) {
  // Parse arguments
  int mapSize = 64;
  int numTicks = 100;
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
  int zoneX1 = -1, zoneY1 = -1, zoneX2 = -1, zoneY2 = -1;
  std::string zoneTypeRaw;
  int runGrowthSteps = 0;
  int seedPopulation = -1;
  int placeRoadX1 = -1, placeRoadY1 = -1, placeRoadX2 = -1, placeRoadY2 = -1;
  int findPathX1 = -1, findPathY1 = -1, findPathX2 = -1, findPathY2 = -1;
  bool listDistrictsFlag = false;
  int printDistrictSummaryId = -1;
  std::vector<std::tuple<std::string, int, int, int, int>> createDistrictRequests;  // name, x1, y1, x2, y2
  
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    
    if (arg == "--help") {
      printHelp();
      return 0;
    } else if (arg == "--size" && i + 1 < argc) {
      mapSize = std::atoi(argv[++i]);
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
      zoneX1 = std::atoi(argv[++i]);
      zoneY1 = std::atoi(argv[++i]);
      zoneX2 = std::atoi(argv[++i]);
      zoneY2 = std::atoi(argv[++i]);
      zoneTypeRaw = argv[++i];
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
    } else if (arg == "--print-buildings") {
      printBuildingsFlag = true;
    } else if (arg == "--connectivity-map") {
      printConnectivityMapFlag = true;
    } else if (arg == "--place-road" && i + 4 < argc) {
      placeRoadX1 = std::atoi(argv[++i]);
      placeRoadY1 = std::atoi(argv[++i]);
      placeRoadX2 = std::atoi(argv[++i]);
      placeRoadY2 = std::atoi(argv[++i]);
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
      createDistrictRequests.emplace_back(name, x1, y1, x2, y2);
    } else if (arg == "--list-districts") {
      listDistrictsFlag = true;
    } else if (arg == "--print-district-summary" && i + 1 < argc) {
      printDistrictSummaryId = std::atoi(argv[++i]);
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
    if ((hasTrafficOriginFilter || hasTrafficDestinationFilter) && printTopEdgesCount <= 0) {
      std::cerr << "Error: --traffic-origin/--traffic-destination require --print-top-edges N\n";
      return 1;
    }
  
  try {
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
      std::cout << "Running Phase 5 benchmark on " << mapSize << "x" << mapSize
                << " map for " << benchmarkPhase5Ticks << " ticks"
                << " (focus=" << benchmarkFocusToString(benchmarkPhase5Focus) << ")...\n";

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

      for (int tick = 0; tick < benchmarkPhase5Ticks; ++tick) {
        const uint32_t tickSeed = seed + static_cast<uint32_t>(tick * 13);
        const ZoneDemand demand = Zoning::calculateDemand(tickSeed);

        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Growth) {
          auto t0 = std::chrono::steady_clock::now();
          (void)GrowthSystem::runStep(benchmarkMap, benchmarkRoads, benchmarkStore, demand, tickSeed, 0.30f);
          auto t1 = std::chrono::steady_clock::now();
          growthMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)GrowthSystem::runStep(benchmarkMap, benchmarkRoads, benchmarkStore, demand, tickSeed, 0.30f);
        }

        const uint32_t requestedPopulation = static_cast<uint32_t>(std::max(1000, mapSize * mapSize / 2))
          + static_cast<uint32_t>((tick % 12) * 100);
        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Population) {
          auto t0 = std::chrono::steady_clock::now();
          (void)PopulationSystem::allocate(benchmarkStore, benchmarkPopulation, requestedPopulation, tickSeed + 1);
          auto t1 = std::chrono::steady_clock::now();
          populationMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)PopulationSystem::allocate(benchmarkStore, benchmarkPopulation, requestedPopulation, tickSeed + 1);
        }

        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Traffic) {
          auto t0 = std::chrono::steady_clock::now();
          (void)TrafficSystem::simulateCommutes(benchmarkStore, benchmarkPopulation, benchmarkRoads, tickSeed + 2);
          auto t1 = std::chrono::steady_clock::now();
          trafficMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)TrafficSystem::simulateCommutes(benchmarkStore, benchmarkPopulation, benchmarkRoads, tickSeed + 2);
        }

        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Economy) {
          auto t0 = std::chrono::steady_clock::now();
          (void)EconomySystem::calculateEconomy(benchmarkStore, benchmarkPopulation);
          auto t1 = std::chrono::steady_clock::now();
          economyMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)EconomySystem::calculateEconomy(benchmarkStore, benchmarkPopulation);
        }

        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Service) {
          auto t0 = std::chrono::steady_clock::now();
          (void)ServiceSystem::evaluateCoverage(benchmarkStore, benchmarkRoads, benchmarkFacilities);
          auto t1 = std::chrono::steady_clock::now();
          serviceMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)ServiceSystem::evaluateCoverage(benchmarkStore, benchmarkRoads, benchmarkFacilities);
        }
      }

      printBenchmarkResults(
        benchmarkPhase5Ticks,
        benchmarkPhase5Focus,
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
    std::cout << "Initializing city (" << mapSize << "x" << mapSize << ")...\n";
    CityMap map({mapSize, mapSize});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
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

    if (zoneX1 >= 0) {
      ZoneType zoneType = ZoneType::None;
      if (!Zoning::parseZoneType(zoneTypeRaw, zoneType)) {
        std::cerr << "Error: Unknown zone type '" << zoneTypeRaw
                  << "'. Use NONE, RESIDENTIAL, COMMERCIAL, INDUSTRIAL, or PARK.\n";
        return 1;
      }

      int zonedCount = 0;
      if (!Zoning::applyZoneRect(map, {zoneX1, zoneY1}, {zoneX2, zoneY2}, zoneType, &zonedCount)) {
        std::cerr << "Error: Zone rectangle is out of bounds\n";
        return 1;
      }

      std::cout << "Applied zone " << Zoning::zoneToString(static_cast<int>(zoneType))
                << " to " << zonedCount << " tiles.\n";
    }
    
    if (placeRoadX1 >= 0) {
      roads.buildRoad({placeRoadX1, placeRoadY1}, {placeRoadX2, placeRoadY2});
      roads.updateConnectivity({placeRoadX1, placeRoadY1});
      std::cout << "Road built from (" << placeRoadX1 << "," << placeRoadY1 
                << ") to (" << placeRoadX2 << "," << placeRoadY2 << ")\n";
      std::cout << "Total roads: " << roads.getRoadCount() << "\n";
    }
    
    if (runGrowthSteps > 0) {
      for (int step = 0; step < runGrowthSteps; ++step) {
        const ZoneDemand demand = Zoning::calculateDemand(seed + static_cast<uint32_t>(step));
        const GrowthStats stats = GrowthSystem::runStep(
          map, roads, store, demand, seed + static_cast<uint32_t>(step), 0.5f
        );

        std::cout << "Growth step " << (step + 1)
                  << ": evaluated=" << stats.evaluatedTiles
                  << " spawned=" << stats.totalSpawned()
                  << " (R=" << stats.spawnedResidential
                  << ", C=" << stats.spawnedCommercial
                  << ", I=" << stats.spawnedIndustrial << ")\n";
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

    // Handle district commands
    if (!createDistrictRequests.empty()) {
      for (const auto& [name, x1, y1, x2, y2] : createDistrictRequests) {
        DistrictId districtId = DistrictSystem::createDistrict(
          name,
          {x1, y1},
          {x2, y2}
        );
        if (districtId == 0) {
          std::cerr << "Error: Failed to create district '" << name << "' (invalid bounds?)\n";
          return 1;
        }
        std::cout << "Created district '" << name << "' with ID " << districtId << "\n";
        std::cout << "  Bounds: (" << x1 << "," << y1 << ") to (" << x2 << "," << y2 << ")\n";
      }
    }

    if (listDistrictsFlag) {
      const auto& districts = DistrictSystem::getDistricts();
      if (districts.empty()) {
        std::cout << "No districts created.\n";
      } else {
        std::cout << "Districts (" << districts.size() << "):\n";
        for (const auto& district : districts) {
          std::cout << "  [" << district.id << "] " << district.name << "\n";
          std::cout << "    Bounds: (" << district.minCorner.x << "," << district.minCorner.y
                    << ") to (" << district.maxCorner.x << "," << district.maxCorner.y << ")\n";
          std::cout << "    Area: " << district.area() << " tiles\n";
          std::cout << "    Tax Rates: Res=" << std::fixed << std::setprecision(2) 
                    << (district.taxRates.residentialRate * 100.0f) << "%  "
                    << "Com=" << (district.taxRates.commercialRate * 100.0f) << "%  "
                    << "Ind=" << (district.taxRates.industrialRate * 100.0f) << "%\n";
          std::cout << "    Service Allocation: " << (district.serviceAllocation * 100.0f) << "%\n";
        }
      }
    }

    if (printDistrictSummaryId >= 0) {
      DistrictMetrics metrics = DistrictSystem::evaluateDistrictMetrics(
        static_cast<DistrictId>(printDistrictSummaryId),
        map, store, population
      );
      
      if (metrics.districtId == 0) {
        std::cerr << "Error: District " << printDistrictSummaryId << " not found\n";
        return 1;
      }
      
      std::cout << "District Summary: " << metrics.districtName << "\n";
      std::cout << "  Population: " << metrics.population << "\n";
      std::cout << "  Buildings: " << metrics.buildings << " (Res: " << metrics.residentialBuildings
                << ", Com: " << metrics.commercialBuildings << ", Ind: " << metrics.industrialBuildings << ")\n";
      std::cout << "  Average Land Value: " << std::fixed << std::setprecision(1) << metrics.averageLandValue << "\n";
      std::cout << "  Budget: Revenue=" << metrics.revenue << ", Expenses=" << metrics.expenses
                << ", Balance=" << metrics.balance << "\n";
      std::cout << "  Service Coverage: " << std::fixed << std::setprecision(1) << (metrics.serviceCoverage * 100.0f) << "%\n";
      std::cout << "  Happiness: " << std::fixed << std::setprecision(1) << (metrics.happiness * 100.0f) << "%\n";
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

    if (zoneX1 >= 0 || placeRoadX1 >= 0 || runGrowthSteps > 0 || printZonesFlag ||
        printDemandFlag || printConnectivityMapFlag || printBuildingsFlag ||
        printGrowthSummaryFlag || seedPopulation >= 0 || printPopulationSummaryFlag ||
        printPopulationGroupsFlag || runCommuteSimulationFlag || printTrafficSummaryFlag ||
        printTopEdgesCount > 0 || runEconomyCalculationFlag || printBudgetSummaryFlag ||
        runServiceEvaluationFlag || printServiceSummaryFlag || !serviceRequests.empty() ||
        printCitySummaryFlag || !renderMapPath.empty() || !saveCityPath.empty() || !loadCityPath.empty() ||
        !createDistrictRequests.empty() || listDistrictsFlag || printDistrictSummaryId >= 0) {
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
