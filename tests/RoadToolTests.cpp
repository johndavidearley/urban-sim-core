#include "src/persistence/ReplayVerifier.hpp"
#include "src/systems/TrafficMicroSim.hpp"
#include <limits>
#include "src/networks/Pathfinding.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/entities/PopulationStore.hpp"
#include <filesystem>
#include "gtest/gtest.h"

#include "src/gameplay/RoadTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

TEST(RoadToolTests, PlansDeterministicManhattanRouteAndChargesPerNewSegment) {
  CityMap map({8, 8});
  RoadNetwork roads(map);

  const RoadPlan plan = RoadTool::plan(map, roads, {1, 1}, {3, 3}, 1000);

  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.tiles, (std::vector<Coord>{{1, 1}, {2, 1}, {3, 1}, {3, 2}, {3, 3}}));
  EXPECT_EQ(plan.newSegments, 4);
  EXPECT_EQ(plan.cost, 400);
}

TEST(RoadToolTests, RejectsWaterBuildingsAndUnaffordableRoutes) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  map.getTile({2, 1}).type = 2;
  EXPECT_FALSE(RoadTool::plan(map, roads, {1, 1}, {3, 1}, 1000).valid);

  map.getTile({2, 1}).type = 0;
  map.getTile({2, 1}).buildingId = 42;
  EXPECT_FALSE(RoadTool::plan(map, roads, {1, 1}, {3, 1}, 1000).valid);

  map.getTile({2, 1}).buildingId = 0;
  EXPECT_FALSE(RoadTool::plan(map, roads, {1, 1}, {3, 1}, 100).valid);
}

TEST(RoadToolTests, BuildsRoadAndDeductsFundsWithoutChargingExistingSegments) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  roads.buildRoad({1, 1}, {2, 1});
  int64_t funds = 500;

  const RoadPlan plan = RoadTool::plan(map, roads, {1, 1}, {3, 1}, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.newSegments, 1);
  EXPECT_EQ(plan.cost, 100);
  ASSERT_TRUE(RoadTool::build(map, roads, plan, funds));
  EXPECT_EQ(funds, 400);
  EXPECT_TRUE(roads.hasRoad({2, 1}, {3, 1}));
}

TEST(RoadToolTests,
     ArterialUpgradeChargesOnlyLocalSegmentsAndPreservesTopology) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  int64_t funds = 10000;
  auto local = RoadTool::plan(map, roads, {2, 8}, {6, 8}, funds);
  ASSERT_TRUE(RoadTool::build(map, roads, local, funds));
  const auto topology = roads.getTopologyVersion();
  roads.updateCongestion({2, 8}, {3, 8}, 20.0f);
  const auto upgrade =
      RoadTool::plan(map, roads, {2, 8}, {6, 8}, funds, RoadClass::Arterial);
  ASSERT_TRUE(upgrade.valid);
  EXPECT_EQ(upgrade.newSegments, 0);
  EXPECT_EQ(upgrade.upgradedSegments, 4);
  EXPECT_EQ(upgrade.cost, 5600);
  ASSERT_TRUE(RoadTool::build(map, roads, upgrade, funds));
  EXPECT_EQ(funds, 4000);
  EXPECT_EQ(roads.getTopologyVersion(), topology);
  EXPECT_EQ(roads.getRoadClass({2, 8}, {3, 8}), RoadClass::Arterial);
  EXPECT_FLOAT_EQ(roads.getEdgeCapacity({2, 8}, {3, 8}), kArterialRoadCapacity);
  EXPECT_FLOAT_EQ(roads.getCongestion({2, 8}, {3, 8}), 20.0f / kArterialRoadCapacity);
  // Autonomous re-laying and local tool selection never downgrade an arterial.
  roads.buildRoad({2, 8}, {3, 8});
  EXPECT_EQ(roads.getRoadClass({2, 8}, {3, 8}), RoadClass::Arterial);
  EXPECT_FALSE(RoadTool::plan(map, roads, {2, 8}, {6, 8}, funds).valid);
  EXPECT_FALSE(
      RoadTool::plan(map, roads, {2, 8}, {6, 8}, funds, RoadClass::Arterial)
          .valid);
}

