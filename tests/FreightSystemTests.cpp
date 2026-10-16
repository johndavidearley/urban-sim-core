#include <gtest/gtest.h>
#include <algorithm>
#include <stdexcept>

#include "src/entities/PopulationStore.hpp"
#include "src/systems/FreightSystem.hpp"
#include "src/systems/TrafficSystem.hpp"

namespace {
struct FreightCity {
  CityMap map{{32, 16}};
  RoadNetwork roads{map};
  EntityStore store;
  FreightCity(int start = 0) {
    for (int x = start; x < 24; ++x)
      roads.buildRoad({x, 8}, {x + 1, 8});
  }
  EntityId add(BuildingType type, Coord position, int occupancy,
               int capacity = 2000) {
    const auto id = store.createBuilding(type, position, capacity);
    store.getBuilding(id)->occupancy = occupancy;
    map.getTile(position).buildingId = id;
    return id;
  }
};
} // namespace

TEST(FreightSystem, ExplicitGatewaysSplitImportsWithoutDuplicateTruckLegs) {
  FreightCity city;
  city.add(BuildingType::Commercial,{20,7},100);
  std::vector<FreightGateway> gateways={{20,{18,8},64,true},{10,{0,8},32,true}};
  const auto r=FreightSystem::simulate(city.map,city.roads,city.store,nullptr,{},&gateways);
  EXPECT_EQ(r.importedGoods,96); EXPECT_EQ(r.unroutedGoods,54);
  EXPECT_EQ(r.trucks,3u); EXPECT_EQ(r.routes,2u);
  ASSERT_EQ(r.gateways.size(),2u);
  EXPECT_EQ(r.gateways[0].gateway.id,10u); EXPECT_EQ(r.gateways[0].importedGoods,32);
  EXPECT_EQ(r.gateways[1].importedGoods,64);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({0,8},{1,8}),1.0f);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({18,8},{19,8}),3.0f);
  EXPECT_EQ(r.importedGoods+r.unroutedGoods,r.goodsConsumed);
}

TEST(FreightSystem, DomesticSupplyPrecedesCapacityLimitedExternalExports) {
  FreightCity city;
  city.add(BuildingType::Industrial,{6,7},100);
  city.add(BuildingType::Commercial,{20,7},50);
  std::vector<FreightGateway> gateways={{1,{0,8},20,true},{2,{18,8},30,true}};
  const auto r=FreightSystem::simulate(city.map,city.roads,city.store,nullptr,{},&gateways);
  EXPECT_EQ(r.domesticGoods,75); EXPECT_EQ(r.exportedGoods,50); EXPECT_EQ(r.unroutedGoods,75);
  EXPECT_EQ(r.importedGoods,0); EXPECT_EQ(r.trucks,5u);
  EXPECT_EQ(r.gateways[0].exportedGoods,20); EXPECT_EQ(r.gateways[1].exportedGoods,30);
  EXPECT_EQ(r.domesticGoods+r.exportedGoods+r.unroutedGoods,r.goodsProduced);
}

TEST(FreightSystem, RestockingSharesGatewayCapacityAcrossProducersWithoutSecondRetailImports) {
  FreightCity city;
  city.add(BuildingType::Industrial,{6,7},25);
  city.add(BuildingType::Industrial,{10,7},25);
  city.add(BuildingType::Commercial,{20,7},200);
  std::vector<FreightGateway> gateways={{1,{0,8},32,true},{2,{18,8},64,true}};
  const auto r=FreightSystem::simulate(city.map,city.roads,city.store,nullptr,{},&gateways);
  EXPECT_EQ(r.goodsProduced,100); EXPECT_EQ(r.goodsConsumed,300);
  EXPECT_EQ(r.importedGoods,96); EXPECT_EQ(r.domesticGoods,196);
  EXPECT_EQ(r.exportedGoods,0); EXPECT_EQ(r.unroutedGoods,104);
  EXPECT_EQ(r.gateways[0].importedGoods,32); EXPECT_EQ(r.gateways[1].importedGoods,64);
  EXPECT_EQ(r.goodsProduced+r.importedGoods,r.domesticGoods);
}

TEST(FreightSystem, UnreachableAndDisabledGatewaysAllowReachableFallbackWithoutDoubleFailure) {
  FreightCity city(6);
  city.add(BuildingType::Commercial,{20,7},100);
  std::vector<FreightGateway> gateways={
    {1,{0,8},200,true},{2,{18,8},200,false},{3,{10,8},200,true}};
  const auto r=FreightSystem::simulate(city.map,city.roads,city.store,nullptr,{},&gateways);
  EXPECT_FALSE(r.gateConnected); EXPECT_EQ(r.importedGoods,150); EXPECT_EQ(r.unroutedGoods,0);
  EXPECT_EQ(r.gateways[0].importedGoods,0); EXPECT_EQ(r.gateways[1].importedGoods,0);
  EXPECT_EQ(r.gateways[2].importedGoods,150); EXPECT_EQ(r.routes,1u); EXPECT_EQ(r.trucks,5u);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({10,8},{11,8}),5.0f);
}

