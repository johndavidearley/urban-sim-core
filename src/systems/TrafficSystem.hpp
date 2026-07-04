#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "src/core/ThreadPool.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/metrics/CityMetrics.hpp"

class TransitOffload;

// A commute's endpoints, independent of congestion weight - the parallel
// pathfinding phase in runCommuteLoop always runs immediately after
// RoadNetwork::resetCongestion(), so every edge is at zero load when those
// paths are computed and the result is purely topology-dependent.
struct RouteEndpointKey {
  glm::ivec2 origin;
  glm::ivec2 destination;

  bool operator==(const RouteEndpointKey& other) const {
    return origin == other.origin && destination == other.destination;
  }
};

struct RouteEndpointKeyHash {
  size_t operator()(const RouteEndpointKey& key) const {
    return hashCombine(Vec2Hash{}(key.origin), Vec2Hash{}(key.destination));
  }
};

// Caller-owned cache of topology-only shortest paths, reused across ticks
// the same way ServiceCoverageCache/TransitCoverageCache persist across
// CitySimulator ticks. Road topology in an autonomous --simulate run only
// changes when the grid expands outward (a handful of times over a whole
// run), so almost every tick is a pure cache hit once the city has grown
// past its initial footprint - avoiding a full Dijkstra re-run per commute
// per tick, which dominates per-tick cost at scale. Invalidated (cleared)
// automatically whenever RoadNetwork::getTopologyVersion() moves past the
// version recorded at last use.
struct TrafficRouteCache {
  std::unordered_map<RouteEndpointKey, Pathfinding::Path, RouteEndpointKeyHash> paths;
  uint64_t topologyVersion = 0;
};

struct TrafficSummary {
  uint32_t commutingPopulation = 0;
  float averageCommuteTime = 0.0f;
  float maxEdgeCongestion = 0.0f;
  float averageEdgeCongestion = 0.0f;
  uint32_t congestionDetectedEdges = 0;
  float totalCommuteBurden = 0.0f;
};

struct EdgeTrafficData {
  glm::ivec2 from;
  glm::ivec2 to;
  float congestion = 0.0f;
  float totalCommuters = 0.0f;
  float totalCommuteTime = 0.0f;
};

struct RouteDiagnosticsFilter {
  bool hasOrigin = false;
  glm::ivec2 origin{0, 0};
  bool hasDestination = false;
  glm::ivec2 destination{0, 0};
};

class TrafficSystem {
public:
  // Simulate commute flows for all employed population.
  // Updates road network congestion, returns traffic summary.
  // If pool is non-null, Dijkstra calls are fanned out across pool workers
  // (specs are collected sequentially to preserve RNG determinism; congestion
  // accumulation is sequential after all paths are found).
  // If transit is non-null, it's consulted once per commute batch (in the
  // same deterministic order the batches were generated) to divert some
  // workers off the road and onto transit before congestion is accumulated -
  // see TransitOffload. Default nullptr matches prior behavior exactly.
  //
  // If routeCache is non-null and pool is also non-null, the parallel
  // pathfinding phase looks up each commute's path by (home, work) endpoints
  // before submitting a Dijkstra task, and stores newly-computed paths back
  // - see TrafficRouteCache. The cache is auto-invalidated (cleared) the
  // first time it's used against a network whose topology has changed since
  // its last use. Default nullptr matches prior behavior exactly (every path
  // recomputed every call).
  static TrafficSummary simulateCommutes(
    EntityStore& store,
    PopulationStore& population,
    RoadNetwork& network,
    uint32_t seed = 0,
    ThreadPool* pool = nullptr,
    TransitOffload* transit = nullptr,
    TrafficRouteCache* routeCache = nullptr
  );

  // Apply traffic metrics to city metrics
  static void applyToMetrics(const TrafficSummary& summary, CityMetrics& metrics);

  // Get traffic data for visualization/reporting (sorted by congestion)
  static std::vector<EdgeTrafficData> getTopCongestedEdges(
    const RoadNetwork& network,
    size_t topN = 10
  );

  // Reconstruct commute paths and return top edges for a filtered route subset.
  // This is diagnostics-only and does not mutate network congestion state.
  static std::vector<EdgeTrafficData> getTopRouteDiagnosticEdges(
    const EntityStore& store,
    const PopulationStore& population,
    const RoadNetwork& network,
    const RouteDiagnosticsFilter& filter,
    size_t topN = 10,
    uint32_t seed = 0
  );
};
