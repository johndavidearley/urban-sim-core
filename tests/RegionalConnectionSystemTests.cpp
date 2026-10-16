#include "gtest/gtest.h"
#include <algorithm>
#include <climits>
#include <stdexcept>
#include "src/systems/RegionalConnectionSystem.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/persistence/RegionalConnectionsJson.hpp"

using namespace regional;

TEST(RegionalConnectionTests, ConfigurationRoundTripIsSortedAndRequiresValidReservations) {
  CityMap map({10,10});
  Connection airport; airport.id=9; airport.kind=Kind::Airport;
  airport.origin={3,3}; airport.width=2; airport.height=2; airport.entrance={3,5};
  airport.passengersPerTick=100;
  Connection road; road.id=2; road.entrance={0,0}; road.goodsPerTick=UINT32_MAX;
  const auto rows=regionalConnectionsJson({airport,road});
  auto parsed=parseRegionalConnections(rows,map);
  ASSERT_EQ(parsed.size(),2u);
  EXPECT_EQ(parsed[0].id,2u);
  EXPECT_EQ(parsed[1].passengersPerTick,100u);
  EXPECT_EQ(regionalConnectionsJson(parsed),rows);
  auto overlap=rows; overlap[1]["x"]=0; overlap[1]["y"]=0;
  EXPECT_THROW(parseRegionalConnections(overlap,map),std::runtime_error);
  auto entrance=rows; entrance[0]["entranceX"]=3;
  EXPECT_THROW(parseRegionalConnections(entrance,map),std::runtime_error);
  // A reserved footprint cannot swallow another terminal's road entrance.
  Connection other=airport; other.id=12; other.origin={3,5}; other.entrance={3,7};
  EXPECT_THROW(parseRegionalConnections(regionalConnectionsJson({airport,other}),map),std::runtime_error);
}

TEST(RegionalConnectionTests, MalformedConfigurationRejectsTypesRangesModesAndDuplicates) {
  CityMap map({10,10}); Connection c; c.id=1;
  const auto valid=regionalConnectionsJson({c});
  for (const auto& value : {nlohmann::json(-1),nlohmann::json(1.5),
                           nlohmann::json("1"),nlohmann::json(4294967296ULL)}) {
    auto rows=valid; rows[0]["passengersPerTick"]=value;
    EXPECT_THROW(parseRegionalConnections(rows,map),std::runtime_error);
  }
  for (const char* field : {"width","height","id"}) {
    auto rows=valid; rows[0][field]=0;
    EXPECT_THROW(parseRegionalConnections(rows,map),std::runtime_error);
  }
  auto rows=valid; rows[0]["enabled"]=1;
  EXPECT_THROW(parseRegionalConnections(rows,map),std::runtime_error);
  rows=valid; rows[0]["kind"]=4;
  EXPECT_THROW(parseRegionalConnections(rows,map),std::runtime_error);
  rows=valid; rows.push_back(rows[0]);
  EXPECT_THROW(parseRegionalConnections(rows,map),std::runtime_error);
  rows=valid; rows[0]["kind"]=3; rows[0]["goodsPerTick"]=1;
  EXPECT_THROW(parseRegionalConnections(rows,map),std::exception);
  EXPECT_THROW(parseRegionalConnections(nlohmann::json::object(),map),std::runtime_error);
  EXPECT_THROW(parseRegionalConnections(nlohmann::json(std::vector<int>(4097)),map),std::runtime_error);
}

TEST(RegionalConnectionTests, SharedCargoCapacityAndStablePriorityPreventDuplicateAdmission) {
  Connection road; road.id=20; road.passengersPerTick=10; road.goodsPerTick=6;
  Connection port; port.id=10; port.kind=Kind::CargoPort; port.goodsPerTick=8;
  std::vector<Offer> offers={{road,Access::Ready},{port,Access::Ready}};
  auto a=allocate(17,{15,10,10},offers);
  std::reverse(offers.begin(),offers.end());
  auto b=allocate(17,{15,10,10},offers);
  ASSERT_EQ(a.connections.size(),2u);
  EXPECT_EQ(a.connections[0].id,10u);
  EXPECT_EQ(a.connections[0].imports,8u);
  EXPECT_EQ(a.connections[1].imports,2u);
  EXPECT_EQ(a.connections[1].exports,4u);
  EXPECT_EQ(a.admitted.passengers,10u);
  EXPECT_EQ(a.unmet.passengers,5u);
  EXPECT_EQ(a.admitted.exports+a.unmet.exports,10u);
  for (size_t i=0;i<a.connections.size();++i) {
    EXPECT_EQ(a.connections[i].imports,b.connections[i].imports);
    EXPECT_EQ(a.connections[i].exports,b.connections[i].exports);
  }
}

