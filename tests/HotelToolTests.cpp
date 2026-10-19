#include "gtest/gtest.h"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include "src/gameplay/HotelTool.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/TourismAccess.hpp"

namespace {
struct HotelFiles {
  std::filesystem::path directory;
  HotelFiles() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    uint32_t suffix = 0;
    do {
      directory = std::filesystem::temp_directory_path() /
        ("urban_m34_hotel_" + std::to_string(stamp) + "_" + std::to_string(suffix++));
    } while (!std::filesystem::create_directory(directory));
  }
  ~HotelFiles() {
    std::error_code error;
    std::filesystem::remove_all(directory,error);
  }
};
struct HotelCity {
  CityMap map{{16,16}};
  RoadNetwork roads{map};
  EntityStore buildings;
  PopulationStore population;
  int64_t funds = 1540000;
  void roadLine(Coord start, Coord end) {
    while (start != end) {
      const Coord next{start.x + (end.x > start.x ? 1 : end.x < start.x ? -1 : 0),
        start.y + (end.y > start.y ? 1 : end.y < start.y ? -1 : 0)};
      roads.buildRoad(start,next); start = next;
    }
  }
  HotelCity() { roadLine({0,8},{12,8}); }
  EntityId build(Coord position = {4,7}) {
    const auto plan = HotelTool::plan(map,roads,{},position,funds);
    EXPECT_TRUE(plan.valid) << plan.error;
    EXPECT_TRUE(HotelTool::build(map,roads,buildings,{},plan,funds));
    return map.getTile(position).buildingId;
  }
};
}

TEST(HotelToolTests, PaidPlacementAddsOneJobBuildingAndSeparateBeds) {
  HotelCity city;
  const auto id = city.build();
  const auto* hotel = city.buildings.getBuilding(id);
  ASSERT_NE(hotel,nullptr);
  EXPECT_EQ(city.funds,40000);
  EXPECT_EQ(hotel->hotelBeds,32u);
  EXPECT_EQ(hotel->capacity,12);
  EXPECT_EQ(hotel->occupancy,0);
  EXPECT_EQ(city.buildings.jobIds().size(),1u);
  EXPECT_EQ(city.buildings.capacityOfType(BuildingType::Commercial),12u);
  EXPECT_EQ(PopulationSystem::summarize(city.buildings,city.population).housedPopulation,0u);
  EXPECT_EQ(city.map.zone({4,7}),0);
}

TEST(HotelToolTests, RevalidatesStalePlansWithoutSpendingOrCreatingEntities) {
  HotelCity city;
  const auto plan = HotelTool::plan(city.map,city.roads,{}, {4,7},city.funds);
  ASSERT_TRUE(plan.valid);
  city.map.getTile({4,7}).connectedToWater = false;
  EXPECT_FALSE(HotelTool::build(city.map,city.roads,city.buildings,{},plan,city.funds));
  EXPECT_EQ(city.funds,1540000); EXPECT_EQ(city.buildings.getBuildingCount(),0u);
  city.map.getTile({4,7}).connectedToWater = true;
  city.funds = 1499999;
  EXPECT_FALSE(HotelTool::build(city.map,city.roads,city.buildings,{},plan,city.funds));
  EXPECT_EQ(city.funds,1499999);
  auto forged = plan; forged.cost = 0; city.funds = 1540000;
  EXPECT_FALSE(HotelTool::build(city.map,city.roads,city.buildings,{},forged,city.funds));
}

TEST(HotelToolTests, RejectsWaterRoadZonesReservationsCivicAndDisconnectedLand) {
  HotelCity city;
  EXPECT_FALSE(HotelTool::plan(city.map,city.roads,{}, {-1,7},1540000).valid);
  EXPECT_FALSE(HotelTool::plan(city.map,city.roads,{}, {4,8},1540000).valid);
  EXPECT_FALSE(HotelTool::plan(city.map,city.roads,{}, {4,3},1540000).valid);
  auto& tile = city.map.getTile({4,7});
  tile.type = 2; EXPECT_FALSE(HotelTool::plan(city.map,city.roads,{}, {4,7},1540000).valid);
  tile.type = 0; tile.zone = 2;
  EXPECT_FALSE(HotelTool::plan(city.map,city.roads,{}, {4,7},1540000).valid);
  tile.zone = 0; tile.regionalConnectionId = 5;
  EXPECT_FALSE(HotelTool::plan(city.map,city.roads,{}, {4,7},1540000).valid);
  tile.regionalConnectionId = 0;
  EXPECT_FALSE(HotelTool::plan(city.map,city.roads,
    {{ServiceType::Fire,{4,7},10,1.0f}}, {4,7},1540000).valid);
}