TEST(RoadToolTests,
     ArterialMixedRouteCostsAndInsufficientFundsAreTransactional) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({2, 8}, {3, 8});
  const auto mixed =
      RoadTool::plan(map, roads, {2, 8}, {4, 8}, 5000, RoadClass::Arterial);
  ASSERT_TRUE(mixed.valid);
  EXPECT_EQ(mixed.cost, 2900);
  int64_t funds = 2899;
  EXPECT_FALSE(RoadTool::build(map, roads, mixed, funds));
  EXPECT_EQ(funds, 2899);
  EXPECT_EQ(roads.getRoadClass({2, 8}, {3, 8}), RoadClass::Local);
  EXPECT_FALSE(roads.hasRoad({3, 8}, {4, 8}));
  funds = 5000;
  ASSERT_TRUE(RoadTool::build(map, roads, mixed, funds));
  EXPECT_EQ(funds, 2100);
  EXPECT_EQ(roads.getRoadClass({3, 8}, {4, 8}), RoadClass::Arterial);
}

TEST(RoadAlignmentTests, FreeformAndSnappedControlPointsProduceConnectedBuildablePaths) {
  CityMap map({24,24});RoadNetwork roads(map);
  const auto free=RoadTool::curve({2.2f,3.3f},{8.4f,2.2f},{14.4f,8.2f},RoadClass::Local,false);
  const auto snapped=RoadTool::curve({2.2f,3.3f},{8.4f,2.2f},{14.4f,8.2f},RoadClass::Local,true);
  EXPECT_FLOAT_EQ(free.curves[0].points[0].x,2.2f);
  EXPECT_FLOAT_EQ(snapped.curves[0].points[0].x,2.5f);
  EXPECT_FLOAT_EQ(snapped.curves[0].points[3].y,8.5f);
  int64_t funds=100000;
  const auto plan=RoadTool::planAlignment(map,roads,free,funds);
  ASSERT_TRUE(plan.valid)<<plan.error;
  for(size_t i=1;i<plan.tiles.size();++i)
    EXPECT_EQ(std::abs(plan.tiles[i].x-plan.tiles[i-1].x)+std::abs(plan.tiles[i].y-plan.tiles[i-1].y),1);
  ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
  ASSERT_EQ(roads.alignments().size(),1u);
  EXPECT_FLOAT_EQ(roads.alignments()[0].curves[0].points[0].x,2.2f);
  for(Coord tile:plan.alignment.footprint) EXPECT_TRUE(map.getTile(tile).hasRoad);
}

TEST(RoadAlignmentTests, WidthAndFreshObstacleChecksPreventInvalidConstruction) {
  CityMap map({24,24});RoadNetwork roads(map);int64_t funds=100000;
  const auto shape=RoadTool::straight({3.1f,4.1f},{14.1f,4.1f},RoadClass::Highway,false);
  const auto plan=RoadTool::planAlignment(map,roads,shape,funds);
  ASSERT_TRUE(plan.valid)<<plan.error;
  map.getTile({8,3}).buildingId=123; // Outside centerline, inside the highway's width.
  EXPECT_FALSE(RoadTool::build(map,roads,plan,funds));
  EXPECT_EQ(roads.getRoadCount(),0u);EXPECT_EQ(funds,100000);
  map.getTile({8,3}).buildingId=0;map.getTile({8,3}).type=2;
  EXPECT_FALSE(RoadTool::planAlignment(map,roads,shape,funds).valid);
}

TEST(RoadAlignmentTests, RoundaboutRoutesClockwiseAndKeepsItsIslandClear) {
  CityMap map({24,24});RoadNetwork roads(map);int64_t funds=100000;
  const auto plan=RoadTool::planAlignment(map,roads,RoadTool::roundabout({10.5f,10.5f},3,RoadClass::Local,true),funds);
  ASSERT_TRUE(plan.valid)<<plan.error;
  ASSERT_EQ(plan.tiles.front(),plan.tiles.back());
  ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
  EXPECT_FALSE(map.getTile({10,10}).hasRoad);
  const auto from=plan.tiles[0],to=plan.tiles[1];
  EXPECT_EQ(Pathfinding::findShortestPath(roads,from,to).waypoints.size(),2u);
  const auto reverse=Pathfinding::findShortestPath(roads,to,from);
  ASSERT_TRUE(reverse.found);
  EXPECT_GT(reverse.waypoints.size(),2u);
  EXPECT_FALSE(roads.canTravel(to,from));
}

