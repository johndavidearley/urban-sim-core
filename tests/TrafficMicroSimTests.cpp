#include <gtest/gtest.h>

#include "src/systems/TrafficMicroSim.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/world/CityMap.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"

namespace {
// Grow a small city so there are homes, jobs, roads, and employed residents.
void growCity(CityMap& map, RoadNetwork& roads, EntityStore& store, PopulationStore& population) {
  SimOptions options;
  options.runTraffic = false;
  CitySimulator::run(map, roads, store, population, 7, 30, options);
}
} // namespace

TEST(TrafficMicroSimTests, NoCommutersProducesNoVehicles) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const MicroTrafficSummary summary = TrafficMicroSim::simulate(store, population, roads, 42);

  EXPECT_EQ(summary.vehicles, 0u);
  EXPECT_EQ(summary.arrived, 0u);
}

TEST(TrafficMicroSimTests, VehiclesRouteAndArrive) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  growCity(map, roads, store, population);

  const MicroTrafficSummary summary = TrafficMicroSim::simulate(store, population, roads, 99);

  ASSERT_GT(summary.vehicles, 0u);
  EXPECT_GT(summary.commutingPopulation, 0u);
  // On a connected grid every spawned vehicle should reach its destination
  // within the step budget.
  EXPECT_EQ(summary.arrived, summary.vehicles);
  EXPECT_GT(summary.averageTripSteps, 0.0f);
}

TEST(TrafficMicroSimTests, CongestionIsEmergent) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  growCity(map, roads, store, population);

  const MicroTrafficSummary summary = TrafficMicroSim::simulate(store, population, roads, 99);

  // With many vehicles sharing a grid, at least one edge carries more than one
  // vehicle at once (congestion emerges from agent density).
  EXPECT_GT(summary.peakEdgeOccupancy, 1.0f);
}

TEST(TrafficMicroSimTests, SignalsCauseWaitsAndCanBeDisabled) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  growCity(map, roads, store, population);

  TrafficMicroSim::Options withSignals;
  withSignals.enableSignals = true;
  const MicroTrafficSummary a = TrafficMicroSim::simulate(store, population, roads, 99, withSignals);

  TrafficMicroSim::Options noSignals;
  noSignals.enableSignals = false;
  const MicroTrafficSummary b = TrafficMicroSim::simulate(store, population, roads, 99, noSignals);

  // A grid city has signalized junctions, and vehicles stop at some reds.
  EXPECT_GT(a.signalizedIntersections, 0u);
  EXPECT_GT(a.averageSignalWaitSteps, 0.0f);
  // Disabling signals removes all waiting.
  EXPECT_EQ(b.signalizedIntersections, 0u);
  EXPECT_FLOAT_EQ(b.averageSignalWaitSteps, 0.0f);
}

TEST(TrafficMicroSimTests, EmergencyVehiclesDispatchFromFacilitiesAndArrive) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  growCity(map, roads, store, population);

  const std::vector<glm::ivec2> roadTiles = roads.getAllRoadTiles();
  ASSERT_FALSE(roadTiles.empty());

  std::vector<ServiceFacility> facilities;
  facilities.push_back(ServiceFacility{ServiceType::Fire, roadTiles.front(), 10, 1.0f});
  facilities.push_back(ServiceFacility{ServiceType::Police, roadTiles.back(), 10, 1.0f});

  TrafficMicroSim::Options options;
  options.emergencyIncidents = 5;

  const MicroTrafficSummary summary = TrafficMicroSim::simulate(store, population, roads, 42, options, &facilities);

  EXPECT_GT(summary.emergencyVehicles, 0u);
  EXPECT_EQ(summary.emergencyArrived, summary.emergencyVehicles);
  EXPECT_GT(summary.averageEmergencyResponseSteps, 0.0f);
  // Total vehicle count includes both commuters and emergency dispatches.
  EXPECT_GE(summary.vehicles, summary.emergencyVehicles);
}

TEST(TrafficMicroSimTests, NoFacilitiesMeansNoEmergencyVehicles) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  growCity(map, roads, store, population);

  TrafficMicroSim::Options options;
  options.emergencyIncidents = 5;  // requested, but no facilities passed

  const MicroTrafficSummary summary = TrafficMicroSim::simulate(store, population, roads, 42, options, nullptr);

  EXPECT_EQ(summary.emergencyVehicles, 0u);
}

TEST(TrafficMicroSimTests, EmergencyVehiclesIgnoreRedSignals) {
  // A single 4-way signalized junction at (2,2): arms north(2,1), south(2,3),
  // west(1,2), east(3,2).
  CityMap map({5, 5});
  RoadNetwork roads(map);
  roads.buildRoad({2, 1}, {2, 2});
  roads.buildRoad({2, 2}, {2, 3});
  roads.buildRoad({1, 2}, {2, 2});
  roads.buildRoad({2, 2}, {3, 2});

  EntityStore store;
  store.createBuilding(BuildingType::Residential, {2, 3}, 10);  // the "incident"
  PopulationStore population;  // no commuters, so only the emergency vehicle moves

  std::vector<ServiceFacility> facilities;
  facilities.push_back(ServiceFacility{ServiceType::Fire, {2, 1}, 10, 1.0f});

  TrafficMicroSim::Options options;
  options.maxSteps = 30;
  options.signalPeriod = 1000;  // phase never flips within maxSteps
  options.emergencyIncidents = 1;

  const MicroTrafficSummary summary = TrafficMicroSim::simulate(store, population, roads, 1, options, &facilities);

  // Route (2,1)->(2,2)->(2,3) crosses (2,2) north-south. With node=(2,2),
  // signalGreen's phase = (step/period + node.x+node.y) % 2 = (0 + 4) % 2 = 0,
  // which is green for the horizontal axis only - so this north-south move is
  // held red for the entire run. A signal-obeying vehicle could never cross
  // within maxSteps; the emergency vehicle must ignore the light to arrive.
  ASSERT_EQ(summary.emergencyVehicles, 1u);
  EXPECT_EQ(summary.emergencyArrived, 1u);
  EXPECT_GT(summary.averageEmergencyResponseSteps, 0.0f);
  EXPECT_LT(summary.averageEmergencyResponseSteps, static_cast<float>(options.maxSteps));
}