TEST(HotelToolTests, ExistingEmploymentAllocatesHotelJobsOnceWithoutVisitorPopulation) {
  HotelCity city;
  const auto id = city.build();
  const auto home = city.buildings.createBuilding(BuildingType::Residential,{6,7},40);
  city.map.getTile({6,7}).buildingId = home;
  const auto summary = PopulationSystem::allocate(city.buildings,city.population,40,123);
  EXPECT_EQ(summary.availableJobs + summary.employedPopulation,12u);
  EXPECT_EQ(summary.housedPopulation,40u);
  EXPECT_LE(summary.employedPopulation,12u);
  ASSERT_NE(city.buildings.getBuilding(id),nullptr);
  EXPECT_LE(city.buildings.getBuilding(id)->occupancy,12);
  EXPECT_EQ(city.buildings.getBuilding(id)->hotelBeds,32u);
}

TEST(HotelToolTests, GrowthCannotRedevelopHotelAndOrdinaryBulldozeRemovesItsIdentity) {
  HotelCity city;
  const auto id = city.build();
  EXPECT_FALSE(ZoneTool::plan(city.map,{4,7},{4,7},ZoneType::Commercial,city.funds).valid);
  ZoneDemand demand; demand.commercial = 1.0f;
  for (uint32_t tick=1;tick<=20;++tick)
    GrowthSystem::runStep(city.map,city.buildings,demand,tick,1.0f);
  ASSERT_NE(city.buildings.getBuilding(id),nullptr);
  EXPECT_EQ(city.buildings.getBuilding(id)->capacity,12);
  std::vector<ServiceFacility> facilities;
  const auto plan = BulldozeTool::plan(city.map,city.roads,facilities,{4,7},{4,7},city.funds);
  ASSERT_TRUE(plan.valid);
  ASSERT_TRUE(BulldozeTool::apply(city.map,city.roads,city.buildings,facilities,plan,city.funds));
  EXPECT_EQ(city.buildings.getBuilding(id),nullptr);
  EXPECT_EQ(city.funds,39800);
  EXPECT_TRUE(tourism::planAccess(city.map,city.roads,city.buildings,{},50).hotels.empty());
}

TEST(HotelToolTests, SnapshotPreservesHotelAndRejectsBadCrossReferencesBeforeMutation) {
  HotelCity city; const auto id = city.build();
  HotelFiles files; const auto path = files.directory/"hotel.json";
  ASSERT_TRUE(SaveLoadSystem::saveToFile(path.string(),city.map,city.roads,city.buildings,city.population));
  nlohmann::json original; { std::ifstream input(path); input >> original; }
  CitySnapshot snapshot;
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(path.string(),snapshot));
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot,city.map,city.roads,city.buildings,city.population));
  EXPECT_EQ(city.buildings.getBuilding(id)->hotelBeds,32u);
  for (const auto& value : {nlohmann::json(-1),nlohmann::json(1.5),nlohmann::json("32"),nlohmann::json(1001)}) {
    auto bad = original; bad["buildings"][0]["hotelBeds"] = value;
    { std::ofstream output(path); output << bad; }
    EXPECT_FALSE(SaveLoadSystem::loadFromFile(path.string(),city.map,city.roads,city.buildings,city.population));
    EXPECT_EQ(city.buildings.getBuilding(id)->hotelBeds,32u);
  }
  auto invalid = snapshot; invalid.buildings.push_back(invalid.buildings[0]);
  invalid.buildings.back().hotelBeds = 0;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,city.map,city.roads,city.buildings,city.population));
  invalid = snapshot; invalid.buildings[0].type = BuildingType::Residential;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,city.map,city.roads,city.buildings,city.population));
  invalid = snapshot; invalid.buildings[0].position = {7,7};
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,city.map,city.roads,city.buildings,city.population));
  invalid = snapshot; invalid.roads.push_back({{4,7},{5,7},0.0f,RoadClass::Local,0.0f});
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,city.map,city.roads,city.buildings,city.population));
  invalid = snapshot;
  for (auto& tile : invalid.tiles) if (tile.buildingId == id) tile.zone = 2;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,city.map,city.roads,city.buildings,city.population));
  auto legacy = original; legacy["buildings"][0].erase("hotelBeds");
  legacy["buildings"][0].erase("hotelOpen");
  { std::ofstream output(path); output << legacy; }
  ASSERT_TRUE(SaveLoadSystem::loadFromFile(path.string(),city.map,city.roads,city.buildings,city.population));
  EXPECT_EQ(city.buildings.getBuilding(id)->hotelBeds,0u);
  std::filesystem::remove(path);
}

