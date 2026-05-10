#include <iostream>
#include <string>
#include <cstdlib>
#include <iomanip>
#include "src/core/SimulationTime.hpp"
#include "src/world/CityMap.hpp"
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
    std::cerr << "Error: Coordinates (" << x << ", " << y << ") out of bounds\n";
    return;
  }
  
  const Tile& tile = map.getTile({x, y});
  
  std::cout << "Tile at (" << x << ", " << y << "):\n"
            << "  Type: ";
  switch (tile.type) {
    case 0: std::cout << "Empty\n"; break;
    case 1: std::cout << "Terrain\n"; break;
    case 2: std::cout << "Water\n"; break;
    default: std::cout << "Unknown\n"; break;
  }
  
  std::cout << "  Zone: ";
  switch (tile.zone) {
    case 0: std::cout << "None\n"; break;
    case 1: std::cout << "Residential\n"; break;
    case 2: std::cout << "Commercial\n"; break;
    case 3: std::cout << "Industrial\n"; break;
    default: std::cout << "Unknown\n"; break;
  }
  
  std::cout << std::fixed << std::setprecision(2)
            << "  Land Value: $" << tile.landValue << "\n"
            << "  Pollution: " << tile.pollution << "%\n"
            << "  Has Road: " << (tile.hasRoad ? "Yes" : "No") << "\n"
            << "  Connected to Road: " << (tile.connectedToRoad ? "Yes" : "No") << "\n"
            << "  Connected to Power: " << (tile.connectedToPower ? "Yes" : "No") << "\n"
            << "  Connected to Water: " << (tile.connectedToWater ? "Yes" : "No") << "\n"
            << "  Building ID: " << tile.buildingId << "\n";
}

int main(int argc, char* argv[]) {
  // Parse arguments
  int mapSize = 64;
  int numTicks = 100;
  uint32_t seed = 42;
  bool printMapFlag = false;
  int printTileX = -1, printTileY = -1;
  
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
    
    // Handle inspection commands
    if (printMapFlag) {
      printMap(map);
      return 0;
    }
    
    if (printTileX >= 0 && printTileY >= 0) {
      printTile(map, printTileX, printTileY);
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
