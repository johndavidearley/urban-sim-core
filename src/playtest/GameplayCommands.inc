// Executed exclusively on the GLFW main thread, sharing the player action
// closures.
if (command == "status" || command == "diagnostics")
  return {{"capabilities", playtestCapabilities()}};
if (command == "stop") {
  const auto action = args.value("action", "discard");
  if (action != "save" && action != "discard")
    throw std::runtime_error("Stop action must be save or discard");
  if (action == "save" && !saveSession())
    throw std::runtime_error(toast.message);
  running = false;
  return Json::object();
}
if (command == "inspect") {
  const auto section = args.value("section", "summary");
  if (section == "summary")
    return Json::object();
  if (section == "ui") {
    Json controls = Json::array();
    auto rect = [&](const std::string &name, visualizer::Rect r) {
      controls.push_back({{"name", name},
                          {"x", r.x},
                          {"y", r.y},
                          {"width", r.w},
                          {"height", r.h}});
    };
    if (showQuitDialog)
      for (int i = 1; i <= 3; ++i)
        rect(std::array<std::string, 3>{"save_and_quit", "discard_and_quit",
                                        "cancel"}[static_cast<size_t>(i - 1)],
             quitButtonRect(static_cast<QuitAction>(i), windowWidth,
                            windowHeight));
    else {
      if (!cleanUiMode)
        for (int i = 1; i <= 7; ++i)
          rect(std::array<std::string, 7>{"pause", "speed_1", "speed_2",
                                          "speed_3", "save",
                                          "load", "disasters"}[static_cast<size_t>(i - 1)],
               hudControlRect(static_cast<HudAction>(i), windowWidth));
      for (int i = 0; i < 5; ++i)
        rect(std::array<std::string, 5>{"road", "zone", "bulldoze", "service",
                                        "district"}[static_cast<size_t>(i)],
             paletteButtonRect(i, windowWidth, windowHeight));
      if (!cleanUiMode && selectedDistrictId != 0)
        for (int i = 1; i <= 2; ++i)
          rect("district_" + std::to_string(i),
               districtPanelButtonRect(static_cast<DistrictPanelAction>(i)));
      if (!cleanUiMode && showLegend)
        for (int i = 0; i < 9; ++i)
          rect("overlay_" + std::to_string(i),
               {kOverlayKeyStartX + i * (kOverlayKeyWidth + kOverlayKeyGap),
                kOverlayKeyY, kOverlayKeyWidth, kOverlayKeyHeight});
    }
    return {{"controls", controls}};
  }
  const int offset = args.value("offset", 0), limit = args.value("limit", 128);
  if (offset < 0 || limit < 1 || limit > 512)
    throw std::runtime_error("Page requires offset >= 0 and limit 1..512");
  Json items = Json::array();
  if (section == "tiles") {
    const int x = args.value("x", 0), y = args.value("y", 0),
              w = args.value("width", 1), h = args.value("height", 1);
    if (x < 0 || y < 0 || w < 1 || h < 1 || w > mapSize || h > mapSize ||
        x > mapSize - w || y > mapSize - h || w * h > 512)
      throw std::runtime_error(
          "Tile region must be inside the map and contain at most 512 tiles");
    for (int ty = y; ty < y + h; ++ty)
      for (int tx = x; tx < x + w; ++tx) {
        const auto &t = map.getTile({tx, ty});
        items.push_back({{"x", tx},
                         {"y", ty},
                         {"type", t.type},
                         {"zone", t.zone},
                         {"road", t.hasRoad},
                         {"building_id", t.buildingId},
                         {"power", t.connectedToPower},
                         {"water", t.connectedToWater},
                         {"road_access", t.connectedToRoad},
                         {"pollution", map.pollution({tx, ty})},
                         {"land_value", map.landValue({tx, ty})}});
        const auto* building = store.getBuilding(t.buildingId);
        if (building && building->type == BuildingType::Residential) {
          items.back()["crime_rate"] = building->crimeRate;
          items.back()["illness_rate"] = building->illnessRate;
        }
      }
    return {{"items", items}, {"total", w * h}};
  }
  if (section == "roads") {
    for (const auto& edge : roads.getAllEdgeTraffic()) {
      items.push_back({{"x1", edge.from.x}, {"y1", edge.from.y},
                       {"x2", edge.to.x}, {"y2", edge.to.y},
                       {"road_class", edge.roadClass == RoadClass::Arterial ? "arterial" : "local"},
                       {"capacity", edge.capacity}, {"load", edge.currentLoad},
                       {"freight_load", edge.freightLoad}, {"congestion", edge.congestion}});
    }
  } else if (section == "entities") {
    std::vector<EntityId> ids;
    for (const auto &item : store.getBuildings())
      ids.push_back(item.first);
    std::sort(ids.begin(), ids.end());
    for (auto id : ids) {
      const auto &b = *store.getBuilding(id);
      items.push_back({{"id", id},
                       {"type", static_cast<int>(b.type)},
                       {"x", b.position.x},
                       {"y", b.position.y},
                       {"capacity", b.capacity},
                       {"occupancy", b.occupancy},
                       {"crime_rate", b.crimeRate},
                       {"illness_rate", b.illnessRate}});
    }
  } else if (section == "facilities") {
    for (const auto &f : facilities) {
      items.push_back({{"type", ServiceSystem::serviceTypeToString(f.type)},
                       {"x", f.position.x},
                       {"y", f.position.y},
                       {"quality", f.quality},
                       {"distance", f.maxTravelDistance}});
      if (f.type == ServiceType::Power) {
        items.back()["power_source"] = ServiceSystem::powerSourceTypeToString(f.powerSource);
        items.back()["power_capacity_mw"] = f.powerCapacityMW;
        items.back()["emissions_kg_per_mwh"] = f.emissionsKgPerMWh;
      } else if (f.type == ServiceType::Water) {
        items.back()["water_supply"] = f.waterSupplyUnits;
      }
    }
  } else if (section == "districts") {
    for (const auto &d : districts.getDistricts())
      items.push_back(
          {{"id", d.id},
           {"name", d.name},
           {"x1", d.minCorner.x},
           {"y1", d.minCorner.y},
           {"x2", d.maxCorner.x},
           {"y2", d.maxCorner.y},
           {"archetype", DistrictSystem::archetypeToString(d.archetype)},
           {"budget_cap", d.serviceBudgetCap}});
  } else if (section == "routes") {
    for (const auto &r : liveState.transitRoutes) {
      Json stops = Json::array();
      for (auto c : r.stops)
        stops.push_back({c.x, c.y});
      items.push_back({{"id", r.id},
                       {"mode", static_cast<int>(r.mode)},
                       {"capacity", r.totalCapacity()},
                       {"stops", stops}});
    }
  } else
    throw std::runtime_error("Unknown inspection section");
  const size_t total = items.size();
  Json page = Json::array();
  for (size_t i = static_cast<size_t>(offset);
       i < total && page.size() < static_cast<size_t>(limit); ++i)
    page.push_back(items[i]);
  return {{"items", page},
          {"total", total},
          {"offset", offset},
          {"next_offset", static_cast<size_t>(offset) + page.size() < total
                              ? Json(static_cast<size_t>(offset) + page.size())
                              : Json(nullptr)}};
}
if (command == "coordinates") {
  const int x = args.at("x"), y = args.at("y");
  if (args.at("direction") == "tile_to_screen") {
    if (!map.isValid({x, y}))
      throw std::runtime_error("Tile outside map");
    const auto point =
        isometricMode ? isometricProjection().tileCenter({x, y})
                      : ScreenPoint{(x - viewX) * tilePixels + tilePixels / 2,
                                    (y - viewY) * tilePixels + tilePixels / 2};
    return {{"x", point.x}, {"y", point.y}};
  }
  if (args.at("direction") != "screen_to_tile")
    throw std::runtime_error("Unknown coordinate direction");
  if (x < 0 || y < 0 || x >= windowWidth || y >= windowHeight)
    throw std::runtime_error("Screen point outside window");
  const auto tile = tileAtScreen(x, y);
  return {{"x", tile.x}, {"y", tile.y}, {"valid", map.isValid(tile)}};
}
if (command == "city") {
  const auto action = args.at("action").get<std::string>();
  if (action == "save") {
    if (!saveSession())
      throw std::runtime_error(toast.message);
  } else if (action == "load") {
    if (!loadSession())
      throw std::runtime_error(toast.message);
  } else
    throw std::runtime_error("New cities require a fresh owned process");
  return Json::object();
}
if (command == "time") {
  const auto action = args.at("action").get<std::string>();
  if (action == "step") {
    const int count = args.value("ticks", 1);
    if (count < 1 || count > 100)
      throw std::runtime_error("Tick batch must be 1..100");
    if (showQuitDialog)
      throw std::runtime_error("Cancel the quit dialog before stepping");
    for (int i = 0; i < count; ++i)
      executeSimulationTick();
  } else if (action == "pause") {
    liveState.paused = args.value("paused", true);
    sessionDirty = true;
  } else if (action == "speed") {
    const int speed = args.at("speed");
    if (speed < 1 || speed > 3)
      throw std::runtime_error("Speed must be 1..3");
    liveState.tickIntervalMs = speed == 1 ? 700u : speed == 2 ? 350u : 175u;
    sessionDirty = true;
  } else if (action == "mode") {
    const auto mode = args.at("mode").get<std::string>();
    if (mode != "deterministic" && mode != "realtime")
      throw std::runtime_error("Unknown time mode");
    bridge->deterministic = mode == "deterministic";
    liveState.lastTickMs = host.timeMs();
  } else
    throw std::runtime_error("Unknown time action");
  return Json::object();
}
if (showQuitDialog)
  throw std::runtime_error("Cancel the quit dialog before gameplay commands");
