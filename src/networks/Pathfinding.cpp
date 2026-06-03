#include "src/networks/Pathfinding.hpp"
#include "src/networks/RoadNetwork.hpp"
#include <queue>
#include <unordered_map>
#include <cmath>
#include <limits>

// Custom comparator for priority queue with glm::ivec2
struct PQCompare {
  bool operator()(const std::pair<float, glm::ivec2>& a, 
                  const std::pair<float, glm::ivec2>& b) const {
    return a.first > b.first;  // Min-heap based on distance
  }
};

float Pathfinding::heuristic(glm::ivec2 a, glm::ivec2 b) {
  // Manhattan distance
  return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

Pathfinding::Path Pathfinding::findShortestPath(
  const RoadNetwork& network,
  glm::ivec2 start,
  glm::ivec2 goal
) {
  if (!network.hasNode(start) || !network.hasNode(goal)) {
    return Path();
  }
  
  if (start == goal) {
    return Path({start}, 0.0f);
  }
  
  // Dijkstra's algorithm
  std::unordered_map<glm::ivec2, float, Vec2Hash> dist;
  std::unordered_map<glm::ivec2, glm::ivec2, Vec2Hash> prev;
  
  using PQItem = std::pair<float, glm::ivec2>;
  std::priority_queue<PQItem, std::vector<PQItem>, PQCompare> pq;
  
  // Initialize
  dist[start] = 0.0f;
  pq.push({0.0f, start});
  
  while (!pq.empty()) {
    auto [d, current] = pq.top();
    pq.pop();
    
    if (current == goal) {
      // Reconstruct path
      std::vector<glm::ivec2> path;
      glm::ivec2 node = goal;
      
      while (true) {
        path.push_back(node);
        if (node == start) break;
        
        auto it = prev.find(node);
        if (it == prev.end()) break;
        node = it->second;
      }
      
      std::reverse(path.begin(), path.end());
      return Path(path, d);
    }
    
    if (d > dist[current]) {
      continue;
    }
    
    const RoadNetwork::Node* currentNode = network.getNode(current);
    if (!currentNode) continue;
    
    for (const RoadNodeId& neighborId : currentNode->adjacent) {
      glm::ivec2 neighbor = neighborId.coord;
      
      float alt = dist[current] + 1.0f; // Each edge has length 1
      
      auto distIt = dist.find(neighbor);
      if (distIt == dist.end() || alt < distIt->second) {
        dist[neighbor] = alt;
        prev[neighbor] = current;
        pq.push({alt, neighbor});
      }
    }
  }
  
  // No path found
  return Path();
}

Pathfinding::Path Pathfinding::findShortestPathWithCongestion(
  const RoadNetwork& network,
  glm::ivec2 start,
  glm::ivec2 goal
) {
  return findShortestPathWithCongestionWeight(network, start, goal, 0.5f);
}

Pathfinding::Path Pathfinding::findShortestPathWithCongestionWeight(
  const RoadNetwork& network,
  glm::ivec2 start,
  glm::ivec2 goal,
  float congestionWeight
) {
  if (!network.hasNode(start) || !network.hasNode(goal)) {
    return Path();
  }
  
  if (start == goal) {
    return Path({start}, 0.0f);
  }
  
  // Dijkstra with congestion cost
  std::unordered_map<glm::ivec2, float, Vec2Hash> dist;
  std::unordered_map<glm::ivec2, glm::ivec2, Vec2Hash> prev;
  
  using PQItem = std::pair<float, glm::ivec2>;
  std::priority_queue<PQItem, std::vector<PQItem>, PQCompare> pq;
  
  // Initialize
  dist[start] = 0.0f;
  pq.push({0.0f, start});
  
  while (!pq.empty()) {
    auto [d, current] = pq.top();
    pq.pop();
    
    if (current == goal) {
      // Reconstruct path
      std::vector<glm::ivec2> path;
      glm::ivec2 node = goal;
      
      while (true) {
        path.push_back(node);
        if (node == start) break;
        
        auto it = prev.find(node);
        if (it == prev.end()) break;
        node = it->second;
      }
      
      std::reverse(path.begin(), path.end());
      return Path(path, d);
    }
    
    if (d > dist[current]) {
      continue;
    }
    
    const RoadNetwork::Node* currentNode = network.getNode(current);
    if (!currentNode) continue;
    
    for (const RoadNodeId& neighborId : currentNode->adjacent) {
      glm::ivec2 neighbor = neighborId.coord;
      
      // Base cost + congestion penalty
      float baseCost = 1.0f;
      float congestionCost = network.getCongestion(current, neighbor) * std::max(0.0f, congestionWeight);
      float edgeCost = baseCost + congestionCost;
      
      float alt = dist[current] + edgeCost;
      
      auto distIt = dist.find(neighbor);
      if (distIt == dist.end() || alt < distIt->second) {
        dist[neighbor] = alt;
        prev[neighbor] = current;
        pq.push({alt, neighbor});
      }
    }
  }
  
  // No path found
  return Path();
}
