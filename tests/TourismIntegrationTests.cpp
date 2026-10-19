#include "gtest/gtest.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include "src/gameplay/HotelTool.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/persistence/TourismJson.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/systems/TourismAccess.hpp"

namespace {
struct VisitorCity {
  CityMap map{{16,16}}; RoadNetwork roads{map}; EntityStore buildings; PopulationStore people;
  std::vector<ServiceFacility> facilities{{ServiceType::Power,{6,7},22,1.0f},
    {ServiceType::Water,{7,7},20,1.0f}};
  PlayableCityTickState state; int64_t cash=1590000;
  VisitorCity() {
    for (int x=0;x<13;++x) roads.buildRoad({x,8},{x+1,8});
    map.setZone({4,9},static_cast<int>(ZoneType::Park));
    const auto plan=HotelTool::plan(map,roads,facilities,{4,7},cash);
    EXPECT_TRUE(HotelTool::build(map,roads,buildings,facilities,plan,cash));
    state.tourism.enabled=true;
  }
  void tick() {playableCityTick(map,roads,buildings,people,facilities,state,cash);}
  CitySnapshot snapshot() {return SaveLoadSystem::captureSnapshot(map,roads,buildings,people);}
};
struct VisitorFiles {
  std::filesystem::path directory;
  VisitorFiles() {
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    uint32_t suffix=0;
    do {directory=std::filesystem::temp_directory_path()/
      ("urban_tourism_"+std::to_string(stamp)+"_"+std::to_string(suffix++));}
    while (!std::filesystem::create_directory(directory));
  }
  ~VisitorFiles() {std::error_code error;std::filesystem::remove_all(directory,error);}
};
}

TEST(TourismIntegrationTests, SharedTicksConserveStaysAndReconcileOnlyExplicitMunicipalAmounts) {
  VisitorCity enabled,disabled; disabled.state.tourism.enabled=false;
  uint32_t previous=0;int64_t accumulatedNet=0;
  for (int tick=0;tick<8;++tick) {
    enabled.tick();disabled.tick();
    const auto& report=enabled.state.tourism.report;
    EXPECT_GT(report.dayArrivals,0u);EXPECT_GT(report.overnightArrivals,0u);
    EXPECT_EQ(previous+report.overnightArrivals,
      report.staying+report.scheduledDepartures+report.earlyDepartures);
    EXPECT_LE(report.staying,32u);
    EXPECT_EQ(enabled.people.getTotalPopulation(),0u);
    EXPECT_EQ(enabled.people.getTotalEmployed(),0u);
    const auto& accounting=enabled.state.tourism.accounting;
    accumulatedNet+=accounting.fees()-accounting.services;
    EXPECT_EQ(enabled.cash-disabled.cash,accumulatedNet);
    EXPECT_EQ(enabled.state.treasuryRevenue-disabled.state.treasuryRevenue,accounting.fees());
    EXPECT_EQ(enabled.state.treasuryExpenses-disabled.state.treasuryExpenses,accounting.services);
    EXPECT_EQ(enabled.state.treasuryLedger.breakdown.tourismFees,accounting.fees());
    EXPECT_EQ(enabled.state.treasuryLedger.breakdown.tourismServices,accounting.services);
    EXPECT_EQ(enabled.state.history.samples.back().revenue,enabled.state.treasuryRevenue);
    EXPECT_EQ(enabled.state.history.samples.back().population,0u);
    previous=report.staying;
  }
}

TEST(TourismIntegrationTests, PausedRefreshDoesNotAdmitDepartBillOrRewriteReport) {
  VisitorCity city;city.tick();
  const auto before=tourismJson(city.state.tourism,city.state.tick);const auto cash=city.cash;
  for (int i=0;i<3;++i) refreshDerivedCityState(city.map,city.roads,city.buildings,city.people,city.facilities,city.state);
  EXPECT_EQ(tourismJson(city.state.tourism,city.state.tick),before);
  EXPECT_EQ(city.cash,cash);EXPECT_EQ(city.state.tick,1u);
  city.state.tourism.enabled=false;
  EXPECT_EQ(tourismJson(city.state.tourism,1).at("report"),before.at("report"));
  city.tick();EXPECT_EQ(city.state.tourism.report.dayArrivals,0u);
  EXPECT_EQ(city.state.tourism.report.staying,0u);
  EXPECT_EQ(city.state.tourism.report.earlyDepartures,before["report"]["staying"].get<uint32_t>());
}

