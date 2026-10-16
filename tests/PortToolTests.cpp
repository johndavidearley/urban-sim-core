#include "gtest/gtest.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "src/gameplay/PortTool.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/CitySimSupport.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"

namespace {
struct PortCity {
  CityMap map{{12,12}}; RoadNetwork roads{map}; EntityStore store; PopulationStore people;
  std::vector<regional::Connection> ports; int64_t funds=50000;
  PortCity() {
    for (int x=0;x<10;++x) map.getTile({x,2}).type=2;
    for (int x=0;x<10;++x) roads.buildRoad({x,4},{x+1,4});
  }
  regional::Connection candidate() const {
    regional::Connection c; c.id=17; c.kind=regional::Kind::CargoPort;
    c.origin={4,3}; c.width=2; c.height=1; c.entrance={4,4}; return c;
  }
  bool build() {return PortTool::build(map,roads,ports,candidate(),{10,4},{},funds);}
};
}

TEST(PortToolTests, PaidConstructionCreatesOwnedFootprintAndFixedCapacity) {
  PortCity city; auto c=city.candidate(); c.goodsPerTick=UINT32_MAX;
  ASSERT_TRUE(PortTool::build(city.map,city.roads,city.ports,c,{10,4},{},city.funds));
  EXPECT_EQ(city.funds,35000); ASSERT_EQ(city.ports.size(),1u);
  EXPECT_TRUE(city.ports[0].constructed); EXPECT_EQ(city.ports[0].goodsPerTick,200u);
  EXPECT_EQ(city.map.getTile({4,3}).regionalConnectionId,17u);
  EXPECT_EQ(city.map.getTile({5,3}).regionalConnectionId,17u);
  EXPECT_EQ(city.map.getTile({4,3}).buildingId,0u);
  EXPECT_FALSE(RegionalConnectionTool::remove(17,0,city.ports,city.funds));
  EXPECT_EQ(city.funds,35000); EXPECT_EQ(city.ports.size(),1u);
  EXPECT_FALSE(city.build()); EXPECT_EQ(city.funds,35000);
  auto passenger=city.candidate(); passenger.id=18; passenger.origin={7,3};
  passenger.entrance={7,4}; passenger.kind=regional::Kind::PassengerTerminal;
  ASSERT_TRUE(PortTool::build(city.map,city.roads,city.ports,passenger,{10,4},{},city.funds));
  EXPECT_EQ(city.funds,25000); EXPECT_EQ(city.ports[1].passengersPerTick,60u);
  EXPECT_EQ(city.ports[1].goodsPerTick,0u);
}

TEST(PortToolTests, StalePlansAndOrdinaryEditsCannotOverwriteConstructedPorts) {
  PortCity city; city.map.setZone({3,3},1);
  const auto road=RoadTool::plan(city.map,city.roads,{3,3},{6,3},city.funds);
  const auto zone=ZoneTool::plan(city.map,{4,3},{5,3},ZoneType::Residential,city.funds);
  const auto service=ServiceTool::plan(city.map,city.roads,{},ServiceType::Power,{4,3},city.funds);
  const auto bulldoze=BulldozeTool::plan(city.map,city.roads,{}, {3,3},{5,3},city.funds);
  ASSERT_TRUE(road.valid); ASSERT_TRUE(zone.valid); ASSERT_TRUE(service.valid); ASSERT_TRUE(bulldoze.valid);
  ASSERT_TRUE(city.build()); const auto cash=city.funds;
  std::vector<ServiceFacility> facilities;
  EXPECT_FALSE(RoadTool::build(city.map,city.roads,road,city.funds));
  EXPECT_FALSE(ZoneTool::apply(city.map,zone,city.funds));
  EXPECT_FALSE(ServiceTool::build(city.map,city.roads,facilities,service,city.funds));
  EXPECT_FALSE(BulldozeTool::apply(city.map,city.roads,city.store,facilities,bulldoze,city.funds));
  EXPECT_EQ(city.funds,cash); EXPECT_EQ(city.map.zone({3,3}),1);
  EXPECT_EQ(city.map.getTile({4,3}).regionalConnectionId,17u);
  EXPECT_FALSE(ZoneTool::plan(city.map,{4,3},{5,3},ZoneType::Park,city.funds).valid);
}

