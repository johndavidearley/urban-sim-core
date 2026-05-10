cmake_minimum_required(VERSION 3.16)

# Basic test example
add_executable(test_core
  CoreTests.cpp
)

target_link_libraries(test_core PRIVATE urban_sim_core gtest gtest_main)
target_include_directories(test_core PRIVATE ${CMAKE_SOURCE_DIR})

add_test(NAME CoreTests COMMAND test_core)
