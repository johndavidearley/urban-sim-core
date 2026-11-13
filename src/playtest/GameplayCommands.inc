// Executed exclusively on the GLFW main thread, sharing the player action
// closures.
if((showSettings || showSaveBrowser) && (command=="tool" || command=="time" || command=="budget" || command=="city"))
  throw std::runtime_error(showSaveBrowser?"Close save browser before gameplay commands":"Close settings before gameplay commands");
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
if (command == "budget") {
  const auto action = args.value("action", "inspect");
  if (action!="inspect" && action!="uniques" && action!="monuments") showUniquePanel=false;
  if (action=="set_hotel") {
    if (!args.at("hotel_id").is_number_integer() || !args.at("hotel_open").is_boolean())
      throw std::runtime_error("Hotel ID must be integer and hotel_open boolean");
    const auto id=args.at("hotel_id").get<int64_t>();
    if (id<=0 || id>UINT32_MAX) throw std::runtime_error("Invalid hotel ID");
    setHotelOpen(static_cast<uint32_t>(id),args.at("hotel_open").get<bool>());
  } else if (action=="set_tourism") {
    if (!args.at("tourism_enabled").is_boolean()) throw std::runtime_error("Tourism policy must be boolean");
    setTourism(args.at("tourism_enabled").get<bool>());
  } else if (action=="uniques" || action=="monuments") {monumentCatalog=action=="monuments";showUniquePanel=true;showRegionalPanel=showTransportPanel=showBudgetPanel=false;toolDragging=cameraPanning=cameraOrbiting=false;}
  else if (action=="transport") {showUniquePanel=false;showRegionalPanel=false;showTransportPanel=true;showBudgetPanel=false;toolDragging=false;cameraPanning=cameraOrbiting=false;}
  else if (action=="set_route_vehicles") {
    if (!args.at("route_id").is_number_integer() || !args.at("vehicles").is_number_integer())
      throw std::runtime_error("Route ID and vehicles must be integers");
    const auto id=args.at("route_id").get<int64_t>(), vehicles=args.at("vehicles").get<int64_t>();
    if (id<0 || id>UINT32_MAX || vehicles<0 || vehicles>20) throw std::runtime_error("Invalid bus fleet policy");
    setTransitVehicles(static_cast<TransitRouteId>(id),static_cast<int>(vehicles));
  } else if (action == "view_alert") {
    if (!args.at("alert_index").is_number_integer()) throw std::runtime_error("Alert index must be integer 0 to 4");
    const auto index = args.at("alert_index").get<int64_t>();
    if (index < 0 || index > 4) throw std::runtime_error("Alert index must be 0 to 4");
    viewCityAlert(static_cast<size_t>(index));
  } else if (action == "set_taxes") {
    const auto& values = args.at("tax_percent");
    if (!values.is_array() || values.size() != 5)
      throw std::runtime_error("Provide five tax percentages from 0 to 20");
    auto policy = liveState.budgetPolicy;
    for (size_t i = 0; i < 5; ++i) {
      if (!values[i].is_number_integer())
        throw std::runtime_error("Tax percentages must be integers from 0 to 20");
      const auto value = values[i].get<int64_t>();
      if (value < 0 || value > 20)
        throw std::runtime_error("Tax percentages must be integers from 0 to 20");
      policy.taxPercent[i] = static_cast<int>(value);
    }
    setBudgetPolicy(policy);
  } else if (action == "set_funding") {
    const auto& values = args.at("service_percent");
    if (!values.is_array() || (values.size() != kServiceTypeCount && values.size() != 11))
      throw std::runtime_error("Provide twelve service funding percentages (legacy eleven accepted) from 0 to 100");
    auto policy = liveState.budgetPolicy;
    for (size_t i = 0; i < values.size(); ++i) {
      if (!values[i].is_number_integer())
        throw std::runtime_error("Service funding must be integer percentages from 0 to 100");
      const auto value = values[i].get<int64_t>();
      if (value < 0 || value > 100)
        throw std::runtime_error("Service funding must be integer percentages from 0 to 100");
      policy.servicePercent[i] = static_cast<int>(value);
    }
    setBudgetPolicy(policy);
  } else if (action == "reset_funding") {
    auto policy = liveState.budgetPolicy;
    policy.servicePercent = BudgetPolicy{}.servicePercent;
    setBudgetPolicy(policy);
  } else if (action == "issue_bond") {
    if (!args.contains("bond_principal") || !args.contains("bond_term") ||
        !args.at("bond_principal").is_number_integer() || !args.at("bond_term").is_number_integer())
      throw std::runtime_error("Provide integer bond_principal and bond_term");
    const auto principal = args.at("bond_principal").get<int64_t>(), term = args.at("bond_term").get<int64_t>();
    if (!TreasurySystem::validBondPrincipal(principal) || !TreasurySystem::validBondTerm(term))
      throw std::runtime_error("Bond principal must be 10000, 50000 or 200000 and term 100, 200 or 400 ticks");
    issueBond(principal, static_cast<uint32_t>(term));
  } else if (action == "pay_off_bond") {
    payOffBond();
  } else if (action == "services" || action == "taxes" || action == "trends" || action == "alerts" || action == "bonds") {
    showRegionalPanel=false;showTransportPanel=false;
    budgetServices = action == "services";
    budgetTrends = action == "trends";
    budgetAlerts = action == "alerts";
    budgetBonds = action == "bonds";
    showBudgetPanel = true; toolDragging = false; cameraPanning = cameraOrbiting = false;
  } else if (action == "reset") setBudgetPolicy(BudgetPolicy{});
  else if (action == "open") { showRegionalPanel=false;showTransportPanel=false; showBudgetPanel = true; toolDragging = false; cameraPanning = cameraOrbiting = false; }
  else if (action == "close") {showUniquePanel=false;showBudgetPanel=false;showRegionalPanel=false;showTransportPanel=false;}
  else if (action != "inspect") throw std::runtime_error("Unknown budget action");
  return budgetStatus();
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
    Json saveRows=Json::array();
    for(const auto& row:saveBrowser.rows) saveRows.push_back({{"name",row.name},{"backup",row.backup},{"tick",row.tick},{"width",row.width},{"height",row.height},{"valid",row.valid}});
    if (showQuitDialog)
      for (int i = 1; i <= 3; ++i)
        rect(std::array<std::string, 3>{"save_and_quit", "discard_and_quit",
                                        "cancel"}[static_cast<size_t>(i - 1)],
             quitButtonRect(static_cast<QuitAction>(i), windowWidth,
                            windowHeight));
    else if(showSaveBrowser) {
      const std::array<const char*,8> names{{"saves_close","saves_load","saves_save","saves_previous","saves_next","saves_clear","saves_name","saves_refresh"}};
      for(int i=0;i<8;++i) rect(names[size_t(i)],SaveBrowser::control(i,windowWidth,windowHeight));
      for(int i=0;i<SaveBrowser::pageSize && saveBrowser.page*SaveBrowser::pageSize+i<int(saveBrowser.rows.size());++i)
        rect("saves_row_"+std::to_string(i),SaveBrowser::control(100+i,windowWidth,windowHeight));
    }
    else if(showSettings) {
      const std::array<const char*,7> names{{"settings_close","settings_resolution","settings_display","settings_text","settings_reset","settings_help","settings_apply"}};
      for(int i=0;i<7;++i) if(!(settingsHelp && i>=1 && i<=4)) rect(names[size_t(i)],settingsButton(i,windowWidth,windowHeight));
    }
    else if (showUniquePanel) {
      if(monumentCatalog) {
        for(size_t i=0;i<monuments::count;++i)
          rect(std::string("monument_")+monuments::catalog[i].key,monumentCatalogRow(static_cast<int>(i),windowWidth,windowHeight));
      } else for (size_t i=0;i<uniques::count;++i)
          rect(std::string("unique_")+uniques::catalog[i].key,uniqueCatalogRow(static_cast<int>(i),windowWidth,windowHeight));
    }
    else if (showRegionalPanel) {
      const char* names[]={"regional_close","regional_prev","regional_next","regional_operation","regional_capacity","regional_tourism","regional_tab","regional_locate","regional_alert_next","regional_alert_manage"};
      const size_t count=regionalHotelTab ? managedHotels(store,liveState.tourism.state).size() :
        managedTerminals(liveState.regionalConnections).size();
      for (int i=0;i<10;++i) {
        rect(names[i],regionalControlRect(i,windowWidth,windowHeight));
        controls.back()["enabled"]=regionalControlEnabled(i,regionalHotelTab,count,regional::diagnose(liveState.regionalSettlement,liveState.tourism).size());
        controls.back()["focused"]=i==regionalFocus;
      }
    }
    else if (showTransportPanel) {
      const char* names[]={"transport_close","route_prev","route_next","vehicles_minus","vehicles_plus","route_coverage","route_bottleneck"};
      for (int i=0;i<7;++i) {rect(names[i],transportControlRect(i,windowWidth,windowHeight));
        controls.back()["enabled"]=transportControlEnabled(i,liveState.transitRoutes,selectedTransitRoute);
        controls.back()["focused"]=i==transportFocus;}
    }
    else if (showBudgetPanel) {
      const auto bonds = bondView();
      for (int i = 0; i < kBudgetControlCount; ++i) {
        if (!budgetControlVisible(i, budgetServices, budgetTrends, budgetAlerts, &bonds)) continue;
        if (i >= 45) {
          static const char* bondNames[] = {"budget_bonds", "bond_principal_0", "bond_principal_1", "bond_principal_2",
            "bond_term_0", "bond_term_1", "bond_term_2", "bond_issue", "bond_pay_off"};
          rect(bondNames[i - 45], budgetControlRect(i, windowWidth, windowHeight));
          continue;
        }
        if (i == 43 || i == 44) { rect(i == 43 ? "funding_11_minus" : "funding_11_plus", budgetControlRect(i, windowWidth, windowHeight, true)); continue; }
        if (i >= 37) { rect(i == 37 ? "budget_alerts" : "alert_view_" + std::to_string(i - 38), budgetControlRect(i, windowWidth, windowHeight)); continue; }
        if (i == 36) { rect("budget_trends", budgetControlRect(i, windowWidth, windowHeight)); continue; }
        rect(i == 0 ? "budget_close" : (i == 1 ? "budget_reset" :
          (i == 12 ? "budget_taxes" : (i == 13 ? "budget_services" :
            (i >= 14 ? "funding_" + std::to_string((i - 14) / 2) : "tax_" + std::to_string((i - 2) / 2)) +
              (i % 2 == 0 ? "_minus" : "_plus")))),
          budgetControlRect(i, windowWidth, windowHeight, budgetServices, budgetTrends, budgetAlerts));
      }
    } else {
      if (!cleanUiMode)
        for (int i = 1; i <= 10; ++i)
          rect(std::array<std::string, 10>{"pause", "speed_1", "speed_2",
                                          "speed_3", "save",
                                          "load", "disasters", "budget", "alerts", "transport"}[static_cast<size_t>(i - 1)],
               hudControlRect(static_cast<HudAction>(i), windowWidth));
      if (!cleanUiMode) {rect("monuments",monumentButtonRect());rect("uniques",uniqueButtonRect());rect("connections",regionalButtonRect());rect("ports",portButtonRect());rect("airport",airportButtonRect());rect("hotel",hotelButtonRect());rect("tourism",tourismButtonRect());}
      rect("settings",settingsShortcut());rect("saves",savesShortcut());
      if(!cleanUiMode && showOnboarding) {rect("guide_action",guideButton(0,windowHeight));rect("guide_help",guideButton(1,windowHeight));rect("guide_hide",guideButton(2,windowHeight));}
      for (int i = 0; i < 6; ++i)
        rect(std::array<std::string, 6>{"road", "zone", "bulldoze", "service",
                                        "district", "pipe"}[static_cast<size_t>(i)],
             paletteButtonRect(i, windowWidth, windowHeight));
      if (!cleanUiMode && selectedDistrictId != 0)
        for (int i = 1; i <= 3; ++i)
          rect("district_" + std::to_string(i),
               districtPanelButtonRect(static_cast<DistrictPanelAction>(i)));
      if (!cleanUiMode && showLegend)
        for (int i = 0; i < 9; ++i)
          rect("overlay_" + std::to_string(i),
               {kOverlayKeyStartX + i * (kOverlayKeyWidth + kOverlayKeyGap),
                kOverlayKeyY, kOverlayKeyWidth, kOverlayKeyHeight});
    }
    if (showBudgetPanel && !showTransportPanel && !showQuitDialog) for (auto& c:controls) {
      const auto bonds = bondView();
      const int action=budgetHitTest(c["x"].get<int>()+1,c["y"].get<int>()+1,windowWidth,windowHeight,budgetServices,budgetTrends,budgetAlerts,&bonds);
      c["enabled"]=budgetControlEnabled(action,liveState.budgetPolicy,budgetServices,budgetTrends,budgetAlerts,&bonds);
      c["focused"]=action==budgetFocus;
    }
    Json charts=Json::array();
    if (showBudgetPanel && budgetTrends && !budgetAlerts && !showQuitDialog) for (int i=0;i<6;++i) {
      const auto r=trendChartRect(i,windowWidth,windowHeight);
      charts.push_back({{"index",i},{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}});
    }
    return {{"save_rows",saveRows}, {"controls", controls},{"charts",charts}};
  }
  const int offset = args.value("offset", 0), limit = args.value("limit", 128);
  if (offset < 0 || limit < 1 || limit > 512)
    throw std::runtime_error("Page requires offset >= 0 and limit 1..512");
  Json items = Json::array();
  if (section == "tourism") return {{"tourism",tourismJson(liveState.tourism,liveState.tick)}};
  if (section == "regional_alerts") {
    for (const auto& alert:regional::diagnose(liveState.regionalSettlement,liveState.tourism))
      items.push_back({{"problem",regional::problemName(alert.problem)},{"tick",alert.tick},
        {"unmet",alert.unmet},{"target_id",alert.targetId},{"advice",regional::problemAdvice(alert.problem)}});
    return {{"items",items}};
  }
  if (section == "regional_report") return regionalSettlementJson(liveState.regionalSettlement);
  if (section == "diagnostics") {
    const Coord pos{args.value("x",0),args.value("y",0)};
    if (!map.isValid(pos)) throw std::runtime_error("Diagnostic coordinate must be inside the map");
    const bool burning = std::any_of(liveState.disasters.burningTiles.begin(),liveState.disasters.burningTiles.end(),
      [&](const auto& tile) { return tile.position == pos; });
    const auto d = BuildingDiagnostics::evaluate(map,roads,store,pos,liveState.demand,liveState.serviceCache,
      burning,&liveState.districtGrowthModifiers,&districts);
    const auto& n = d.neighborhood;
    Json advice = Json::array();
    for (const auto& code : d.growth.blockers) advice.push_back(BuildingDiagnostics::advice(code));
    return {{"x",pos.x},{"y",pos.y},{"tick",liveState.tick},
      {"growth",{{"occupied",d.growth.occupied},{"blockers",d.growth.blockers},{"advice",advice},
        {"road_access",d.growth.roadAccess},{"power",d.growth.power},{"water",d.growth.water},
        {"demand",d.growth.demand},{"civic_coverage",d.growth.civicCoverage},
        {"congestion",d.growth.congestion},{"location_factor",d.growth.locationFactor},
        {"scope","local gates; city balance and seeded chance still apply"}}},
      {"building",{{"capacity",d.capacity},{"occupancy",d.occupancy},{"residential",d.residential}}},
      {"services_mask",d.services},{"local_fire_response",d.fireResponse},
      {"department_served",d.departmentServed},{"department_unserved",d.departmentUnserved},
      {"neighborhood",{{"radius",n.radius},{"tiles",n.tiles},{"buildings",n.buildings},
        {"residents",n.residents},{"housing_capacity",n.housingCapacity},{"filled_jobs",n.filledJobs},
        {"job_capacity",n.jobCapacity},{"crime_rate",n.crime},{"illness_rate",n.illness},
        {"service_coverage",n.serviceCoverage}}}};
  }
  if(section=="utilities") {
    const auto funded=liveState.budgetPolicy.fundedFacilities(facilities);
    const auto result=UtilityNetworkSystem::evaluate(map,store,funded);
    Json lifts=Json::array(),boosters=Json::array();
    size_t water=0,sewer=0;const auto dims=map.getDimensions();
    for(int y=0;y<dims.y;++y) for(int x=0;x<dims.x;++x) {water+=map.utility({x,y}).waterPipe;sewer+=map.utility({x,y}).sewerPipe;
      const auto i=static_cast<size_t>(y)*dims.x+x;
      if(map.utility({x,y}).waterBooster) boosters.push_back({{"x",x},{"y",y},{"powered",result.boosterPowered[i]},{"flow",result.boosterUsed[i]},{"capacity",UtilityNetworkSystem::boosterCapacity}});
      if(map.utility({x,y}).sewageLift) lifts.push_back({{"x",x},{"y",y},{"powered",result.liftPowered[i]},{"flow",result.liftUsed[i]},{"capacity",UtilityNetworkSystem::liftCapacity}});}
    return {{"boosters",boosters},{"station_upkeep",UtilityNetworkSystem::operatingCost(map)},{"booster_upkeep",boosters.size()*UtilityNetworkSystem::boosterUpkeep},{"lifts",lifts},{"lift_upkeep",lifts.size()*UtilityNetworkSystem::liftUpkeep},{"enabled",map.pipeNetworksEnabled()},{"water_pipe_tiles",water},{"sewer_pipe_tiles",sewer},
      {"water_delivered",std::accumulate(result.sourceUsed.begin(),result.sourceUsed.end(),0.)},
      {"sewage_generated",result.generated},{"sewage_handled",result.discharged},
      {"saved_sewage_backlog",map.environment().sewageBacklog},{"projected_sewage_backlog",result.totalBacklog},
      {"pipe_capacity_m3_tick",UtilityNetworkSystem::pipeCapacity},{"pump_head_m",UtilityNetworkSystem::pumpHead}};
  }
  if(section=="environment") {
    double volume=0,mass=0;const auto dims=map.getDimensions();
    for(int z=0;z<dims.y;++z) for(int x=0;x<dims.x;++x) {volume+=map.environment({x,z}).water;mass+=map.environment({x,z}).contaminant;}
    const auto effective=liveState.budgetPolicy.fundedFacilities(facilities);
    const auto water=EnvironmentSystem::waterCapacities(map,effective);Json sites=Json::array();
    for(size_t i=0;i<effective.size();++i) {
      const auto& f=effective[i];
      sites.push_back({{"x",f.position.x},{"y",f.position.y},{"type",int(f.type)},
        {"rated_power_mw",f.powerCapacityMW},{"actual_power_mw",f.powerCapacityMW*(f.powerSource==PowerSourceType::Wind ? EnvironmentSystem::windPowerFactor(map,f.position) : 1.f)},
        {"water_capacity",water[i]},{"intake_clean_fraction",f.surfaceWater ? EnvironmentSystem::cleanFraction(map,f.position) : 1.f},
        {"drainage_factor",f.surfaceWater ? EnvironmentSystem::drainageFactor(map,f.position) : 1.f}});
    }
    return {{"environment",map.environment()},{"water_volume_m3",volume},{"contaminant_mass",mass},{"facilities",sites},
      {"units",{{"water","m3"},{"wind","m/s"},{"current","tiles/tick"}}}};
  }
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
                         {"regional_connection_id", t.regionalConnectionId},
                         {"power", t.connectedToPower},
                         {"water", t.connectedToWater},
                         {"road_access", t.connectedToRoad},
                         {"pollution", map.pollution({tx, ty})},
                         {"land_value", map.landValue({tx, ty})}});
        items.back()["environment"]=map.environment({tx,ty});
        items.back()["utility"]=map.utility({tx,ty});
        items.back()["sewer"]=t.connectedToSewer;
        items.back()["utility"]["booster_flow"]=map.utility({tx,ty}).boosterFlow;
        items.back()["utility"]["booster_powered"]=map.utility({tx,ty}).boosterPowered;
        items.back()["utility"]["pressure_margin_m"]=map.utility({tx,ty}).waterPressure;
        items.back()["utility"]["lift_flow"]=map.utility({tx,ty}).liftFlow;
        items.back()["utility"]["lift_powered"]=map.utility({tx,ty}).liftPowered;
        items.back()["utility"]["water_demand"]=map.utility({tx,ty}).waterDemand;
        items.back()["utility"]["water_delivered"]=map.utility({tx,ty}).waterDelivered;
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
      items.push_back({{"x1", roadTile(edge.from).x}, {"y1", roadTile(edge.from).y},
                       {"x2", roadTile(edge.to).x}, {"y2", roadTile(edge.to).y},
                       {"from_level",roadLevel(edge.from)},{"to_level",roadLevel(edge.to)},
                       {"road_class", roadClassToken(edge.roadClass)},
                       {"one_way",edge.oneWay},{"elevation",edge.elevation},{"length",edge.length},{"capacity", edge.capacity}, {"load", edge.currentLoad},
                       {"freight_load", edge.freightLoad},{"visitor_load",edge.visitorLoad}, {"congestion", edge.congestion}});
    }
  } else if (section == "entities") {
    std::vector<EntityId> ids;
    for (const auto &item : store.getBuildings())
      ids.push_back(item.first);
    std::sort(ids.begin(), ids.end());
    for (auto id : ids) {
      const auto &b = *store.getBuilding(id);
      std::array<double,3> served{}, unserved{};
      const ServiceType departmentTypes[] = {ServiceType::Fire,ServiceType::Police,ServiceType::Sanitation};
      for (size_t i=0;i<3;++i) {
        served[i] = std::max(0,b.occupancy) * ServiceSystem::departmentFactorAt(liveState.serviceCache,departmentTypes[i],b.position);
        unserved[i] = std::max(0,b.occupancy) - served[i];
      }
      items.push_back({{"department_served",served},{"department_unserved",unserved},{"id", id},
                       {"type", static_cast<int>(b.type)},
                       {"x", b.position.x},
                       {"y", b.position.y},
                       {"capacity", b.capacity},
                       {"hotel_beds", b.hotelBeds},
                       {"unique_kind", b.uniqueKind},
                       {"monument_kind", b.monumentKind},
                       {"hotel_open", b.hotelOpen},
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
      if (ServiceSystem::departmentIndex(f.type) >= 0) {
        const float funded = std::clamp(liveState.budgetPolicy.servicePercent[static_cast<size_t>(f.type)],0,100) / 100.f;
        items.back()["occupant_capacity"] = ServiceSystem::departmentCapacity(f.type) * std::clamp(f.quality,0.f,1.f) * funded;
      }
      if (f.type == ServiceType::Education || f.type == ServiceType::Health) {
        const size_t index = static_cast<size_t>(f.type);
        const float funded = std::clamp(liveState.budgetPolicy.servicePercent[index], 0, 100) / 100.f;
        items.back()["funded_capacity"] = (f.type == ServiceType::Education ? 800.f : 400.f)
          * std::clamp(f.quality, 0.f, 1.f) * funded;
      }
      if (f.type == ServiceType::Garbage) {
        items.back()["landfill_remaining"] = f.landfillRemaining;
        items.back()["landfill_last_fill"] = f.landfillLastFill;
      }
      if (f.type == ServiceType::Power) {
        items.back()["power_source"] = ServiceSystem::powerSourceTypeToString(f.powerSource);
        items.back()["power_capacity_mw"] = f.powerCapacityMW;
        items.back()["emissions_kg_per_mwh"] = f.emissionsKgPerMWh;
      } else if (f.type == ServiceType::Water) {
        items.back()["water_supply"] = f.waterSupplyUnits;
        if (f.surfaceWater) items.back()["source_clean_fraction"] = ServiceSystem::intakeCleanFraction(
          roads, liveState.budgetPolicy.fundedFacilities(facilities), f.position);
      }
      if (f.type == ServiceType::Incinerator) {
        const auto funded = liveState.budgetPolicy.fundedFacilities(facilities);
        ServiceCoverageCache cache;
        ServiceSystem::buildCache(roads, funded, cache);
        const auto coverage = ServiceSystem::evaluateFromCache(store, roads, cache);
        const auto waste = WasteSystem::evaluate(store, cache.wasteFacilities, coverage);
        items.back()["city_waste_incinerated"] = waste.incinerated;
        items.back()["city_waste_power_mw"] = waste.powerGenerationMW;
        items.back()["emissions_kg_per_mwh"] = waste.emissionsKgPerMWh;
      }
      items.back()["surface_water"] = f.surfaceWater;
      items.back()["sewage_treatment"] = f.sewageTreatment;
      items.back()["name"] = ServiceSystem::serviceTypeToString(f.type, f.surfaceWater, f.sewageTreatment);
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
           {"budget_cap", d.serviceBudgetCap},
           {"height_cap", d.heightCap}});
  } else if (section == "regional_connections") {
    const regional::SeaAccess sea(map);
    for (const auto& c:liveState.regionalConnections) {
      const char* names[]={"road_gateway","cargo_port","passenger_terminal","airport"};
      const auto access=regional::diagnoseOperation(c,map,roads,sea,c.entrance);
      items.push_back({{"id",c.id},{"kind",names[static_cast<int>(c.kind)]},
        {"available_passengers_per_tick",access==regional::Access::Ready ? c.passengersPerTick : 0u},
        {"available_goods_per_tick",access==regional::Access::Ready ? c.goodsPerTick : 0u},
        {"origin",{c.origin.x,c.origin.y}},{"width",c.width},{"height",c.height},
        {"entrance",{c.entrance.x,c.entrance.y}},{"enabled",c.enabled},
        {"passengers_per_tick",c.passengersPerTick},{"goods_per_tick",c.goodsPerTick},
        {"constructed",c.constructed},
        {"site_access",regional::accessName(regional::diagnoseOperation(c,map,roads,sea,c.entrance))}});
    }
  } else if (section == "routes") {
    for (const auto &r : liveState.transitRoutes) {
      Json stops = Json::array();
      for (auto c : r.stops)
        stops.push_back({c.x, c.y});
      items.push_back({{"id", r.id},
                       {"mode", static_cast<int>(r.mode)},
                       {"capacity", r.totalCapacity()},
                       {"vehicle_count",r.vehicleCount},{"capacity_per_vehicle",r.capacityPerVehicle},
                       {"operating_cost",TransitSystem::operatingCost(r)},
                       {"coverage_peak_congestion",routePeak(static_cast<size_t>(&r-liveState.transitRoutes.data())).first},
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
    const float height=args.value("height",0.f);
    if(!std::isfinite(height) || height<0 || height>16 || (!threeDimensionalView && height!=0))
      throw std::runtime_error("Height must be 0..16 and requires a 3D view");
    const auto point =
        threeDimensionalView ? camera3D().worldToScreen({x+.5f,height,y+.5f})
                      : ScreenPoint{(x - viewX) * tilePixels + tilePixels / 2,
                                    (y - viewY) * tilePixels + tilePixels / 2};
    return {{"x", point.x}, {"y", point.y}};
  }
  const bool visible=args.at("direction")=="screen_to_visible_tile";
  if (!visible && args.at("direction") != "screen_to_tile")
    throw std::runtime_error("Unknown coordinate direction");
  if (x < 0 || y < 0 || x >= windowWidth || y >= windowHeight)
    throw std::runtime_error("Screen point outside window");
  const auto tile = visible ? inspectorTileAtScreen(x,y) : tileAtScreen(x, y);
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
  if(action=="environment") {
    const double value=args.value("inflow_scale",map.environment().inflowScale);
    if(!std::isfinite(value)||value<0||value>2) throw std::runtime_error("Inflow scale must be 0..2");
    if(args.value("enabled",false) && !map.environment().initialized) {
      EnvironmentSystem::initialize(map,1000u+liveState.simulationSeed-98765u);sessionDirty=true;
    }
    if(args.contains("inflow_scale")) {
      map.environment().inflowScale=value;sessionDirty=true;
    }
  } else if (action == "overlay") {
    const int value = args.at("overlay");
    if (value < 0 || value > 11)
      throw std::runtime_error("Overlay must be 0..11");
    overlayMode = static_cast<OverlayMode>(value);
  } else if (action == "projection") {
    const auto value = args.at("projection").get<std::string>();
    if (value != "3d" && value != "perspective" && value != "isometric" && value != "top_down")
      throw std::runtime_error("Unknown projection");
    threeDimensionalView = value != "top_down";
    if(threeDimensionalView) perspectiveView = value == "perspective";
    cameraOrbiting = false;
    toolDragging = false;
    roadPlan = {};
    zonePlan = {};
    bulldozePlan = {};
  } else if (action == "camera") {
    const int scale = args.value("tile_pixels", tilePixels);
    if (scale < minimumTilePixels || scale > 48)
      throw std::runtime_error("Tile scale outside supported range");
    const int vx = args.value("view_x", viewX),
              vy = args.value("view_y", viewY);
    const float ox = args.value("offset_x", cameraOffsetX),
                oy = args.value("offset_y", cameraOffsetY);
    if (vx < 0 || vy < 0 || vx >= mapSize || vy >= mapSize ||
        !std::isfinite(ox) || !std::isfinite(oy) ||
        std::abs(ox) > 100000 || std::abs(oy) > 100000)
      throw std::runtime_error("Camera outside supported range");
    const float yaw=args.value("yaw",cameraYaw), pitch=args.value("pitch",cameraPitch);
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || yaw<0 || yaw>=360 || pitch<20 || pitch>80)
      throw std::runtime_error("Camera yaw must be 0..<360 and pitch 20..80");
    cameraYaw=yaw; cameraPitch=pitch;
    tilePixels = scale;
    viewX = vx;
    viewY = vy;
    cameraOffsetX = ox;
    cameraOffsetY = oy;
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
    const bool setArchetype = args.contains("archetype");
    DistrictArchetype archetype = DistrictArchetype::General;
    if (setArchetype &&
        !DistrictSystem::parseArchetype(args.at("archetype"), archetype))
      throw std::runtime_error("Unknown archetype");
    const bool setBudget = args.contains("budget_cap");
    const int64_t budget = setBudget ? args.at("budget_cap").get<int64_t>() : 0;
    const bool setHeight = args.contains("height_cap");
    const int height = setHeight ? args.at("height_cap").get<int>() : 0;
    if (setHeight && !liveState.milestones.unlocks(catalog::heightCaps))
      throw std::runtime_error(catalog::lockedReason(liveState.milestones, catalog::heightCaps));
    if (setHeight && !DistrictSystem::isHeightCap(height))
      throw std::runtime_error(
          "Height cap must be 0, 1, 2, 4, 8, 16, 32, or 64");
    if (setArchetype)
      districts.setDistrictArchetype(id, archetype);
    if (setBudget)
      districts.setDistrictServiceBudgetCap(id, budget);
    if (setHeight)
      districts.setDistrictHeightCap(id, height);
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
  if (name == "pipe")
    type = PaletteTool::Pipe;
  else if (name == "road")
    type = PaletteTool::Road;
  else if (name == "zone")
    type = PaletteTool::Zone;
  else if (name == "bulldoze")
    type = PaletteTool::Bulldoze;
  else if (name == "service")
    type = PaletteTool::Service;
  else if (name == "district")
    type = PaletteTool::District;
  else if (name == "port" || name == "airport" || name == "hotel" || name == "unique" || name=="monument")
    type = PaletteTool::Port;
  else if (name == "none")
    type = PaletteTool::None;
  else
    throw std::runtime_error("Unknown tool");
  int selectedPortAction=portAction;
  int portCapacity=-1;
  uint32_t requestedUnique=selectedUnique;
  uint32_t requestedMonument=selectedMonument;
  if(args.contains("monument") && name!="monument") throw std::runtime_error("Monument kind requires the monument tool");
  if (name=="monument") {
    const auto* item=monuments::spec(args.at("monument").get<std::string>());
    if(!item) throw std::runtime_error("Unknown monument");
    if(args.contains("port_action") || args.contains("capacity") || args.contains("unique"))
      throw std::runtime_error("Monument placement does not accept other catalog options");
    requestedMonument=item->id;selectedPortAction=8;
  } else if (name=="unique") {
    const auto* item=uniques::spec(args.at("unique").get<std::string>());
    if (!item) throw std::runtime_error("Unknown unique building");
    if (args.contains("port_action") || args.contains("capacity"))
      throw std::runtime_error("Unique placement does not accept terminal options");
    requestedUnique=item->id;
    selectedPortAction=7;
  } else if (args.contains("unique")) {
    throw std::runtime_error("Unique kind requires the unique tool");
  } else if (name=="hotel") {
    if (args.contains("port_action") || args.contains("capacity"))
      throw std::runtime_error("Hotel placement does not accept terminal options");
    selectedPortAction=6;
  } else if (type==PaletteTool::Port) {
    const auto mode=args.value("port_action",name=="airport" ? std::string("airport") : std::string("cargo"));
    const std::array<std::string,6> modes{{"cargo","passenger","remove","toggle","capacity","airport"}};
    const auto found=std::find(modes.begin(),modes.end(),mode);
    if (found==modes.end()) throw std::runtime_error("Unknown port action");
    selectedPortAction=static_cast<int>(found-modes.begin());
    if (args.contains("capacity")) {
      if (selectedPortAction!=4 || !args.at("capacity").is_number_integer())
        throw std::runtime_error("Capacity requires the port capacity action and an integer");
      const auto value=args.at("capacity").get<int64_t>();
      if (value<0 || value>200) throw std::runtime_error("Invalid port capacity");
      portCapacity=static_cast<int>(value);
    }
  } else if (args.contains("port_action") || args.contains("capacity"))
    throw std::runtime_error("Port options require the port tool");
  RoadClass roadClass = selectedRoadClass;
  if (args.contains("road_class")) {
    const auto value = args.at("road_class").get<std::string>();
    const std::array<std::string,4> classes{{"local","arterial","highway","ramp"}};
    const auto found=std::find(classes.begin(),classes.end(),value);
    if(type!=PaletteTool::Road || found==classes.end()) throw std::runtime_error("Unknown road class");
    roadClass=static_cast<RoadClass>(found-classes.begin());
  }
  int layout=0;
  if(args.contains("road_layout")) {
    const std::array<std::string,4> layouts{{"grid","line","curve","roundabout"}};
    const auto name=args.at("road_layout").get<std::string>();
    const auto found=std::find(layouts.begin(),layouts.end(),name);
    if(type!=PaletteTool::Road || found==layouts.end()) throw std::runtime_error("Unknown road layout");
    layout=static_cast<int>(found-layouts.begin());
  }
  const int elevation=args.value("elevation",0);
  if(elevation < -1 || elevation > 1 || (args.contains("elevation") && type!=PaletteTool::Road && type!=PaletteTool::Bulldoze))
    throw std::runtime_error("Elevation must be -1, 0 or 1 on road/bulldoze tools");
  if((int(roadClass)>=2 || elevation!=0) && layout==0) layout=1;

  ZoneType zone = selectedZone;
  ServiceType service = selectedService;
  DistrictArchetype archetype = selectedArchetype;
  if (args.contains("zone") && !Zoning::parseZoneType(args.at("zone"), zone))
    throw std::runtime_error("Unknown zone");
  if (args.contains("service") &&
      !ServiceSystem::parseServiceType(args.at("service"), service))
    throw std::runtime_error("Unknown service");
  if (args.contains("power_source")) {
    if (type != PaletteTool::Service || service != ServiceType::Power)
      throw std::runtime_error("Power source requires the Power service tool");
    PowerSourceType source = PowerSourceType::Generic;
    if (!args.at("power_source").is_string() ||
        !ServiceSystem::parsePowerSourceType(args.at("power_source").get<std::string>(), source))
      throw std::runtime_error("Unknown power source");
    selectedPowerSource = source;
  }
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
  if (args.contains("surface_water") && (type != PaletteTool::Service ||
      (args.at("surface_water").get<bool>() && service != ServiceType::Water && service != ServiceType::Sanitation)))
    throw std::runtime_error("Surface water mode requires the Water or Sanitation service tool");
  if (args.value("sewage_treatment", false) && (type != PaletteTool::Service ||
      service != ServiceType::Sanitation || !args.value("surface_water", false)))
    throw std::runtime_error("Treatment requires shoreline Sanitation");
  const std::string pipeType=args.value("pipe_kind",std::string("combined"));
  if(pipeType!="water" && pipeType!="sewer" && pipeType!="combined" && pipeType!="lift" && pipeType!="booster") throw std::runtime_error("Unknown pipe kind");
  if((args.contains("pipe_kind") || args.contains("remove")) && type!=PaletteTool::Pipe) throw std::runtime_error("Pipe options require pipe tool");
  activatePaletteTool(type);
  if(type==PaletteTool::Pipe) {pipeKind=pipeType=="booster"?5:pipeType=="lift"?4:pipeType=="water"?1:pipeType=="sewer"?2:3;pipeSnap=args.value("snapped",true);pipeRemove=args.value("remove",false);}
  portAction=selectedPortAction;
  selectedUnique=requestedUnique;
  selectedMonument=requestedMonument;
  showUniquePanel=false;
  selectedRoadClass = roadClass;
  if(type==PaletteTool::Road) {roadLayout=layout;roadSnap=args.value("snapped",true);roadClickStage=0;}
  if(type==PaletteTool::Road || type==PaletteTool::Bulldoze) roadElevation=elevation;
  selectedZone = zone;
  selectedService = service;
  if (type == PaletteTool::Service) {
    surfaceWaterTool = args.value("surface_water", false);
    sewageTreatmentTool = args.value("sewage_treatment", false);
  }
  selectedArchetype = archetype;
  if (action == "apply") {
    toolDragStart = start;
    lastActionSuccess = true;
    if(type==PaletteTool::Pipe) {
      pipePlan=PipeTool::plan(map,{args.value("start_x",start.x+.5f),args.value("start_y",start.y+.5f)},
        {args.value("end_x",end.x+.5f),args.value("end_y",end.y+.5f)},pipeKind,pipeSnap,pipeRemove,funds);
      applyPlacement(end);
    } else if(type==PaletteTool::Road && roadLayout!=0) {
      const glm::vec2 first{args.value("start_x",start.x+.5f),args.value("start_y",start.y+.5f)};
      const glm::vec2 last{args.value("end_x",end.x+.5f),args.value("end_y",end.y+.5f)};
      RoadAlignment alignment;
      if(roadLayout==3) alignment=RoadTool::roundabout(first,args.value("radius",3.f),roadClass,roadSnap);
      else if(roadLayout==2) {
        if(!args.contains("control_x") || !args.contains("control_y")) throw std::runtime_error("Curve requires control_x/control_y");
        alignment=RoadTool::curve(first,{args.at("control_x").get<float>(),args.at("control_y").get<float>()},last,roadClass,roadSnap);
      } else alignment=RoadTool::straight(first,last,roadClass,roadSnap);
      alignment.elevation=roadElevation;
      roadPlan=RoadTool::planAlignment(map,roads,alignment,funds,&facilities);
      applyPlacement(end);
    } else if (type == PaletteTool::Port)
      applyPort(start,portCapacity);
    else if (type == PaletteTool::Service)
      applyService(start);
    else
      applyPlacement(end);
    if (!lastActionSuccess)
      throw std::runtime_error(toast.message);
  }
  return Json::object();
}
throw std::runtime_error("Unknown command: " + command);