TEST(RegionalConnectionTests, DisabledAndDisconnectedOffersAdmitNothingAndLargeCountsDoNotOverflow) {
  Connection c; c.id=1; c.passengersPerTick=UINT32_MAX; c.goodsPerTick=UINT32_MAX;
  c.enabled=false;
  auto r=allocate(1,{UINT32_MAX,UINT32_MAX,UINT32_MAX},{{c,Access::Ready}});
  EXPECT_EQ(r.admitted.passengers,0u);
  EXPECT_EQ(r.connections[0].access,Access::Disabled);
  c.enabled=true;
  r=allocate(1,{UINT32_MAX,UINT32_MAX,UINT32_MAX},{{c,Access::NoRoadPath}});
  EXPECT_EQ(r.admitted.imports,0u);
  r=allocate(1,{UINT32_MAX,UINT32_MAX,UINT32_MAX},{{c,Access::Ready}});
  EXPECT_EQ(r.admitted.imports,UINT32_MAX);
  EXPECT_EQ(r.admitted.exports,0u);
  EXPECT_EQ(r.unmet.exports,UINT32_MAX);
}

TEST(RegionalConnectionTests, InvalidIdsAndUnsupportedCargoModesReject) {
  Connection c;
  EXPECT_THROW(allocate(1,{},{{c,Access::Ready}}),std::invalid_argument);
  c.id=1;
  EXPECT_THROW(allocate(1,{},{{c,Access::Ready},{c,Access::Ready}}),std::invalid_argument);
  c.kind=Kind::Airport; c.goodsPerTick=1;
  EXPECT_THROW(allocate(1,{},{{c,Access::Ready}}),std::invalid_argument);
  c.goodsPerTick=0; c.kind=Kind::CargoPort; c.passengersPerTick=1;
  EXPECT_THROW(allocate(1,{},{{c,Access::Ready}}),std::invalid_argument);
}

TEST(RegionalConnectionTests, SeaAccessRejectsPondsAndDiagonalWaterContact) {
  CityMap map({10,10});
  for (int x=0;x<4;++x) map.getTile({x,3}).type=2;
  map.getTile({4,4}).type=2; // Diagonal contact is not a water route.
  map.getTile({7,7}).type=2;
  SeaAccess sea(map);
  EXPECT_TRUE(sea.reachesBoundary({3,3}));
  EXPECT_FALSE(sea.reachesBoundary({4,4}));
  EXPECT_FALSE(sea.reachesBoundary({7,7}));
  EXPECT_FALSE(sea.reachesBoundary({-1,3}));
  map.getTile({4,3}).type=2;
  EXPECT_FALSE(sea.reachesBoundary({4,4})); // Derived snapshot stays stable.
  EXPECT_TRUE(SeaAccess(map).reachesBoundary({4,4}));
}

TEST(RegionalConnectionTests, SitePreflightRequiresRealDestinationAccessAndPreservesCity) {
  CityMap map({10,10}); RoadNetwork roads(map);
  roads.buildRoad({3,5},{4,5}); roads.buildRoad({4,5},{5,5});
  roads.buildRoad({7,7},{8,7});
  for (int x=0;x<5;++x) map.getTile({x,2}).type=2;
  SeaAccess sea(map);
  Connection c; c.id=1; c.kind=Kind::CargoPort; c.origin={3,3};
  c.width=2; c.height=2; c.entrance={3,5};
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{5,5}),Access::Ready);
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{7,7}),Access::NoRoadPath);
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{5,5},{c}),Access::Occupied);
  map.getTile({4,4}).connectedToPower=false;
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{5,5}),Access::MissingPower);
  map.getTile({4,4}).connectedToPower=true;
  map.getTile({4,4}).connectedToWater=false;
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{5,5}),Access::MissingWater);
  EXPECT_EQ(map.getTile({3,3}).buildingId,0u);
  EXPECT_EQ(map.getTile({3,3}).zone,0);
  c.width=INT_MAX;
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{5,5}),Access::InvalidFootprint);
}