TEST(TourismAccessTests, MeasuresOnlyParksAndHotelsReachableFromRealGateway) {
  HotelCity city; const auto id = city.build();
  city.map.setZone({5,9},static_cast<int>(ZoneType::Park));
  city.map.setZone({5,3},static_cast<int>(ZoneType::Park));
  const auto plan = tourism::planAccess(city.map,city.roads,city.buildings,{},50);
  EXPECT_EQ(plan.attractions.accessibleParks,1u);
  ASSERT_EQ(plan.offers.size(),1u);
  EXPECT_EQ(plan.offers[0].connection.passengersPerTick,100u);
  EXPECT_EQ(plan.offers[0].access,regional::Access::Ready);
  ASSERT_EQ(plan.hotels.size(),1u); EXPECT_EQ(plan.hotels[0].id,id);
  ASSERT_EQ(plan.hotels[0].reachableConnections.size(),1u);
  const auto arrivals = tourism::advance({},1,plan.attractions,plan.offers,plan.hotels);
  EXPECT_EQ(arrivals.report.dayArrivals,23u);
  EXPECT_EQ(arrivals.report.overnightArrivals,7u);
  EXPECT_EQ(city.buildings.getBuilding(id)->occupancy,0);
  EXPECT_EQ(city.funds,40000);
}

TEST(TourismAccessTests, EntranceReadinessAloneDoesNotCreateAttractionAccess) {
  HotelCity city; city.build();
  city.roadLine({3,3},{8,3});
  city.map.setZone({5,2},static_cast<int>(ZoneType::Park));
  auto plan = tourism::planAccess(city.map,city.roads,city.buildings,{},100);
  EXPECT_EQ(plan.attractions.accessibleParks,0u);
  EXPECT_EQ(plan.offers[0].access,regional::Access::NoRoadPath);
  EXPECT_TRUE(plan.hotels[0].reachableConnections.empty());
  city.roadLine({3,3},{3,8});
  plan = tourism::planAccess(city.map,city.roads,city.buildings,{},100);
  EXPECT_EQ(plan.attractions.accessibleParks,1u);
  EXPECT_EQ(plan.offers[0].access,regional::Access::Ready);
  EXPECT_EQ(plan.hotels[0].reachableConnections.size(),1u);
}

TEST(TourismAccessTests, UtilityLossClosesHotelAndReleasesGuestsOnCompletedTick) {
  HotelCity city; city.build(); city.map.setZone({5,9},static_cast<int>(ZoneType::Park));
  auto plan = tourism::planAccess(city.map,city.roads,city.buildings,{},0);
  const auto first = tourism::advance({},1,plan.attractions,plan.offers,plan.hotels);
  ASSERT_EQ(first.report.overnightArrivals,5u);
  city.map.getTile({4,7}).connectedToPower = false;
  plan = tourism::planAccess(city.map,city.roads,city.buildings,{},0);
  EXPECT_FALSE(plan.hotels[0].enabled);
  const auto next = tourism::advance(first.state,2,plan.attractions,plan.offers,plan.hotels);
  EXPECT_EQ(next.report.earlyDepartures,5u);
  EXPECT_EQ(next.report.overnightArrivals,0u);
}


TEST(HotelToolTests, HotelUsesExistingCommercialAccountingWithoutSecondMaintenanceOrFee) {
  HotelCity city; const auto id = city.build();
  const auto hotelEconomy = EconomySystem::calculateEconomy(city.buildings,city.population,{},&city.map);
  city.buildings.getBuilding(id)->hotelBeds = 0;
  const auto ordinary = EconomySystem::calculateEconomy(city.buildings,city.population,{},&city.map);
  EXPECT_EQ(hotelEconomy.commercialMaintenance,ordinary.commercialMaintenance);
  EXPECT_EQ(hotelEconomy.totalExpenses,ordinary.totalExpenses);
  EXPECT_EQ(hotelEconomy.totalRevenue,ordinary.totalRevenue);
  EXPECT_EQ(hotelEconomy.goodsConsumed,ordinary.goodsConsumed);
}


TEST(HotelToolTests, SessionRejectsCivicCollisionBeforeChangingWorldOrFunds) {
  HotelCity city; const auto id = city.build();
  HotelFiles files; const auto path = files.directory/"session.json";
  GameplaySessionState session; session.funds = city.funds;
  session.facilities = {{ServiceType::Power,{8,7},22,1.0f}};
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.buildings,city.population,session));
  nlohmann::json root; { std::ifstream input(path); input >> root; }
  root["facilities"][0]["x"] = 4; root["facilities"][0]["y"] = 7;
  { std::ofstream output(path); output << root; }
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.population,session));
  EXPECT_EQ(session.funds,40000);
  ASSERT_NE(city.buildings.getBuilding(id),nullptr);
  EXPECT_EQ(city.buildings.getBuilding(id)->hotelBeds,32u);
  session.facilities[0].position = {4,7};
  EXPECT_FALSE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.buildings,city.population,session));
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".city.json");
}

