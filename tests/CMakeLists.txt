# Test executable with all test sources
add_executable(test_core
  CoreTests.cpp
  EntityStoreTests.cpp
  WorldTests.cpp
  NetworkTests.cpp
  GrowthSystemTests.cpp
)

target_link_libraries(test_core PRIVATE urban_sim_core gtest gtest_main)
target_include_directories(test_core PRIVATE ${CMAKE_SOURCE_DIR})

add_test(NAME CoreTests COMMAND test_core)