TEST(TourismIntegrationTests, PausedDemolitionReleasesReferencesAndBooksDeparturesExactlyOnce) {
  VisitorCity city;city.tick();
  const auto staying=city.state.tourism.report.staying;ASSERT_GT(staying,0u);
  const auto id=city.map.getTile({4,7}).buildingId;
  city.buildings.removeBuilding(id);city.map.getTile({4,7}).buildingId=0;
  refreshDerivedCityState(city.map,city.roads,city.buildings,city.people,city.facilities,city.state);
  EXPECT_TRUE(city.state.tourism.state.stays.empty());
  EXPECT_EQ(city.state.tourism.state.pendingEarlyDepartures,staying);
  const auto data=tourismJson(city.state.tourism,city.state.tick);
  EXPECT_NO_THROW(parseTourism(data,city.state.tick,city.snapshot()));
  refreshDerivedCityState(city.map,city.roads,city.buildings,city.people,city.facilities,city.state);
  EXPECT_EQ(city.state.tourism.state.pendingEarlyDepartures,staying);
  city.tick();EXPECT_EQ(city.state.tourism.report.earlyDepartures,staying);
  EXPECT_EQ(city.state.tourism.state.pendingEarlyDepartures,0u);
  EXPECT_EQ(city.state.tourism.report.overnightArrivals,0u);
  EXPECT_GT(city.state.tourism.report.unmetOvernightBeds,0u);
  city.tick();EXPECT_EQ(city.state.tourism.report.earlyDepartures,0u);
}

TEST(TourismIntegrationTests, StrictPersistenceRejectsTamperedCountsReferencesAndMeasurements) {
  VisitorCity city;city.tick();const auto snapshot=city.snapshot();
  const auto valid=tourismJson(city.state.tourism,1);
  auto impossible=city.state.tourism.state;impossible.pendingEarlyDepartures=1500;
  EXPECT_THROW(tourism::advance(impossible,2,{}, {}, {}),std::invalid_argument);
  EXPECT_EQ(tourismJson(parseTourism(valid,1,snapshot),1),valid);
  for (const char* field:{"staying","dayArrivals","dayDepartures","overnightArrivals","requestedDay",
       "requestedOvernight","parkContribution","qualityContribution","unmetDayTransport"}) {
    auto bad=valid;bad["report"][field]=valid["report"][field].get<uint32_t>()+1;
    EXPECT_THROW(parseTourism(bad,1,snapshot),std::runtime_error) << field;
  }
  for (const auto& value:{nlohmann::json(-1),nlohmann::json(1.5),nlohmann::json("5"),nlohmann::json(1001)}) {
    auto bad=valid;bad["state"]["stays"][0]["guests"]=value;
    EXPECT_THROW(parseTourism(bad,1,snapshot),std::runtime_error);
  }
  auto bad=valid;bad["state"]["stays"][0]["hotelId"]=999;
  EXPECT_THROW(parseTourism(bad,1,snapshot),std::runtime_error);
  bad=valid;bad["state"]["stays"].push_back(bad["state"]["stays"][0]);
  EXPECT_THROW(parseTourism(bad,1,snapshot),std::runtime_error);
  bad=valid;bad["report"]["connections"][0]["capacity"]=0;
  EXPECT_THROW(parseTourism(bad,1,snapshot),std::runtime_error);
  bad=valid;bad["state"]["tick"]=2;
  EXPECT_THROW(parseTourism(bad,1,snapshot),std::runtime_error);
  bad=valid;bad["report"]["enabled"]=false;
  EXPECT_THROW(parseTourism(bad,1,snapshot),std::runtime_error);
}

TEST(TourismIntegrationTests, SessionRestoresCohortsAndRejectsBeforeReplacingWorldAndCash) {
  VisitorCity city;city.tick();VisitorFiles files;const auto path=files.directory/"session.json";
  GameplaySessionState session;session.tick=1;session.tourism=city.state.tourism;session.treasuryLedger=city.state.treasuryLedger;
  session.funds=city.cash;session.facilities=city.facilities;
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.buildings,city.people,session));
  const auto before=tourismJson(session.tourism,1);const auto cash=session.funds;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.people,session));
  EXPECT_EQ(tourismJson(session.tourism,1),before);
  nlohmann::json valid;{std::ifstream input(path);input>>valid;}
  auto bad=valid;bad["funds"]=1;bad["tourism"]["state"]["stays"][0]["hotelId"]=999;
  {std::ofstream output(path);output<<bad;}
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.people,session));
  EXPECT_EQ(session.funds,cash);EXPECT_EQ(tourismJson(session.tourism,1),before);
  auto legacy=valid;legacy.erase("tourism");legacy.erase("treasuryLedger");{std::ofstream output(path);output<<legacy;}
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.people,session));
  EXPECT_FALSE(session.tourism.enabled);EXPECT_TRUE(session.tourism.state.stays.empty());
  EXPECT_EQ(session.tourism.state.tick,1u);
  EXPECT_NO_THROW(GameplaySessionSystem::advance(city.map,city.roads,city.buildings,city.people,session));
}

