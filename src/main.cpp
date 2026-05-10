#include <iostream>
#include <string>
#include <cstdlib>
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
            << "  --help                   Show this help message\n";
}

int main(int argc, char* argv[]) {
  // Parse arguments
  int mapSize = 64;
  int numTicks = 100;
  uint32_t seed = 42;
  
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
    }
  }
  
  // Validate arguments
  if (mapSize <= 0 || mapSize > 512) {
    std::cerr << "Error: Map size must be between 1 and 512\n";
    return 1;
  }
  if (numTicks <= 0) {
    std::cerr << "Error: Number of ticks must be positive\n";
    return 1;
  }
  
  try {
    // Initialize city
    std::cout << "Initializing city (" << mapSize << "x" << mapSize << ")...\n";
    CityMap map({mapSize, mapSize});
    
    SimulationTime time;
    time.ticksPerDay = 24;
    time.ticksPerMonth = 720;
    
    CityMetrics metrics;
    
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
    
    std::cout << "\nSimulation complete.\n";
    std::cout << metrics.toString();
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
