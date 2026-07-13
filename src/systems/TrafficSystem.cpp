#include "TrafficSystem.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/systems/TransitSystem.hpp"
#include <algorithm>
#include <future>
#include <random>
#include <numeric>

namespace {

struct RouteKey {
  glm::ivec2 origin;
  glm::ivec2 destination;
  bool useCongestion = false;
  uint16_t congestionWeightMilli = 0;

  bool operator==(const RouteKey& other) const {
    return origin == other.origin &&
           destination == other.destination &&
           useCongestion == other.useCongestion &&
           congestionWeightMilli == other.congestionWeightMilli;
  }
};

struct RouteKeyHash {
  size_t operator()(const RouteKey& key) const {
    Vec2Hash hash;
    size_t value = hashCombine(hash(key.origin), hash(key.destination));
    value = hashCombine(value, static_cast<size_t>(key.useCongestion ? 1 : 0));
    value = hashCombine(value, static_cast<size_t>(key.congestionWeightMilli));
    return value;
  }
};

using RoutePathCache = std::unordered_map<RouteKey, Pathfinding::Path, RouteKeyHash>;

bool vec2Less(const glm::ivec2& a, const glm::ivec2& b) {
  if (a.x != b.x) {
    return a.x < b.x;
  }
  return a.y < b.y;
}

RoadNetwork::EdgeKey makeCanonicalEdgeKey(glm::ivec2 from, glm::ivec2 to) {
  if (vec2Less(to, from)) {
    return RoadNetwork::EdgeKey{to, from};
  }
  return RoadNetwork::EdgeKey{from, to};
}

bool matchesRouteFilter(
  const RouteDiagnosticsFilter& filter,
  const Building* residential,
  const Building* job
) {
  if (residential == nullptr || job == nullptr) {
    return false;
  }
  if (filter.hasOrigin && residential->position != filter.origin) {
    return false;
  }
  if (filter.hasDestination && job->position != filter.destination) {
    return false;
  }
  return true;
}

// Entity stores are hash maps, so iteration order varies with absolute ID
// values (and across standard libraries). Commute sampling consumes seeded RNG
// draws per element, so iterate in creation (ID) order to keep replays
// deterministic.
void sortBuildingsById(std::vector<const Building*>& buildings) {
  std::sort(buildings.begin(), buildings.end(),
    [](const Building* a, const Building* b) { return a->id < b->id; });
}

std::vector<const PopulationGroup*> groupsInIdOrder(const PopulationStore& population) {
  std::vector<const PopulationGroup*> ordered;
  ordered.reserve(population.getGroups().size());
  for (const auto& [id, group] : population.getGroups()) {
    (void)id;
    ordered.push_back(&group);
  }
  std::sort(ordered.begin(), ordered.end(),
    [](const PopulationGroup* a, const PopulationGroup* b) { return a->id < b->id; });
  return ordered;
}

const Pathfinding::Path& getOrComputeRoute(
  const RoadNetwork& network,
  glm::ivec2 origin,
  glm::ivec2 destination,
  float congestionWeight,
  RoutePathCache& cache
) {
  const uint16_t weightMilli = static_cast<uint16_t>(std::max(0.0f, congestionWeight) * 1000.0f + 0.5f);
  const RouteKey key{origin, destination, true, weightMilli};
  auto it = cache.find(key);
  if (it != cache.end()) {
    return it->second;
  }

  Pathfinding::Path computed =
    Pathfinding::findShortestPathWithCongestionWeight(network, origin, destination, congestionWeight);

  auto inserted = cache.emplace(key, std::move(computed));
  return inserted.first->second;
}

struct CommuteSpec {
  const Building* home;
  const Building* work;
  uint32_t workers;
  Coord homeAnchor;  // road tile home->position resolves to (see RoadNetwork::resolveRoadAnchor)
  Coord workAnchor;  // road tile work->position resolves to
};

// Distance- and capacity-weighted job pick for a commute batch anchored at
// `home`: nearer, larger job buildings are more likely, rather than every
// job building anywhere on the map being equally likely regardless of
// distance (the previous model). Straight-line (Manhattan) distance is used
// as a cheap proxy for commute cost rather than a per-draw road-network BFS
// - the same "cheap heuristic, actual routing follows real roads via
// Pathfinding" tradeoff CitySimulator's own transit-route placement already
// makes. `jobBuildings` must be in a fixed order (sorted by ID) for
// determinism; consumes exactly one RNG draw, same as the uniform pick it
// replaces.
const Building* pickWeightedJob(
  const std::vector<const Building*>& jobBuildings,
  Coord home,
  std::mt19937& rng
) {
  constexpr float kDistanceDecay = 0.05f;  // higher = stronger preference for nearby jobs
  std::vector<float> weights(jobBuildings.size());
  float totalWeight = 0.0f;
  for (size_t i = 0; i < jobBuildings.size(); ++i) {
    const int dx = jobBuildings[i]->position.x - home.x;
    const int dy = jobBuildings[i]->position.y - home.y;
    const float dist = static_cast<float>(std::abs(dx) + std::abs(dy));
    const float capacityWeight = static_cast<float>(std::max(1, jobBuildings[i]->capacity));
    const float w = capacityWeight / (1.0f + kDistanceDecay * dist);
    weights[i] = w;
    totalWeight += w;
  }

  std::uniform_real_distribution<float> pick(0.0f, totalWeight);
  float r = pick(rng);
  for (size_t i = 0; i < weights.size(); ++i) {
    r -= weights[i];
    if (r <= 0.0f) return jobBuildings[i];
  }
  return jobBuildings.back();  // floating-point rounding fallback
}

// Phase 1: deterministic RNG walk to collect commute home/work/count triples.
// Must stay sequential to preserve replay determinism.
//
// A building's own tile does not necessarily touch a road edge - zoning
// only requires road access at the tile itself *or* one of its 4 neighbors
// (see CitySimulator's hasRoadAccess) - so home/work positions are resolved
// to their road anchor here via RoadNetwork::resolveRoadAnchor rather than
// used directly; every downstream pathfinding/congestion/transit-offload
// step in this file routes between anchors, not raw building positions.
// A building with no road adjacency at all (neither itself nor any
// neighbor) is skipped, same as before.
std::vector<CommuteSpec> collectCommuteSpecs(
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& network,
  uint32_t seed,
  uint32_t& outTotalCommuters
) {
  std::vector<CommuteSpec> specs;
  outTotalCommuters = 0;

  const auto& buildings = store.getBuildings();
  std::vector<const Building*> residentialBuildings;
  std::vector<const Building*> jobBuildings;
  residentialBuildings.reserve(buildings.size());
  jobBuildings.reserve(buildings.size());
  for (const auto& [id, b] : buildings) {
    (void)id;
    if (b.type == BuildingType::Residential) residentialBuildings.push_back(&b);
    else if (b.type == BuildingType::Commercial || b.type == BuildingType::Industrial ||
             b.type == BuildingType::Office) jobBuildings.push_back(&b);
  }
  if (residentialBuildings.empty() || jobBuildings.empty()) return specs;

  sortBuildingsById(residentialBuildings);
  sortBuildingsById(jobBuildings);

  std::mt19937 rng(seed);
  std::uniform_int_distribution<size_t> resDist(0, residentialBuildings.size() - 1);

  for (const PopulationGroup* gp : groupsInIdOrder(population)) {
    if (gp->employed == 0) continue;
    outTotalCommuters += gp->employed;
    const uint32_t wpc = std::max(1u, gp->employed / 10u);
    const uint32_t n = (gp->employed + wpc - 1) / wpc;
    for (uint32_t c = 0; c < n; ++c) {
      const Building* home = residentialBuildings[resDist(rng)];
      const Building* work = pickWeightedJob(jobBuildings, home->position, rng);
      Coord homeAnchor, workAnchor;
      if (!network.resolveRoadAnchor(home->position, homeAnchor)) continue;
      if (!network.resolveRoadAnchor(work->position, workAnchor)) continue;
      const uint32_t workers = std::min(wpc, gp->employed - c * wpc);
      specs.push_back({home, work, workers, homeAnchor, workAnchor});
    }
  }
  return specs;
}

// Core commute loop shared by the live simulation and route diagnostics so
// both produce identical routes (same RNG draws, same congestion-adaptive
// routing). Mutates the given network's congestion as commutes accumulate;
// diagnostics pass a scratch copy. The visitor is invoked for every routed
// commute with the chosen buildings, path, and commuter count.
//
// When pool is non-null, pathfinding is fanned out across pool workers.
// All paths use the initial congestion weight (0.5) — intra-tick adaptive
// feedback is skipped in parallel mode. Inter-tick feedback via lastCongestion
// is unaffected. The diagnostic path always passes pool=nullptr.
//
// If transit is non-null, it's asked once per commute batch (in the fixed
// order specs were generated) how many of that batch's workers ride transit
// instead; only the remainder contributes to road congestion. The full
// worker count still flows into totalCommuteTime/the visitor - transit
// riders' trips aren't modeled separately, just diverted off the road.
template <typename CommuteVisitor>
TrafficSummary runCommuteLoop(
  const EntityStore& store,
  const PopulationStore& population,
  RoadNetwork& network,
  uint32_t seed,
  CommuteVisitor&& visit,
  ThreadPool* pool = nullptr,
  TransitOffload* transit = nullptr,
  TrafficRouteCache* routeCache = nullptr
) {
  TrafficSummary summary;
  network.resetCongestion();

  if (routeCache != nullptr && routeCache->topologyVersion != network.getTopologyVersion()) {
    routeCache->paths.clear();
    routeCache->topologyVersion = network.getTopologyVersion();
  }

  uint32_t totalCommuters = 0;
  std::vector<CommuteSpec> specs = collectCommuteSpecs(store, population, network, seed, totalCommuters);
  summary.commutingPopulation = totalCommuters;

  if (specs.empty()) return summary;

  // Transit offload: resolved once per commute batch, in spec order, before
  // any congestion accumulation below - so both the sequential and parallel
  // pathfinding phases see the already-reduced (car-only) worker counts.
  // specs[i].workers (the full count) is untouched and still drives
  // totalCommuteTime/the visitor - only road congestion is reduced.
  std::vector<uint32_t> effectiveWorkers(specs.size());
  for (size_t i = 0; i < specs.size(); ++i) {
    uint32_t workers = specs[i].workers;
    if (transit != nullptr) {
      workers -= transit->offload(specs[i].homeAnchor, specs[i].workAnchor, specs[i].workers);
    }
    effectiveWorkers[i] = workers;
  }

  // Phase 2: pathfinding — parallel when pool is provided (all edges are 0
  // after resetCongestion, so concurrent reads on edges are safe).
  std::vector<Pathfinding::Path> paths(specs.size());

  if (pool != nullptr && specs.size() > 1) {
    // Every edge is at zero load right after resetCongestion() above, and
    // this phase runs entirely before any congestion is accumulated (that
    // happens in phase 3 below) - so the path found here depends only on
    // topology, never on congestionWeight or which tick it is. That's what
    // makes routeCache safe to reuse across ticks.
    constexpr float kCongestionWeight = 0.5f;
    std::vector<std::future<Pathfinding::Path>> futures(specs.size());
    std::vector<bool> pending(specs.size(), false);
    for (size_t i = 0; i < specs.size(); ++i) {
      const RouteEndpointKey key{specs[i].homeAnchor, specs[i].workAnchor};
      if (routeCache != nullptr) {
        auto it = routeCache->paths.find(key);
        if (it != routeCache->paths.end()) {
          paths[i] = it->second;
          continue;
        }
      }
      pending[i] = true;
      futures[i] = pool->submit([&network, spec = specs[i]]() {
        return Pathfinding::findShortestPathWithCongestionWeight(
          network, spec.homeAnchor, spec.workAnchor, kCongestionWeight);
      });
    }
    for (size_t i = 0; i < specs.size(); ++i) {
      if (!pending[i]) continue;
      paths[i] = futures[i].get();
      if (routeCache != nullptr) {
        const RouteEndpointKey key{specs[i].homeAnchor, specs[i].workAnchor};
        routeCache->paths.emplace(key, paths[i]);
      }
    }
  } else {
    // Sequential with route cache + adaptive congestion weight feedback.
    RoutePathCache pathCache;
    pathCache.reserve(256);
    float adaptiveCongestionWeight = 0.5f;
    float pendingCongestionWeight = 0.5f;
    constexpr uint32_t kFeedbackEpoch = 8;
    uint32_t processed = 0;

    for (size_t i = 0; i < specs.size(); ++i) {
      if (processed > 0 && (processed % kFeedbackEpoch) == 0 &&
          std::abs(pendingCongestionWeight - adaptiveCongestionWeight) > 0.0001f) {
        adaptiveCongestionWeight = pendingCongestionWeight;
        pathCache.clear();
      }
      paths[i] = getOrComputeRoute(
        network, specs[i].homeAnchor, specs[i].workAnchor,
        adaptiveCongestionWeight, pathCache);

      // Accumulate congestion immediately so later paths route around buildup.
      if (paths[i].found && paths[i].waypoints.size() > 1) {
        float peakCongestion = 0.0f;
        for (size_t j = 0; j + 1 < paths[i].waypoints.size(); ++j) {
          network.updateCongestion(paths[i].waypoints[j], paths[i].waypoints[j + 1],
                                   static_cast<float>(effectiveWorkers[i]));
          peakCongestion = std::max(peakCongestion,
            network.getCongestion(paths[i].waypoints[j], paths[i].waypoints[j + 1]));
        }
        if (peakCongestion >= 0.85f) pendingCongestionWeight = std::min(2.0f, pendingCongestionWeight + 0.20f);
        else if (peakCongestion <= 0.35f) pendingCongestionWeight = std::max(0.5f, pendingCongestionWeight - 0.05f);
      }
      ++processed;
    }
  }

  // Phase 3: accumulate congestion + call visitor (sequential).
  // In the sequential case, congestion was already accumulated in phase 2.
  float totalCommuteTime = 0.0f;
  for (size_t i = 0; i < specs.size(); ++i) {
    const Pathfinding::Path& path = paths[i];
    if (!path.found || path.waypoints.size() <= 1) continue;

    if (pool != nullptr) {
      // Parallel mode: congestion not yet accumulated.
      for (size_t j = 0; j + 1 < path.waypoints.size(); ++j) {
        network.updateCongestion(path.waypoints[j], path.waypoints[j + 1],
                                 static_cast<float>(effectiveWorkers[i]));
      }
    }

    totalCommuteTime += path.totalDistance * static_cast<float>(specs[i].workers);
    visit(specs[i].home, specs[i].work, path, specs[i].workers);
  }

  if (totalCommuters > 0) {
    summary.averageCommuteTime = totalCommuteTime / static_cast<float>(totalCommuters);
    summary.totalCommuteBurden = totalCommuteTime;
  }

  float maxCongestion = 0.0f;
  float totalCongestion = 0.0f;
  uint32_t congestionDetected = 0;
  for (const auto& e : network.getAllEdgeTraffic()) {
    if (e.congestion > 0.0f) {
      ++congestionDetected;
      totalCongestion += e.congestion;
      maxCongestion = std::max(maxCongestion, e.congestion);
    }
  }
  summary.maxEdgeCongestion = maxCongestion;
  summary.averageEdgeCongestion = congestionDetected > 0
    ? totalCongestion / static_cast<float>(congestionDetected) : 0.0f;
  summary.congestionDetectedEdges = congestionDetected;
  return summary;
}

} // namespace