TEST(TrafficMicroSimTests, LaneCapacityReducesCongestionDelay) {
  // A single home and single job connected by one straight 6-edge road: every
  // commute batch takes the exact same route and starts in lockstep, so all
  // vehicles share every edge simultaneously - a clean, deterministic worst
  // case for testing how lane capacity affects the congestion penalty.
  CityMap map({8, 3});
  RoadNetwork roads(map);
  for (int x = 0; x < 6; ++x) {
    roads.buildRoad({x, 0}, {x + 1, 0});
  }

  EntityStore store;
  store.createBuilding(BuildingType::Residential, {0, 1}, 100);
  store.createBuilding(BuildingType::Commercial, {6, 1}, 100);

  PopulationStore population;
  population.createGroup(IncomeBand::Middle, 40, 40);  // 40 employed -> 10 vehicles, one route

  TrafficMicroSim::Options oneLane;
  oneLane.lanesPerRoad = 1;  // any 2nd vehicle on the edge causes slowing
  const MicroTrafficSummary narrow = TrafficMicroSim::simulate(store, population, roads, 5, oneLane);

  TrafficMicroSim::Options manyLanes;
  manyLanes.lanesPerRoad = 20;  // capacity exceeds all 10 vehicles -> no slowing
  const MicroTrafficSummary wide = TrafficMicroSim::simulate(store, population, roads, 5, manyLanes);

  ASSERT_EQ(narrow.vehicles, 10u);
  ASSERT_EQ(wide.vehicles, 10u);
  EXPECT_EQ(narrow.arrived, narrow.vehicles);
  EXPECT_EQ(wide.arrived, wide.vehicles);
  // All 10 vehicles start together and move in lockstep (speed depends only on
  // shared count, which is identical for all of them each step).
  EXPECT_GE(narrow.peakEdgeOccupancy, 10.0f);
  EXPECT_GE(wide.peakEdgeOccupancy, 10.0f);
  // A wider road absorbs the same traffic with far less delay.
  EXPECT_LT(wide.averageTripSteps, narrow.averageTripSteps);
}

TEST(TrafficMicroSimTests, VehiclesSpreadAcrossLanesRatherThanBunching) {
  // 4 vehicles sharing one straight route in lockstep (single home/job pair,
  // same setup style as LaneCapacityReducesCongestionDelay). With 2 real
  // lanes and lane changing, they should settle into two lanes of two
  // vehicles each (excess=1 -> speed 0.5/1.2 ~ 0.417/step, ~15 steps for 6
  // edges) rather than all four bunching into a single lane (excess=3 ->
  // speed 0.5/1.6 ~ 0.3125/step, ~20 steps) - the old aggregate model (no
  // explicit lane state) could not distinguish these cases.
  CityMap map({8, 3});
  RoadNetwork roads(map);
  for (int x = 0; x < 6; ++x) {
    roads.buildRoad({x, 0}, {x + 1, 0});
  }

  EntityStore store;
  store.createBuilding(BuildingType::Residential, {0, 1}, 100);
  store.createBuilding(BuildingType::Commercial, {6, 1}, 100);

  PopulationStore population;
  population.createGroup(IncomeBand::Middle, 4, 4);  // 4 employed -> 4 vehicles, one route

  TrafficMicroSim::Options options;
  options.lanesPerRoad = 2;
  options.enableSignals = false;  // straight road has no junctions; be explicit anyway

  const MicroTrafficSummary summary = TrafficMicroSim::simulate(store, population, roads, 5, options);

  ASSERT_EQ(summary.vehicles, 4u);
  EXPECT_EQ(summary.arrived, 4u);
  // Properly spread (2+2, ~15 steps) vs. bunched in one lane (~20 steps).
  EXPECT_LE(summary.averageTripSteps, 16.0f);
}

TEST(TrafficMicroSimTests, DeterministicForSameSeed) {
  auto runOnce = [](uint32_t seed) {
    CityMap map({48, 48});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    growCity(map, roads, store, population);
    return TrafficMicroSim::simulate(store, population, roads, seed);
  };

  const MicroTrafficSummary a = runOnce(123);
  const MicroTrafficSummary b = runOnce(123);

  EXPECT_EQ(a.vehicles, b.vehicles);
  EXPECT_EQ(a.arrived, b.arrived);
  EXPECT_EQ(a.stepsSimulated, b.stepsSimulated);
  EXPECT_FLOAT_EQ(a.averageTripSteps, b.averageTripSteps);
  EXPECT_FLOAT_EQ(a.peakEdgeOccupancy, b.peakEdgeOccupancy);
}
