#include "src/networks/Pathfinding.hpp"
#include "src/networks/RoadNetwork.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace {

// Custom comparator for priority queue with glm::ivec2
struct PQCompare {
  bool operator()(const std::pair<float, glm::ivec2>& a,
                  const std::pair<float, glm::ivec2>& b) const {
    return a.first > b.first;  // Min-heap based on distance
  }
};

// A* over the road graph. Edge cost is 1.0 plus an optional congestion
// penalty (congestionWeight <= 0 disables the penalty). Manhattan distance
// is admissible because every edge costs at least 1.0, so fewer nodes are
// expanded than plain Dijkstra for typical OD pairs on a sparse road net.
Pathfinding::Path runAStar(
  const RoadNetwork& network,
  glm::ivec2 start,
  glm::ivec2 goal,
  float congestionWeight
) {
  // Trivial path: always succeed for identical endpoints, even when the
  // lazy road graph has not created a node at that tile yet.
  if (start == goal) {
    return Pathfinding::Path({start}, 0.0f);
  }

  if (!network.hasNode(start) || !network.hasNode(goal)) {
    return Pathfinding::Path();
  }

  // Lazy road nodes may exist only after buildRoad; also bail when a node
  // was left without edges (should not happen after removeRoad cleanup).
  if (!network.hasRoadAdjacency(start) || !network.hasRoadAdjacency(goal)) {
    return Pathfinding::Path();
  }

  const bool useCongestion = congestionWeight > 0.0f;

  std::unordered_map<glm::ivec2, float, Vec2Hash> gScore;
  std::unordered_map<glm::ivec2, glm::ivec2, Vec2Hash> prev;
  // Heuristic on a typical developed city is a few dozen tiles; reserve
  // modestly to cut rehash during the first expansions.
  gScore.reserve(256);
  prev.reserve(256);

  using PQItem = std::pair<float, glm::ivec2>;  // f-score, node
  std::priority_queue<PQItem, std::vector<PQItem>, PQCompare> pq;

  gScore[start] = 0.0f;
  pq.push({Pathfinding::heuristic(start, goal), start});

  while (!pq.empty()) {
    auto [f, current] = pq.top();
    pq.pop();

    const auto gIt = gScore.find(current);
    if (gIt == gScore.end()) {
      continue;
    }
    const float g = gIt->second;
    // Stale queue entry (a better path to current was found later).
    if (f > g + Pathfinding::heuristic(current, goal) + 1e-4f) {
      continue;
    }

    if (current == goal) {
      std::vector<glm::ivec2> path;
      path.reserve(static_cast<size_t>(g) + 1u);
      glm::ivec2 node = goal;

      while (true) {
        path.push_back(node);
        if (node == start) break;

        auto it = prev.find(node);
        if (it == prev.end()) break;
        node = it->second;
      }

      std::reverse(path.begin(), path.end());
      return Pathfinding::Path(path, g);
    }

    const RoadNetwork::Node* currentNode = network.getNode(current);
    if (!currentNode) continue;

    for (const RoadNodeId& neighborId : currentNode->adjacent) {
      glm::ivec2 neighbor = neighborId.coord;

      float edgeCost = 1.0f;
      if (useCongestion) {
        edgeCost += network.getCongestion(current, neighbor) * congestionWeight;
      }

      const float alt = g + edgeCost;

      auto distIt = gScore.find(neighbor);
      if (distIt == gScore.end() || alt < distIt->second) {
        gScore[neighbor] = alt;
        prev[neighbor] = current;
        pq.push({alt + Pathfinding::heuristic(neighbor, goal), neighbor});
      }
    }
  }

  return Pathfinding::Path();
}

} // namespace

float Pathfinding::heuristic(glm::ivec2 a, glm::ivec2 b) {
  // Manhattan distance
  return static_cast<float>(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

Pathfinding::Path Pathfinding::findShortestPath(
  const RoadNetwork& network,
  glm::ivec2 start,
  glm::ivec2 goal
) {
  return runAStar(network, start, goal, 0.0f);
}

Pathfinding::Path Pathfinding::findShortestPathWithCongestion(
  const RoadNetwork& network,
  glm::ivec2 start,
  glm::ivec2 goal
) {
  return runAStar(network, start, goal, 0.5f);
}

Pathfinding::Path Pathfinding::findShortestPathWithCongestionWeight(
  const RoadNetwork& network,
  glm::ivec2 start,
  glm::ivec2 goal,
  float congestionWeight
) {
  return runAStar(network, start, goal, std::max(0.0f, congestionWeight));
}
