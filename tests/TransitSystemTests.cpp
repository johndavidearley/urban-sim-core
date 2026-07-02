#include <gtest/gtest.h>

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/world/CityMap.hpp"

namespace {
// A straight road along y=0, x=0..length-1.
void buildStraightRoad(RoadNetwork& roads, int length) {
  for (int x = 0; x < length - 1; ++x) {
    roads.buildRoad({x, 0}, {x + 1, 0});
  }
}

TransitRoute makeRoute(TransitRouteId id, std::vector<Coord> stops, int vehicles = 2,
                        int capacityPerVehicle = 30, int radius = 4) {
  TransitRoute route;
  route.id = id;
  route.stops = std::move(stops);
  route.vehicleCount = vehicles;
  route.capacityPerVehicle = capacityPerVehicle;
  route.stopCoverageRadius = radius;
  return route;
}
} // namespace

TEST(TransitSystemTests, BuildCacheReachesTilesWithinStopCoverageRadius) {
  CityMap map({20, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 20);

  std::vector<TransitRoute> routes = {makeRoute(1, {{0, 0}}, 2, 30, 3)};
  TransitCoverageCache cache;
  TransitSystem::buildCache(roads, routes, cache);

  ASSERT_EQ(cache.entries.size(), 1u);
  const auto& field = cache.entries[0].distanceField;
  EXPECT_EQ(field.at(Coord{0, 0}), 0);
  EXPECT_EQ(field.at(Coord{3, 0}), 3);
  EXPECT_EQ(field.count(Coord{4, 0}), 0u);  // beyond the radius cap
}

TEST(TransitSystemTests, OffloadCarriesWorkersWhenBothEndsCovered) {
  CityMap map({20, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 20);

  std::vector<TransitRoute> routes = {makeRoute(1, {{0, 0}, {10, 0}}, 2, 30, 4)};
  TransitCoverageCache cache;
  TransitSystem::buildCache(roads, routes, cache);

  TransitOffload offload(routes, cache);
  const uint32_t carried = offload.offload({0, 0}, {10, 0}, 20);

  EXPECT_EQ(carried, 20u);
  EXPECT_EQ(offload.totalRidership(), 20u);
  EXPECT_EQ(offload.totalDemand(), 20u);
}

TEST(TransitSystemTests, OffloadReturnsZeroWhenNoRouteCoversBothEnds) {
  CityMap map({20, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 20);

  // Route only covers the home end; the work end (x=15) is far outside radius.
  std::vector<TransitRoute> routes = {makeRoute(1, {{0, 0}}, 2, 30, 3)};
  TransitCoverageCache cache;
  TransitSystem::buildCache(roads, routes, cache);

  TransitOffload offload(routes, cache);
  const uint32_t carried = offload.offload({0, 0}, {15, 0}, 20);

  EXPECT_EQ(carried, 0u);
  EXPECT_EQ(offload.totalRidership(), 0u);
  EXPECT_EQ(offload.totalDemand(), 0u);
}

TEST(TransitSystemTests, OffloadCapsRidershipAtRouteCapacityButStillCountsFullDemand) {
  CityMap map({20, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 20);

  // Small route: 1 vehicle x 10 capacity = 10 total.
  std::vector<TransitRoute> routes = {makeRoute(1, {{0, 0}, {10, 0}}, 1, 10, 4)};
  TransitCoverageCache cache;
  TransitSystem::buildCache(roads, routes, cache);

  TransitOffload offload(routes, cache);
  const uint32_t carried = offload.offload({0, 0}, {10, 0}, 25);  // more workers than capacity

  EXPECT_EQ(carried, 10u);              // capped at capacity
  EXPECT_EQ(offload.totalRidership(), 10u);
  EXPECT_EQ(offload.totalDemand(), 25u); // full demand still recorded

  // A second commute on the same exhausted route gets nothing further, but
  // its demand is still recorded since the route's coverage still reaches it.
  const uint32_t secondCarried = offload.offload({0, 0}, {10, 0}, 5);
  EXPECT_EQ(secondCarried, 0u);
  EXPECT_EQ(offload.totalRidership(), 10u);
  EXPECT_EQ(offload.totalDemand(), 30u);
}

TEST(TransitSystemTests, SummarizeAggregatesRoutesAndComputesModalShare) {
  CityMap map({20, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 20);

  std::vector<TransitRoute> routes = {
    makeRoute(1, {{0, 0}, {5, 0}}, 2, 30, 4),   // capacity 60
    makeRoute(2, {{10, 0}, {15, 0}}, 1, 20, 4)  // capacity 20
  };
  TransitCoverageCache cache;
  TransitSystem::buildCache(roads, routes, cache);

  TransitOffload offload(routes, cache);
  offload.offload({0, 0}, {5, 0}, 40);

  const TransitSummary summary = TransitSystem::summarize(routes, offload, /*totalCommuters=*/100);

  EXPECT_EQ(summary.totalRoutes, 2u);
  EXPECT_EQ(summary.totalStops, 4u);
  EXPECT_EQ(summary.totalCapacity, 80u);
  EXPECT_EQ(summary.ridership, 40u);
  EXPECT_EQ(summary.demand, 40u);
  EXPECT_FLOAT_EQ(summary.modalShare, 0.4f);
}

TEST(TransitSystemTests, NoRoutesMeansNoCoverageAndZeroSummary) {
  CityMap map({20, 3});
  RoadNetwork roads(map);
  buildStraightRoad(roads, 20);

  std::vector<TransitRoute> routes;
  TransitCoverageCache cache;
  TransitSystem::buildCache(roads, routes, cache);
  EXPECT_TRUE(cache.entries.empty());

  TransitOffload offload(routes, cache);
  EXPECT_EQ(offload.offload({0, 0}, {5, 0}, 10), 0u);

  const TransitSummary summary = TransitSystem::summarize(routes, offload, 50);
  EXPECT_EQ(summary.totalRoutes, 0u);
  EXPECT_EQ(summary.ridership, 0u);
  EXPECT_FLOAT_EQ(summary.modalShare, 0.0f);
}
