#include "gtest/gtest.h"
#include "src/gameplay/TerminalTool.hpp"
#include "src/gameplay/PortTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/persistence/RegionalSettlementJson.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include <filesystem>
#include <fstream>

namespace {
struct AirportCity {
  CityMap map{{24,24}};RoadNetwork roads{map};EntityStore store;PopulationStore people;
  std::vector<regional::Connection> connections;int64_t funds=1825000;
  AirportCity() {
    for (int x=0;x<20;++x) {roads.buildRoad({x,9},{x+1,9});roads.buildRoad({x,12},{x+1,12});}
    for (int y=9;y<12;++y) roads.buildRoad({9,y},{9,y+1});
  }
  TerminalPlan plan() const {return TerminalTool::plan(map,roads,connections,{10,10},regional::Kind::Airport,1,{{2,8}},funds);}
  bool build() {const auto p=plan();return p.valid && TerminalTool::build(map,roads,connections,p.connection,p.destination,{{2,8}},funds);}
  std::vector<ServiceFacility> facilities() const {
    ServiceFacility p;p.type=ServiceType::Power;p.position={2,8};p.maxTravelDistance=40;p.powerCapacityMW=10000;
    ServiceFacility w;w.type=ServiceType::Water;w.position={3,8};w.maxTravelDistance=40;w.waterSupplyUnits=10000;
    return {p,w};
  }
};
}
TEST(AirportTests, PaidInlandConstructionReservesAllEightTilesAndAddsNoCargoGateway) {
  AirportCity city;const auto p=city.plan();ASSERT_TRUE(p.valid)<<p.error;
  EXPECT_EQ(p.cost,1800000);EXPECT_EQ(p.tiles.size(),8u);EXPECT_TRUE(city.connections.empty());
  ASSERT_TRUE(city.build());EXPECT_EQ(city.funds,25000);
  ASSERT_EQ(city.connections.size(),1u);const auto& c=city.connections[0];
  EXPECT_EQ(c.kind,regional::Kind::Airport);EXPECT_EQ(c.passengersPerTick,120u);EXPECT_EQ(c.goodsPerTick,0u);
  for (int y=10;y<12;++y) for (int x=10;x<14;++x) EXPECT_EQ(city.map.getTile({x,y}).regionalConnectionId,1u);
  EXPECT_FALSE(regional::hasConstructedCargoPort(city.connections));
  EXPECT_EQ(regional::planLogistics(city.map,city.roads,city.connections).gateways.size(),1u);
  EXPECT_EQ(regional::portOperatingCost(city.connections),0);EXPECT_EQ(regional::regionalOperatingCost(city.connections),30);
  const auto report=regional::measurePassengerReadiness(1,city.map,city.roads,city.connections,0.01);
  ASSERT_EQ(report.allocation.connections.size(),1u);
  EXPECT_EQ(report.allocation.connections[0].availablePassengers,120u);
  EXPECT_EQ(report.allocation.connections[0].availableGoods,0u);
  EXPECT_EQ(report.delivered.passengers,0u);EXPECT_EQ(report.allocation.requested.passengers,0u);
}
TEST(AirportTests, PlansAndExecutionRejectWaterMissingUtilitiesAndOccupiedLandWithoutPayment) {
  AirportCity city;const auto p=city.plan();ASSERT_TRUE(p.valid);
  city.map.getTile({13,11}).connectedToWater=false;
  EXPECT_FALSE(TerminalTool::build(city.map,city.roads,city.connections,p.connection,p.destination,{{2,8}},city.funds));
  EXPECT_FALSE(city.plan().valid);EXPECT_EQ(city.funds,1825000);EXPECT_TRUE(city.connections.empty());
  city.map.getTile({13,11}).connectedToWater=true;city.map.getTile({13,11}).type=2;
  EXPECT_FALSE(city.plan().valid);city.map.getTile({13,11}).type=0;
  city.map.getTile({12,10}).zone=1;EXPECT_FALSE(city.plan().valid);
  city.map.getTile({12,10}).zone=0;city.funds=1799999;EXPECT_FALSE(city.plan().valid);
}
TEST(AirportTests, FootprintEditingGuardsAndWholeRemovalAreAtomicEvenWhenDisconnected) {
  AirportCity city;ASSERT_TRUE(city.build());
  const auto zone=ZoneTool::plan(city.map,{10,10},{13,11},ZoneType::Residential,city.funds);
  EXPECT_FALSE(zone.valid);
  EXPECT_FALSE(PortTool::remove(city.map,city.connections,1,city.funds));
  city.map.getTile({13,11}).regionalConnectionId=0;
  EXPECT_FALSE(TerminalTool::remove(city.map,city.connections,1,regional::Kind::Airport,city.funds));
  EXPECT_EQ(city.funds,25000);EXPECT_EQ(city.connections.size(),1u);
  city.map.getTile({13,11}).regionalConnectionId=1;city.map.getTile({10,10}).connectedToPower=false;
  city.funds=999;EXPECT_FALSE(TerminalTool::remove(city.map,city.connections,1,regional::Kind::Airport,city.funds));
  city.funds=25000;ASSERT_TRUE(TerminalTool::remove(city.map,city.connections,1,regional::Kind::Airport,city.funds));
  EXPECT_EQ(city.funds,24000);EXPECT_TRUE(city.connections.empty());
  for (int y=10;y<12;++y) for (int x=10;x<14;++x) EXPECT_EQ(city.map.getTile({x,y}).regionalConnectionId,0u);
}
TEST(AirportTests, CompletedTicksMeasureReadinessAndBillUpkeepWithoutActivatingCargoOrVisitors) {
  AirportCity city;ASSERT_TRUE(city.build());
  PlayableCityTickState state;state.regionalConnections=city.connections;
  PlayableCityTickOptions options;options.enableTransit=false;options.growthChance=0;
  playableCityTick(city.map,city.roads,city.store,city.people,city.facilities(),state,city.funds,options);
  EXPECT_FALSE(state.regionalLogisticsEnabled);EXPECT_EQ(city.people.getTotalPopulation(),0u);
  EXPECT_EQ(city.funds,24897);EXPECT_EQ(state.treasuryLedger.breakdown.expenses[3],103);
  ASSERT_TRUE(state.regionalSettlement.valid);
  EXPECT_EQ(state.regionalSettlement.allocation.connections[0].availablePassengers,120u);
  EXPECT_EQ(state.regionalSettlement.operatingExpense,30);EXPECT_EQ(state.regionalSettlement.delivered.passengers,0u);
  const auto before=regionalSettlementJson(state.regionalSettlement);const auto cash=city.funds;
  refreshDerivedCityState(city.map,city.roads,city.store,city.people,city.facilities(),state);
  EXPECT_EQ(regionalSettlementJson(state.regionalSettlement),before);EXPECT_EQ(city.funds,cash);
  state.regionalConnections[0].enabled=false;
  playableCityTick(city.map,city.roads,city.store,city.people,city.facilities(),state,city.funds,options);
  EXPECT_EQ(city.funds,cash-73);
  EXPECT_EQ(state.regionalSettlement.allocation.connections[0].availablePassengers,0u);
  EXPECT_EQ(state.regionalSettlement.operatingExpense,0);
  state.regionalConnections[0].enabled=true;state.regionalConnections[0].passengersPerTick=30;
  playableCityTick(city.map,city.roads,city.store,city.people,city.facilities(),state,city.funds,options);
  EXPECT_EQ(state.regionalSettlement.allocation.connections[0].availablePassengers,30u);
  EXPECT_FALSE(state.regionalLogisticsEnabled);
}
TEST(AirportTests, SessionRoundTripRebuildsAirportAndRejectsBadShapeBeforeMutation) {
  AirportCity city;ASSERT_TRUE(city.build());GameplaySessionState session;
  session.regionalConnections=city.connections;session.funds=city.funds;session.facilities=city.facilities();
  const auto path=std::filesystem::temp_directory_path()/"urban_airport_session.json";
  ASSERT_TRUE(GameplaySessionSystem::save(path.string(),city.map,city.roads,city.store,city.people,session));
  nlohmann::json valid;{std::ifstream input(path);input>>valid;}
  GameplaySessionState loaded;
  ASSERT_TRUE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.store,city.people,loaded));
  EXPECT_EQ(loaded.nextRegionalConnectionId,2u);EXPECT_FALSE(loaded.regionalLogisticsEnabled);
  EXPECT_EQ(city.map.getTile({13,11}).regionalConnectionId,1u);
  auto bad=valid;bad["regionalConnections"][0]["width"]=2;
  {std::ofstream output(path);output<<bad;}
  loaded.funds=123;
  EXPECT_FALSE(GameplaySessionSystem::load(path.string(),city.map,city.roads,city.store,city.people,loaded));
  EXPECT_EQ(loaded.funds,123);EXPECT_EQ(city.map.getTile({13,11}).regionalConnectionId,1u);
  std::filesystem::remove(path);std::filesystem::remove(path.string()+".city.json");
}
TEST(AirportTests, MeasuredCapacityRoundTripsAndRejectsOverAdmissionAndPartialMetadata) {
  regional::Connection c;c.id=1;c.kind=regional::Kind::Airport;c.passengersPerTick=10;
  regional::Settlement report;
  ASSERT_TRUE(regional::settle(regional::allocate(1,{5,0,0},{{c,regional::Access::Ready}}),{{1,{5,0,0}}},{},report));
  const auto valid=regionalSettlementJson(report);
  EXPECT_EQ(regionalSettlementJson(parseRegionalSettlement(valid,1)),valid);
  auto bad=valid;bad["allocation"]["connections"][0]["availablePassengers"]=4;
  EXPECT_THROW(parseRegionalSettlement(bad,1),std::exception);
  bad=valid;bad["allocation"]["connections"][0].erase("availableGoods");
  EXPECT_THROW(parseRegionalSettlement(bad,1),std::exception);
  auto legacy=valid;legacy["allocation"]["connections"][0].erase("availableGoods");
  legacy["allocation"]["connections"][0].erase("availablePassengers");
  EXPECT_EQ(regionalSettlementJson(parseRegionalSettlement(legacy,1)),legacy);
}
TEST(AirportTests, RunwayTerminalAndTowerUseBoundedGeometryInBothProjections) {
  for (bool iso:{false,true}) {
    visualizer::DrawList list;
    for (int y=0;y<2;++y) for (int x=0;x<4;++x)
      visualizer::drawAirportTile(&list,{100,100},48,iso ? 24 : 48,iso,true,x,y);
    ASSERT_FALSE(list.vertices.empty());
    for (const auto& v:list.vertices) {EXPECT_GE(v.x,76);EXPECT_LE(v.x,148);EXPECT_GE(v.y,60);EXPECT_LE(v.y,148);}
  }
}

