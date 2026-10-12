# Test executable with all test sources
add_executable(test_core
  VisualizerGeometryTests.cpp
  CoreTests.cpp
  EntityStoreTests.cpp
  WorldTests.cpp
  TerrainGeneratorTests.cpp
  NetworkTests.cpp
  GrowthSystemTests.cpp
  GrowthMetricsTests.cpp
  PopulationSystemTests.cpp
  MigrationSystemTests.cpp
  TrafficSystemTests.cpp
  EconomySystemTests.cpp
  MetricsSystemTests.cpp
  ServiceSystemTests.cpp
  DistrictSystemTests.cpp
  CitySimulatorTests.cpp
  CitySimSupportTests.cpp
  PlayableCityTickTests.cpp
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
  IsometricProjectionTests.cpp
  RoadToolTests.cpp
  ZoneToolTests.cpp
  BulldozeToolTests.cpp
  ServiceToolTests.cpp
  DistrictToolTests.cpp
  GameplaySessionSystemTests.cpp
  TreasurySystemTests.cpp
  WasteSystemTests.cpp
  DeathcareSystemTests.cpp
  )

target_link_libraries(test_core PRIVATE gtest_main)
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

if(URBAN_SIM_BUILD_VISUALIZER)
  target_sources(test_core PRIVATE VisualizerInputTests.cpp)
  target_link_libraries(test_core PRIVATE urban_sim_visualizer glfw)
  get_target_property(URBAN_SIM_TEST_GLFW_TYPE glfw TYPE)
  if(WIN32 AND URBAN_SIM_TEST_GLFW_TYPE STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET test_core POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:glfw>" "$<TARGET_FILE_DIR:test_core>"
      VERBATIM)
  endif()
else()
  target_link_libraries(test_core PRIVATE urban_sim_visualizer_drawing)
endif()