TEST(FreightSystem, ExplicitEmptyGatewayListDoesNotRestoreUnlimitedLegacyTrade) {
  FreightCity city;
  city.add(BuildingType::Commercial,{20,7},100);
  const std::vector<FreightGateway> gateways;
  const auto r=FreightSystem::simulate(city.map,city.roads,city.store,nullptr,{},&gateways);
  EXPECT_TRUE(r.gateConnected); EXPECT_EQ(r.importedGoods,0); EXPECT_EQ(r.unroutedGoods,150);
  EXPECT_EQ(r.routes,0u); EXPECT_EQ(r.trucks,0u);
  // Omitting the override remains the original fixed-gateway path.
  EXPECT_EQ(FreightSystem::simulate(city.map,city.roads,city.store).importedGoods,150);
}

TEST(FreightSystem, GatewayOrderCapacityEditsAndTopologyChangesRespectRouteCache) {
  FreightCity city;
  city.add(BuildingType::Commercial,{20,7},100);
  FreightRouteCache cache;
  std::vector<FreightGateway> gateways={{2,{18,8},64,true},{1,{0,8},32,true}};
  const auto first=FreightSystem::simulate(city.map,city.roads,city.store,&cache,{},&gateways);
  const auto edges=city.roads.getAllEdgeTraffic();
  city.roads.resetCongestion(); std::reverse(gateways.begin(),gateways.end());
  const auto repeated=FreightSystem::simulate(city.map,city.roads,city.store,&cache,{},&gateways);
  EXPECT_EQ(first.importedGoods,repeated.importedGoods);
  EXPECT_EQ(first.unroutedGoods,repeated.unroutedGoods);
  for (size_t i=0;i<edges.size();++i)
    EXPECT_FLOAT_EQ(edges[i].freightLoad,city.roads.getAllEdgeTraffic()[i].freightLoad);
  gateways[0].goodsPerTick=0; city.roads.resetCongestion();
  EXPECT_EQ(FreightSystem::simulate(city.map,city.roads,city.store,&cache,{},&gateways).importedGoods,64);
  city.roads.removeRoad({18,8},{19,8}); city.roads.resetCongestion();
  const auto cut=FreightSystem::simulate(city.map,city.roads,city.store,&cache,{},&gateways);
  EXPECT_EQ(cut.importedGoods,0); EXPECT_EQ(cut.unroutedGoods,150);
  EXPECT_EQ(cache.topologyVersion,city.roads.getTopologyVersion());
}

TEST(FreightSystem, MalformedGatewaysRejectBeforeChangingTrafficOrCache) {
  FreightCity city;
  city.add(BuildingType::Commercial,{20,7},100);
  city.roads.updateCongestion({10,8},{11,8},3.0f);
  FreightRouteCache cache;
  for (const auto& gateways:std::vector<std::vector<FreightGateway>>{
      {{0,{0,8},10,true}},{{1,{0,8},-1,true}},{{1,{-1,8},10,true}},
      {{1,{0,8},10,true},{1,{18,8},10,true}}}) {
    EXPECT_THROW(FreightSystem::simulate(city.map,city.roads,city.store,&cache,{},&gateways),std::invalid_argument);
    EXPECT_TRUE(cache.paths.empty());
    EXPECT_FLOAT_EQ(city.roads.getFreightLoad({10,8},{11,8}),0.0f);
  }
}

TEST(FreightSystem, DomesticAndExportsAddToExistingCommuterLoad) {
  FreightCity city;
  city.add(BuildingType::Industrial, {6, 7}, 1000);
  city.add(BuildingType::Commercial, {20, 7}, 500);
  city.roads.updateCongestion({6, 8}, {7, 8}, 3.0f);
  const auto freight =
      FreightSystem::simulate(city.map, city.roads, city.store);
  EXPECT_EQ(freight.goodsProduced, 2000);
  EXPECT_EQ(freight.goodsConsumed, 750);
  EXPECT_EQ(freight.domesticGoods, 750);
  EXPECT_EQ(freight.exportedGoods, 1250);
  EXPECT_EQ(freight.importedGoods, 0);
  EXPECT_EQ(freight.unroutedGoods, 0);
  EXPECT_EQ(freight.trucks, 64u);
  EXPECT_EQ(freight.gate, Coord(0, 8));
  EXPECT_TRUE(freight.gateConnected);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({6, 8}, {7, 8}), 24.0f);
  EXPECT_FLOAT_EQ(city.roads.getAllEdgeTraffic()[6].currentLoad, 27.0f);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({0, 8}, {1, 8}), 40.0f);
  TrafficSummary summary;
  summary.freight = freight;
  TrafficSystem::refreshCongestion(city.roads, summary);
  EXPECT_GT(summary.averageEdgeCongestion, 0.0f);
  city.roads.setRoadClass({6, 8}, {7, 8}, RoadClass::Arterial);
  EXPECT_FLOAT_EQ(city.roads.getCongestion({6, 8}, {7, 8}), 27.0f / kArterialRoadCapacity);
}

