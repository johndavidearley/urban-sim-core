#include <gtest/gtest.h>

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
