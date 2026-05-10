#include <gtest/gtest.h>
#include "src/networks/RoadNetwork.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/world/CityMap.hpp"

// Road network tests
TEST(RoadNetworkTests, CreateNetwork) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  EXPECT_EQ(network.getRoadCount(), 0);
  EXPECT_TRUE(network.hasNode({0, 0}));
  EXPECT_TRUE(network.hasNode({31, 31}));
}

TEST(RoadNetworkTests, BuildSingleRoad) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  
  EXPECT_TRUE(network.hasRoad({10, 10}, {10, 11}));
  EXPECT_EQ(network.getRoadCount(), 1);
  EXPECT_TRUE(map.getTile({10, 10}).hasRoad);
  EXPECT_TRUE(map.getTile({10, 11}).hasRoad);
}

TEST(RoadNetworkTests, BuildMultipleRoads) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  network.buildRoad({10, 11}, {11, 11});
  network.buildRoad({11, 11}, {11, 10});
  
  EXPECT_EQ(network.getRoadCount(), 3);
  EXPECT_TRUE(network.hasRoad({10, 10}, {10, 11}));
  EXPECT_TRUE(network.hasRoad({10, 11}, {11, 11}));
  EXPECT_TRUE(network.hasRoad({11, 11}, {11, 10}));
}

TEST(RoadNetworkTests, RemoveRoad) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  EXPECT_EQ(network.getRoadCount(), 1);
  
  network.removeRoad({10, 10}, {10, 11});
  EXPECT_EQ(network.getRoadCount(), 0);
  EXPECT_FALSE(network.hasRoad({10, 10}, {10, 11}));
}

TEST(RoadNetworkTests, RoadNonAdjacent) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  // Try to build road between non-adjacent tiles
  network.buildRoad({10, 10}, {12, 10});
  
  EXPECT_EQ(network.getRoadCount(), 0);
}

TEST(RoadNetworkTests, RoadBidirectional) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  
  // Should work in both directions
  EXPECT_TRUE(network.hasRoad({10, 10}, {10, 11}));
  EXPECT_TRUE(network.hasRoad({10, 11}, {10, 10}));
}

TEST(RoadNetworkTests, ConnectivitySingleRoad) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  network.updateConnectivity({10, 10});
  
  EXPECT_TRUE(network.isConnected({10, 10}));
  EXPECT_TRUE(network.isConnected({10, 11}));
  EXPECT_FALSE(network.isConnected({11, 11})); // Not connected
}

TEST(RoadNetworkTests, ConnectivityLinearPath) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  // Build a linear path
  network.buildRoad({10, 10}, {10, 11});
  network.buildRoad({10, 11}, {10, 12});
  network.buildRoad({10, 12}, {10, 13});
  
  network.updateConnectivity({10, 10});
  
  EXPECT_TRUE(network.isConnected({10, 10}));
  EXPECT_TRUE(network.isConnected({10, 11}));
  EXPECT_TRUE(network.isConnected({10, 12}));
  EXPECT_TRUE(network.isConnected({10, 13}));
  EXPECT_FALSE(network.isConnected({11, 10}));
}

TEST(RoadNetworkTests, ConnectivityBranching) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  // Build branching path
  network.buildRoad({10, 10}, {10, 11});
  network.buildRoad({10, 11}, {10, 12});
  network.buildRoad({10, 11}, {11, 11});
  
  network.updateConnectivity({10, 10});
  
  EXPECT_TRUE(network.isConnected({10, 10}));
  EXPECT_TRUE(network.isConnected({10, 11}));
  EXPECT_TRUE(network.isConnected({10, 12}));
  EXPECT_TRUE(network.isConnected({11, 11}));
}

TEST(RoadNetworkTests, GetRoadTiles) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  network.buildRoad({10, 11}, {11, 11});
  
  auto roadTiles = network.getAllRoadTiles();
  
  EXPECT_EQ(roadTiles.size(), 3);
}

TEST(RoadNetworkTests, CongestionUpdate) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  
  EXPECT_FLOAT_EQ(network.getCongestion({10, 10}, {10, 11}), 0.0f);
  
  network.updateCongestion({10, 10}, {10, 11}, 5.0f);
  EXPECT_FLOAT_EQ(network.getCongestion({10, 10}, {10, 11}), 0.5f); // 5/10 capacity
}