TEST(RoadAlignmentTests, HighwayExitAndEntranceConnectAndRespectRampDirection) {
  CityMap map({32,32});RoadNetwork roads(map);int64_t funds=1000000;
  auto highway=RoadTool::planAlignment(map,roads,RoadTool::straight({3.5f,8.5f},{27.5f,8.5f},RoadClass::Highway,true),funds);
  ASSERT_TRUE(highway.valid)<<highway.error;
  ASSERT_TRUE(RoadTool::build(map,roads,highway,funds));
  auto exit=RoadTool::planAlignment(map,roads,RoadTool::curve({12.5f,8.5f},{17.5f,8.5f},{18.5f,17.5f},RoadClass::Ramp,false),funds);
  ASSERT_TRUE(exit.valid)<<exit.error;
  ASSERT_TRUE(RoadTool::build(map,roads,exit,funds));
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,{3,8},exit.tiles.back()).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(roads,exit.tiles.back(),{3,8}).found);
  auto entrance=RoadTool::planAlignment(map,roads,RoadTool::curve({5.5f,19.5f},{4.5f,8.5f},{8.5f,8.5f},RoadClass::Ramp,false),funds);
  ASSERT_TRUE(entrance.valid)<<entrance.error;
  ASSERT_TRUE(RoadTool::build(map,roads,entrance,funds));
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,entrance.tiles.front(),{27,8}).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(roads,{27,8},entrance.tiles.front()).found);
  EXPECT_FLOAT_EQ(roads.getEdgeCapacity({3,8},{4,8}),800.f);
}

TEST(RoadAlignmentTests, SaveLoadRetainsExactGeometryAndOneWayRoutingAndRejectsCorruption) {
  CityMap map({24,24});RoadNetwork roads(map);EntityStore store;PopulationStore people;int64_t funds=100000;
  auto plan=RoadTool::planAlignment(map,roads,RoadTool::curve({2.2f,3.3f},{9.1f,2.6f},{15.4f,8.2f},RoadClass::Ramp,false),funds);
  ASSERT_TRUE(plan.valid)<<plan.error;ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
  const auto path=(std::filesystem::temp_directory_path()/"urban-road-alignment-test.json").string();
  ASSERT_TRUE(SaveLoadSystem::saveToFile(path,map,roads,store,people));
  CityMap restored({24,24});RoadNetwork loaded(restored);EntityStore loadedStore;PopulationStore loadedPeople;
  ASSERT_TRUE(SaveLoadSystem::loadFromFile(path,restored,loaded,loadedStore,loadedPeople));
  std::filesystem::remove(path);
  ASSERT_EQ(loaded.alignments().size(),1u);
  EXPECT_EQ(loaded.alignments()[0].path,roads.alignments()[0].path);
  for(size_t i=0;i<4;++i) EXPECT_TRUE(loaded.alignments()[0].curves[0].points[i]==roads.alignments()[0].curves[0].points[i]);
  EXPECT_TRUE(Pathfinding::findShortestPath(loaded,plan.tiles.front(),plan.tiles.back()).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(loaded,plan.tiles.back(),plan.tiles.front()).found);
  auto snapshot=SaveLoadSystem::captureSnapshot(map,roads,store,people);
  snapshot.roadAlignments[0].curves[0].points[0].x=-100;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,restored,loaded,loadedStore,loadedPeople));
  EXPECT_EQ(loaded.alignments().size(),1u); // Rejected before mutating the city.
}

