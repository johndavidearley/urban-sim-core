#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"

// Milestone 11 (Traffic Micro-Simulation): individual vehicle agents that route
// over the road graph and step along their paths. Congestion is emergent -
// speed on an edge falls as more vehicles share it - rather than computed from
// a static batch load as in the aggregate TrafficSystem. This runs alongside
// TrafficSystem (which is unchanged); it does not replace it.

enum class VehicleType : int {
  Car = 0,
  Emergency = 1,  // ignores congestion slowing (weaves through traffic)
};

struct Vehicle {
  uint32_t id = 0;
  VehicleType type = VehicleType::Car;
  std::vector<glm::ivec2> route;  // node coordinates from pathfinding
  size_t segment = 0;             // index of the current edge's start node in route
  float progress = 0.0f;          // 0..1 along the current edge
  uint32_t people = 1;            // commuters represented by this vehicle
  bool arrived = false;
  uint32_t arriveStep = 0;
};

struct MicroTrafficSummary {
  uint32_t vehicles = 0;
  uint32_t arrived = 0;
  uint32_t commutingPopulation = 0;
  uint32_t stepsSimulated = 0;
  float averageTripSteps = 0.0f;    // mean steps taken by arrived vehicles
  float peakEdgeOccupancy = 0.0f;   // most vehicles on a single edge at once
  float averageEdgeOccupancy = 0.0f;
  uint32_t signalizedIntersections = 0;
  float averageSignalWaitSteps = 0.0f;  // mean steps per vehicle spent stopped at reds
};

class TrafficMicroSim {
public:
  struct Options {
    int maxSteps = 240;               // step budget before giving up on stragglers
    float baseSpeed = 0.5f;           // edges/step at free flow (~2 steps per tile)
    float congestionSlowing = 0.20f;  // speed penalty per extra vehicle sharing an edge
    float minSpeed = 0.05f;           // floor so gridlock still crawls
    bool enableSignals = true;        // gate intersections with alternating signals
    int signalPeriod = 6;             // steps of green per axis at each signal
  };

  // Spawns one vehicle per commute batch (home -> job), routes each with the
  // shortest path, and steps them until all arrive or maxSteps is reached.
  // Deterministic for a given seed.
  static MicroTrafficSummary simulate(
    const EntityStore& store,
    const PopulationStore& population,
    const RoadNetwork& roads,
    uint32_t seed,
    const Options& options = {}
  );
};
