#include "gtest/gtest.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include "src/gameplay/HotelTool.hpp"
#include "src/persistence/TourismJson.hpp"
#include "src/systems/TourismTravel.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/systems/PopulationSystem.hpp"

namespace {
void roadLine(RoadNetwork& roads,int y,int length) {
  for (int x=0;x<length-1;++x) roads.buildRoad({x,y},{x+1,y});
}
TransitRoute route(uint32_t id,int capacity) {
  TransitRoute result;result.id=id;result.stops={{0,2},{14,2}};
  result.vehicleCount=1;result.capacityPerVehicle=capacity;result.stopCoverageRadius=16;
  return result;
}
struct TravelFiles {
  std::filesystem::path directory;
  TravelFiles() {
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();uint32_t suffix=0;
    do {directory=std::filesystem::temp_directory_path()/
      ("urban_travel_"+std::to_string(stamp)+"_"+std::to_string(suffix++));}
    while (!std::filesystem::create_directory(directory));
  }
  ~TravelFiles() {std::error_code error;std::filesystem::remove_all(directory,error);}
};
}

TEST(TourismTravelTests, ResidentsReserveSeatsFirstAndVisitorsShareRemainingRoutesOnce) {
  CityMap map({16,5});RoadNetwork roads(map);roadLine(roads,2,15);
  const std::vector<TransitRoute> routes{route(1,10),route(2,5)};
  TransitCoverageCache cache;TransitSystem::buildCache(roads,routes,cache);
  const std::vector<TransitRouteUsage> residents{{1,10,8,8},{2,5,4,4}};
  roads.updateCongestion({0,2},{1,2},30);roads.addFreightLoad({0,2},{1,2},2);
  const auto result=tourism::applyTravel(1,
    {{tourism::TripKind::DayVisit,{0,2},{14,2},6},{tourism::TripKind::DayVisit,{0,2},{14,2},4}},
    roads,routes,cache,residents);
  EXPECT_EQ(result.requested,10u);EXPECT_EQ(result.transit,3u);EXPECT_EQ(result.road,7u);
  ASSERT_EQ(result.routes.size(),2u);
  EXPECT_EQ(result.routes[0].residentRiders,8u);EXPECT_EQ(result.routes[0].ridership,2u);
  EXPECT_EQ(result.routes[1].residentRiders,4u);EXPECT_EQ(result.routes[1].ridership,1u);
  EXPECT_EQ(result.routes[0].demand+result.routes[1].demand,10u);
  EXPECT_EQ(residents[0].ridership,8u);
  for (const auto& edge:roads.getAllEdgeTraffic()) {
    EXPECT_FLOAT_EQ(edge.visitorLoad,7.0f);
    if (edge.from==Coord{0,2}) {
      EXPECT_FLOAT_EQ(edge.freightLoad,2.0f);EXPECT_FLOAT_EQ(edge.currentLoad,39.0f);
    }
  }
}

TEST(TourismTravelTests, FullResidentRouteLeavesVisitorRoadFallbackWithoutChangingCommuters) {
  CityMap map({16,5});RoadNetwork roads(map);roadLine(roads,2,15);
  std::vector<TransitRoute> routes{route(1,10)};TransitCoverageCache cache;
  TransitSystem::buildCache(roads,routes,cache);
  const auto result=tourism::applyTravel(1,{{tourism::TripKind::DayVisit,{0,2},{14,2},8}},
    roads,routes,cache,{{1,10,12,10}});
  EXPECT_EQ(result.road,8u);EXPECT_EQ(result.transit,0u);
  EXPECT_EQ(result.routes[0].demand,8u);EXPECT_EQ(result.routes[0].residentRiders,10u);
}

TEST(TourismTravelTests, WalkingBrokenPathsAndMissingOriginsConsumeNoTransitOrPhantomRoadLoad) {
  CityMap map({16,5});RoadNetwork roads(map);roadLine(roads,2,15);
  std::vector<TransitRoute> routes{route(1,10)};TransitCoverageCache cache;
  TransitSystem::buildCache(roads,routes,cache);
  roads.removeRoad({7,2},{8,2});
  const auto result=tourism::applyTravel(1,{
    {tourism::TripKind::HotelAttraction,{0,2},{0,2},2},
    {tourism::TripKind::DayVisit,{0,2},{14,2},3},
    {tourism::TripKind::Departure,{-1,-1},{-1,-1},4}},roads,routes,cache,{});
  EXPECT_EQ(result.walking,2u);EXPECT_EQ(result.unrouted,7u);
  EXPECT_EQ(result.road,0u);EXPECT_EQ(result.transit,0u);
  for (const auto& edge:roads.getAllEdgeTraffic()) EXPECT_FLOAT_EQ(edge.visitorLoad,0.0f);
}