TEST(RoadAlignmentTests, BulldozingAnyPartRemovesTheWholeAlignmentAndReleasesItsFootprint) {
  CityMap map({24,24});RoadNetwork roads(map);EntityStore store;std::vector<ServiceFacility> facilities;int64_t funds=100000;
  auto plan=RoadTool::planAlignment(map,roads,RoadTool::curve({2.2f,3.3f},{9.1f,2.6f},{15.4f,8.2f},RoadClass::Local,false),funds);
  ASSERT_TRUE(plan.valid)<<plan.error;ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
  const auto tile=plan.tiles[plan.tiles.size()/2];
  const auto demolition=BulldozeTool::plan(map,roads,facilities,tile,tile,funds);
  ASSERT_TRUE(demolition.valid);ASSERT_TRUE(BulldozeTool::apply(map,roads,store,facilities,demolition,funds));
  EXPECT_TRUE(roads.alignments().empty());EXPECT_EQ(roads.getRoadCount(),0u);
  for(Coord p:plan.alignment.footprint) EXPECT_FALSE(map.getTile(p).hasRoad);
}

TEST(RoadAlignmentTests, RemovingAnExitPreservesTheSharedHighwayApproach) {
  CityMap map({32,32});RoadNetwork roads(map);EntityStore store;std::vector<ServiceFacility> facilities;int64_t funds=1000000;
  auto highway=RoadTool::planAlignment(map,roads,RoadTool::straight({3.5f,8.5f},{27.5f,8.5f},RoadClass::Highway,true),funds);
  ASSERT_TRUE(RoadTool::build(map,roads,highway,funds));
  const auto highwayEdges=roads.getRoadCount();
  auto exit=RoadTool::planAlignment(map,roads,RoadTool::curve({12.5f,8.5f},{17.5f,8.5f},{18.5f,17.5f},RoadClass::Ramp,false),funds);
  ASSERT_TRUE(exit.valid)<<exit.error;ASSERT_TRUE(RoadTool::build(map,roads,exit,funds));
  const auto last=exit.tiles.back();
  const auto demolition=BulldozeTool::plan(map,roads,facilities,last,last,funds);
  ASSERT_TRUE(demolition.valid);ASSERT_TRUE(BulldozeTool::apply(map,roads,store,facilities,demolition,funds));
  EXPECT_EQ(roads.getRoadCount(),highwayEdges);
  ASSERT_EQ(roads.alignments().size(),1u);EXPECT_EQ(roads.alignments()[0].roadClass,RoadClass::Highway);
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,{3,8},{27,8}).found);
}

TEST(RoadCrossingTests, OverpassesAndUnderpassesCrossWithoutCreatingJunctions) {
  for(int level:{-1,1}) {
    CityMap map({40,40});RoadNetwork roads(map);int64_t funds=1000000;
    const auto street=RoadTool::plan(map,roads,{20,4},{20,35},funds);
    ASSERT_TRUE(RoadTool::build(map,roads,street,funds));
    auto geometry=RoadTool::straight({4.5f,20.5f},{35.5f,20.5f},RoadClass::Highway,true);
    geometry.elevation=level;
    const auto plan=RoadTool::planAlignment(map,roads,geometry,funds);
    ASSERT_TRUE(plan.valid)<<plan.error;
    ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
    const auto across=Pathfinding::findShortestPath(roads,{4,20},{35,20});
    ASSERT_TRUE(across.found);EXPECT_FLOAT_EQ(across.totalDistance,31.f);
    EXPECT_EQ(across.waypoints.size(),2u);
    EXPECT_FALSE(Pathfinding::findShortestPath(roads,{4,20},{20,4}).found);
    EXPECT_TRUE(Pathfinding::findShortestPath(roads,{20,4},{20,35}).found);
    EXPECT_EQ(roads.distanceField({{4,20}},10).count({35,20}),0u);
    EXPECT_EQ(roads.distanceField({{4,20}},40).at({35,20}),31);
    EXPECT_EQ(roads.edgeElevation({4,20},{35,20}),level);
    EXPECT_NEAR(roads.alignments().back().height(.5f),level*.8f,.0001f);
  }
}

