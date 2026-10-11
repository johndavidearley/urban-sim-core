#include "src/playtest/PlaytestBridge.hpp"
#include "src/visualization/VisualizerApp.hpp"
#include <cstdlib>
#include <iostream>
int main(int argc, char *argv[]) {
  try {
    visualizer::PlaytestBridge bridge;
    for (int i = 1; i < argc; i += 2) {
      if (i + 1 >= argc)
        throw std::runtime_error("Playtest options require values");
      const std::string option = argv[i], value = argv[i + 1];
      if (option == "--time-mode") {
        if (value != "realtime" && value != "deterministic")
          throw std::runtime_error("Unknown time mode");
        bridge.deterministic = value == "deterministic";
      } else {
        size_t used = 0;
        const auto seed = std::stoull(value, &used);
        if (used != value.size() || seed > 4294967295ULL)
          throw std::runtime_error("Seed must be uint32");
        if (option == "--terrain-seed")
          bridge.terrainSeed = static_cast<uint32_t>(seed);
        else if (option == "--simulation-seed")
          bridge.simulationSeed = static_cast<uint32_t>(seed);
        else
          throw std::runtime_error("Unknown playtest option");
      }
    }
    return runVisualizer(argc, argv, &bridge);
  } catch (const std::exception &error) {
    std::cout << visualizer::Json{{"version", 1},
                                  {"id", nullptr},
                                  {"error",
                                   {{"code", "initialization_failed"},
                                    {"message", error.what()}}}}
                     .dump()
              << std::endl;
    std::cerr << "Playtest: " << error.what() << '\n';
    return 1;
  }
}