TEST(TourismTravelTests, AggregateDayHotelAndDepartureLegsReconcileWithCohorts) {
  CityMap map({16,16});RoadNetwork roads(map);roadLine(roads,8,14);
  EntityStore buildings;int64_t funds=50000;
  ASSERT_TRUE(HotelTool::build(map,roads,buildings,{},HotelTool::plan(map,roads,{}, {4,7},funds),funds));
  map.setZone({8,9},static_cast<int>(ZoneType::Park));
  const auto access=tourism::planAccess(map,roads,buildings,{},100);
  tourism::State previous;
  for (uint32_t tick=1;tick<=4;++tick) {
    const auto visitors=tourism::advance(previous,tick,access.attractions,access.offers,access.hotels);
    roads.resetCongestion();
    const auto travel=tourism::applyTravel(tick,
      tourism::planTravel(previous,visitors.state,visitors.report,access,roads),roads,{}, {}, {});
    EXPECT_EQ(travel.requested,2*visitors.report.dayArrivals+visitors.report.overnightArrivals+
      2*visitors.report.staying+visitors.report.scheduledDepartures+visitors.report.earlyDepartures);
    EXPECT_EQ(travel.road,travel.requested);EXPECT_EQ(travel.unrouted,0u);
    if (tick==1) {
      EXPECT_EQ(travel.requested,90u);
      for (const auto& edge:roads.getAllEdgeTraffic()) {
        if (edge.from==Coord{0,8}) { EXPECT_FLOAT_EQ(edge.visitorLoad,70.0f); }
        if (edge.from==Coord{5,8}) { EXPECT_FLOAT_EQ(edge.visitorLoad,80.0f); }
      }
    }
    if (tick==4) {EXPECT_EQ(visitors.report.scheduledDepartures,10u);EXPECT_EQ(travel.requested,140u);}
    previous=visitors.state;
  }
}

TEST(TourismTravelTests, InvalidBatchesAndReservationsRejectBeforeAddingLoads) {
  CityMap map({16,5});RoadNetwork roads(map);roadLine(roads,2,15);
  std::vector<TransitRoute> routes{route(1,10)};TransitCoverageCache cache;
  TransitSystem::buildCache(roads,routes,cache);
  const std::vector<tourism::TravelLeg> valid{{tourism::TripKind::DayVisit,{0,2},{14,2},4}};
  EXPECT_THROW(tourism::applyTravel(1,valid,roads,routes,cache,{{1,10,11,11}}),std::invalid_argument);
  EXPECT_THROW(tourism::applyTravel(1,valid,roads,routes,cache,{{999,10,1,1}}),std::invalid_argument);
  auto huge=valid;huge.push_back({tourism::TripKind::DayVisit,{0,2},{14,2},8000});
  EXPECT_THROW(tourism::applyTravel(1,huge,roads,routes,cache,{}),std::invalid_argument);
  for (const auto& edge:roads.getAllEdgeTraffic()) EXPECT_FLOAT_EQ(edge.currentLoad,0.0f);
}

TEST(TourismTravelTests, TravelSaveValidatesJointCapacityCountsCoordinatesAndLegacyAbsence) {
  CityMap map({16,5});RoadNetwork roads(map);roadLine(roads,2,15);
  std::vector<TransitRoute> routes{route(1,10)};TransitCoverageCache cache;
  TransitSystem::buildCache(roads,routes,cache);
  TransitReport resident{true,1,8,{{1,10,8,8}}};
  const auto travel=tourism::applyTravel(1,{{tourism::TripKind::DayVisit,{0,2},{14,2},10}},
    roads,routes,cache,resident.routes);
  tourism::Report visitors;visitors.tick=1;visitors.dayArrivals=5;
  const auto data=tourismTravelJson(travel);
  EXPECT_EQ(tourismTravelJson(parseTourismTravel(data,visitors,16,5,&resident)),data);
  auto bad=data;bad["routes"][0]["residentRiders"]=9;
  EXPECT_THROW(parseTourismTravel(bad,visitors,16,5,&resident),std::runtime_error);
  bad=data;bad["routes"][0]["capacity"]=99;
  EXPECT_THROW(parseTourismTravel(bad,visitors,16,5,&resident),std::runtime_error);
  bad=data;bad["legs"][0]["destination"]["x"]=-1;
  EXPECT_THROW(parseTourismTravel(bad,visitors,16,5,&resident),std::runtime_error);
  bad=data;bad["road"]=9;
  EXPECT_THROW(parseTourismTravel(bad,visitors,16,5,&resident),std::runtime_error);
  bad=data;bad["legs"][0]["kind"]=3;
  EXPECT_THROW(parseTourismTravel(bad,visitors,16,5,&resident),std::runtime_error);
  tourism::Runtime legacy;legacy.state.tick=1;
  auto saved=tourismJson(legacy,1);saved.erase("travel");
  CitySnapshot snapshot;snapshot.width=16;snapshot.height=5;
  EXPECT_FALSE(parseTourism(saved,1,snapshot).travel.measured);
}

