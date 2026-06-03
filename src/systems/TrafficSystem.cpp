#include "TrafficSystem.hpp"
#include "src/networks/Pathfinding.hpp"
#include <algorithm>
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
    size_t value = hash(key.origin) ^ (hash(key.destination) << 1);
    value ^= (static_cast<size_t>(key.useCongestion ? 1 : 0) << 2);
    value ^= (static_cast<size_t>(key.congestionWeightMilli) << 3);
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

const Pathfinding::Path& getOrComputeRoute(
  const RoadNetwork& network,
  glm::ivec2 origin,
  glm::ivec2 destination,
  bool useCongestion,
  float congestionWeight,
  RoutePathCache& cache
) {
  const uint16_t weightMilli = static_cast<uint16_t>(std::max(0.0f, congestionWeight) * 1000.0f + 0.5f);
  const RouteKey key{origin, destination, useCongestion, weightMilli};
  auto it = cache.find(key);
  if (it != cache.end()) {
    return it->second;
  }

  Pathfinding::Path computed = useCongestion
    ? Pathfinding::findShortestPathWithCongestionWeight(network, origin, destination, congestionWeight)
    : Pathfinding::findShortestPath(network, origin, destination);

  auto inserted = cache.emplace(key, std::move(computed));
  return inserted.first->second;
}

} // namespace