TrafficSummary TrafficSystem::simulateCommutes(
  EntityStore& store,
  PopulationStore& population,
  RoadNetwork& network,
  uint32_t seed,
  ThreadPool* pool,
  TransitOffload* transit,
  TrafficRouteCache* routeCache
) {
  return runCommuteLoop(
    store, population, network, seed,
    [](const Building*, const Building*, const Pathfinding::Path&, uint32_t) {},
    pool,
    transit,
    routeCache
  );
}

void TrafficSystem::applyToMetrics(const TrafficSummary& summary, CityMetrics& metrics) {
  // Commute burden as a 0-1 scale based on average commute time
  // Longer commutes reduce happiness/increase burden
  // Reference: 5 units = 0.5 burden, 10 units = 0.8 burden
  float commuteScale = summary.averageCommuteTime / 5.0f;
  metrics.commuteBurden = std::min(1.0f, commuteScale * 0.1f);

  // Traffic congestion directly maps to network congestion
  metrics.trafficCongestion = summary.maxEdgeCongestion;

  // Reduce happiness based on commute burden and traffic
  float happinessPenalty = metrics.commuteBurden * 0.2f + metrics.trafficCongestion * 0.15f;
  metrics.happiness = std::max(0.0f, metrics.happiness - happinessPenalty);
}

