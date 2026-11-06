#include "gtest/gtest.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "src/gameplay/UniqueBuildings.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/persistence/ReplayVerifier.hpp"
#include "src/systems/LandValueSystem.hpp"
#include "src/systems/PlayableCityTick.hpp"

namespace {
struct UniqueCity {
  CityMap map{{32,32}};
  RoadNetwork roads{map};
  EntityStore store;
  PopulationStore population;
  std::vector<ServiceFacility> facilities;
  uniques::Progress progress;
  int64_t funds=10000000;
  UniqueCity() {
    for (int x=4;x<27;++x) roads.buildRoad({x,16},{x+1,16});
    roads.updateConnectivity({4,16});
    for (size_t i=0;i<uniques::count;++i) progress.values[i]=uniques::catalog[i].threshold;
  }
  bool build(uint32_t kind,Coord position) {
    return UniqueBuildingTool::build(map,roads,store,facilities,progress,
      UniqueBuildingTool::plan(map,roads,store,facilities,progress,kind,position,funds),funds);
  }
};
struct UniqueFiles {
  std::filesystem::path dir=std::filesystem::temp_directory_path()/
    ("urban-unique-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  UniqueFiles() {std::filesystem::create_directory(dir);}
  ~UniqueFiles() {std::error_code error;std::filesystem::remove_all(dir,error);}
};
}

TEST(UniqueBuildings, AchievementPeaksAndDeliveredTotalsArePermanentAndBounded) {
  uniques::Progress progress;
  progress.observeCurrent(499999,4999,2499);
  for (auto id:{1u,5u,6u}) EXPECT_FALSE(progress.unlocked(id));
  progress.observeCurrent(500000,5000,2500);
  progress.observeCurrent(0,0,0);
  for (auto id:{1u,5u,6u}) EXPECT_TRUE(progress.unlocked(id));
  for (size_t i:{1u,2u,3u}) {
    progress.add(i,uniques::catalog[i].threshold-1);
    EXPECT_FALSE(progress.unlocked(static_cast<uint32_t>(i+1)));
    progress.add(i,UINT64_MAX);
    EXPECT_TRUE(progress.unlocked(static_cast<uint32_t>(i+1)));
    EXPECT_EQ(progress.values[i],uniques::catalog[i].threshold);
  }
  EXPECT_FALSE(progress.unlocked(0));EXPECT_FALSE(progress.unlocked(7));
}

TEST(UniqueBuildings, EveryKindRevalidatesLocksPriceFundsAndDuplicatePlacement) {
  for (const auto& item:uniques::catalog) {
    UniqueCity city;
    auto locked=city.progress;locked.values[item.id-1]=item.threshold-1;
    EXPECT_FALSE(UniqueBuildingTool::plan(city.map,city.roads,city.store,city.facilities,locked,item.id,{8,15},city.funds).valid);
    EXPECT_FALSE(UniqueBuildingTool::plan(city.map,city.roads,city.store,city.facilities,city.progress,item.id,{8,15},item.cost-1).valid);
    auto proposed=UniqueBuildingTool::plan(city.map,city.roads,city.store,city.facilities,city.progress,item.id,{8,15},city.funds);
    ASSERT_TRUE(proposed.valid);EXPECT_EQ(proposed.cost,item.cost);
    --proposed.cost;
    EXPECT_FALSE(UniqueBuildingTool::build(city.map,city.roads,city.store,city.facilities,city.progress,proposed,city.funds));
    ++proposed.cost;
    EXPECT_FALSE(UniqueBuildingTool::build(city.map,city.roads,city.store,city.facilities,locked,proposed,city.funds));
    city.funds=item.cost;
    ASSERT_TRUE(UniqueBuildingTool::build(city.map,city.roads,city.store,city.facilities,city.progress,proposed,city.funds));
    EXPECT_EQ(city.funds,0);
    city.funds=item.cost;
    EXPECT_FALSE(city.build(item.id,{9,15}));EXPECT_EQ(city.funds,item.cost);
    EXPECT_EQ(uniques::upkeep(city.store),item.upkeep);
    EXPECT_EQ(city.store.capacityOfType(BuildingType::Office),0u);
    EXPECT_EQ(city.store.countOfType(BuildingType::Office),0u);
    EXPECT_TRUE(city.store.jobIds().empty());
    DistrictSystem districts;
    const auto district= districts.createDistrict("CIVIC",{0,0},{31,31});
    const auto metrics=districts.evaluateDistrictMetrics(district,city.map,city.store,city.population);
    EXPECT_EQ(metrics.buildings,1u);EXPECT_EQ(metrics.officeBuildings,0u);
  }
}

TEST(UniqueBuildings, SiteRulesAndDemolitionRebuildPreserveOwnership) {
  UniqueCity city;
  auto plan=[&](Coord p){return UniqueBuildingTool::plan(city.map,city.roads,city.store,city.facilities,city.progress,1,p,city.funds);};
  EXPECT_FALSE(plan({-1,0}).valid);EXPECT_FALSE(plan({8,16}).valid);EXPECT_FALSE(plan({8,8}).valid);
  city.map.getTile({8,15}).type=2;EXPECT_FALSE(plan({8,15}).valid);city.map.getTile({8,15}).type=0;
  city.map.getTile({8,15}).connectedToPower=false;EXPECT_FALSE(plan({8,15}).valid);city.map.getTile({8,15}).connectedToPower=true;
  ServiceFacility solar;solar.type=ServiceType::Power;solar.powerSource=PowerSourceType::Solar;solar.position={7,14};
  city.facilities.push_back(solar);EXPECT_FALSE(plan({8,15}).valid);city.facilities.clear();
  ASSERT_TRUE(city.build(1,{8,15}));
  EXPECT_FALSE(ZoneTool::plan(city.map,{8,15},{8,15},ZoneType::Office,city.funds).valid);
  EXPECT_FALSE(RoadTool::plan(city.map,city.roads,{8,15},{9,15},city.funds).valid);
  auto demolition=BulldozeTool::plan(city.map,city.roads,city.facilities,{8,15},{8,15},city.funds);
  ASSERT_TRUE(BulldozeTool::apply(city.map,city.roads,city.store,city.facilities,demolition,city.funds));
  EXPECT_EQ(uniques::upkeep(city.store),0);
  EXPECT_TRUE(city.progress.unlocked(1));EXPECT_TRUE(city.build(1,{9,15}));
}

TEST(UniqueBuildings, LocalValueTapersDoesNotStackAndStopsWithoutUtilities) {
  UniqueCity city;
  city.map.setZone({9,15},static_cast<int>(ZoneType::Residential));
  city.map.setZone({23,15},static_cast<int>(ZoneType::Residential));
  auto refresh=[&](){LandValueSystem::updateLandValues(city.map,city.roads,city.store,nullptr,0,0,31,31);};
  refresh();const float near=city.map.landValue({9,15}),far=city.map.landValue({23,15});
  ASSERT_TRUE(city.build(1,{8,15}));refresh();
  EXPECT_NEAR(city.map.landValue({9,15})-near,20.f*5.f/6.f,.001f);
  EXPECT_FLOAT_EQ(city.map.landValue({23,15}),far);
  const auto economy=EconomySystem::calculateEconomy(city.store,city.population);
  EXPECT_EQ(economy.officeTaxRevenue,0);EXPECT_EQ(economy.officeMaintenance,0);
  EXPECT_EQ(economy.totalMaintenance,1200);
  ASSERT_TRUE(city.build(6,{10,15}));refresh();
  EXPECT_NEAR(city.map.landValue({9,15})-near,40.f*7.f/8.f,.001f);
  city.map.getTile({8,15}).connectedToPower=false;
  city.map.getTile({10,15}).connectedToWater=false;refresh();
  EXPECT_FLOAT_EQ(city.map.landValue({9,15}),near);
  city.facilities={{ServiceType::Power,{18,15},30,1.f},{ServiceType::Water,{20,15},30,1.f}};
  PlayableCityTickState state;
  refreshDerivedCityState(city.map,city.roads,city.store,city.population,city.facilities,state);
  EXPECT_TRUE(city.map.getTile({8,15}).connectedToPower);
  EXPECT_TRUE(city.map.getTile({8,15}).connectedToWater);
  EXPECT_EQ(uniques::activeEffects(city.map,city.roads,city.store).size(),2u);
  EXPECT_EQ(state.serviceSummary.totalBuildings,0u);
}

TEST(UniqueBuildings, SaveRoundTripKeepsUnlocksSitesAndDeterministicContinuation) {
  UniqueCity city;ASSERT_TRUE(city.build(1,{8,15}));ASSERT_TRUE(city.build(6,{10,15}));
  UniqueFiles files;const auto path=(files.dir/"session.json").string();
  GameplaySessionState session;session.uniqueProgress=city.progress;session.funds=city.funds;
  ASSERT_TRUE(GameplaySessionSystem::save(path,city.map,city.roads,city.store,city.population,session));
  for(int i=0;i<4;++i) GameplaySessionSystem::advance(city.map,city.roads,city.store,city.population,session);
  const auto checksum=ReplayVerifier::calculateChecksum(city.map,city.roads,city.store,city.population);
  const auto funds=session.funds;
  ASSERT_TRUE(GameplaySessionSystem::load(path,city.map,city.roads,city.store,city.population,session));
  EXPECT_EQ(session.uniqueProgress.values,city.progress.values);
  EXPECT_TRUE(uniques::built(city.store,1));EXPECT_TRUE(uniques::built(city.store,6));
  for(int i=0;i<4;++i) GameplaySessionSystem::advance(city.map,city.roads,city.store,city.population,session);
  EXPECT_EQ(session.funds,funds);
  EXPECT_EQ(ReplayVerifier::calculateChecksum(city.map,city.roads,city.store,city.population),checksum);
  nlohmann::json root;{std::ifstream in(path);in>>root;}
  root["uniqueProgress"][1]=-1;{std::ofstream out(path);out<<root;}
  EXPECT_FALSE(GameplaySessionSystem::load(path,city.map,city.roads,city.store,city.population,session));
  root["uniqueProgress"][1]=50001;{std::ofstream out(path);out<<root;}
  EXPECT_FALSE(GameplaySessionSystem::load(path,city.map,city.roads,city.store,city.population,session));
}

TEST(UniqueBuildings, MalformedSpecializationsAndDuplicateKindsAreRejected) {
  UniqueCity city;ASSERT_TRUE(city.build(1,{8,15}));
  auto original=SaveLoadSystem::captureSnapshot(city.map,city.roads,city.store,city.population);
  auto invalid=original;invalid.buildings.front().uniqueKind=7;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,city.map,city.roads,city.store,city.population));
  invalid=original;invalid.buildings.front().capacity=1;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,city.map,city.roads,city.store,city.population));
  invalid=original;auto duplicate=invalid.buildings.front();duplicate.id+=100;duplicate.position={9,15};invalid.buildings.push_back(duplicate);
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,city.map,city.roads,city.store,city.population));
}