TEST(PortToolTests, DirectAndAutonomousRoadZoningAndGrowthRespectReservationIndex) {
  PortCity city; ASSERT_TRUE(city.build());
  city.map.setZone({4,3},1); EXPECT_EQ(city.map.zone({4,3}),0);
  city.roads.buildRoad({3,3},{4,3}); EXPECT_FALSE(city.roads.hasRoad({3,3},{4,3}));
  city_sim::layRoadGrid(city.map,city.roads,{6,6},6,2);
  EXPECT_FALSE(city.map.getTile({4,3}).hasRoad);
  city.map.getTile({4,3}).zone=1; // Even an invalid raw zone cannot bypass growth.
  GrowthSystem::runStep(city.map,city.store,{1,1,1,1},42,1.0f);
  EXPECT_EQ(city.map.getTile({4,3}).buildingId,0u);
  const auto diagnosis=GrowthSystem::diagnoseSite(city.map,city.roads,city.store,{4,3},{1,1,1,1});
  EXPECT_NE(std::find(diagnosis.blockers.begin(),diagnosis.blockers.end(),"regional_terminal"),diagnosis.blockers.end());
}

TEST(PortToolTests, FailedRemovalPreservesOwnershipAndSuccessfulRemovalReleasesOnlyItsSite) {
  PortCity city; ASSERT_TRUE(city.build());
  city.funds=499;
  EXPECT_FALSE(PortTool::remove(city.map,city.ports,17,city.funds));
  EXPECT_EQ(city.funds,499); EXPECT_EQ(city.ports.size(),1u);
  city.funds=35000; city.map.getTile({5,3}).regionalConnectionId=99;
  EXPECT_FALSE(PortTool::remove(city.map,city.ports,17,city.funds));
  EXPECT_EQ(city.funds,35000); EXPECT_EQ(city.map.getTile({5,3}).regionalConnectionId,99u);
  city.map.getTile({5,3}).regionalConnectionId=17;
  ASSERT_TRUE(PortTool::remove(city.map,city.ports,17,city.funds));
  EXPECT_EQ(city.funds,34500); EXPECT_TRUE(city.ports.empty());
  EXPECT_EQ(city.map.getTile({4,3}).regionalConnectionId,0u);
  EXPECT_EQ(city.map.getTile({5,3}).regionalConnectionId,0u);
  EXPECT_TRUE(ZoneTool::plan(city.map,{4,3},{5,3},ZoneType::Residential,city.funds).valid);
}

TEST(PortToolTests, GameplayLoadRebuildsDerivedReservationsAndLegacyLoadClearsThem) {
  PortCity city; ASSERT_TRUE(city.build());
  GameplaySessionState session; session.regionalConnections=city.ports; session.funds=city.funds;
  const auto path=std::filesystem::temp_directory_path()/"urban_physical_ports.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.store,city.people,session));
  nlohmann::json root; {std::ifstream input(path); input>>root;}
  nlohmann::json core; {std::ifstream input(path.string()+".city.json"); input>>core;}
  EXPECT_FALSE(core["tiles"][0].contains("regionalConnectionId"));
  city.map.getTile({1,1}).regionalConnectionId=99;
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.store,city.people,loaded));
  EXPECT_EQ(city.map.getTile({1,1}).regionalConnectionId,0u);
  EXPECT_EQ(city.map.getTile({4,3}).regionalConnectionId,17u);
  root.erase("regionalConnections"); {std::ofstream output(path); output<<root;}
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.store,city.people,loaded));
  EXPECT_EQ(city.map.getTile({4,3}).regionalConnectionId,0u);
  EXPECT_TRUE(loaded.regionalConnections.empty());
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".city.json");
}

