#include "TransitSystem.hpp"

#include <algorithm>
#include <queue>
#include <map>
#include <set>
#include <stdexcept>

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
  cache.topologyVersion = roads.getTopologyVersion();
}

TransitOffload::TransitOffload(const std::vector<TransitRoute>& routes, const TransitCoverageCache& cache,
    const std::vector<TransitRouteUsage>* reserved)
  : routes_(routes), cache_(cache) {
  std::map<TransitRouteId,TransitRouteUsage> prior;
  if (reserved) for (const auto& row:*reserved)
    if (row.ridership>row.capacity || row.ridership>row.demand || !prior.emplace(row.id,row).second)
      throw std::invalid_argument("invalid resident transit reservation");
  std::set<TransitRouteId> routeIds;
  remainingCapacity_.reserve(routes_.size());
  for (const TransitRoute& route : routes_) {
    auto capacity=static_cast<uint32_t>(std::max(0,route.totalCapacity()));
    if (reserved && !routeIds.insert(route.id).second)
      throw std::invalid_argument("duplicate transit route ID");
    const auto found=prior.find(route.id);
    if (found!=prior.end()) {
      if (found->second.capacity!=capacity) throw std::invalid_argument("resident transit capacity changed");
      capacity-=found->second.ridership;prior.erase(found);
    }
    remainingCapacity_.push_back(capacity);
    usage_.push_back({route.id,static_cast<uint32_t>(route.totalCapacity()),0,0});
  }
  if (!prior.empty()) throw std::invalid_argument("unknown resident transit route");
}

uint32_t TransitOffload::offloadAcrossRoutes(Coord origin,Coord destination,uint32_t trips) {
  uint32_t remaining=trips;
  size_t first=routes_.size();
  for (size_t i=0;i<routes_.size() && i<cache_.entries.size();++i) {
    const auto& field=cache_.entries[i].distanceField;
    const auto from=field.find(origin),to=field.find(destination);
    if (from==field.end() || to==field.end() || from->second>routes_[i].stopCoverageRadius ||
        to->second>routes_[i].stopCoverageRadius) continue;
    if (first==routes_.size()) first=i;
    const auto carried=std::min(remaining,remainingCapacity_[i]);
    remainingCapacity_[i]-=carried;remaining-=carried;
    usage_[i].ridership+=carried;usage_[i].demand+=carried;
    if (!remaining) break;
  }
  if (first!=routes_.size()) {usage_[first].demand+=remaining;demand_+=trips;}
  ridership_+=trips-remaining;
  return trips-remaining;
}

uint32_t TransitOffload::offload(Coord home, Coord work, uint32_t workers) {
  bool anyRouteCovers = false;
  size_t firstCovering = 0;
  for (size_t r = 0; r < routes_.size() && r < cache_.entries.size(); ++r) {
    const TransitCoverageCache::Entry& entry = cache_.entries[r];
    const auto homeIt = entry.distanceField.find(home);
    const auto workIt = entry.distanceField.find(work);
    if (homeIt == entry.distanceField.end() || workIt == entry.distanceField.end()) continue;
    if (homeIt->second > routes_[r].stopCoverageRadius || workIt->second > routes_[r].stopCoverageRadius) continue;

    if (!anyRouteCovers) firstCovering = r;
    anyRouteCovers = true;
    if (remainingCapacity_[r] == 0) continue;

    const uint32_t carried = std::min(workers, remainingCapacity_[r]);
    remainingCapacity_[r] -= carried;
    usage_[r].ridership += carried;
    usage_[r].demand += workers;
    ridership_ += carried;
    demand_ += workers;
    return carried;
  }

  if (anyRouteCovers) {
    usage_[firstCovering].demand += workers;
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
  summary.routeUsage = offload.routeUsage();
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