TEST(RoadCrossingTests, ApproachConnectionsAndOneWayRampsWorkWithoutInteriorAccess) {
  CityMap map({40,40});RoadNetwork roads(map);int64_t funds=1000000;
  auto geometry=RoadTool::curve({4.5f,20.5f},{20.5f,13.5f},{35.5f,20.5f},RoadClass::Ramp,false);
  geometry.elevation=1;
  const auto plan=RoadTool::planAlignment(map,roads,geometry,funds);
  ASSERT_TRUE(plan.valid)<<plan.error;ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
  auto approach=RoadTool::plan(map,roads,{1,20},{4,20},funds);
  ASSERT_TRUE(approach.valid)<<approach.error;ASSERT_TRUE(RoadTool::build(map,roads,approach,funds));
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,{1,20},{35,20}).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(roads,{35,20},{1,20}).found);
  const auto middle=plan.tiles[plan.tiles.size()/2];
  EXPECT_FALSE(roads.hasRoadAdjacency(middle));
}

TEST(RoadCrossingTests, ClearanceChecksApplyInEitherConstructionOrderAndRevalidatePlans) {
  CityMap map({40,40});RoadNetwork roads(map);int64_t funds=1000000;
  auto geometry=RoadTool::straight({4.5f,20.5f},{35.5f,20.5f},RoadClass::Local,true);geometry.elevation=1;
  auto plan=RoadTool::planAlignment(map,roads,geometry,funds);
  ASSERT_TRUE(plan.valid)<<plan.error;
  auto slopeStreet=RoadTool::plan(map,roads,{6,10},{6,30},funds);
  ASSERT_TRUE(RoadTool::build(map,roads,slopeStreet,funds));
  const auto cash=funds;EXPECT_FALSE(RoadTool::build(map,roads,plan,funds));EXPECT_EQ(funds,cash);
  roads.clear();
  plan=RoadTool::planAlignment(map,roads,geometry,funds);
  ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
  EXPECT_FALSE(RoadTool::plan(map,roads,{6,10},{6,30},funds).valid);
  auto shortGeometry=RoadTool::straight({4.5f,5.5f},{10.5f,5.5f},RoadClass::Local,true);shortGeometry.elevation=1;
  EXPECT_FALSE(RoadTool::planAlignment(map,roads,shortGeometry,funds).valid);
}

TEST(RoadCrossingTests, DemolitionRemovesOnlyTheSelectedLevel) {
  for(int level:{-1,1}) {
    CityMap map({40,40});RoadNetwork roads(map);EntityStore store;std::vector<ServiceFacility> facilities;int64_t funds=1000000;
    auto street=RoadTool::plan(map,roads,{20,4},{20,35},funds);
    ASSERT_TRUE(RoadTool::build(map,roads,street,funds));
    const auto streetCount=roads.getRoadCount();
    auto geometry=RoadTool::straight({4.5f,20.5f},{35.5f,20.5f},RoadClass::Local,true);geometry.elevation=level;
    auto plan=RoadTool::planAlignment(map,roads,geometry,funds);
    ASSERT_TRUE(plan.valid)<<plan.error;ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
    const auto demolition=BulldozeTool::plan(map,roads,facilities,{20,20},{20,20},funds,level);
    ASSERT_TRUE(demolition.valid)<<demolition.error;
    EXPECT_EQ(demolition.roadEdges.size(),1u);
    ASSERT_TRUE(BulldozeTool::apply(map,roads,store,facilities,demolition,funds));
    EXPECT_EQ(roads.getRoadCount(),streetCount);
    EXPECT_TRUE(roads.alignments().empty());
    EXPECT_TRUE(Pathfinding::findShortestPath(roads,{20,4},{20,35}).found);
    EXPECT_TRUE(map.getTile({20,20}).hasRoad);
  }
}