TEST(UniqueBuildings, CompletedTicksCountDeliveredActivityAndRefreshDoesNotCountItAgain) {
  UniqueCity city;
  city.progress={};
  for(int x=0;x<4;++x) city.roads.buildRoad({x,16},{x+1,16});
  city.map.setZone({5,15},static_cast<int>(ZoneType::Park));
  city.facilities={{ServiceType::Power,{18,15},30,1.f},{ServiceType::Water,{20,15},30,1.f}};
  const auto home=city.store.createBuilding(BuildingType::Residential,{8,15},1000);
  const auto job=city.store.createBuilding(BuildingType::Industrial,{22,15},500);
  city.store.getBuilding(home)->occupancy=800;
  city.store.getBuilding(job)->occupancy=500;
  city.map.getTile({8,15}).buildingId=home;city.map.setZone({8,15},1);
  city.map.getTile({22,15}).buildingId=job;city.map.setZone({22,15},3);
  city.population.createGroup(800,500);
  PlayableCityTickState state;state.tourism.enabled=true;
  TransitRoute route;route.id=1;route.stops={{8,16},{22,16}};route.vehicleCount=4;route.capacityPerVehicle=100;
  state.transitRoutes.push_back(route);
  playableCityTick(city.map,city.roads,city.store,city.population,city.facilities,state,city.funds);
  EXPECT_GT(state.economy.exportedGoods,0);
  EXPECT_EQ(state.uniqueProgress.values[2],static_cast<uint64_t>(state.economy.exportedGoods));
  uint64_t carried=0;for(const auto& usage:state.transitSummary.routeUsage) carried+=usage.ridership;
  EXPECT_GT(carried,0u);EXPECT_EQ(state.uniqueProgress.values[1],carried);
  const auto visitors=state.tourism.report.dayArrivals+state.tourism.report.overnightArrivals;
  EXPECT_GT(visitors,0u);EXPECT_EQ(state.uniqueProgress.values[3],visitors);
  const auto progress=state.uniqueProgress.values;
  refreshDerivedCityState(city.map,city.roads,city.store,city.population,city.facilities,state);
  EXPECT_EQ(state.uniqueProgress.values,progress);
  state.transitRoutes.front().mode=TransitMode::Rail;
  playableCityTick(city.map,city.roads,city.store,city.population,city.facilities,state,city.funds);
  EXPECT_EQ(state.uniqueProgress.values[1],carried); // Rail never earns the bus achievement.
}
