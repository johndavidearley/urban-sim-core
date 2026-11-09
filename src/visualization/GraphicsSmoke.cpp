#include "src/visualization/Scene3D.hpp"
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
  for (int x = 3; x <= 6; ++x) {
    map.getTile({x, 14}).type = 0;
    map.getTile({x, 14}).zone = static_cast<int>(ZoneType::Park);
  }
  // A separate flat fixture exposes every utility source and full terminal
  // footprints without changing the retained historical captures.
  CityMap sitesMap({24,24}); RoadNetwork sitesRoads(sitesMap);
  EntityStore sitesStore;
  std::vector<ServiceFacility> sitesFacilities;
  std::vector<regional::Connection> terminals;
  for(int z=18;z<24;++z) for(int x=0;x<24;++x) sitesMap.getTile({x,z}).type=2;
  for(int x=1;x<23;++x) {
    if(x>1) sitesRoads.buildRoad({x-1,8},{x,8});
    sitesMap.getTile({x,10}).zone=int(ZoneType::Park);
  }
  for(size_t i=0;i<kServiceTypeCount;++i) {
    ServiceFacility site{static_cast<ServiceType>(i),{2+int(i),7},10,1.f};
    if(site.type==ServiceType::Garbage) site.landfillRemaining=site.landfillCapacity/3;
    sitesFacilities.push_back(site);
  }
  for(int source=0;source<8;++source) {
    ServiceFacility site{ServiceType::Power,{1+source*3,3},10,1.f};
    site.powerSource=static_cast<PowerSourceType>(source);
    sitesFacilities.push_back(site);
  }
  ServiceFacility intake{ServiceType::Water,{20,17},10,1.f};
  intake.surfaceWater=true;sitesFacilities.push_back(intake);
  ServiceFacility treatment{ServiceType::Sanitation,{22,17},10,1.f};
  treatment.surfaceWater=true;treatment.sewageTreatment=true;sitesFacilities.push_back(treatment);
  for(auto kind:{regional::Kind::Airport,regional::Kind::CargoPort,regional::Kind::PassengerTerminal}) {
    regional::Connection terminal;
    terminal.id=uint32_t(terminals.size()+1);terminal.kind=kind;terminal.constructed=true;
    terminal.origin=kind==regional::Kind::Airport ? Coord{3,12} :
                    kind==regional::Kind::CargoPort ? Coord{10,17} : Coord{15,17};
    terminal.width=kind==regional::Kind::Airport ? 4 : 2;terminal.height=2;
    terminals.push_back(terminal);
    for(int dz=0;dz<terminal.height;++dz) for(int dx=0;dx<terminal.width;++dx)
      sitesMap.getTile({terminal.origin.x+dx,terminal.origin.y+dz}).regionalConnectionId=terminal.id;
  }
  CityMap architectureMap({24,24});RoadNetwork architectureRoads(architectureMap);
  EntityStore architectureStore;
  for(int x=1;x<23;++x) {
    if(x>1) architectureRoads.buildRoad({x-1,6},{x,6});
    if(x>1) architectureRoads.buildRoad({x-1,17},{x,17});
  }
  for(int row=0;row<4;++row) for(int column=0;column<6;++column) {
    const auto type=static_cast<BuildingType>(row+1);
    const Coord tile{2+column*3,2+row*3};
    const auto id=architectureStore.createBuilding(type,tile,defaultBuildingCapacity(type)*(1<<column));
    architectureMap.getTile(tile).buildingId=id;
  }
  for(uint32_t kind=1;kind<=uniques::count;++kind) {
    const Coord tile{2+int(kind-1)*3,15};
    Building b;b.id=architectureStore.createBuilding(BuildingType::Office,tile,0);
    b.type=BuildingType::Office;b.position=tile;b.uniqueKind=kind;
    architectureStore.upsertBuilding(b);architectureMap.getTile(tile).buildingId=b.id;
  }
  for(uint32_t kind=1;kind<=4;++kind) {
    const Coord tile{2+int(kind-1)*4,20};
    Building b;b.id=architectureStore.createBuilding(BuildingType::Office,tile,0);
    b.type=BuildingType::Office;b.position=tile;b.monumentKind=kind;
    architectureStore.upsertBuilding(b);architectureMap.getTile(tile).buildingId=b.id;
  }
  for(int hotel=0;hotel<2;++hotel) {
    const Coord tile{19+hotel*3,20};
    Building b;b.id=architectureStore.createBuilding(BuildingType::Commercial,tile,12);
    b.type=BuildingType::Commercial;b.position=tile;b.capacity=12;b.hotelBeds=32;b.hotelOpen=hotel==0;
    architectureStore.upsertBuilding(b);architectureMap.getTile(tile).buildingId=b.id;
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
  const int frameCount = benchmark ? 240 : 40;
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
    const auto mode = scene>=20 ? OverlayMode::Zone : static_cast<OverlayMode>(scene % 8);
    IsometricProjection projection(32, 16, {width / 2 - 110, 180});
    if(scene>=32) {
      Camera3D camera;camera.yaw=45.f+((scene-32)%4)*90.f;
      camera.scale=24.f;camera.centerX=width*.5f;camera.centerY=height*.5f;
      camera.focalLength=height*1.2f;camera.perspective=scene>=36;
      camera.centerOn({12,0,12},width*.5f,height*.5f);
      list.viewportWidth=width;list.viewportHeight=height;
      DistrictSystem districts;
      drawCity3D(&list,camera,architectureMap,architectureRoads,architectureStore,{},
                 OverlayMode::Zone,{},0.f,districts,0,nullptr);
    } else if(scene>=20) {
      Camera3D camera;camera.yaw=45.f+((scene-20)%4)*90.f;
      camera.scale=scene>=28 ? 80.f : 22.f;
      camera.originX=width*.5f;camera.originY=height*.5f;
      camera.centerX=width*.5f;camera.centerY=height*.5f;
      camera.focalLength=height*1.2f;
      camera.perspective=scene>=24;
      if(scene>=28) camera.pitch=20.f;
      camera.centerOn({12,0,12},width*.5f,height*.5f);
      list.viewportWidth=width;list.viewportHeight=height;
      DistrictSystem districts;
      drawCity3D(&list,camera,sitesMap,sitesRoads,sitesStore,sitesFacilities,
                 OverlayMode::Zone,heat,0.f,districts,0,nullptr,&terminals);
      if(scene>=28) {
        // Select a plot crossing the near plane to exercise preview clipping.
        bool selected=false;
        for(int z=0;z<24 && !selected;++z) for(int x=0;x<24 && !selected;++x) {
          float nearest=1e9f,farthest=-1e9f;
          for(int dz:{0,1}) for(int dx:{0,1}) {
            const float depth=camera.viewDepth({float(x+dx),.04f,float(z+dz)});
            nearest=std::min(nearest,depth);farthest=std::max(farthest,depth);
          }
          if(nearest<Camera3D::nearPlane && farthest>=Camera3D::nearPlane) {
            drawGroundHighlight(&list,camera,{x,z},{80,220,120},165);selected=true;
          }
        }
      }
    } else if(benchmark || scene>=16) {
      Camera3D camera; camera.yaw=45.f+(scene-16)*90.f;
      camera.scale=32.f; camera.originX=width*.5f;
      camera.originY=height*.5f;
      const auto center=camera.project({8,0,8});
      camera.originX+=width*.5f-center.x;
      camera.originY+=height*.5f-center.y;
      list.viewportWidth=width;list.viewportHeight=height;
      DistrictSystem districts;
      drawCity3D(&list,camera,map,roads,store,facilities,mode,heat,0.f,districts,0,nullptr);
    } else for (int y = 0; y < 16; ++y)
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
    if(scene>=16) {
      const auto start=list.vertices.size();
      drawFilledRect(&list,110,height-40,20,20,{255,0,0});
      for(size_t i=start;i<list.vertices.size();++i) list.vertices[i].z=.2f;
      const auto back=list.vertices.size();
      drawFilledRect(&list,110,height-40,20,20,{0,0,255});
      for(size_t i=back;i<list.vertices.size();++i) list.vertices[i].z=.8f;
      drawFilledRect(&list,116,height-34,6,6,{0,255,0});
    }
    const auto generated = std::chrono::steady_clock::now();
    const auto capture = !benchmark
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
    if (frame == (benchmark ? 10 : frameCount-3))
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