TEST(RegionalConnectionTests, PortNeedsBoundaryWaterButAirportDoesNot) {
  CityMap map({10,10}); RoadNetwork roads(map); roads.buildRoad({3,5},{4,5});
  map.getTile({3,2}).type=2;
  Connection c; c.id=1; c.kind=Kind::PassengerTerminal; c.origin={3,3};
  c.width=2; c.height=2; c.entrance={3,5};
  SeaAccess sea(map);
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{4,5}),Access::NoSeaAccess);
  c.kind=Kind::Airport;
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{4,5}),Access::Ready);
  c.entrance={8,8};
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{4,5}),Access::MissingEntrance);
}

TEST(RegionalConnectionTests, BuiltPortUsesOwnReservationAndOnlyNeedsItsCitySideRoad) {
  CityMap map({10,10}); RoadNetwork roads(map);
  roads.buildRoad({3,4},{4,4}); roads.buildRoad({4,4},{5,4});
  roads.buildRoad({7,7},{8,7});
  for (int x=0;x<5;++x) map.getTile({x,2}).type=2;
  Connection c; c.id=17; c.kind=Kind::CargoPort; c.origin={3,3};
  c.width=2; c.height=1; c.entrance={3,4}; c.goodsPerTick=200;
  SeaAccess sea(map);
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::NotConstructed);
  c.constructed=true;
  for (int x=3;x<5;++x) map.getTile({x,3}).regionalConnectionId=c.id;
  EXPECT_EQ(diagnoseSite(c,map,roads,sea,{5,4}),Access::Occupied);
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::Ready);
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,{5,4}),Access::Ready);
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,{7,7}),Access::NoRoadPath);
  // No route to the legacy west-edge gateway is needed for maritime access.
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,{0,5}),Access::NoRoadPath);
  EXPECT_EQ(map.getTile({3,3}).regionalConnectionId,c.id);
  for (const auto& edge:roads.getAllEdgeTraffic()) EXPECT_FLOAT_EQ(edge.currentLoad,0.0f);
  c.enabled=false;
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::Disabled);
}

TEST(RegionalConnectionTests, BuiltPortRechecksOwnershipUtilitiesAndSeaAfterWorldEdits) {
  CityMap map({10,10}); RoadNetwork roads(map); roads.buildRoad({3,4},{4,4});
  for (int x=0;x<5;++x) map.getTile({x,2}).type=2;
  Connection c; c.id=17; c.kind=Kind::PassengerTerminal; c.origin={3,3};
  c.width=2; c.height=1; c.entrance={3,4}; c.constructed=true;
  auto& first=map.getTile({3,3}); auto& second=map.getTile({4,3});
  first.regionalConnectionId=c.id; second.regionalConnectionId=c.id;
  SeaAccess sea(map);
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::Ready);
  second.regionalConnectionId=0;
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::Occupied);
  second.regionalConnectionId=18;
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::Occupied);
  second.regionalConnectionId=c.id; second.connectedToPower=false;
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::MissingPower);
  second.connectedToPower=true; second.connectedToWater=false;
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::MissingWater);
  second.connectedToWater=true;
  map.getTile({1,2}).type=0;
  EXPECT_EQ(diagnoseOperation(c,map,roads,SeaAccess(map),c.entrance),Access::NoSeaAccess);
  map.getTile({1,2}).type=2;
  EXPECT_EQ(diagnoseOperation(c,map,roads,SeaAccess(map),c.entrance),Access::Ready);
  c.entrance={9,9};
  EXPECT_EQ(diagnoseOperation(c,map,roads,sea,c.entrance),Access::MissingEntrance);
}

TEST(RegionalConnectionTests, UnconstructedConnectionsNeverAdmitOperationalCapacity) {
  CityMap map({10,10}); RoadNetwork roads(map); SeaAccess sea(map);
  Connection c; c.id=1; c.kind=Kind::CargoPort; c.goodsPerTick=200;
  const auto access=diagnoseOperation(c,map,roads,sea,c.entrance);
  auto report=allocate(1,{0,100,100},{{c,access}});
  EXPECT_EQ(report.admitted.imports,0u);
  EXPECT_EQ(report.admitted.exports,0u);
  EXPECT_EQ(report.unmet.imports,100u);
  EXPECT_STREQ(accessName(report.connections[0].access),"not constructed");
}
