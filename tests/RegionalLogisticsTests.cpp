#include "gtest/gtest.h"
#include "src/systems/RegionalLogistics.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/gameplay/PortTool.hpp"
#include "src/persistence/RegionalSettlementJson.hpp"
#include "src/gameplay/TreasurySystem.hpp"
#include <cmath>

namespace {
struct City {
  CityMap map{{32,16}}; RoadNetwork roads{map}; EntityStore store; PopulationStore people;
  std::vector<regional::Connection> ports;
  City() {
    for (int x=0;x<25;++x) roads.buildRoad({x,8},{x+1,8});
    for (int x=0;x<23;++x) map.getTile({x,6}).type=2;
  }
  regional::Connection candidate(uint32_t id=17) const {
    regional::Connection c; c.id=id; c.kind=regional::Kind::CargoPort;
    c.origin={18,7}; c.width=2; c.height=1; c.entrance={18,8}; return c;
  }
  void build() {
    int64_t cash=1850000;
    ASSERT_TRUE(PortTool::build(map,roads,ports,candidate(),{25,8},{},cash));
  }
  void add(BuildingType type,Coord p,int occupancy) {
    const auto id=store.createBuilding(type,p,1000);
    store.getBuilding(id)->occupancy=occupancy; map.getTile(p).buildingId=id;
  }
};
}
TEST(RegionalLogisticsTests, OnlyConstructedCargoAddsAReadyGatewayAndIDsCannotCollide) {
  City city; city.ports.push_back(city.candidate(UINT32_MAX));
  auto plan=regional::planLogistics(city.map,city.roads,city.ports);
  ASSERT_EQ(plan.gateways.size(),1u);
  EXPECT_EQ(plan.gateways[0].id,UINT32_MAX-1);
  EXPECT_EQ(plan.gateways[0].goodsPerTick,100);
  EXPECT_EQ(plan.offers.back().access,regional::Access::NotConstructed);
  EXPECT_FALSE(regional::hasConstructedCargoPort(city.ports));
  city.ports.clear(); city.build();
  plan=regional::planLogistics(city.map,city.roads,city.ports);
  ASSERT_EQ(plan.gateways.size(),2u);
  EXPECT_TRUE(plan.gateways[0].enabled);
  EXPECT_EQ(plan.gateways[0].id,17u);
  EXPECT_TRUE(regional::hasConstructedCargoPort(city.ports));
  EXPECT_EQ(regional::portOperatingCost(city.ports),20);
  city.ports[0].kind=regional::Kind::PassengerTerminal;
  city.ports[0].goodsPerTick=0; city.ports[0].passengersPerTick=60;
  EXPECT_FALSE(regional::hasConstructedCargoPort(city.ports));
  EXPECT_EQ(regional::planLogistics(city.map,city.roads,city.ports).gateways.size(),1u);
  EXPECT_EQ(regional::portOperatingCost(city.ports),10);
  city.ports[0].enabled=false;
  EXPECT_EQ(regional::portOperatingCost(city.ports),0);
}
TEST(RegionalLogisticsTests, PortAndRoadCapacityLimitActualTradeAndUnavailablePortFallsBack) {
  City city; city.build(); city.add(BuildingType::Commercial,{24,7},300);
  auto plan=regional::planLogistics(city.map,city.roads,city.ports);
  auto freight=FreightSystem::simulate(city.map,city.roads,city.store,nullptr,{},&plan.gateways);
  EXPECT_EQ(freight.importedGoods,300); EXPECT_EQ(freight.unroutedGoods,150);
  EXPECT_EQ(freight.gateways[0].importedGoods,200);
  EXPECT_EQ(freight.gateways[1].importedGoods,100);
  city.map.getTile({19,7}).connectedToWater=false;
  plan=regional::planLogistics(city.map,city.roads,city.ports);
  EXPECT_EQ(plan.offers[0].access,regional::Access::MissingWater);
  EXPECT_FALSE(plan.gateways[0].enabled);
  freight=FreightSystem::simulate(city.map,city.roads,city.store,nullptr,{},&plan.gateways);
  EXPECT_EQ(freight.importedGoods,100); EXPECT_EQ(freight.unroutedGoods,350);
  const auto report=regional::measureLogistics(1,plan,freight,0.01,20);
  EXPECT_EQ(report.delivered.imports,100u); EXPECT_EQ(report.allocation.unmet.imports,350u);
  EXPECT_EQ(report.allocation.connections[0].imports,0u);
  EXPECT_DOUBLE_EQ(report.amountScale,0.01);
  EXPECT_EQ(report.expenses,6000); EXPECT_EQ(report.operatingExpense,20);
  EXPECT_EQ(regionalSettlementJson(parseRegionalSettlement(regionalSettlementJson(report),1)),regionalSettlementJson(report));
}
TEST(RegionalLogisticsTests, UnfulfilledTradeNeverEarnsMoneyAndRetailShortageReducesTaxes) {
  City city; city.add(BuildingType::Commercial,{24,7},100);
  const auto baseline=EconomySystem::calculateEconomy(city.store,city.people,{},&city.map,{},1,{},EconomyValuation::playable());
  FreightSummary freight; freight.goodsConsumed=150;
  auto blocked=baseline;
  regional::applyFulfilledTrade(blocked,freight);
  EXPECT_EQ(blocked.importCost,0); EXPECT_EQ(blocked.commercialTaxRevenue,0);
  EXPECT_EQ(blocked.goodsAvailability,0); EXPECT_EQ(blocked.economicHealth,0);
  EXPECT_LT(blocked.totalTaxRevenue,baseline.totalTaxRevenue);
  freight.importedGoods=150;
  auto full=baseline; regional::applyFulfilledTrade(full,freight);
  EXPECT_EQ(full.importCost,9000);
  EXPECT_EQ(full.commercialTaxRevenue,baseline.commercialTaxRevenue);
  EXPECT_FLOAT_EQ(full.goodsAvailability,1);
  int64_t cash=100000;
  const auto flow=TreasurySystem::applyEconomy(full,cash,0.01,20);
  EXPECT_EQ(flow.expenses,static_cast<int64_t>(std::llround(full.totalExpenses*0.01))+20);
  EXPECT_EQ(cash,100000+flow.revenue-flow.expenses);
  auto exporter=baseline; freight={}; freight.goodsProduced=100; freight.exportedGoods=20;
  regional::applyFulfilledTrade(exporter,freight);
  EXPECT_EQ(exporter.exportRevenue,800);
  EXPECT_EQ(exporter.totalRevenue,exporter.totalTaxRevenue+800);
}
TEST(RegionalLogisticsTests, ReadOnlyRefreshNeverActivatesOrBillsAndCompletedTickPersistsModeAfterRemoval) {
  City city; city.build();
  PlayableCityTickState state; state.regionalConnections=city.ports;
  DerivedCityRefreshOptions refresh; refresh.enableTransit=false;
  refreshDerivedCityState(city.map,city.roads,city.store,city.people,{},state,refresh);
  EXPECT_FALSE(state.regionalLogisticsEnabled);
  EXPECT_FALSE(state.regionalSettlement.valid);
  int64_t funds=50000;
  PlayableCityTickOptions options; options.enableTransit=false; options.growthChance=0;
  playableCityTick(city.map,city.roads,city.store,city.people,{},state,funds,options);
  EXPECT_TRUE(state.regionalLogisticsEnabled);
  EXPECT_EQ(state.regionalSettlement.allocation.tick,1u);
  EXPECT_EQ(state.regionalSettlement.operatingExpense,20);
  EXPECT_EQ(funds,49980);
  const auto report=regionalSettlementJson(state.regionalSettlement);
  refreshDerivedCityState(city.map,city.roads,city.store,city.people,{},state,refresh);
  EXPECT_EQ(funds,49980); EXPECT_EQ(state.tick,1u);
  EXPECT_EQ(regionalSettlementJson(state.regionalSettlement),report);
  ASSERT_TRUE(PortTool::remove(city.map,state.regionalConnections,17,funds));
  playableCityTick(city.map,city.roads,city.store,city.people,{},state,funds,options);
  EXPECT_TRUE(state.regionalLogisticsEnabled);
  ASSERT_EQ(state.trafficSummary.freight.gateways.size(),1u);
  EXPECT_EQ(state.trafficSummary.freight.gateways[0].gateway.goodsPerTick,100);
  EXPECT_EQ(state.regionalSettlement.operatingExpense,0);
}