TEST(PortToolTests, ConflictingSavedFootprintsRejectBeforeDestinationWorldMutation) {
  PortCity city; ASSERT_TRUE(city.build());
  GameplaySessionState session; session.regionalConnections=city.ports;
  const auto path=std::filesystem::temp_directory_path()/"urban_port_collision.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.store,city.people,session));
  nlohmann::json core; {std::ifstream input(path.string()+".city.json"); input>>core;}
  for (auto& tile:core["tiles"]) if (tile["x"]==4 && tile["y"]==3) tile["zone"]=1;
  {std::ofstream output(path.string()+".city.json"); output<<core;}
  city.store.createBuilding(BuildingType::Residential,{1,1},10);
  session.funds=12345;
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.store,city.people,session));
  EXPECT_EQ(city.store.getBuildingCount(),1u); EXPECT_EQ(session.funds,12345);
  EXPECT_EQ(city.map.getTile({4,3}).regionalConnectionId,17u);
  for (auto& tile:core["tiles"]) if (tile["x"]==4 && tile["y"]==3) tile["zone"]=0;
  core["roads"].push_back({{"from",{{"x",4},{"y",3}}},{"to",{{"x",4},{"y",4}}},
    {"currentLoad",0.0},{"roadClass",0},{"freightLoad",0.0}});
  {std::ofstream output(path.string()+".city.json"); output<<core;}
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.store,city.people,session));
  EXPECT_EQ(city.store.getBuildingCount(),1u); EXPECT_EQ(city.map.getTile({4,3}).regionalConnectionId,17u);
  core["roads"].erase(core["roads"].size()-1);
  core["buildings"].push_back({{"id",123},{"type",1},{"position",{{"x",4},{"y",3}}},
    {"capacity",10},{"occupancy",0},{"crimeRate",0.0},{"illnessRate",0.0}});
  core["nextEntityId"]=124;
  {std::ofstream output(path.string()+".city.json"); output<<core;}
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.store,city.people,session));
  EXPECT_EQ(city.store.getBuildingCount(),1u); EXPECT_EQ(session.funds,12345);
  // Save also rejects a conflicting physical site before writing a snapshot.
  city.map.getTile({4,3}).zone=1;
  EXPECT_FALSE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.store,city.people,session));
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".city.json");
}

TEST(PortToolTests, PreviewChoosesRealCivicRoadEntranceWithoutMutationAndExecutionRechecks) {
  PortCity city;
  const auto plan=PortTool::plan(city.map,city.roads,city.ports,{4,3},regional::Kind::CargoPort,17,{{1,3}},city.funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_TRUE(plan.error.empty());
  EXPECT_EQ(plan.connection.entrance,Coord(4,4));
  EXPECT_EQ(plan.destination,Coord(1,4));
  EXPECT_EQ(plan.tiles.size(),2u); EXPECT_EQ(plan.cost,15000);
  EXPECT_EQ(city.funds,50000); EXPECT_TRUE(city.ports.empty());
  EXPECT_EQ(city.map.getTile({4,3}).regionalConnectionId,0u);
  city.map.getTile({5,3}).connectedToWater=false;
  EXPECT_FALSE(PortTool::build(city.map,city.roads,city.ports,plan.connection,
    plan.destination,{{1,3}},city.funds));
  EXPECT_EQ(city.funds,50000); EXPECT_TRUE(city.ports.empty());
}

TEST(PortToolTests, PreviewRejectsNoCityDestinationBlockedSiteInsufficientFundsAndExhaustedIDs) {
  PortCity city;
  EXPECT_FALSE(PortTool::plan(city.map,city.roads,city.ports,{4,3},regional::Kind::CargoPort,17,{},city.funds).valid);
  EXPECT_FALSE(PortTool::plan(city.map,city.roads,city.ports,{4,3},regional::Kind::CargoPort,17,{{1,3}},14999).valid);
  EXPECT_FALSE(PortTool::plan(city.map,city.roads,city.ports,{4,3},regional::Kind::CargoPort,
    static_cast<uint64_t>(UINT32_MAX)+1,{{1,3}},city.funds).valid);
  EXPECT_FALSE(PortTool::plan(city.map,city.roads,city.ports,{4,3},regional::Kind::Airport,17,{{1,3}},city.funds).valid);
  ASSERT_TRUE(city.build());
  EXPECT_FALSE(PortTool::plan(city.map,city.roads,city.ports,{4,3},regional::Kind::PassengerTerminal,18,{{1,3}},city.funds).valid);
}
