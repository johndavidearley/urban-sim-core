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
  TransitSystemTests.cpp
  FireSystemTests.cpp
  CrimeSystemTests.cpp
  HealthSystemTests.cpp
  NaturalDisasterSystemTests.cpp
  SaveLoadSystemTests.cpp
  ReplayVerifierTests.cpp
  MapRendererTests.cpp
  RoadToolTests.cpp
  ZoneToolTests.cpp
  BulldozeToolTests.cpp
  ServiceToolTests.cpp
  GameplaySessionSystemTests.cpp
  TreasurySystemTests.cpp
  WasteSystemTests.cpp
  DeathcareSystemTests.cpp
  )

target_link_libraries(test_core PRIVATE urban_sim_core gtest_main)
target_include_directories(test_core PRIVATE ${CMAKE_SOURCE_DIR})
if(MSVC)
  target_compile_options(test_core PRIVATE
    "/I${CMAKE_SOURCE_DIR}/extern/googletest/googletest/include")
else()
  # Tests use quoted GoogleTest includes so this path wins over a globally
  # installed, potentially ABI-incompatible copy in /usr/local/include.
  target_compile_options(test_core PRIVATE
    "-iquote${CMAKE_SOURCE_DIR}/extern/googletest/googletest/include")
endif()

include(GoogleTest)
gtest_discover_tests(test_core
  DISCOVERY_TIMEOUT 30
  PROPERTIES LABELS "unit"
)
