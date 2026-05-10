#include <iostream>
#include <string>
#include <cstdlib>
#include <iomanip>
#include "src/core/SimulationTime.hpp"
#include "src/world/CityMap.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/metrics/CityMetrics.hpp"

void printHelp() {
  std::cout << "UrbanSimCore CLI v0.1.0\n"
            << "Usage: UrbanSimCore-cli [options]\n"
            << "Options:\n"
            << "  --size SIZE              Map size (default: 64)\n"
            << "  --ticks N                Number of ticks to simulate (default: 100)\n"
            << "  --seed SEED              Random seed (default: 42)\n"
            << "  --print-map              Print ASCII map representation and exit\n"
            << "  --print-tile X Y         Print detailed info for tile at (X,Y)\n"
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
  std::cout << "  Zone: " << static_cast<int>(tile.zone) << "\n";
  std::cout << "  Type: " << static_cast<int>(tile.type) << "\n";
  std::cout << "  Land Value: " << tile.landValue << "\n";
  std::cout << "  Pollution: " << tile.pollution << "\n";
  std::cout << "  Has Road: " << (tile.hasRoad ? "Yes" : "No") << "\n";
  std::cout << "  Connected to Road: " << (tile.connectedToRoad ? "Yes" : "No") << "\n";
  if (tile.buildingId != 0) {
    std::cout << "  Building ID: " << tile.buildingId << "\n";
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
  bool printConnectivityMapFlag = false;
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
    
    // Handle inspection commands
    if (printMapFlag) {
      printMap(map);
      return 0;
    }
    
    if (printTileX >= 0 && printTileY >= 0) {
      printTile(map, printTileX, printTileY);
      return 0;
    }
    
    if (printConnectivityMapFlag) {
      roads.updateConnectivity({0, 0}); // Check connectivity from top-left
      printConnectivityMap(map, roads);
      return 0;
    }
    
    if (placeRoadX1 >= 0) {
      roads.buildRoad({placeRoadX1, placeRoadY1}, {placeRoadX2, placeRoadY2});
      roads.updateConnectivity({placeRoadX1, placeRoadY1});
      std::cout << "Road built from (" << placeRoadX1 << "," << placeRoadY1 
                << ") to (" << placeRoadX2 << "," << placeRoadY2 << ")\n";
      std::cout << "Total roads: " << roads.getRoadCount() << "\n";
      printConnectivityMap(map, roads);
      return 0;
    }
    
    if (findPathX1 >= 0) {
      Pathfinding::Path path = Pathfinding::findShortestPath(
        roads, {findPathX1, findPathY1}, {findPathX2, findPathY2}
      );
      printPath(path);
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