std::vector<EdgeTrafficData> TrafficSystem::getTopCongestedEdges(
  const RoadNetwork& network,
  size_t topN
) {
  std::vector<EdgeTrafficData> edges;

  auto allTraffic = network.getAllEdgeTraffic();
  for (const auto& traffic : allTraffic) {
    if (traffic.congestion > 0.0f) {
      EdgeTrafficData data;
      data.from = traffic.from;
      data.to = traffic.to;
      data.congestion = traffic.congestion;
      data.totalCommuters = traffic.currentLoad;
      edges.push_back(data);
    }
  }

  // Sort by congestion descending
  std::sort(edges.begin(), edges.end(),
    [](const EdgeTrafficData& a, const EdgeTrafficData& b) {
      return a.congestion > b.congestion;
    });

  // Return top N
  if (edges.size() > topN) {
    edges.resize(topN);
  }

  return edges;
}

std::vector<EdgeTrafficData> TrafficSystem::getTopRouteDiagnosticEdges(
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& network,
  const RouteDiagnosticsFilter& filter,
  size_t topN,
  uint32_t seed
) {
  std::unordered_map<RoadNetwork::EdgeKey, EdgeTrafficData, RoadNetwork::EdgeKeyHash> edgeTotals;

  // Replay the exact commute simulation (same seed, same congestion-adaptive
  // routing) on a scratch copy of the network so diagnostics report the
  // routes commuters actually take without mutating live congestion state.
  RoadNetwork scratchNetwork(network);

  (void)runCommuteLoop(
    store,
    population,
    scratchNetwork,
    seed,
    [&](const Building* residential, const Building* job, const Pathfinding::Path& path, uint32_t commuters) {
      if (!matchesRouteFilter(filter, residential, job)) {
        return;
      }

      const float commuterLoad = static_cast<float>(commuters);
      const float pathCommuteTime = path.totalDistance * commuterLoad;

      for (size_t i = 0; i + 1 < path.waypoints.size(); ++i) {
        const glm::ivec2 from = path.waypoints[i];
        const glm::ivec2 to = path.waypoints[i + 1];
        const RoadNetwork::EdgeKey key = makeCanonicalEdgeKey(from, to);

        auto& edge = edgeTotals[key];
        edge.from = key.a;
        edge.to = key.b;
        edge.totalCommuters += commuterLoad;
        edge.totalCommuteTime += pathCommuteTime;
      }
    }
  );

  std::vector<EdgeTrafficData> edges;
  edges.reserve(edgeTotals.size());
  for (auto& [key, edge] : edgeTotals) {
    (void)key;
    edge.congestion = std::min(1.0f, edge.totalCommuters / 10.0f);
    edges.push_back(edge);
  }

  std::sort(edges.begin(), edges.end(),
    [](const EdgeTrafficData& a, const EdgeTrafficData& b) {
      if (a.congestion != b.congestion) {
        return a.congestion > b.congestion;
      }
      return a.totalCommuters > b.totalCommuters;
    });

  if (edges.size() > topN) {
    edges.resize(topN);
  }

  return edges;
}
