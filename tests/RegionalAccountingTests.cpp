#include "gtest/gtest.h"
#include <climits>
#include <limits>
#include "src/gameplay/RegionalConnectionTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/RegionalAccounting.hpp"
#include "src/persistence/RegionalSettlementJson.hpp"

using namespace regional;

TEST(RegionalAccountingTests, PersistedSettlementPreservesFinalizationGuardAndRejectsTampering) {
  Connection c; c.id=3; c.passengersPerTick=10; c.goodsPerTick=20;
  auto report=allocate(7,{10,10,10},{{c,Access::Ready}});
  Settlement ledger;
  ASSERT_TRUE(settle(report,{{3,{5,6,7}}},{2,3,4,5},ledger));
  const auto data=regionalSettlementJson(ledger);
  auto loaded=parseRegionalSettlement(data,10);
  EXPECT_EQ(regionalSettlementJson(loaded),data);
  EXPECT_FALSE(settle(report,{{3,{5,6,7}}},{2,3,4,5},loaded));
  EXPECT_EQ(loaded.revenue,38);
  EXPECT_THROW(parseRegionalSettlement(data,6),std::runtime_error);
  for (const char* field: {"revenue","expenses"}) {
    auto invalid=data; invalid[field]=99;
    EXPECT_THROW(parseRegionalSettlement(invalid,10),std::runtime_error);
  }
  auto invalid=data; invalid["delivered"]["passengers"]=6;
  EXPECT_THROW(parseRegionalSettlement(invalid,10),std::runtime_error);
  invalid=data; invalid["prices"]["importCost"]=1.5;
  EXPECT_THROW(parseRegionalSettlement(invalid,10),std::runtime_error);
  invalid=data; invalid["deliveries"].push_back(invalid["deliveries"][0]);
  EXPECT_THROW(parseRegionalSettlement(invalid,10),std::runtime_error);
  invalid=regionalSettlementJson({}); invalid["revenue"]=1;
  EXPECT_THROW(parseRegionalSettlement(invalid,10),std::runtime_error);
  EXPECT_FALSE(parseRegionalSettlement(regionalSettlementJson({}),0).valid);
}

TEST(RegionalAccountingTests, PlacementRechecksSiteAndFundsBeforeReservingOrCharging) {
  CityMap map({10,10}); RoadNetwork roads(map);
  roads.buildRoad({3,5},{4,5});
  Connection airport; airport.id=3; airport.kind=Kind::Airport;
  airport.origin={3,3}; airport.width=2; airport.height=2; airport.entrance={3,5};
  airport.passengersPerTick=60;
  std::vector<Connection> registry;
  int64_t funds=1000; std::string error;
  EXPECT_FALSE(RegionalConnectionTool::place(airport,map,roads,{4,5},{},1001,registry,funds,&error));
  EXPECT_EQ(funds,1000); EXPECT_TRUE(registry.empty());
  EXPECT_FALSE(RegionalConnectionTool::place(airport,map,roads,{4,5},{{4,4}},200,registry,funds));
  EXPECT_TRUE(registry.empty()); EXPECT_EQ(funds,1000);
  // A plan's access may change before execution; the tool evaluates it now.
  map.getTile({3,3}).connectedToPower=false;
  EXPECT_FALSE(RegionalConnectionTool::place(airport,map,roads,{4,5},{},200,registry,funds));
  EXPECT_EQ(funds,1000); EXPECT_TRUE(registry.empty());
  map.getTile({3,3}).connectedToPower=true;
  ASSERT_TRUE(RegionalConnectionTool::place(airport,map,roads,{4,5},{},200,registry,funds,&error));
  EXPECT_TRUE(error.empty()); EXPECT_EQ(funds,800); ASSERT_EQ(registry.size(),1u);
  EXPECT_FALSE(RegionalConnectionTool::place(airport,map,roads,{4,5},{},200,registry,funds));
  EXPECT_EQ(funds,800); EXPECT_EQ(registry.size(),1u);
  auto other=airport; other.id=4;
  EXPECT_FALSE(RegionalConnectionTool::place(other,map,roads,{4,5},{},200,registry,funds));
  EXPECT_EQ(funds,800); EXPECT_EQ(registry.size(),1u);
  EXPECT_EQ(map.getTile({3,3}).buildingId,0u); // Registry, not a fake building ID.
}

