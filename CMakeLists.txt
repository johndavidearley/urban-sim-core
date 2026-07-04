cmake_minimum_required(VERSION 3.16)
project(UrbanSimCore
  VERSION 0.1.0
  LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Release build when none is specified. This is a headless
# simulation engine with hot per-tick loops (traffic pathfinding, land value,
# pollution field) - shipping unoptimized (-O0, the implicit default when
# CMAKE_BUILD_TYPE is unset) silently disables auto-vectorization and inlining
# entirely, which is a much bigger effect than any individual hot-loop
# micro-optimization. Single-config generators only (Makefiles/Ninja);
# multi-config generators (Xcode/MSVC) pick the configuration at build time
# instead and are left alone.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug/Release/RelWithDebInfo/MinSizeRel)" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Compiler flags
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fcolor-diagnostics)
  endif()
endif()

# Dependencies: nlohmann_json is required for snapshot persistence.
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found locally; fetching v3.11.3")
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# Google Test (for testing)
enable_testing()
if(EXISTS "${CMAKE_SOURCE_DIR}/extern/googletest/CMakeLists.txt")
  add_subdirectory(extern/googletest EXCLUDE_FROM_ALL)
  set(URBAN_SIM_HAS_GTEST ON)
else()
  message(WARNING "googletest submodule not available; test targets will be skipped")
  set(URBAN_SIM_HAS_GTEST OFF)
endif()

# Main source library
add_subdirectory(src)

# Tests
if(URBAN_SIM_HAS_GTEST)
  add_subdirectory(tests)
endif()

# Summary
message(STATUS "UrbanSimCore ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
