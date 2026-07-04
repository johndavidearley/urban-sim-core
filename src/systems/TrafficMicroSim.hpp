#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "src/core/TileScale.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/ServiceSystem.hpp"

// Physical width of one traffic lane. Used only by vehicleWorldPosition()'s
// perpendicular lane offset (see below) - it does not feed back into
// simulation dynamics (congestion/car-following stay purely progress- and
// lane-index-based, as before), so changing it can never affect
// determinism or existing behavior, only the derived 2D position query.
constexpr float kLaneWidthMeters = 3.5f;

// Milestone 11 (Traffic Micro-Simulation): individual vehicle agents that route
// over the road graph and step along their paths. Congestion is emergent -
// each edge has `lanesPerRoad` independent lanes, each behaving like its own
// single-file channel (more than one vehicle sharing a lane slows it down) -
// rather than computed from a static batch load as in the aggregate
// TrafficSystem. This runs alongside TrafficSystem (which is unchanged); it
// does not replace it.
//
// Each vehicle carries an explicit lane index on its current edge: it picks
// the least-loaded lane when entering an edge, and switches lanes mid-edge
// (overtaking) when another lane on the same edge is meaningfully less
// crowded. Within a lane, vehicles car-follow: a follower's speed each step
// is capped so it cannot close to within `minFollowingGap + vehicleLengthMeters
// / kMetersPerTile` of whichever vehicle is immediately ahead of it in the
// same lane, so a slow or stopped leader (e.g. queued at a red) visibly
// backs up traffic behind it rather than every vehicle in the lane sharing
// one aggregate progress value. `vehicleLengthMeters` (0 by default) is a
// physical quantity - unlike `minFollowingGap`, an opaque fraction of an
// edge - representing genuine vehicle size rather than pure safety margin;
// see vehicleWorldPosition() below for the other previously-unmodeled
// piece, a literal 2D in-lane position.

enum class VehicleType : int {
  Car = 0,
  Emergency = 1,  // ignores congestion slowing and lane assignment (weaves through traffic)
};

struct Vehicle {
  uint32_t id = 0;
  VehicleType type = VehicleType::Car;
  std::vector<glm::ivec2> route;  // node coordinates from pathfinding
  size_t segment = 0;             // index of the current edge's start node in route
  float progress = 0.0f;          // 0..1 along the current edge
  int lane = 0;                   // lane index on the current edge (Car only; unused by Emergency)
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

  uint32_t emergencyVehicles = 0;
  uint32_t emergencyArrived = 0;
  float averageEmergencyResponseSteps = 0.0f;  // mean steps for emergency vehicles to reach their incident
};

class TrafficMicroSim {
public:
  struct Options {
    int maxSteps = 240;               // step budget before giving up on stragglers
    float baseSpeed = 0.5f;           // edges/step at free flow (~2 steps per tile)
    float congestionSlowing = 0.20f;  // speed penalty per vehicle beyond lane capacity
    float minSpeed = 0.05f;           // floor so gridlock still crawls
    int lanesPerRoad = 2;              // vehicles an edge carries before congestion kicks in
    float minFollowingGap = 0.15f;     // min in-lane spacing (fraction of an edge) a follower keeps behind its leader
    float vehicleLengthMeters = 0.0f;  // physical vehicle length added on top of minFollowingGap (see below); 0 = no effect, matching prior behavior for every existing caller
    bool enableSignals = true;         // gate intersections with alternating signals
    int signalPeriod = 6;              // steps of green per axis at each signal
    int emergencyIncidents = 0;        // number of emergency dispatches to spawn (needs facilities)
    float emergencySpeedMultiplier = 1.6f;  // speed boost on top of ignoring congestion/reds
  };

  // Spawns one vehicle per commute batch (home -> job), routes each with the
  // shortest path, and steps them until all arrive or maxSteps is reached.
  // If facilities is non-null and options.emergencyIncidents > 0, also spawns
  // that many emergency vehicles, each dispatched from the nearest facility to
  // a randomly chosen building (the "incident"). Emergency vehicles ignore
  // congestion slowing and red signals, and get a speed multiplier.
  // Deterministic for a given seed.
  static MicroTrafficSummary simulate(
    const EntityStore& store,
    const PopulationStore& population,
    const RoadNetwork& roads,
    uint32_t seed
  );

  static MicroTrafficSummary simulate(
    const EntityStore& store,
    const PopulationStore& population,
    const RoadNetwork& roads,
    uint32_t seed,
    const Options& options,
    const std::vector<ServiceFacility>* facilities = nullptr
  );

  // Literal 2D position of `v` in tile-space (fractional tile coordinates,
  // not pixels or meters): its progress along the current edge, offset
  // perpendicular to the direction of travel by its lane index (centered
  // so lanes straddle the edge's centerline) times kLaneWidthMeters. Before
  // this, a vehicle's only tracked position was a single float (progress)
  // plus a lane index - enough to drive congestion/car-following, but not
  // enough to place a vehicle anywhere a renderer could actually draw
  // distinct lanes side by side. Pure function of `v`'s existing state; has
  // no effect on simulation dynamics (nothing in simulate() calls this) -
  // it exists purely as a query for callers (e.g. a future visualizer) that
  // want an actual (x, y). Emergency vehicles and single-lane roads report
  // the unoffset centerline position (Emergency doesn't track a lane; a
  // single lane has nothing to offset from).
  static glm::vec2 vehicleWorldPosition(const Vehicle& v, int lanesPerRoad);
};