TEST(TourismAccessTests, AirportNeedsAnAttractionRouteEvenWithoutAnExternalRoadGateway) {
  HotelCity city;
  for (int x=0;x<12;++x) city.roads.removeRoad({x,8},{x+1,8});
  city.roadLine({5,10},{14,10});
  regional::Connection airport;
  airport.id = 7; airport.kind = regional::Kind::Airport;
  airport.origin = {10,11}; airport.width = 4; airport.height = 2;
  airport.entrance = {10,10}; airport.passengersPerTick = 120; airport.constructed = true;
  for (int y=11;y<13;++y) for (int x=10;x<14;++x)
    city.map.getTile({x,y}).regionalConnectionId = 7;
  city.map.setZone({5,9},static_cast<int>(ZoneType::Park));
  const auto plan = tourism::planAccess(city.map,city.roads,city.buildings,{airport},100);
  ASSERT_EQ(plan.offers.size(),2u);
  EXPECT_EQ(plan.offers[0].connection.id,7u);
  EXPECT_EQ(plan.offers[0].access,regional::Access::Ready);
  const auto result = tourism::advance({},1,plan.attractions,plan.offers,plan.hotels);
  EXPECT_EQ(result.report.dayArrivals,30u);
  EXPECT_EQ(result.report.connections[0].day,30u);
  airport.enabled = false;
  const auto disabled = tourism::planAccess(city.map,city.roads,city.buildings,{airport},100);
  EXPECT_EQ(disabled.attractions.accessibleParks,1u);
  const auto closed=tourism::advance({},1,disabled.attractions,disabled.offers,disabled.hotels);
  EXPECT_EQ(closed.report.dayArrivals,0u);
  EXPECT_EQ(closed.report.unmetDayTransport,30u);
  airport.enabled=true;airport.passengersPerTick=0;
  const auto zero=tourism::planAccess(city.map,city.roads,city.buildings,{airport},100);
  EXPECT_EQ(zero.attractions.accessibleParks,1u);
  const auto saturated=tourism::advance({},1,zero.attractions,zero.offers,zero.hotels);
  EXPECT_EQ(saturated.report.dayArrivals,0u);
  EXPECT_EQ(saturated.report.unmetDayTransport,30u);
}

TEST(HotelToolTests, OpenPolicyRoundTripsDefaultsLegacyAndRejectsInvalidImportsBeforeMutation) {
  HotelCity city;const auto id=city.build();HotelFiles files;const auto path=files.directory/"policy.json";
  ASSERT_TRUE(HotelTool::setOpen(city.buildings,id,false));
  ASSERT_TRUE(SaveLoadSystem::saveToFile(path.string(),city.map,city.roads,city.buildings,city.population));
  nlohmann::json saved;{std::ifstream input(path);input>>saved;}
  ASSERT_TRUE(SaveLoadSystem::loadFromFile(path.string(),city.map,city.roads,city.buildings,city.population));
  EXPECT_FALSE(city.buildings.getBuilding(id)->hotelOpen);
  for (const auto& value:{nlohmann::json(0),nlohmann::json("false"),nlohmann::json(nullptr)}) {
    auto bad=saved;bad["buildings"][0]["hotelOpen"]=value;
    {std::ofstream output(path);output<<bad;}
    EXPECT_FALSE(SaveLoadSystem::loadFromFile(path.string(),city.map,city.roads,city.buildings,city.population));
    EXPECT_FALSE(city.buildings.getBuilding(id)->hotelOpen);
  }
  auto bad=saved;bad["buildings"][0].erase("hotelBeds");
  {std::ofstream output(path);output<<bad;}
  EXPECT_FALSE(SaveLoadSystem::loadFromFile(path.string(),city.map,city.roads,city.buildings,city.population));
  auto legacy=saved;legacy["buildings"][0].erase("hotelOpen");
  {std::ofstream output(path);output<<legacy;}
  ASSERT_TRUE(SaveLoadSystem::loadFromFile(path.string(),city.map,city.roads,city.buildings,city.population));
  EXPECT_TRUE(city.buildings.getBuilding(id)->hotelOpen);
  const auto ordinary=city.buildings.createBuilding(BuildingType::Commercial,{6,7},12);
  EXPECT_FALSE(HotelTool::setOpen(city.buildings,ordinary,false));
}
