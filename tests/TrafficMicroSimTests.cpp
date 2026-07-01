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
