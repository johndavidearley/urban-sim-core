cmake_minimum_required(VERSION 3.16)
project(UrbanSimCore
  VERSION 0.1.0
  LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(URBAN_SIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(URBAN_SIM_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(URBAN_SIM_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(URBAN_SIM_BUILD_VISUALIZER "Build the SDL2 live visualizer" ON)

if(URBAN_SIM_ENABLE_ASAN_UBSAN AND URBAN_SIM_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan cannot be enabled in the same build")
endif()

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
  if(URBAN_SIM_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(URBAN_SIM_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fcolor-diagnostics)
  endif()
endif()

if(URBAN_SIM_ENABLE_ASAN_UBSAN OR URBAN_SIM_ENABLE_TSAN)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "Sanitizer builds require Clang or GCC")
  endif()

  if(URBAN_SIM_ENABLE_ASAN_UBSAN)
    set(URBAN_SIM_SANITIZERS "address,undefined")
  else()
    set(URBAN_SIM_SANITIZERS "thread")
  endif()
  add_compile_options(-fsanitize=${URBAN_SIM_SANITIZERS} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${URBAN_SIM_SANITIZERS})
endif()

# Dependencies: nlohmann_json is required for snapshot persistence.
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found locally; fetching v3.11.3")
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# SDL2 is used by the live visualizer. Prefer a package supplied by the system
# or toolchain, then fetch a pinned SDL2 release so the default build works on
# Windows, macOS, and Linux without a separate SDL2 installation.
if(URBAN_SIM_BUILD_VISUALIZER)
  find_package(SDL2 QUIET CONFIG)
  if(NOT TARGET SDL2::SDL2)
    message(STATUS "SDL2 not found locally; fetching 2.32.10")
    include(FetchContent)
    set(SDL_SHARED ON CACHE BOOL "Build the SDL2 shared library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Build the SDL2 static library" FORCE)
    set(SDL_TEST OFF CACHE BOOL "Build the SDL2 test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Build the SDL2 tests" FORCE)
    set(SDL_CCACHE OFF CACHE BOOL "Use ccache while building SDL2" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "Disable SDL2 install rules" FORCE)
    FetchContent_Declare(SDL2
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-2.32.10
      GIT_SHALLOW TRUE
    )

    # Do not leak this project's warning/sanitizer flags into third-party C
    # sources. The SDL subdirectory supplies its own compiler options.
    get_directory_property(URBAN_SIM_COMPILE_OPTIONS COMPILE_OPTIONS)
    get_directory_property(URBAN_SIM_LINK_OPTIONS LINK_OPTIONS)
    set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
    set_property(DIRECTORY PROPERTY LINK_OPTIONS "")
    FetchContent_MakeAvailable(SDL2)
    set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${URBAN_SIM_COMPILE_OPTIONS}")
    set_property(DIRECTORY PROPERTY LINK_OPTIONS "${URBAN_SIM_LINK_OPTIONS}")
  endif()
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
message(STATUS "Warnings as errors: ${URBAN_SIM_WARNINGS_AS_ERRORS}")
message(STATUS "ASan/UBSan: ${URBAN_SIM_ENABLE_ASAN_UBSAN}")
message(STATUS "TSan: ${URBAN_SIM_ENABLE_TSAN}")