TEST(RegionalLogisticsTests, PassengerTerminalAloneNeverActivatesCargoAndDisabledPortsHaveNoUpkeep) {
  City city; auto c=city.candidate(); c.kind=regional::Kind::PassengerTerminal;
  int64_t funds=1540000;
  ASSERT_TRUE(PortTool::build(city.map,city.roads,city.ports,c,{25,8},{},funds));
  PlayableCityTickState state; state.regionalConnections=city.ports;
  PlayableCityTickOptions options; options.enableTransit=false; options.growthChance=0;
  playableCityTick(city.map,city.roads,city.store,city.people,{},state,funds,options);
  EXPECT_FALSE(state.regionalLogisticsEnabled);
  EXPECT_TRUE(state.regionalSettlement.valid);
  EXPECT_EQ(state.regionalSettlement.delivered.passengers,0u);
  EXPECT_TRUE(state.trafficSummary.freight.gateways.empty());
  EXPECT_EQ(funds,39990);
  state.regionalConnections[0].enabled=false;
  playableCityTick(city.map,city.roads,city.store,city.people,{},state,funds,options);
  EXPECT_EQ(funds,39990);
}

TEST(RegionalLogisticsTests, NewModeRequiresBothGoodsUtilitiesWhileLegacyFreightIsUnchanged) {
  City city; city.add(BuildingType::Commercial,{24,7},100);
  city.map.getTile({24,7}).connectedToWater=false;
  const auto legacy=FreightSystem::simulate(city.map,city.roads,city.store);
  EXPECT_EQ(legacy.goodsConsumed,150); EXPECT_EQ(legacy.importedGoods,150);
  const auto plan=regional::planLogistics(city.map,city.roads,{});
  const auto modern=FreightSystem::simulate(city.map,city.roads,city.store,nullptr,{},&plan.gateways);
  EXPECT_EQ(modern.goodsConsumed,0); EXPECT_EQ(modern.importedGoods,0);
}