if (command == "view") {
  const auto action = args.at("action").get<std::string>();
  if (action == "overlay") {
    const int value = args.at("overlay");
    if (value < 0 || value > 8)
      throw std::runtime_error("Overlay must be 0..8");
    overlayMode = static_cast<OverlayMode>(value);
  } else if (action == "projection") {
    const auto value = args.at("projection").get<std::string>();
    if (value != "isometric" && value != "top_down")
      throw std::runtime_error("Unknown projection");
    isometricMode = value == "isometric";
    toolDragging = false;
    roadPlan = {};
    zonePlan = {};
    bulldozePlan = {};
  } else if (action == "camera") {
    const int scale = args.value("tile_pixels", tilePixels);
    if (scale < minimumTilePixels || scale > 48)
      throw std::runtime_error("Tile scale outside supported range");
    const int vx = args.value("view_x", viewX),
              vy = args.value("view_y", viewY),
              ox = args.value("offset_x", isometricCameraX),
              oy = args.value("offset_y", isometricCameraY);
    if (vx < 0 || vy < 0 || vx >= mapSize || vy >= mapSize ||
        std::abs(static_cast<int64_t>(ox)) > 100000 ||
        std::abs(static_cast<int64_t>(oy)) > 100000)
      throw std::runtime_error("Camera outside supported range");
    tilePixels = scale;
    viewX = vx;
    viewY = vy;
    isometricCameraX = ox;
    isometricCameraY = oy;
  } else if (action == "focus")
    focusDevelopedArea();
  else if (action == "toggle") {
    const auto name = args.at("name").get<std::string>();
    const bool enabled = args.at("enabled");
    if (name == "legend")
      showLegend = enabled;
    else if (name == "compact_ui")
      cleanUiMode = enabled;
    else if (name == "large_text")
      gReadableUiText = enabled;
    else if (name == "onboarding")
      showOnboarding = enabled;
    else if (name == "disasters") {
      liveState.enableDisasters = enabled;
      if (!enabled) liveState.disasters.stopEvents();
      sessionDirty = true;
    } else if (name == "autonomous_growth") {
      liveState.autonomousGrowth = enabled;
      sessionDirty = true;
    } else
      throw std::runtime_error("Unknown toggle");
  } else
    throw std::runtime_error("Unknown view action");
  return Json::object();
}
if (command == "tool") {
  const auto action = args.at("action").get<std::string>();
  if (action == "district_policy") {
    const auto id = args.at("id").get<DistrictId>();
    if (!districts.getDistrictConst(id))
      throw std::runtime_error("Unknown district id");
    if (args.contains("archetype")) {
      DistrictArchetype a;
      if (!DistrictSystem::parseArchetype(args.at("archetype"), a))
        throw std::runtime_error("Unknown archetype");
      districts.setDistrictArchetype(id, a);
    }
    if (args.contains("budget_cap"))
      districts.setDistrictServiceBudgetCap(
          id, args.at("budget_cap").get<int64_t>());
    sessionDirty = true;
    return Json::object();
  }
  if (action == "district_delete") {
    if (!districts.deleteDistrict(args.at("id").get<DistrictId>()))
      throw std::runtime_error("Unknown district id");
    selectedDistrictId = 0;
    sessionDirty = true;
    return Json::object();
  }
  const auto name = args.at("name").get<std::string>();
  PaletteTool type;
  if (name == "road")
    type = PaletteTool::Road;
  else if (name == "zone")
    type = PaletteTool::Zone;
  else if (name == "bulldoze")
    type = PaletteTool::Bulldoze;
  else if (name == "service")
    type = PaletteTool::Service;
  else if (name == "district")
    type = PaletteTool::District;
  else if (name == "none")
    type = PaletteTool::None;
  else
    throw std::runtime_error("Unknown tool");
  RoadClass roadClass = selectedRoadClass;
  if (args.contains("road_class")) {
    const auto value = args.at("road_class").get<std::string>();
    if (type != PaletteTool::Road || (value != "local" && value != "arterial"))
      throw std::runtime_error("Road class must be local or arterial on the road tool");
    roadClass = value == "arterial" ? RoadClass::Arterial : RoadClass::Local;
  }
  ZoneType zone = selectedZone;
  ServiceType service = selectedService;
  DistrictArchetype archetype = selectedArchetype;
  if (args.contains("zone") && !Zoning::parseZoneType(args.at("zone"), zone))
    throw std::runtime_error("Unknown zone");
  if (args.contains("service") &&
      !ServiceSystem::parseServiceType(args.at("service"), service))
    throw std::runtime_error("Unknown service");
  if (args.contains("archetype") &&
      !DistrictSystem::parseArchetype(args.at("archetype"), archetype))
    throw std::runtime_error("Unknown archetype");
  Coord start{args.value("x1", 0), args.value("y1", 0)},
      end{args.value("x2", start.x), args.value("y2", start.y)};
  if (action != "select" && action != "apply")
    throw std::runtime_error("Tool action must be select or apply");
  if (action == "apply" &&
      (type == PaletteTool::None || !map.isValid(start) || !map.isValid(end)))
    throw std::runtime_error("Placement outside map or tool missing");
  activatePaletteTool(type);
  selectedRoadClass = roadClass;
  selectedZone = zone;
  selectedService = service;
  selectedArchetype = archetype;
  if (action == "apply") {
    toolDragStart = start;
    lastActionSuccess = true;
    if (type == PaletteTool::Service)
      applyService(start);
    else
      applyPlacement(end);
    if (!lastActionSuccess)
      throw std::runtime_error(toast.message);
  }
  return Json::object();
}
throw std::runtime_error("Unknown command: " + command);