TEST(RegionalAccountingTests, PlacementProtectsEntrancesAndRejectsInvalidFinancialInputs) {
  CityMap map({10,10}); RoadNetwork roads(map);
  roads.buildRoad({3,5},{4,5});
  Connection c; c.id=1; c.kind=Kind::Airport; c.origin={3,3}; c.entrance={3,4};
  roads.buildRoad({3,4},{4,4});
  Connection other; other.id=2; other.kind=Kind::Airport;
  other.origin={3,2}; other.entrance={3,3};
  std::vector<Connection> registry={other}; int64_t funds=1000;
  EXPECT_FALSE(RegionalConnectionTool::place(c,map,roads,{4,4},{},100,registry,funds));
  EXPECT_EQ(funds,1000); EXPECT_EQ(registry.size(),1u);
  registry.clear();
  EXPECT_FALSE(RegionalConnectionTool::place(c,map,roads,{4,4},{},-1,registry,funds));
  c.kind=Kind::RoadGateway;
  EXPECT_FALSE(RegionalConnectionTool::place(c,map,roads,{4,4},{},0,registry,funds));
  EXPECT_TRUE(registry.empty()); EXPECT_EQ(funds,1000);
}

TEST(RegionalAccountingTests, RemovalReleasesOnlyItsReservationWithoutImplicitRefund) {
  Connection a; a.id=1; Connection b; b.id=2;
  std::vector<Connection> registry={a,b}; int64_t funds=50;
  EXPECT_FALSE(RegionalConnectionTool::remove(9,10,registry,funds));
  EXPECT_FALSE(RegionalConnectionTool::remove(1,51,registry,funds));
  EXPECT_FALSE(RegionalConnectionTool::remove(1,-1,registry,funds));
  EXPECT_EQ(registry.size(),2u); EXPECT_EQ(funds,50);
  ASSERT_TRUE(RegionalConnectionTool::remove(1,10,registry,funds));
  ASSERT_EQ(registry.size(),1u); EXPECT_EQ(registry[0].id,2u); EXPECT_EQ(funds,40);
  EXPECT_FALSE(RegionalConnectionTool::remove(1,0,registry,funds));
  EXPECT_EQ(funds,40);
}

TEST(RegionalAccountingTests, PlacementRebuildsSeaAccessAfterPlanningTerrainChanges) {
  CityMap map({10,10}); RoadNetwork roads(map); roads.buildRoad({3,5},{4,5});
  for (int x=0;x<5;++x) map.getTile({x,2}).type=2;
  Connection port; port.id=1; port.kind=Kind::CargoPort;
  port.origin={3,3}; port.width=2; port.height=2; port.entrance={3,5}; port.goodsPerTick=40;
  SeaAccess planningSea(map);
  ASSERT_EQ(diagnoseSite(port,map,roads,planningSea,{4,5}),Access::Ready);
  map.getTile({0,2}).type=0; // The previously connected sea is now an inland pond.
  std::vector<Connection> registry; int64_t funds=1000; std::string error;
  EXPECT_FALSE(RegionalConnectionTool::place(port,map,roads,{4,5},{},100,registry,funds,&error));
  EXPECT_EQ(error,"no boundary-connected water");
  EXPECT_TRUE(registry.empty()); EXPECT_EQ(funds,1000);
}

TEST(RegionalAccountingTests, OnlyConfirmedDeliveryEarnsCreditsAndConservationReconciles) {
  Connection c; c.id=1; c.passengersPerTick=10; c.goodsPerTick=20;
  auto report=allocate(10,{20,10,20},{{c,Access::Ready}});
  Settlement ledger; Prices prices{2,3,4,5};
  ASSERT_TRUE(settle(report,{{1,{4,6,7}}},prices,ledger));
  EXPECT_EQ(ledger.delivered.passengers,4u); EXPECT_EQ(ledger.undelivered.passengers,6u);
  EXPECT_EQ(ledger.delivered.imports,6u); EXPECT_EQ(ledger.undelivered.imports,4u);
  EXPECT_EQ(ledger.delivered.exports,7u); EXPECT_EQ(ledger.undelivered.exports,3u);
  EXPECT_EQ(ledger.revenue,36); EXPECT_EQ(ledger.expenses,23);
  EXPECT_EQ(ledger.allocation.unmet.passengers,10u);
  EXPECT_EQ(ledger.allocation.unmet.exports,10u);
  EXPECT_FALSE(settle(report,{{1,{4,6,7}}},prices,ledger));
  EXPECT_EQ(ledger.revenue,36); EXPECT_EQ(ledger.allocation.tick,10u);
  report.tick=11;
  ASSERT_TRUE(settle(report,{},prices,ledger));
  EXPECT_EQ(ledger.revenue,0); EXPECT_EQ(ledger.expenses,5);
  EXPECT_EQ(ledger.undelivered.exports,10u);
}

