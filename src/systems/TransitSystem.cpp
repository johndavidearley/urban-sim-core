#include "TransitSystem.hpp"

#include <algorithm>
#include <queue>

namespace {

// Multi-source BFS: distance from every road-reachable tile to the nearest
// of `sources`, capped at maxDistance (mirrors LandValueSystem's job-access
// BFS and ServiceSystem's per-facility BFS - the same shape of problem, one
// route's stops instead of one facility's position or several job buildings).
std::unordered_map<Coord, int, Vec2Hash> multiSourceDistanceField(
  const RoadNetwork& roads,
  const std::vector<Coord>& sources,
  int maxDistance
) {
  std::unordered_map<Coord, int, Vec2Hash> distance;
  if (maxDistance < 0) {
    return distance;
  }

  std::queue<Coord> frontier;
  for (const Coord& s : sources) {
    if (roads.getNode(s) == nullptr) continue;
    if (distance.count(s) != 0) continue;
    distance[s] = 0;
    frontier.push(s);
  }

  while (!frontier.empty()) {
    const Coord current = frontier.front();
    frontier.pop();
    const int currentDistance = distance[current];
    if (currentDistance >= maxDistance) continue;

    const RoadNetwork::Node* node = roads.getNode(current);
    if (node == nullptr) continue;

    for (const RoadNodeId& neighborId : node->adjacent) {
      const Coord next = neighborId.coord;
      if (distance.count(next) != 0) continue;
      distance[next] = currentDistance + 1;
      frontier.push(next);
    }
  }

  return distance;
}

} // namespace

void TransitSystem::buildCache(
  const RoadNetwork& roads,
  const std::vector<TransitRoute>& routes,
  TransitCoverageCache& cache
) {
  cache.entries.clear();
  cache.entries.reserve(routes.size());
  for (const TransitRoute& route : routes) {
    auto distField = multiSourceDistanceField(roads, route.stops, route.stopCoverageRadius);
    cache.entries.push_back({route.id, std::move(distField)});
  }
  cache.builtForRouteCount = routes.size();
}

TransitOffload::TransitOffload(const std::vector<TransitRoute>& routes, const TransitCoverageCache& cache)
  : routes_(routes), cache_(cache) {
  remainingCapacity_.reserve(routes_.size());
  for (const TransitRoute& route : routes_) {
    remainingCapacity_.push_back(static_cast<uint32_t>(std::max(0, route.totalCapacity())));
  }
}

uint32_t TransitOffload::offload(Coord home, Coord work, uint32_t workers) {
  bool anyRouteCovers = false;
  for (size_t r = 0; r < routes_.size() && r < cache_.entries.size(); ++r) {
    const TransitCoverageCache::Entry& entry = cache_.entries[r];
    const auto homeIt = entry.distanceField.find(home);
    const auto workIt = entry.distanceField.find(work);
    if (homeIt == entry.distanceField.end() || workIt == entry.distanceField.end()) continue;
    if (homeIt->second > routes_[r].stopCoverageRadius || workIt->second > routes_[r].stopCoverageRadius) continue;

    anyRouteCovers = true;
    if (remainingCapacity_[r] == 0) continue;

    const uint32_t carried = std::min(workers, remainingCapacity_[r]);
    remainingCapacity_[r] -= carried;
    ridership_ += carried;
    demand_ += workers;
    return carried;
  }

  if (anyRouteCovers) {
    demand_ += workers;  // covered by a route, but every covering route is full
  }
  return 0;
}

TransitSummary TransitSystem::summarize(
  const std::vector<TransitRoute>& routes,
  const TransitOffload& offload,
  uint32_t totalCommuters
) {
  TransitSummary summary;
  summary.totalRoutes = static_cast<uint32_t>(routes.size());
  for (const TransitRoute& route : routes) {
    summary.totalStops += static_cast<uint32_t>(route.stops.size());
    summary.totalCapacity += static_cast<uint32_t>(std::max(0, route.totalCapacity()));
    if (route.mode == TransitMode::Rail) {
      ++summary.railRoutes;
    } else {
      ++summary.busRoutes;
    }
  }
  summary.ridership = offload.totalRidership();
  summary.demand = offload.totalDemand();
  summary.modalShare = totalCommuters > 0
    ? static_cast<float>(summary.ridership) / static_cast<float>(totalCommuters)
    : 0.0f;
  return summary;
}
