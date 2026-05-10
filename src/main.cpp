#include <iostream>
#include <string>
#include <cstdlib>
#include <iomanip>
#include <algorithm>
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
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/persistence/ReplayVerifier.hpp"
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
            << "  --run-economy-calculation Run economy/tax calculation\n"
            << "  --print-budget-summary    Print revenue/expense/economic health summary\n"
            << "  --print-city-summary      Print consolidated city metrics summary\n"
            << "  --save-city FILE          Save city snapshot JSON to FILE\n"
            << "  --load-city FILE          Load city snapshot JSON from FILE\n"
            << "  --verify-replay N         Run deterministic replay check using N growth steps\n"
            << "  --help                   Show this help message\n";
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
  bool printCitySummaryFlag = false;
  int verifyReplayGrowthSteps = -1;
  std::string saveCityPath;
  std::string loadCityPath;
  int printTopEdgesCount = -1;
  int zoneX1 = -1, zoneY1 = -1, zoneX2 = -1, zoneY2 = -1;
  std::string zoneTypeRaw;
  int runGrowthSteps = 0;
  int seedPopulation = -1;
  int placeRoadX1 = -1, placeRoadY1 = -1, placeRoadX2 = -1, placeRoadY2 = -1;
  int findPathX1 = -1, findPathY1 = -1, findPathX2 = -1, findPathY2 = -1;
  
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
    } else if (arg == "--run-economy-calculation") {
      runEconomyCalculationFlag = true;
    } else if (arg == "--print-budget-summary") {
      printBudgetSummaryFlag = true;
    } else if (arg == "--print-city-summary") {
      printCitySummaryFlag = true;
    } else if (arg == "--save-city" && i + 1 < argc) {
      saveCityPath = argv[++i];
    } else if (arg == "--load-city" && i + 1 < argc) {
      loadCityPath = argv[++i];
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
  
  try {
    CitySnapshot loadedSnapshot;
    if (!loadCityPath.empty()) {
      if (!SaveLoadSystem::loadSnapshotFromFile(loadCityPath, loadedSnapshot)) {
        std::cerr << "Error: Failed to load city snapshot from '" << loadCityPath << "'\n";
        return 1;
      }
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
    bool hasPopulationSummary = false;
    bool hasTrafficSummary = false;
    bool hasEconomyState = false;

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
      if (!hasTrafficSummary) {
        trafficSummary = TrafficSystem::simulateCommutes(
          store, population, roads, seed
        );
        hasTrafficSummary = true;
      }
      auto topEdges = TrafficSystem::getTopCongestedEdges(roads, printTopEdgesCount);
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

      CityMetrics summaryMetrics = MetricsSystem::collectCityMetrics(
        populationSummary, trafficSummary, economyState
      );
      std::cout << MetricsSystem::createCitySummaryReport(summaryMetrics);
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
        printCitySummaryFlag || !saveCityPath.empty() || !loadCityPath.empty()) {
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
