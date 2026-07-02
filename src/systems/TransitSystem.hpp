#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "src/networks/RoadNetwork.hpp"

using TransitRouteId = uint32_t;

// A fixed bus route: an ordered path of road-network stops. Modeled as
// static infrastructure (like ServiceFacility) rather than literal moving
// vehicle agents - TrafficMicroSim already covers agent-level simulation for
// cars, and modeling buses at that fidelity is out of scope here. Instead
// this feeds into the same aggregate commute model TrafficSystem uses, the
// same way ServiceSystem feeds coverage into desirability.
struct TransitRoute {
  TransitRouteId id = 0;
  std::vector<Coord> stops;     // ordered path along the road network
  int vehicleCount = 2;         // buses assigned to this route
  int capacityPerVehicle = 30;  // riders per bus per tick
  int stopCoverageRadius = 4;   // road-hops a stop covers (walk-to-stop distance)

  int totalCapacity() const {
    return vehicleCount * capacityPerVehicle;
  }
};

struct TransitSummary {
  uint32_t totalRoutes = 0;
  uint32_t totalStops = 0;
  uint32_t totalCapacity = 0;  // sum of route capacities
  uint32_t ridership = 0;      // commuters actually carried by transit this tick
  uint32_t demand = 0;         // commuters whose home+work share a route's coverage, whether or not capacity existed
  float modalShare = 0.0f;     // ridership / (ridership + car commuters); 0 if no commuters at all
};

// Pre-built BFS distance fields for all routes (one multi-source BFS per
// route, from all its stops - mirroring ServiceCoverageCache). Rebuild only
// when the route list changes; reused across ticks.
struct TransitCoverageCache {
  struct Entry {
    TransitRouteId routeId = 0;
    std::unordered_map<Coord, int, Vec2Hash> distanceField;  // hops from nearest stop on this route
  };
  std::vector<Entry> entries;  // aligned index-for-index with the routes vector it was built from
  size_t builtForRouteCount = 0;
};

// Sequential, stateful helper: tracks remaining per-route capacity as
// TrafficSystem::simulateCommutes calls offload() once per commute batch, in
// the same fixed deterministic order the batches were generated in. Not
// thread-safe by design - must only be invoked from TrafficSystem's
// sequential accumulation phase, never concurrently.
class TransitOffload {
public:
  TransitOffload(const std::vector<TransitRoute>& routes, const TransitCoverageCache& cache);

  // Returns how many of `workers` ride transit for this home->work commute
  // (0 if no route's coverage reaches both ends, or every covering route is
  // already at capacity). Updates internal ridership/demand counters.
  uint32_t offload(Coord home, Coord work, uint32_t workers);

  uint32_t totalRidership() const { return ridership_; }
  uint32_t totalDemand() const { return demand_; }

private:
  const std::vector<TransitRoute>& routes_;
  const TransitCoverageCache& cache_;
  std::vector<uint32_t> remainingCapacity_;
  uint32_t ridership_ = 0;
  uint32_t demand_ = 0;
};

class TransitSystem {
public:
  // Rebuild the BFS distance fields (call only when the route list changes).
  static void buildCache(
    const RoadNetwork& roads,
    const std::vector<TransitRoute>& routes,
    TransitCoverageCache& cache
  );

  // Package totals from an already-run TransitOffload plus static route
  // stats into a reportable summary. `totalCommuters` is the full commuting
  // population (car + transit combined, e.g. TrafficSummary::commutingPopulation).
  static TransitSummary summarize(
    const std::vector<TransitRoute>& routes,
    const TransitOffload& offload,
    uint32_t totalCommuters
  );
};