TEST(FreightSystem, CommercialOnlyCityImportsAndUnpoweredStockDoesNotShip) {
  FreightCity city;
  const auto id = city.add(BuildingType::Commercial, {20, 7}, 100);
  auto freight = FreightSystem::simulate(city.map, city.roads, city.store);
  EXPECT_EQ(freight.importedGoods, 150);
  EXPECT_EQ(freight.trucks, 5u);
  EXPECT_EQ(freight.exportedGoods, 0);
  city.map.getTile(city.store.getBuilding(id)->position).connectedToPower =
      false;
  city.roads.resetCongestion();
  freight = FreightSystem::simulate(city.map, city.roads, city.store);
  EXPECT_EQ(freight.goodsConsumed, 0);
  EXPECT_EQ(freight.trucks, 0u);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({0, 8}, {1, 8}), 0.0f);
}

TEST(FreightSystem, MissingGateReportsUnroutedGoodsWithoutBuildingRoads) {
  FreightCity city(6);
  city.add(BuildingType::Industrial, {6, 7}, 100);
  const auto edges = city.roads.getRoadCount();
  const auto freight =
      FreightSystem::simulate(city.map, city.roads, city.store);
  EXPECT_FALSE(freight.gateConnected);
  EXPECT_EQ(freight.unroutedGoods, 200);
  EXPECT_EQ(freight.trucks, 0u);
  EXPECT_EQ(city.roads.getRoadCount(), edges);
}

TEST(FreightSystem,
     CapacityAndDistanceWeightLocalDestinationAndGoodsRoundLikeEconomy) {
  FreightCity city(4);
  city.add(BuildingType::Industrial, {4, 7}, 1);
  city.add(BuildingType::Commercial, {6, 7}, 1, 100);
  city.add(BuildingType::Commercial, {20, 7}, 1, 1000);
  PopulationStore people;
  const auto economy =
      EconomySystem::calculateEconomy(city.store, people, {}, &city.map);
  const auto freight =
      FreightSystem::simulate(city.map, city.roads, city.store);
  EXPECT_EQ(freight.goodsProduced, economy.goodsProduced);
  EXPECT_EQ(freight.goodsConsumed, economy.goodsConsumed);
  EXPECT_EQ(freight.domesticGoods, 2);
  EXPECT_EQ(freight.routes,
            1u); // all available stock goes to the stronger far destination
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({4, 8}, {5, 8}), 1.0f);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({19, 8}, {20, 8}), 1.0f);
}

TEST(FreightSystem, CacheInvalidatesOnTopologyAndMatchesFreshRouting) {
  FreightCity city;
  city.add(BuildingType::Industrial, {6, 7}, 100);
  city.add(BuildingType::Commercial, {20, 7}, 100);
  FreightRouteCache cache;
  FreightSystem::simulate(city.map, city.roads, city.store, &cache);
  const auto expected = city.roads.getAllEdgeTraffic();
  city.roads.resetCongestion();
  FreightSystem::simulate(city.map, city.roads, city.store, &cache);
  const auto repeated = city.roads.getAllEdgeTraffic();
  ASSERT_EQ(expected.size(), repeated.size());
  for (size_t i = 0; i < expected.size(); ++i)
    EXPECT_FLOAT_EQ(expected[i].freightLoad, repeated[i].freightLoad);
  city.roads.removeRoad({12, 8}, {13, 8});
  city.roads.resetCongestion();
  const auto split =
      FreightSystem::simulate(city.map, city.roads, city.store, &cache);
  EXPECT_EQ(split.domesticGoods, 0);
  EXPECT_GT(split.unroutedGoods, 0);
  EXPECT_EQ(cache.topologyVersion, city.roads.getTopologyVersion());
}

TEST(FreightSystem, RouteCacheRemainsBounded) {
  CityMap map({256, 8});
  RoadNetwork roads(map);
  EntityStore store;
  for (int x = 0; x < 240; ++x)
    roads.buildRoad({x, 4}, {x + 1, 4});
  for (int x = 2; x < 162; ++x) {
    const auto id = store.createBuilding(BuildingType::Industrial, {x, 3}, 10);
    store.getBuilding(id)->occupancy = 1;
  }
  const auto consumer =
      store.createBuilding(BuildingType::Commercial, {240, 3}, 20000);
  store.getBuilding(consumer)->occupancy = 20000;
  FreightRouteCache cache;
  FreightSystem::simulate(map, roads, store, &cache);
  EXPECT_LE(cache.paths.size(), FreightSystem::kMaxCachedRoutes);
}

TEST(FreightSystem, ImportsRestockIndustryBeforeLocalRetailDelivery) {
  FreightCity city;
  city.add(BuildingType::Industrial, {6, 7}, 50);
  city.add(BuildingType::Commercial, {20, 7}, 100);
  const auto freight =
      FreightSystem::simulate(city.map, city.roads, city.store);
  EXPECT_EQ(freight.importedGoods, 50);
  EXPECT_EQ(freight.domesticGoods, 150);
  EXPECT_EQ(freight.exportedGoods, 0);
  EXPECT_EQ(freight.unroutedGoods, 0);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({0, 8}, {1, 8}), 2.0f);
  EXPECT_FLOAT_EQ(city.roads.getFreightLoad({6, 8}, {7, 8}), 5.0f);
}