TEST(TourismIntegrationTests, HotelClosureIsPausedPolicyAndReleasesStaysOnceBeforeReopening) {
  VisitorCity city;city.tick();
  const auto id=city.map.getTile({4,7}).buildingId;
  const auto record=tourismJson(city.state.tourism,city.state.tick);
  const auto guests=city.state.tourism.report.staying;
  ASSERT_GT(guests,0u);
  const auto cash=city.cash;
  const auto capacity=city.buildings.getBuilding(id)->capacity;
  ASSERT_TRUE(HotelTool::setOpen(city.buildings,id,false));
  EXPECT_FALSE(HotelTool::setOpen(city.buildings,0,false));
  refreshDerivedCityState(city.map,city.roads,city.buildings,city.people,city.facilities,city.state);
  EXPECT_EQ(tourismJson(city.state.tourism,city.state.tick),record);
  EXPECT_EQ(city.cash,cash);
  EXPECT_EQ(city.buildings.getBuilding(id)->capacity,capacity);
  EXPECT_FALSE(tourism::planAccess(city.map,city.roads,city.buildings,{},100).hotels[0].enabled);
  VisitorFiles files;const auto path=files.directory/"closed.json";
  GameplaySessionState session;session.tick=city.state.tick;session.tourism=city.state.tourism;session.treasuryLedger=city.state.treasuryLedger;
  session.transitReport=city.state.transitReport;session.facilities=city.facilities;session.funds=city.cash;
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.buildings,city.people,session));
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.people,loaded));
  EXPECT_FALSE(city.buildings.getBuilding(id)->hotelOpen);
  EXPECT_EQ(tourismJson(loaded.tourism,loaded.tick),record);
  city.tick();
  EXPECT_EQ(city.state.tourism.report.earlyDepartures,guests);
  EXPECT_EQ(city.state.tourism.report.overnightArrivals,0u);
  EXPECT_EQ(city.state.tourism.report.staying,0u);
  EXPECT_GT(city.state.tourism.report.dayArrivals,0u);
  city.tick();EXPECT_EQ(city.state.tourism.report.earlyDepartures,0u);
  ASSERT_TRUE(HotelTool::setOpen(city.buildings,id,true));city.tick();
  EXPECT_GT(city.state.tourism.report.overnightArrivals,0u);
}

TEST(TourismIntegrationTests, AccountingSaveRejectsChargesAndLedgerTamperingBeforeMutationAndLoadsLegacy) {
  VisitorCity city;city.tick();VisitorFiles files;const auto path=files.directory/"account.json";
  GameplaySessionState session;session.tick=city.state.tick;session.funds=city.cash;
  session.facilities=city.facilities;session.tourism=city.state.tourism;
  session.treasuryLedger=city.state.treasuryLedger;session.transitReport=city.state.transitReport;
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.buildings,city.people,session));
  nlohmann::json valid;{std::ifstream input(path);input>>valid;}
  const auto before=tourismJson(session.tourism,session.tick);const auto funds=session.funds;
  for (const char* name:{"dayFees","stayFees","services"}) {
    auto bad=valid;bad["funds"]=1;bad["tourism"]["accounting"][name]=9999;
    {std::ofstream output(path);output<<bad;}
    EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.people,session));
    EXPECT_EQ(session.funds,funds);EXPECT_EQ(tourismJson(session.tourism,session.tick),before);
  }
  auto bad=valid;bad["tourism"].erase("accounting");
  {std::ofstream output(path);output<<bad;}
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.people,session));
  bad=valid;bad["treasuryLedger"]["tourismFees"]=0;
  {std::ofstream output(path);output<<bad;}
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.people,session));
  auto legacy=valid;legacy["tourism"].erase("accounting");legacy.erase("treasuryLedger");
  {std::ofstream output(path);output<<legacy;}
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.buildings,city.people,session));
  EXPECT_FALSE(session.tourism.accounting.measured);EXPECT_EQ(session.funds,funds);
  EXPECT_NO_THROW(GameplaySessionSystem::advance(city.map,city.roads,city.buildings,city.people,session));
  EXPECT_TRUE(session.tourism.accounting.measured);
  EXPECT_EQ(session.treasuryLedger.breakdown.tourismFees,session.tourism.accounting.fees());
}
