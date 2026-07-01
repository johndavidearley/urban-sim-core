# Test executable with all test sources
add_executable(test_core
  CoreTests.cpp
  EntityStoreTests.cpp
  WorldTests.cpp
  TerrainGeneratorTests.cpp
  NetworkTests.cpp
  GrowthSystemTests.cpp
  GrowthMetricsTests.cpp
  PopulationSystemTests.cpp
  TrafficSystemTests.cpp
  EconomySystemTests.cpp
  MetricsSystemTests.cpp
  ServiceSystemTests.cpp
  DistrictSystemTests.cpp
  CitySimulatorTests.cpp
  TrafficMicroSimTests.cpp
  LandValueSystemTests.cpp
  SaveLoadSystemTests.cpp
  ReplayVerifierTests.cpp
  MapRendererTests.cpp
  )

target_link_libraries(test_core PRIVATE urban_sim_core gtest gtest_main)
target_include_directories(test_core PRIVATE ${CMAKE_SOURCE_DIR})

add_test(NAME CoreTests COMMAND test_core)

