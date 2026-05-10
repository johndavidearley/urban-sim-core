#include <iostream>
#include <string>
#include <cstdlib>
#include <iomanip>
#include "src/core/SimulationTime.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/systems/GrowthSystem.hpp"
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
            << "  --print-buildings        Print all spawned buildings\n"
            << "  --place-road X1 Y1 X2 Y2  Build a road segment between tiles\n"
            << "  --connectivity-map       Print connectivity status and exit\n"
            << "  --find-path X1 Y1 X2 Y2  Find shortest path from (X1,Y1) to (X2,Y2)\n"
            << "  --help                   Show this help message\n";
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
  bool printBuildingsFlag = false;
  bool printConnectivityMapFlag = false;
  int zoneX1 = -1, zoneY1 = -1, zoneX2 = -1, zoneY2 = -1;
  std::string zoneTypeRaw;
  int runGrowthSteps = 0;
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
    // Initialize city
    std::cout << "Initializing city (" << mapSize << "x" << mapSize << ")...\n";
    CityMap map({mapSize, mapSize});
    RoadNetwork roads(map);
    EntityStore store;
    
    // Handle inspection commands
    if (printMapFlag) {
      printMap(map);
      return 0;
    }
    
    if (printTileX >= 0 && printTileY >= 0) {
      printTile(map, printTileX, printTileY);
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

    if (findPathX1 >= 0) {
      Pathfinding::Path path = Pathfinding::findShortestPath(
        roads, {findPathX1, findPathY1}, {findPathX2, findPathY2}
      );
      printPath(path);
      return 0;
    }

    if (zoneX1 >= 0 || placeRoadX1 >= 0 || runGrowthSteps > 0 || printZonesFlag ||
        printDemandFlag || printConnectivityMapFlag || printBuildingsFlag ||
        printGrowthSummaryFlag) {
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
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
