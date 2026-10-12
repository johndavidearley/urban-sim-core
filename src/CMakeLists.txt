# Core library
add_library(urban_sim_core
  core/EntityId.cpp
  core/SimulationTime.cpp
  core/Random.cpp
  entities/EntityStore.cpp
  entities/PopulationStore.cpp
  systems/GrowthSystem.cpp
  systems/PopulationSystem.cpp
  systems/MigrationSystem.cpp
  systems/TrafficSystem.cpp
  systems/EconomySystem.cpp
  systems/MetricsSystem.cpp
  systems/ServiceSystem.cpp
  systems/DistrictSystem.cpp
  systems/CitySimSupport.cpp
  systems/CitySimulator.cpp
  systems/PlayableCityTick.cpp
  systems/TrafficMicroSim.cpp
  systems/LandValueSystem.cpp
  systems/TransitSystem.cpp
  systems/FireSystem.cpp
  systems/CrimeSystem.cpp
  systems/HealthSystem.cpp
  systems/NaturalDisasterSystem.cpp
  systems/WasteSystem.cpp
  systems/DeathcareSystem.cpp
  gameplay/RoadTool.cpp
  gameplay/ZoneTool.cpp
  gameplay/BulldozeTool.cpp
  gameplay/ServiceTool.cpp
  gameplay/DistrictTool.cpp
  gameplay/TreasurySystem.cpp
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
  persistence/GameplaySessionSystem.cpp
  visualization/MapRenderer.cpp
  visualization/IsometricProjection.cpp
)

target_include_directories(urban_sim_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# PUBLIC: tests include <nlohmann/json.hpp> directly to author snapshot fixtures.
target_link_libraries(urban_sim_core PUBLIC nlohmann_json::nlohmann_json)

# CLI executable
add_executable(UrbanSimCore-cli
  main.cpp
  cli/CliParse.cpp
  cli/CliEarlyDispatch.cpp
  cli/CliCityWorkflow.cpp
  cli/CliApp.cpp
  cli/Benchmark.cpp
  cli/CityPrinters.cpp
  cli/CliUtils.cpp
  cli/DistrictCommands.cpp
  cli/GrowthPressureReport.cpp
  cli/PolicySweep.cpp
  cli/Simulate.cpp
  cli/MicroTraffic.cpp
  cli/SnapshotAudit.cpp
  cli/CommutePolicySweep.cpp
)

target_link_libraries(UrbanSimCore-cli PRIVATE urban_sim_core)

# CPU-side visualizer geometry remains usable without a window or graphics SDK.
add_library(urban_sim_visualizer_drawing STATIC
  visualization/VisualizerTypes.cpp
  visualization/VisualizerOverlay.cpp
  visualization/VisualizerRender.cpp
  visualization/VisualizerHud.cpp
)
target_link_libraries(urban_sim_visualizer_drawing PUBLIC urban_sim_core)

# Optional live visualizer: simulation targets have no graphics dependency.
if(URBAN_SIM_BUILD_VISUALIZER)
  set(URBAN_SIM_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  file(MAKE_DIRECTORY "${URBAN_SIM_SHADER_DIR}")
  set(URBAN_SIM_SHADER_HEADERS)
  foreach(stage vert frag)
    set(shader "${CMAKE_CURRENT_SOURCE_DIR}/visualization/shaders/colored.${stage}")
    set(binary "${URBAN_SIM_SHADER_DIR}/colored_${stage}.spv")
    set(header "${URBAN_SIM_SHADER_DIR}/colored_${stage}.hpp")
    add_custom_command(OUTPUT "${header}"
      COMMAND "${URBAN_SIM_GLSLC}" --target-env=vulkan1.0 "${shader}" -o "${binary}"
      COMMAND "${CMAKE_COMMAND}" "-DINPUT=${binary}" "-DOUTPUT=${header}" "-DSYMBOL=colored_${stage}"
        -P "${CMAKE_SOURCE_DIR}/cmake/EmbedShader.cmake"
      DEPENDS "${shader}" "${CMAKE_SOURCE_DIR}/cmake/EmbedShader.cmake"
      VERBATIM)
    list(APPEND URBAN_SIM_SHADER_HEADERS "${header}")
  endforeach()
  add_library(urban_sim_visualizer STATIC
    visualization/VisualizerSession.cpp
    visualization/WindowHost.cpp
    visualization/VulkanRenderer.cpp
    visualization/GraphicsSmoke.cpp
    visualization/VisualizerApp.cpp
    playtest/PlaytestBridge.cpp
    ${URBAN_SIM_SHADER_HEADERS}
  )
  target_include_directories(urban_sim_visualizer PRIVATE "${URBAN_SIM_SHADER_DIR}")
  target_link_libraries(urban_sim_visualizer PUBLIC urban_sim_visualizer_drawing PRIVATE glfw Vulkan::Vulkan)
  add_executable(UrbanSimCore-visualizer WIN32 visualization/VisualizerGLFW.cpp)
  target_link_libraries(UrbanSimCore-visualizer PRIVATE urban_sim_visualizer)
  add_executable(UrbanSimCore-playtest playtest/PlaytestMain.cpp)
  target_link_libraries(UrbanSimCore-playtest PRIVATE urban_sim_visualizer)
  install(TARGETS UrbanSimCore-playtest RUNTIME DESTINATION bin)
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/playtest/" DESTINATION share/urban-sim/playtest
    PATTERN "__pycache__" EXCLUDE PATTERN "*.egg-info" EXCLUDE PATTERN "tests" EXCLUDE)
  get_target_property(URBAN_SIM_GLFW_TYPE glfw TYPE)
  if(WIN32 AND URBAN_SIM_GLFW_TYPE STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET UrbanSimCore-playtest POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:glfw>" "$<TARGET_FILE_DIR:UrbanSimCore-playtest>" VERBATIM)
    add_custom_command(TARGET UrbanSimCore-visualizer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:glfw>" "$<TARGET_FILE_DIR:UrbanSimCore-visualizer>"
      VERBATIM)
    install(FILES "$<TARGET_FILE:glfw>" DESTINATION bin)
  endif()
  if(APPLE)
    find_library(URBAN_SIM_MOLTENVK MoltenVK
      HINTS "$ENV{VULKAN_SDK}/lib" /usr/local/lib /opt/homebrew/lib)
    if(NOT URBAN_SIM_MOLTENVK)
      message(FATAL_ERROR "MoltenVK is required for the macOS visualizer (Vulkan SDK or brew install molten-vk).")
    endif()
    set_target_properties(UrbanSimCore-visualizer UrbanSimCore-playtest PROPERTIES INSTALL_RPATH "@executable_path/../lib/urban-sim")
    set(URBAN_SIM_GLFW_LIBRARY "$<TARGET_FILE:glfw>")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/InstallVulkanMac.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/InstallVulkanMac.cmake.in" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/InstallVulkanMac-$<CONFIG>.cmake"
      INPUT "${CMAKE_CURRENT_BINARY_DIR}/InstallVulkanMac.cmake.in")
  endif()
  install(TARGETS UrbanSimCore-visualizer RUNTIME DESTINATION bin)
  if(APPLE)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/InstallVulkanMac-$<CONFIG>.cmake")
  endif()
  message(STATUS "Building GLFW/Vulkan UrbanSimCore-visualizer")
endif()

# Install
install(TARGETS UrbanSimCore-cli DESTINATION bin)