TEST(RoadCrossingTests, SaveLoadRetainsSeparationLengthAndRejectsForgedLinks) {
  CityMap map({40,40});RoadNetwork roads(map);EntityStore store;PopulationStore people;int64_t funds=1000000;
  auto street=RoadTool::plan(map,roads,{20,4},{20,35},funds);
  ASSERT_TRUE(RoadTool::build(map,roads,street,funds));
  auto geometry=RoadTool::straight({4.5f,20.5f},{35.5f,20.5f},RoadClass::Local,true);geometry.elevation=-1;
  const auto plan=RoadTool::planAlignment(map,roads,geometry,funds);
  ASSERT_TRUE(plan.valid)<<plan.error;ASSERT_TRUE(RoadTool::build(map,roads,plan,funds));
  const auto file=(std::filesystem::temp_directory_path()/"urban-crossing-test.json").string();
  ASSERT_TRUE(SaveLoadSystem::saveToFile(file,map,roads,store,people));
  CityMap loadedMap({40,40});RoadNetwork loaded(loadedMap);EntityStore loadedStore;PopulationStore loadedPeople;
  ASSERT_TRUE(SaveLoadSystem::loadFromFile(file,loadedMap,loaded,loadedStore,loadedPeople));std::filesystem::remove(file);
  EXPECT_EQ(loaded.alignments()[0].elevation,-1);
  EXPECT_FLOAT_EQ(loaded.edgeLength({4,20},{35,20}),31.f);
  EXPECT_FALSE(Pathfinding::findShortestPath(loaded,{4,20},{20,4}).found);
  auto snapshot=SaveLoadSystem::captureSnapshot(map,roads,store,people);
  for(auto& edge:snapshot.roads) if(edge.elevation!=0) edge.length=1;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,loadedMap,loaded,loadedStore,loadedPeople));
  EXPECT_EQ(loaded.alignments().size(),1u);
}

TEST(ElevatedRampTests, EntranceAndExitJoinDeckWithoutConnectingTheStreetBelow) {
  CityMap map({64,64});RoadNetwork roads(map);int64_t funds=2000000;
  auto street=RoadTool::plan(map,roads,{24,4},{24,55},funds);
  ASSERT_TRUE(RoadTool::build(map,roads,street,funds));
  auto highway=RoadTool::straight({4.5f,20.5f},{52.5f,20.5f},RoadClass::Highway,true);highway.elevation=1;
  auto h=RoadTool::planAlignment(map,roads,highway,funds);
  ASSERT_TRUE(h.valid)<<h.error;ASSERT_TRUE(RoadTool::build(map,roads,h,funds));
  roads.updateCongestion({4,20},{52,20},16);
  auto exit=RoadTool::curve({24.5f,20.5f},{32.5f,20.5f},{38.5f,38.5f},RoadClass::Ramp,false);exit.elevation=1;
  auto e=RoadTool::planAlignment(map,roads,exit,funds);
  ASSERT_TRUE(e.valid)<<e.error;EXPECT_EQ(e.alignment.startLevel,1);EXPECT_EQ(e.alignment.endLevel,0);
  EXPECT_FLOAT_EQ(e.alignment.height(0),.8f);EXPECT_FLOAT_EQ(e.alignment.height(1),0);
  ASSERT_TRUE(RoadTool::build(map,roads,e,funds));
  const auto deck=roadNode({24,20},1);
  ASSERT_NE(roads.getNode(deck),nullptr);EXPECT_EQ(roads.getNode(deck)->adjacent.size(),3u);
  EXPECT_NE(roads.getNode(deck),roads.getNode({24,20}));
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,{4,20},{38,38}).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(roads,{38,38},{4,20}).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(roads,{24,4},deck).found);
  auto entrance=RoadTool::curve({10.5f,38.5f},{12.5f,20.5f},{20.5f,20.5f},RoadClass::Ramp,false);entrance.elevation=1;
  auto entry=RoadTool::planAlignment(map,roads,entrance,funds);
  ASSERT_TRUE(entry.valid)<<entry.error;EXPECT_EQ(entry.alignment.endLevel,1);
  ASSERT_TRUE(RoadTool::build(map,roads,entry,funds));
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,{10,38},{52,20}).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(roads,{52,20},{10,38}).found);
  EXPECT_FLOAT_EQ(Pathfinding::findShortestPath(roads,{4,20},{52,20}).totalDistance,48.f);
  EXPECT_EQ(roads.alignments()[0].deckJunctions.size(),2u);
  for(const auto& edge:roads.getAllEdgeTraffic()) if(edge.roadClass==RoadClass::Highway) {
    EXPECT_FLOAT_EQ(edge.currentLoad,16);EXPECT_FLOAT_EQ(edge.capacity,800);
  }
  for(Coord tile:roads.getAllRoadTiles()) EXPECT_TRUE(map.isValid(tile));
}

