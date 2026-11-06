#include "gtest/gtest.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "src/gameplay/Monuments.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/BuildingDiagnostics.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/persistence/ReplayVerifier.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/systems/LandValueSystem.hpp"

namespace {
struct MonumentCity {
  CityMap map{{64,64}};RoadNetwork roads{map};EntityStore store;PopulationStore people;
  std::vector<ServiceFacility> facilities{{ServiceType::Power,{42,31},64,1.f},{ServiceType::Water,{44,31},64,1.f}};
  uniques::Progress progress;int64_t funds=100000000;PlayableCityTickState state;
  MonumentCity() {
    for(int x=4;x<58;++x) roads.buildRoad({x,32},{x+1,32});
    roads.updateConnectivity({4,32});
    for(size_t i=0;i<uniques::count;++i) progress.values[i]=uniques::catalog[i].threshold;
    state.uniqueProgress=progress;
  }
  bool unique(uint32_t kind) {
    const Coord p{6+static_cast<int>(kind)*2,31};
    return UniqueBuildingTool::build(map,roads,store,facilities,progress,
      UniqueBuildingTool::plan(map,roads,store,facilities,progress,kind,p,funds),funds);
  }
  bool monument(uint32_t kind,Coord p={30,31}) {
    return MonumentTool::build(map,roads,store,facilities,MonumentTool::plan(map,roads,store,facilities,kind,p,funds),funds);
  }
  void prerequisites(uint32_t kind) {for(auto required:monuments::spec(kind)->prerequisites) ASSERT_TRUE(unique(required));}
  void refresh() {refreshDerivedCityState(map,roads,store,people,facilities,state);}
  void residents() {
    for(auto p:{Coord{38,31},Coord{50,50}}) {
      auto id=store.createBuilding(BuildingType::Residential,p,1000);store.getBuilding(id)->occupancy=500;
      map.getTile(p).buildingId=id;map.setZone(p,1);
    }
    people.createGroup(1000,0);
  }
};
struct MonumentFiles {
  std::filesystem::path dir=std::filesystem::temp_directory_path()/
    ("urban-monument-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  MonumentFiles(){std::filesystem::create_directory(dir);}
  ~MonumentFiles(){std::error_code error;std::filesystem::remove_all(dir,error);}
};
}

TEST(Monuments, AllKindsRequireBuiltSetAndRevalidatePriceFundsAndDuplicates) {
  for(const auto& item:monuments::catalog) {
    MonumentCity city;
    EXPECT_FALSE(MonumentTool::plan(city.map,city.roads,city.store,city.facilities,item.id,{30,31},city.funds).valid);
    // Earning everything alone is insufficient: all three sites must exist.
    ASSERT_TRUE(city.unique(item.prerequisites[0]));ASSERT_TRUE(city.unique(item.prerequisites[1]));
    EXPECT_FALSE(monuments::eligible(city.store,item.id));
    ASSERT_TRUE(city.unique(item.prerequisites[2]));
    auto plan=MonumentTool::plan(city.map,city.roads,city.store,city.facilities,item.id,{30,31},city.funds);
    ASSERT_TRUE(plan.valid);EXPECT_EQ(plan.cost,item.cost);
    EXPECT_GE(item.cost,2000*1850); // Retained 10k journey's median surplus.
    auto fake=plan;--fake.cost;
    EXPECT_FALSE(MonumentTool::build(city.map,city.roads,city.store,city.facilities,fake,city.funds));
    city.funds=item.cost-1;
    EXPECT_FALSE(MonumentTool::build(city.map,city.roads,city.store,city.facilities,plan,city.funds));
    city.funds=item.cost;
    ASSERT_TRUE(MonumentTool::build(city.map,city.roads,city.store,city.facilities,plan,city.funds));
    EXPECT_EQ(city.funds,0);EXPECT_EQ(monuments::upkeep(city.store),item.upkeep);
    city.funds=item.cost;
    EXPECT_FALSE(city.monument(item.id,{31,31}));EXPECT_EQ(city.funds,item.cost);
    EXPECT_EQ(city.store.countOfType(BuildingType::Office),0u);EXPECT_TRUE(city.store.jobIds().empty());
  }
}

TEST(Monuments, StalePrerequisiteAndOccupiedOrUnservicedSitesAreRefused) {
  MonumentCity city;city.prerequisites(1);
  auto plan=MonumentTool::plan(city.map,city.roads,city.store,city.facilities,1,{30,31},city.funds);ASSERT_TRUE(plan.valid);
  const auto id=city.map.getTile({8,31}).buildingId;ASSERT_TRUE(city.store.removeBuilding(id));city.map.getTile({8,31}).buildingId=0;
  EXPECT_FALSE(MonumentTool::build(city.map,city.roads,city.store,city.facilities,plan,city.funds));
  ASSERT_TRUE(city.unique(1));
  for(auto p:{Coord{-1,0},Coord{30,32},Coord{8,31},Coord{42,31},Coord{50,50}})
    EXPECT_FALSE(MonumentTool::plan(city.map,city.roads,city.store,city.facilities,1,p,city.funds).valid);
  city.map.getTile({30,31}).connectedToPower=false;
  EXPECT_FALSE(city.monument(1));city.map.getTile({30,31}).connectedToPower=true;
  city.map.setZone({30,31},1);EXPECT_FALSE(city.monument(1));
}

TEST(Monuments, HealthCoversRoadlessResidentsWithoutBedShortageAndRemovalRestoresRules) {
  MonumentCity city;city.residents();city.prerequisites(1);city.refresh();
  const auto remote=city.map.getTile({50,50}).buildingId;
  const float untreated=city.store.getBuilding(remote)->illnessRate;
  ASSERT_TRUE(city.monument(1));city.refresh();
  EXPECT_FLOAT_EQ(city.state.serviceSummary.healthCoverage,1.f);
  EXPECT_FLOAT_EQ(city.state.serviceSummary.hospitalCapacityFactor,1.f);
  EXPECT_GT(city.state.serviceSummary.patientDemand,0.f);
  EXPECT_LT(city.store.getBuilding(remote)->illnessRate,untreated);
  EXPECT_TRUE(BuildingDiagnostics::serviceMask(city.roads,city.state.serviceCache,{50,50}) & (1u<<2));
  auto plan=BulldozeTool::plan(city.map,city.roads,city.facilities,{30,31},{30,31},city.funds);
  ASSERT_TRUE(BulldozeTool::apply(city.map,city.roads,city.store,city.facilities,plan,city.funds));city.refresh();
  EXPECT_FLOAT_EQ(city.state.serviceSummary.healthCoverage,0.f);
  EXPECT_NEAR(city.store.getBuilding(remote)->illnessRate,untreated,.0001f);
}

TEST(Monuments, EducationHasSeatsEverywhereAndFeedsTheNextTickLaborSplit) {
  MonumentCity city;city.residents();city.prerequisites(2);ASSERT_TRUE(city.monument(2));city.refresh();
  EXPECT_FLOAT_EQ(city.state.serviceSummary.educationCoverage,1.f);
  EXPECT_FLOAT_EQ(city.state.serviceSummary.effectiveEducationCoverage,1.f);
  EXPECT_GE(city.state.serviceSummary.schoolSeats,city.state.serviceSummary.schoolDemand);
  EXPECT_TRUE(BuildingDiagnostics::serviceMask(city.roads,city.state.serviceCache,{50,50}) & (1u<<3));
  EXPECT_FLOAT_EQ(city.state.laggedEducationCoverage,0.f);
  playableCityTick(city.map,city.roads,city.store,city.people,city.facilities,city.state,city.funds);
  EXPECT_FLOAT_EQ(city.state.laggedEducationCoverage,1.f);
}

TEST(Monuments, SpireReplacesPlantOutputWithZeroEmissionPowerAndNeedsItsRoad) {
  MonumentCity city;city.residents();city.prerequisites(3);ASSERT_TRUE(city.monument(3));
  city.facilities.erase(city.facilities.begin());city.refresh();
  EXPECT_FLOAT_EQ(city.state.serviceSummary.powerGenerationMW,2000.f);
  EXPECT_FLOAT_EQ(city.state.serviceSummary.powerEmissionsKgPerMWh,0.f);
  EXPECT_TRUE(city.map.getTile({38,31}).connectedToPower);
  EXPECT_FLOAT_EQ(city.state.serviceSummary.powerSupplyRatio,1.f);
  city.roads.removeRoad({29,32},{30,32});city.roads.removeRoad({30,32},{31,32});city.refresh();
  EXPECT_FALSE(monuments::active(city.store,city.roads,3));
  EXPECT_FLOAT_EQ(city.state.serviceSummary.powerGenerationMW,0.f);
  EXPECT_FALSE(city.map.getTile({38,31}).connectedToPower);
}

TEST(Monuments, CleanupIsOncePerTickAndLandValueBonusReachesDistantTiles) {
  MonumentCity city;city.prerequisites(4);
  city.map.setZone({50,50},1);
  LandValueSystem::updateLandValues(city.map,city.roads,city.store,nullptr,0,0,63,63);
  const float base=city.map.landValue({50,50});
  ASSERT_TRUE(city.monument(4));
  LandValueSystem::updateLandValues(city.map,city.roads,city.store,nullptr,0,0,63,63);
  EXPECT_FLOAT_EQ(city.map.landValue({50,50})-base,25.f);
  city.map.pollution({2,2})=.8f;
  city.refresh();city.refresh();EXPECT_FLOAT_EQ(city.map.pollution({2,2}),.8f);
  PlayableCityTickOptions options;options.refreshPollution=false;
  playableCityTick(city.map,city.roads,city.store,city.people,city.facilities,city.state,city.funds,options);
  EXPECT_FLOAT_EQ(city.map.pollution({2,2}),.2f);
  city.refresh();EXPECT_FLOAT_EQ(city.map.pollution({2,2}),.2f);
  playableCityTick(city.map,city.roads,city.store,city.people,city.facilities,city.state,city.funds,options);
  EXPECT_FLOAT_EQ(city.map.pollution({2,2}),.05f);
}

TEST(Monuments, AllEffectsAndUpkeepContinueIdenticallyAcrossSaveLoad) {
  MonumentCity city;
  for(uint32_t i=1;i<=6;++i) ASSERT_TRUE(city.unique(i));
  for(uint32_t i=1;i<=4;++i) ASSERT_TRUE(city.monument(i,{28+static_cast<int>(i)*2,31}));
  city.residents();city.refresh();
  GameplaySessionState session;session.uniqueProgress=city.progress;session.funds=city.funds;session.facilities=city.facilities;
  MonumentFiles files;const auto path=(files.dir/"monuments.json").string();
  ASSERT_TRUE(GameplaySessionSystem::save(path,city.map,city.roads,city.store,city.people,session));
  for(int i=0;i<6;++i) GameplaySessionSystem::advance(city.map,city.roads,city.store,city.people,session);
  const auto expected=ReplayVerifier::calculateChecksum(city.map,city.roads,city.store,city.people);
  const auto funds=session.funds;
  ASSERT_TRUE(GameplaySessionSystem::load(path,city.map,city.roads,city.store,city.people,session));
  for(int i=0;i<6;++i) GameplaySessionSystem::advance(city.map,city.roads,city.store,city.people,session);
  EXPECT_EQ(session.funds,funds);EXPECT_EQ(ReplayVerifier::calculateChecksum(city.map,city.roads,city.store,city.people),expected);
  EXPECT_EQ(monuments::upkeep(city.store),540);
  // Prerequisites are a construction gate, not an unsavable dependency after demolition.
  const auto id=city.map.getTile({8,31}).buildingId;ASSERT_TRUE(city.store.removeBuilding(id));city.map.getTile({8,31}).buildingId=0;
  ASSERT_TRUE(GameplaySessionSystem::save(path,city.map,city.roads,city.store,city.people,session));
  ASSERT_TRUE(GameplaySessionSystem::load(path,city.map,city.roads,city.store,city.people,session));
}

TEST(Monuments, InvalidKindsDuplicatesAndUnqualifiedSessionsAreRejected) {
  MonumentCity city;city.prerequisites(1);ASSERT_TRUE(city.monument(1));
  auto snapshot=SaveLoadSystem::captureSnapshot(city.map,city.roads,city.store,city.people);
  auto found=std::find_if(snapshot.buildings.begin(),snapshot.buildings.end(),[](const auto& b){return b.monumentKind!=0;});
  ASSERT_NE(found,snapshot.buildings.end());found->monumentKind=5;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,city.map,city.roads,city.store,city.people));
  found->monumentKind=1;found->uniqueKind=1;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,city.map,city.roads,city.store,city.people));
  found->uniqueKind=0;auto duplicate=*found;duplicate.id+=100;duplicate.position={31,31};snapshot.buildings.push_back(duplicate);
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,city.map,city.roads,city.store,city.people));
  GameplaySessionState session;session.funds=city.funds;session.facilities=city.facilities;
  MonumentFiles files;
  EXPECT_FALSE(GameplaySessionSystem::save((files.dir/"bad.json").string(),city.map,city.roads,city.store,city.people,session));
}