TEST(TourismTravelTests, SnapshotSeparatesVisitorFreightAndOtherLoadsAndRejectsCorruptSplits) {
  CityMap map({16,5});RoadNetwork roads(map);roadLine(roads,2,15);
  EntityStore buildings;PopulationStore people;TravelFiles files;const auto path=files.directory/"core.json";
  roads.updateCongestion({0,2},{1,2},4);roads.addFreightLoad({0,2},{1,2},2);roads.addVisitorLoad({0,2},{1,2},3);
  ASSERT_TRUE(SaveLoadSystem::saveToFile(path.string(),map,roads,buildings,people));
  CitySnapshot saved;ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(path.string(),saved));
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(saved,map,roads,buildings,people));
  auto edge=roads.getAllEdgeTraffic()[0];EXPECT_FLOAT_EQ(edge.currentLoad,9);EXPECT_FLOAT_EQ(edge.visitorLoad,3);
  EXPECT_FLOAT_EQ(edge.freightLoad,2);
  auto bad=saved;bad.roads[0].visitorLoad=8;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(bad,map,roads,buildings,people));
  EXPECT_FLOAT_EQ(roads.getAllEdgeTraffic()[0].visitorLoad,3);
  nlohmann::json legacy;{std::ifstream input(path);input>>legacy;}
  for (auto& row:legacy["roads"]) row.erase("visitorLoad");
  {std::ofstream output(path);output<<legacy;}
  ASSERT_TRUE(SaveLoadSystem::loadFromFile(path.string(),map,roads,buildings,people));
  EXPECT_FLOAT_EQ(roads.getAllEdgeTraffic()[0].currentLoad,9);
  EXPECT_FLOAT_EQ(roads.getAllEdgeTraffic()[0].visitorLoad,0);
}

TEST(TourismTravelTests, SharedTickKeepsResidentPriorityAndPausedRefreshDoesNotDuplicateVisitorLoads) {
  CityMap map({16,16});RoadNetwork roads(map);roadLine(roads,8,14);
  EntityStore buildings;PopulationStore people;int64_t cash=100000;
  std::vector<ServiceFacility> facilities{{ServiceType::Power,{6,7},22,1.0f},{ServiceType::Water,{7,7},20,1.0f}};
  ASSERT_TRUE(HotelTool::build(map,roads,buildings,facilities,HotelTool::plan(map,roads,facilities,{4,7},cash),cash));
  const auto home=buildings.createBuilding(BuildingType::Residential,{2,7},80);
  const auto work=buildings.createBuilding(BuildingType::Commercial,{10,7},50);
  map.getTile({2,7}).buildingId=home;map.getTile({10,7}).buildingId=work;
  map.setZone({8,9},static_cast<int>(ZoneType::Park));
  PopulationSystem::allocate(buildings,people,40,100);
  PlayableCityTickState state;state.tourism.enabled=true;
  auto bus=route(1,10);bus.stops={{0,8},{13,8}};state.transitRoutes={bus};
  PlayableCityTickOptions options;options.growthChance=0;
  playableCityTick(map,roads,buildings,people,facilities,state,cash,options);
  ASSERT_EQ(state.transitReport.routes.size(),1u);
  EXPECT_EQ(state.transitReport.routes[0].ridership,10u);
  EXPECT_EQ(state.tourism.travel.transit,0u);EXPECT_GT(state.tourism.travel.road,0u);
  EXPECT_EQ(state.tourism.travel.routes[0].residentRiders,10u);
  const auto before=roads.getAllEdgeTraffic();const auto record=tourismJson(state.tourism,state.tick);
  refreshDerivedCityState(map,roads,buildings,people,facilities,state);
  const auto firstProjection=roads.getAllEdgeTraffic();
  for (int i=0;i<3;++i) refreshDerivedCityState(map,roads,buildings,people,facilities,state);
  const auto after=roads.getAllEdgeTraffic();ASSERT_EQ(before.size(),after.size());
  for (size_t i=0;i<before.size();++i) {
    EXPECT_FLOAT_EQ(before[i].visitorLoad,after[i].visitorLoad);
    // Resident routing may change with a refresh seed, but repeated identical
    // projections must not accumulate either resident or visitor traffic.
    EXPECT_FLOAT_EQ(firstProjection[i].currentLoad,after[i].currentLoad);
  }
  EXPECT_EQ(tourismJson(state.tourism,state.tick),record);
}
