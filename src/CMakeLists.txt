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
  world/CityMap.cpp
  world/Tile.cpp
  world/Zoning.cpp
  networks/RoadNetwork.cpp
  networks/Pathfinding.cpp
  metrics/GrowthMetrics.cpp
  metrics/CityMetrics.cpp
  persistence/SaveLoadSystem.cpp
  persistence/ReplayVerifier.cpp
)

target_include_directories(urban_sim_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# CLI executable
add_executable(UrbanSimCore-cli
  main.cpp
)

target_link_libraries(UrbanSimCore-cli PRIVATE urban_sim_core)

# Install
install(TARGETS UrbanSimCore-cli DESTINATION bin)
