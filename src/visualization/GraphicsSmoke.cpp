#include "src/visualization/GraphicsSmoke.hpp"
#include "src/visualization/VisualizerHud.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/VulkanRenderer.hpp"
#include "src/visualization/WindowHost.hpp"
#include "src/world/TerrainGenerator.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace visualizer {
int runGraphicsSmoke(WindowHost &host, VulkanRenderer &gpu,
                     const std::string &outputDirectory, bool benchmark) {
  if (!benchmark)
    std::filesystem::create_directories(outputDirectory);
  CityMap map({16, 16});
  TerrainGenerator::generate(map, 12345);
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  std::vector<ServiceFacility> facilities;
  std::unordered_map<Coord, float, Vec2Hash> heat;
  for (int x = 2; x < 14; ++x) {
    auto &tile = map.getTile({x, 8});
    tile.type = 0;
    if (x > 2)
      roads.buildRoad({x - 1, 8}, {x, 8});
    for (int y : {6, 7, 9, 10}) {
      auto &building = map.getTile({x, y});
      building.type = 0;
      building.zone =
          x % 4 == 3 ? static_cast<int>(ZoneType::Office) : (x % 4) + 1;
      map.landValue({x, y}) = static_cast<float>(x * 12);
      map.pollution({x, y}) = static_cast<float>(y) / 16.f;
      building.buildingId = store.createBuilding(
          static_cast<BuildingType>((x % 4) + 1), {x, y}, 16 * (1 + x % 4));
      heat[{x, y}] = static_cast<float>(x) / 16.f;
    }
  }
  // Include an arterial junction so street widths and crosswalks are captured.
  for (int y = 3; y <= 13; ++y) {
    map.getTile({14, y}).type = 0;
    if (y > 3) {
      roads.buildRoad({14, y - 1}, {14, y});
      roads.setRoadClass({14, y - 1}, {14, y}, RoadClass::Arterial);
    }
  }
  roads.buildRoad({13, 8}, {14, 8});
  for (size_t service = 0; service < kServiceTypeCount; ++service) {
    const Coord position{2 + static_cast<int>(service), 12};
    map.getTile(position).type = 0;
    facilities.push_back({static_cast<ServiceType>(service), position, 10, 1.f});
  }
  LiveSimulationState state;
  state.paused = true;
  state.demand = {0.4f, 0.3f, 0.2f, 0.1f};
  DrawList list;
  list.frameTimeMs = 1800;
  const auto started = std::chrono::steady_clock::now();
  size_t submitted = 0, warmedCapacity = 0;
  std::vector<double> frameTimes;
  std::vector<double> generationTimes;
  const int frameCount = benchmark ? 240 : 20;
  for (int frame = 0; frame < frameCount; ++frame) {
    const int scene = benchmark ? 0 : frame;
    const auto frameStart = std::chrono::steady_clock::now();
    if (scene == 8)
      host.resize(1100, 720);
    if (scene == 16)
      host.resize(1280, 800);
    InputEvent event;
    while (host.pollEvent(event))
      if (event.type == EventType::Quit)
        throw std::runtime_error("Graphics smoke interrupted.");
    int width, height;
    host.dimensions(width, height);
    list.color = {91, 108, 91, 255};
    list.clear();
    const bool iso = benchmark || (scene >= 8 && scene < 16);
    const auto mode = static_cast<OverlayMode>(scene % 8);
    IsometricProjection projection(32, 16, {width / 2 - 110, 180});
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) {
        if (iso)
          drawIsometricTile(&list, map, roads, store, facilities, heat, 0.f,
                            mode, projection, {x, y});
        else {
          const auto color =
              tileColor(map, roads, store, {x, y}, mode, facilities, heat, 0.f);
          drawFilledRect(&list, 32 + x * 28, 180 + y * 28, 28, 28, color);
          if (mode == OverlayMode::Zone)
            drawNormalTileDetail(&list, map, store, facilities, {x, y},
                                 32 + x * 28, 180 + y * 28, 28);
        }
      }
    drawGameplayHud(&list, width, 0, store.getBuildingCount(), 50000, "SMOKE",
                    state, population, 0, 0);
    drawToolPalette(&list, width, height, false, false, false, false, false, 0,
                    0);
    drawLegendPanel(&list, mode, width, height, 0, 0);
    drawText(&list, 40, height - 110, "GLFW VULKAN 123", {255, 240, 180}, 3);
    // Fixed pixel/color probes independent of city layout and HUD.
    drawFilledRect(&list, 8, height - 40, 20, 20, {255, 0, 0});
    drawFilledRect(&list, 28, height - 40, 20, 20, {0, 0, 255});
    drawFilledRect(&list, 28, height - 40, 20, 20, {255, 0, 0}, 128);
    drawRectOutline(&list, 50, height - 40, 20, 20, {0, 255, 0}, 128);
    setDrawColor(&list, 255, 255, 255, 255);
    list.line(74, height - 40, 94, height - 20);
    const auto generated = std::chrono::steady_clock::now();
    const auto capture = !benchmark && scene < 16
                             ? (std::filesystem::path(outputDirectory) /
                                ("scene-" + std::to_string(scene) + ".ppm"))
                                   .string()
                             : std::string{};
    bool presented = false;
    for (int attempt = 0; attempt < 100 && !presented; ++attempt) {
      presented = gpu.present(list, capture);
      if (!presented) {
        host.waitEvents();
        while (host.pollEvent(event))
          if (event.type == EventType::Quit)
            throw std::runtime_error("Graphics smoke interrupted.");
      }
    }
    if (!presented)
      throw std::runtime_error(
          "Graphics smoke could not present within its bounded retry count.");
    submitted += list.vertices.size();
    if (!benchmark || frame >= 10) {
      frameTimes.push_back(std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - frameStart)
                               .count());
      generationTimes.push_back(
          std::chrono::duration<double, std::milli>(generated - frameStart)
              .count());
    }
    if (frame == (benchmark ? 10 : 17))
      warmedCapacity = gpu.bufferCapacity();
    if (frame == frameCount - 1 && gpu.bufferCapacity() != warmedCapacity)
      throw std::runtime_error("Geometry buffers grew after warm-up.");
  }
  if (gpu.validationErrors())
    throw std::runtime_error(
        "Graphics smoke reported Vulkan validation errors.");
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - started)
                      .count();
  std::cout << "Graphics " << (benchmark ? "benchmark" : "smoke") << ": "
            << frameCount << " frames, " << frameCount << " draw calls, "
            << submitted << " vertices, " << gpu.bufferCapacity()
            << " buffer bytes, " << ms << " ms (includes capture and vsync)\n";
  if (benchmark) {
    std::sort(frameTimes.begin(), frameTimes.end());
    std::sort(generationTimes.begin(), generationTimes.end());
    std::cout << "Median frame: " << frameTimes[frameTimes.size() / 2]
              << " ms; median geometry: "
              << generationTimes[generationTimes.size() / 2]
              << " ms (vsync enabled, no captures)\n";
  }
  return 0;
}
} // namespace visualizer