TEST(RegionalAccountingTests, DuplicateUnknownAndExcessDeliveriesRejectWithoutChangingLedger) {
  Connection c; c.id=1; c.passengersPerTick=10; c.goodsPerTick=20;
  auto report=allocate(2,{10,10,10},{{c,Access::Ready}});
  Settlement ledger;
  auto previous=report; previous.tick=1;
  ASSERT_TRUE(settle(previous,{},Prices{},ledger));
  for (const auto& rows:std::vector<std::vector<Delivery>>{
      {{1,{1,0,0}},{1,{1,0,0}}},{{2,{1,0,0}}},{{1,{11,0,0}}},{{1,{0,11,0}}}}) {
    EXPECT_FALSE(settle(report,rows,{},ledger));
    EXPECT_EQ(ledger.allocation.tick,1u); EXPECT_EQ(ledger.revenue,0);
  }
  report.unmet.passengers=1;
  EXPECT_FALSE(settle(report,{},Prices{},ledger));
  report=allocate(2,{10,10,10},{{c,Access::Ready}});
  report.connections[0].access=Access::NoRoadPath;
  EXPECT_FALSE(settle(report,{},Prices{},ledger));
  EXPECT_EQ(ledger.allocation.tick,1u);
}

TEST(RegionalAccountingTests, OverflowNegativePricesAndStaleTicksNeverPartiallyFinalize) {
  Connection c; c.id=1; c.passengersPerTick=UINT32_MAX; c.goodsPerTick=UINT32_MAX;
  auto report=allocate(1,{UINT32_MAX,0,UINT32_MAX},{{c,Access::Ready}});
  Settlement ledger;
  Prices prices; prices.passengerFee=std::numeric_limits<int64_t>::max();
  EXPECT_FALSE(settle(report,{{1,{UINT32_MAX,0,0}}},prices,ledger));
  EXPECT_FALSE(ledger.valid);
  prices.passengerFee=-1;
  EXPECT_FALSE(settle(report,{},prices,ledger));
  prices={}; prices.operatingCost=-1;
  EXPECT_FALSE(settle(report,{},prices,ledger));
  prices={}; prices.passengerFee=std::numeric_limits<int64_t>::max(); prices.exportRevenue=1;
  EXPECT_FALSE(settle(report,{{1,{1,0,1}}},prices,ledger));
  EXPECT_FALSE(ledger.valid);
  ASSERT_TRUE(settle(report,{},Prices{},ledger));
  report.tick=0;
  EXPECT_FALSE(settle(report,{},Prices{},ledger));
  EXPECT_EQ(ledger.allocation.tick,1u);
}

TEST(RegionalAccountingTests, DormantSiteStatusRoundTripsWithoutFlowAndRejectsUnknownStatus) {
  Connection c; c.id=17; c.kind=Kind::CargoPort; c.goodsPerTick=200;
  const auto report=allocate(1,{0,100,100},{{c,Access::NotConstructed}});
  Settlement ledger;
  ASSERT_TRUE(settle(report,{}, {},ledger));
  const auto data=regionalSettlementJson(ledger);
  EXPECT_EQ(regionalSettlementJson(parseRegionalSettlement(data,1)),data);
  EXPECT_EQ(ledger.revenue,0);
  EXPECT_EQ(ledger.expenses,0);
  auto invalid=report;
  invalid.connections[0].access=static_cast<Access>(10);
  Settlement untouched;
  EXPECT_FALSE(settle(invalid,{}, {},untouched));
  EXPECT_FALSE(untouched.valid);
}