TEST(ElevatedRampTests, SplittingPreservesOneWayHighwayDirectionAndInvalidatesRoutingCache) {
  CityMap map({64,64});RoadNetwork roads(map);int64_t funds=2000000;
  auto highway=RoadTool::straight({4.5f,20.5f},{52.5f,20.5f},RoadClass::Highway,true);highway.elevation=1;highway.oneWay=true;
  auto h=RoadTool::planAlignment(map,roads,highway,funds);ASSERT_TRUE(RoadTool::build(map,roads,h,funds));
  const auto version=roads.getRoutingVersion();
  auto exit=RoadTool::curve({24.5f,20.5f},{32.5f,20.5f},{38.5f,38.5f},RoadClass::Ramp,false);exit.elevation=1;
  auto e=RoadTool::planAlignment(map,roads,exit,funds);ASSERT_TRUE(e.valid)<<e.error;ASSERT_TRUE(RoadTool::build(map,roads,e,funds));
  EXPECT_GT(roads.getRoutingVersion(),version);
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,{4,20},{52,20}).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(roads,{52,20},{4,20}).found);
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,{4,20},{38,38}).found);
}

TEST(ElevatedRampTests, DemolishingRampPreservesHighwayAndDemolishingHostRemovesAttachedRamps) {
  CityMap map({64,64});RoadNetwork roads(map);EntityStore store;std::vector<ServiceFacility> facilities;int64_t funds=2000000;
  auto street=RoadTool::plan(map,roads,{24,4},{24,55},funds);ASSERT_TRUE(RoadTool::build(map,roads,street,funds));
  auto highway=RoadTool::straight({4.5f,20.5f},{52.5f,20.5f},RoadClass::Highway,true);highway.elevation=1;
  auto h=RoadTool::planAlignment(map,roads,highway,funds);ASSERT_TRUE(RoadTool::build(map,roads,h,funds));
  auto exit=RoadTool::curve({24.5f,20.5f},{32.5f,20.5f},{38.5f,38.5f},RoadClass::Ramp,false);exit.elevation=1;
  auto e=RoadTool::planAlignment(map,roads,exit,funds);ASSERT_TRUE(e.valid)<<e.error;ASSERT_TRUE(RoadTool::build(map,roads,e,funds));
  auto demolition=BulldozeTool::plan(map,roads,facilities,{38,38},{38,38},funds,1);
  ASSERT_TRUE(demolition.valid);ASSERT_TRUE(BulldozeTool::apply(map,roads,store,facilities,demolition,funds));
  ASSERT_EQ(roads.alignments().size(),1u);
  EXPECT_FLOAT_EQ(Pathfinding::findShortestPath(roads,{4,20},{52,20}).totalDistance,48);
  e=RoadTool::planAlignment(map,roads,exit,funds);ASSERT_TRUE(e.valid)<<e.error;ASSERT_TRUE(RoadTool::build(map,roads,e,funds));
  demolition=BulldozeTool::plan(map,roads,facilities,{4,20},{4,20},funds,1);
  ASSERT_TRUE(demolition.valid);ASSERT_TRUE(BulldozeTool::apply(map,roads,store,facilities,demolition,funds));
  EXPECT_TRUE(roads.alignments().empty());
  EXPECT_TRUE(Pathfinding::findShortestPath(roads,{24,4},{24,55}).found);
  EXPECT_FALSE(roads.hasNode(roadNode({24,20},1)));
}

