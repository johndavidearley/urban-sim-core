# Core library
add_library(urban_sim_core
  core/EntityId.cpp
  core/SimulationTime.cpp
  core/Random.cpp
  world/CityMap.cpp
  world/Tile.cpp
  networks/RoadNetwork.cpp
  networks/Pathfinding.cpp
  metrics/CityMetrics.cpp
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