TrafficSummary TrafficSystem::simulateCommutes(
  EntityStore& store,
  PopulationStore& population,
  RoadNetwork& network,
  uint32_t seed
) {
  TrafficSummary summary;

  // Reset network congestion for this tick
  network.resetCongestion();

  // Get all population groups
  const auto& groups = population.getGroups();
  uint32_t totalCommuters = 0;
  float totalCommuteTime = 0.0f;

  // Seeded RNG for deterministic job assignment
  std::mt19937 rng(seed);

  // Get all buildings by type
  const auto& buildings = store.getBuildings();
  std::vector<const Building*> residentialBuildings;
  std::vector<const Building*> jobBuildings; // Commercial + Industrial
  residentialBuildings.reserve(buildings.size());
  jobBuildings.reserve(buildings.size());

  for (const auto& [id, building] : buildings) {
    (void)id;
    if (building.type == BuildingType::Residential) {
      residentialBuildings.push_back(&building);
    } else if (building.type == BuildingType::Commercial || 
               building.type == BuildingType::Industrial) {
      jobBuildings.push_back(&building);
    }
  }

  // If no job buildings, no commutes possible
  if (jobBuildings.empty() || residentialBuildings.empty()) {
    summary.commutingPopulation = 0;
    summary.averageCommuteTime = 0.0f;
    return summary;
  }

  std::uniform_int_distribution<size_t> residentialDist(0, residentialBuildings.size() - 1);
  std::uniform_int_distribution<size_t> jobDist(0, jobBuildings.size() - 1);
  RoutePathCache routeCache;
  routeCache.reserve(256);

  // Adaptive routing feedback: escalate congestion sensitivity when peak edges saturate.
  float adaptiveCongestionWeight = 0.5f;
  const uint32_t feedbackEpochCommutes = 8;
  uint32_t processedCommutes = 0;

  // For each population group, assign workers to jobs and simulate commutes
  for (const auto& [groupId, group] : groups) {
    (void)groupId;
    // Only people who are employed commute
    if (group.employed == 0) {
      continue;
    }

    totalCommuters += group.employed;

    // Distribute workers across commutes
    // Each employed person takes a commute path
    uint32_t workersPerCommute = std::max(1u, group.employed / 10u);
    uint32_t numCommutes = (group.employed + workersPerCommute - 1) / workersPerCommute;

    for (uint32_t c = 0; c < numCommutes; ++c) {
      // Select source residential building (deterministically from seed)
      const Building* residentialBldg = residentialBuildings[residentialDist(rng)];

      // Select destination job building (deterministically from seed)
      const Building* jobBldg = jobBuildings[jobDist(rng)];

      if (residentialBldg == nullptr || jobBldg == nullptr) {
        continue;
      }

      // Most spawned buildings are not guaranteed to be on road nodes; avoid costly pathfinding attempts.
      if (!network.hasNode(residentialBldg->position) || !network.hasNode(jobBldg->position)) {
        continue;
      }

      if (processedCommutes > 0 && (processedCommutes % feedbackEpochCommutes) == 0) {
        routeCache.clear();
      }

      // Baseline cache: reuse pathfinding results for repeated OD pairs within this tick.
      const Pathfinding::Path& path = getOrComputeRoute(
        network,
        residentialBldg->position,
        jobBldg->position,
        true,
        adaptiveCongestionWeight,
        routeCache
      );

      if (path.found && path.waypoints.size() > 1) {
        bool invalidateCache = false;
        float peakPathCongestion = 0.0f;

        // Accumulate traffic on path edges
        for (size_t i = 0; i < path.waypoints.size() - 1; ++i) {
          glm::ivec2 from = path.waypoints[i];
          glm::ivec2 to = path.waypoints[i + 1];

          // Update network edge with commuter load
          network.updateCongestion(from, to, static_cast<float>(workersPerCommute));
          peakPathCongestion = std::max(peakPathCongestion, network.getCongestion(from, to));
        }

        const float previousWeight = adaptiveCongestionWeight;
        if (peakPathCongestion >= 0.85f) {
          adaptiveCongestionWeight = std::min(2.0f, adaptiveCongestionWeight + 0.20f);
        } else if (peakPathCongestion <= 0.35f) {
          adaptiveCongestionWeight = std::max(0.5f, adaptiveCongestionWeight - 0.05f);
        }
        if (std::abs(adaptiveCongestionWeight - previousWeight) > 0.0001f) {
          invalidateCache = true;
        }

        // Add commute time (scaled by actual commuters in this batch)
        uint32_t commuters = std::min(workersPerCommute, group.employed - (c * workersPerCommute));
        totalCommuteTime += path.totalDistance * commuters;

        if (invalidateCache) {
          routeCache.clear();
        }
      }

      processedCommutes += 1;
    }
  }

  // Calculate summary metrics
  summary.commutingPopulation = totalCommuters;

  if (totalCommuters > 0) {
    summary.averageCommuteTime = totalCommuteTime / static_cast<float>(totalCommuters);
    summary.totalCommuteBurden = totalCommuteTime;
  }

  // Calculate edge congestion statistics
  float maxCongestion = 0.0f;
  float totalCongestion = 0.0f;
  uint32_t congestionDetected = 0;

  auto allTraffic = network.getAllEdgeTraffic();
  for (const auto& traffic : allTraffic) {
    float congestion = traffic.congestion;
    if (congestion > 0.0f) {
      congestionDetected++;
      totalCongestion += congestion;
      maxCongestion = std::max(maxCongestion, congestion);
    }
  }

  summary.maxEdgeCongestion = maxCongestion;
  summary.averageEdgeCongestion = congestionDetected > 0 ? 
    totalCongestion / static_cast<float>(congestionDetected) : 0.0f;
  summary.congestionDetectedEdges = congestionDetected;

  return summary;
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
  std::vector<EdgeTrafficData> edges;

  const auto& groups = population.getGroups();
  const auto& buildings = store.getBuildings();

  std::vector<const Building*> residentialBuildings;
  std::vector<const Building*> jobBuildings;
  residentialBuildings.reserve(buildings.size());
  jobBuildings.reserve(buildings.size());

  for (const auto& [id, building] : buildings) {
    (void)id;
    if (building.type == BuildingType::Residential) {
      residentialBuildings.push_back(&building);
    } else if (building.type == BuildingType::Commercial ||
               building.type == BuildingType::Industrial) {
      jobBuildings.push_back(&building);
    }
  }

  if (residentialBuildings.empty() || jobBuildings.empty()) {
    return edges;
  }

  std::mt19937 rng(seed);
  std::uniform_int_distribution<size_t> residentialDist(0, residentialBuildings.size() - 1);
  std::uniform_int_distribution<size_t> jobDist(0, jobBuildings.size() - 1);
  RoutePathCache routeCache;
  routeCache.reserve(256);

  std::unordered_map<RoadNetwork::EdgeKey, EdgeTrafficData, RoadNetwork::EdgeKeyHash> edgeTotals;

  for (const auto& [groupId, group] : groups) {
    (void)groupId;
    if (group.employed == 0) {
      continue;
    }

    const uint32_t workersPerCommute = std::max(1u, group.employed / 10u);
    const uint32_t numCommutes = (group.employed + workersPerCommute - 1) / workersPerCommute;

    for (uint32_t c = 0; c < numCommutes; ++c) {
      const Building* residentialBldg = residentialBuildings[residentialDist(rng)];
      const Building* jobBldg = jobBuildings[jobDist(rng)];

      if (!matchesRouteFilter(filter, residentialBldg, jobBldg)) {
        continue;
      }

      if (residentialBldg == nullptr || jobBldg == nullptr) {
        continue;
      }

      if (!network.hasNode(residentialBldg->position) || !network.hasNode(jobBldg->position)) {
        continue;
      }

      const Pathfinding::Path& path = getOrComputeRoute(
        network,
        residentialBldg->position,
        jobBldg->position,
        false,
        0.0f,
        routeCache
      );

      if (!path.found || path.waypoints.size() < 2) {
        continue;
      }

      const uint32_t commuters = std::min(workersPerCommute, group.employed - (c * workersPerCommute));
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
  }

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
