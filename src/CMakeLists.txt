# Core library
add_library(urban_sim_core
  core/EntityId.cpp
  core/SimulationTime.cpp
  core/Random.cpp
  entities/EntityStore.cpp
  entities/PopulationStore.cpp
  systems/GrowthSystem.cpp
  systems/PopulationSystem.cpp
  systems/TrafficSystem.cpp
  systems/EconomySystem.cpp
  systems/MetricsSystem.cpp
  systems/ServiceSystem.cpp
  systems/DistrictSystem.cpp
  systems/CitySimulator.cpp
  systems/TrafficMicroSim.cpp
  world/CityMap.cpp
  world/Tile.cpp
  world/Zoning.cpp
  world/TerrainGenerator.cpp
  networks/RoadNetwork.cpp
  networks/Pathfinding.cpp
  metrics/GrowthMetrics.cpp
  metrics/CityMetrics.cpp
  persistence/SaveLoadSystem.cpp
  persistence/ReplayVerifier.cpp
  visualization/MapRenderer.cpp
)

target_include_directories(urban_sim_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# PUBLIC: tests include <nlohmann/json.hpp> directly to author snapshot fixtures.
target_link_libraries(urban_sim_core PUBLIC nlohmann_json::nlohmann_json)

# CLI executable
add_executable(UrbanSimCore-cli
  main.cpp
  cli/Benchmark.cpp
  cli/CityPrinters.cpp
  cli/CliUtils.cpp
  cli/DistrictCommands.cpp
  cli/GrowthPressureReport.cpp
  cli/PolicySweep.cpp
  cli/Simulate.cpp
  cli/MicroTraffic.cpp
)

target_link_libraries(UrbanSimCore-cli PRIVATE urban_sim_core)

# Optional live visualizer (SDL2)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(UrbanSimCore-visualizer
    visualization/VisualizerSDL.cpp
  )
  target_link_libraries(UrbanSimCore-visualizer PRIVATE urban_sim_core SDL2::SDL2)
  target_compile_definitions(UrbanSimCore-visualizer PRIVATE URBAN_SIM_HAS_SDL2=1)
  install(TARGETS UrbanSimCore-visualizer DESTINATION bin)
  message(STATUS "SDL2 found: building UrbanSimCore-visualizer")
else()
  message(STATUS "SDL2 not found: skipping UrbanSimCore-visualizer target")
endif()

# Install
install(TARGETS UrbanSimCore-cli DESTINATION bin)