// Pathfinding tests
TEST(PathfindingTests, PathSameLocation) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  auto path = Pathfinding::findShortestPath(network, {10, 10}, {10, 10});
  
  EXPECT_TRUE(path.found);
  EXPECT_EQ(path.waypoints.size(), 1);
  EXPECT_EQ(path.waypoints[0], glm::ivec2(10, 10));
  EXPECT_FLOAT_EQ(path.totalDistance, 0.0f);
}

TEST(PathfindingTests, PathDirectAdjacent) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  
  auto path = Pathfinding::findShortestPath(network, {10, 10}, {10, 11});
  
  EXPECT_TRUE(path.found);
  EXPECT_EQ(path.waypoints.size(), 2);
  EXPECT_EQ(path.waypoints[0], glm::ivec2(10, 10));
  EXPECT_EQ(path.waypoints[1], glm::ivec2(10, 11));
}

TEST(PathfindingTests, PathLinear) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  network.buildRoad({10, 10}, {10, 11});
  network.buildRoad({10, 11}, {10, 12});
  network.buildRoad({10, 12}, {10, 13});
  
  auto path = Pathfinding::findShortestPath(network, {10, 10}, {10, 13});
  
  EXPECT_TRUE(path.found);
  EXPECT_EQ(path.waypoints.size(), 4);
  EXPECT_FLOAT_EQ(path.totalDistance, 3.0f);
}

TEST(PathfindingTests, PathNoConnection) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  // No roads, so no path
  auto path = Pathfinding::findShortestPath(network, {10, 10}, {20, 20});
  
  EXPECT_FALSE(path.found);
}

TEST(PathfindingTests, PathWithBranch) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  // Create a T-shaped path
  network.buildRoad({10, 10}, {10, 11});
  network.buildRoad({10, 11}, {10, 12});
  network.buildRoad({10, 11}, {11, 11});
  network.buildRoad({11, 11}, {12, 11});
  
  // Path should choose the straight line
  auto path1 = Pathfinding::findShortestPath(network, {10, 10}, {10, 12});
  EXPECT_TRUE(path1.found);
  EXPECT_EQ(path1.waypoints.size(), 3);
  
  // Path to the branch
  auto path2 = Pathfinding::findShortestPath(network, {10, 10}, {12, 11});
  EXPECT_TRUE(path2.found);
  EXPECT_EQ(path2.waypoints.size(), 4);
}

TEST(PathfindingTests, PathShouldTakeShortest) {
  CityMap map({32, 32});
  RoadNetwork network(map);
  
  // Create a grid pattern
  // Direct path: (10,10) -> (10,11) -> (10,12) [distance 2]
  // Long path: (10,10) -> (11,10) -> (11,11) -> (11,12) -> (10,12) [distance 4]
  
  // Build short path
  network.buildRoad({10, 10}, {10, 11});
  network.buildRoad({10, 11}, {10, 12});
  
  // Build long path
  network.buildRoad({10, 10}, {11, 10});
  network.buildRoad({11, 10}, {11, 11});
  network.buildRoad({11, 11}, {11, 12});
  network.buildRoad({11, 12}, {10, 12});
  
  auto path = Pathfinding::findShortestPath(network, {10, 10}, {10, 12});
  
  EXPECT_TRUE(path.found);
  EXPECT_FLOAT_EQ(path.totalDistance, 2.0f);
  EXPECT_EQ(path.waypoints.size(), 3);
}

TEST(PathfindingTests, Heuristic) {
  glm::ivec2 a{0, 0};
  glm::ivec2 b{3, 4};
  
  float h = Pathfinding::heuristic(a, b);
  EXPECT_FLOAT_EQ(h, 7.0f); // Manhattan distance: |3-0| + |4-0|
}

TEST(PathfindingTests, HeuristicSymmetric) {
  glm::ivec2 a{5, 10};
  glm::ivec2 b{8, 12};
  
  float h1 = Pathfinding::heuristic(a, b);
  float h2 = Pathfinding::heuristic(b, a);
  
  EXPECT_FLOAT_EQ(h1, h2);
}