TEST(RegionalLogisticsTests, PausedProjectionRepricesWithoutAddingRoadLoadOrFinalizingAndDisabledFreightEarnsNothing) {
  City city; city.add(BuildingType::Commercial,{24,7},100);
  PlayableCityTickState state; state.regionalLogisticsEnabled=true;
  ServiceFacility power; power.type=ServiceType::Power; power.position={15,7};
  power.maxTravelDistance=32; power.powerCapacityMW=10000;
  ServiceFacility water; water.type=ServiceType::Water; water.position={17,7};
  water.maxTravelDistance=32; water.waterSupplyUnits=10000;
  const std::vector<ServiceFacility> facilities={power,water};
  DerivedCityRefreshOptions options; options.enableTransit=false;
  refreshDerivedCityState(city.map,city.roads,city.store,city.people,facilities,state,options);
  ASSERT_EQ(state.trafficSummary.freight.importedGoods,100);
  ASSERT_EQ(state.economy.importCost,6000);
  const auto before=city.roads.getAllEdgeTraffic();
  ASSERT_TRUE(std::any_of(before.begin(),before.end(),[](const auto& e){return e.freightLoad>0;}));
  options.runTraffic=false;
  refreshDerivedCityState(city.map,city.roads,city.store,city.people,facilities,state,options);
  EXPECT_EQ(state.economy.importCost,state.trafficSummary.freight.importedGoods*60);
  EXPECT_FALSE(state.regionalSettlement.valid); EXPECT_EQ(state.tick,0u);
  const auto after=city.roads.getAllEdgeTraffic();
  ASSERT_EQ(before.size(),after.size());
  for (size_t i=0;i<before.size();++i) {
    EXPECT_FLOAT_EQ(before[i].currentLoad,after[i].currentLoad);
    EXPECT_FLOAT_EQ(before[i].freightLoad,after[i].freightLoad);
  }
  options.enableFreight=false;
  refreshDerivedCityState(city.map,city.roads,city.store,city.people,facilities,state,options);
  EXPECT_EQ(state.economy.importCost,0); EXPECT_EQ(state.economy.exportRevenue,0);
  EXPECT_EQ(state.economy.goodsConsumed,150);
  EXPECT_FLOAT_EQ(state.economy.goodsAvailability,0);
}