TEST(ElevatedRampTests, StaleTargetRejectsPlacementWithoutSpendingMoneyOrMutatingTheGraph) {
  CityMap map({64,64});RoadNetwork roads(map);EntityStore store;std::vector<ServiceFacility> facilities;int64_t funds=2000000;
  auto highway=RoadTool::straight({4.5f,20.5f},{52.5f,20.5f},RoadClass::Highway,true);highway.elevation=1;
  auto h=RoadTool::planAlignment(map,roads,highway,funds);ASSERT_TRUE(RoadTool::build(map,roads,h,funds));
  auto exit=RoadTool::curve({24.5f,20.5f},{32.5f,20.5f},{38.5f,38.5f},RoadClass::Ramp,false);exit.elevation=1;
  auto e=RoadTool::planAlignment(map,roads,exit,funds);ASSERT_TRUE(e.valid)<<e.error;
  auto demolition=BulldozeTool::plan(map,roads,facilities,{4,20},{4,20},funds,1);
  ASSERT_TRUE(BulldozeTool::apply(map,roads,store,facilities,demolition,funds));
  const auto cash=funds;EXPECT_FALSE(RoadTool::build(map,roads,e,funds));
  EXPECT_EQ(funds,cash);EXPECT_EQ(roads.getRoadCount(),0u);EXPECT_TRUE(roads.alignments().empty());
}

TEST(ElevatedRampTests, SaveLoadRetainsDeckConnectivityAndRejectsDanglingAttachments) {
  CityMap map({64,64});RoadNetwork roads(map);EntityStore store;PopulationStore people;int64_t funds=2000000;
  auto highway=RoadTool::straight({4.5f,20.5f},{52.5f,20.5f},RoadClass::Highway,true);highway.elevation=1;
  auto h=RoadTool::planAlignment(map,roads,highway,funds);ASSERT_TRUE(RoadTool::build(map,roads,h,funds));
  auto exit=RoadTool::curve({24.5f,20.5f},{32.5f,20.5f},{38.5f,38.5f},RoadClass::Ramp,false);exit.elevation=1;
  auto e=RoadTool::planAlignment(map,roads,exit,funds);ASSERT_TRUE(e.valid)<<e.error;ASSERT_TRUE(RoadTool::build(map,roads,e,funds));
  const auto checksum=ReplayVerifier::calculateChecksum(map,roads,store,people);
  const auto file=(std::filesystem::temp_directory_path()/"urban-elevated-ramp-test.json").string();
  ASSERT_TRUE(SaveLoadSystem::saveToFile(file,map,roads,store,people));
  CityMap loadedMap({64,64});RoadNetwork loaded(loadedMap);EntityStore loadedStore;PopulationStore loadedPeople;
  ASSERT_TRUE(SaveLoadSystem::loadFromFile(file,loadedMap,loaded,loadedStore,loadedPeople));std::filesystem::remove(file);
  EXPECT_EQ(ReplayVerifier::calculateChecksum(loadedMap,loaded,loadedStore,loadedPeople),checksum);
  EXPECT_TRUE(Pathfinding::findShortestPath(loaded,{4,20},{38,38}).found);
  EXPECT_FALSE(Pathfinding::findShortestPath(loaded,{38,38},{4,20}).found);
  ASSERT_EQ(loaded.alignments().size(),2u);EXPECT_EQ(loaded.alignments()[1].startLevel,1);
  auto snapshot=SaveLoadSystem::captureSnapshot(map,roads,store,people);
  snapshot.roadAlignments[0].deckJunctions={std::numeric_limits<size_t>::max()};
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,loadedMap,loaded,loadedStore,loadedPeople));
  snapshot=SaveLoadSystem::captureSnapshot(map,roads,store,people);
  snapshot.roadAlignments[0].deckJunctions.clear();
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,loadedMap,loaded,loadedStore,loadedPeople));
  EXPECT_EQ(loaded.alignments().size(),2u);
}

TEST(ElevatedRampTests, VehiclePositionsUsePhysicalTilesForDeckNodes) {
  Vehicle vehicle;vehicle.type=VehicleType::Emergency;
  vehicle.route={roadNode({24,20},1),{38,38}};vehicle.progress=.5f;
  const auto point=TrafficMicroSim::vehicleWorldPosition(vehicle,2);
  EXPECT_FLOAT_EQ(point.x,31.f);EXPECT_FLOAT_EQ(point.y,29.f);
}