TEST(AirportTests, OperationalReadinessDropsToZeroWhenEntranceOrUtilitiesAreLost) {
  AirportCity city;ASSERT_TRUE(city.build());
  const auto c=city.connections[0];
  city.roads.removeRoad(c.entrance,{c.entrance.x-1,c.entrance.y});
  city.roads.removeRoad(c.entrance,{c.entrance.x+1,c.entrance.y});
  auto report=regional::measurePassengerReadiness(1,city.map,city.roads,city.connections,0.01);
  ASSERT_EQ(report.allocation.connections.size(),1u);
  EXPECT_EQ(report.allocation.connections[0].access,regional::Access::MissingEntrance);
  EXPECT_EQ(report.allocation.connections[0].availablePassengers,0u);
  EXPECT_EQ(report.delivered.passengers,0u);
  city.roads.buildRoad(c.entrance,{c.entrance.x-1,c.entrance.y});
  city.map.getTile({13,11}).connectedToPower=false;
  report=regional::measurePassengerReadiness(2,city.map,city.roads,city.connections,0.01);
  EXPECT_EQ(report.allocation.connections[0].access,regional::Access::MissingPower);
  EXPECT_EQ(report.allocation.connections[0].availablePassengers,0u);
  EXPECT_EQ(report.operatingExpense,30); // Fixed upkeep continues through access outages.
}
