#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// Forward declaration
class RoadNetwork;

class Pathfinding {
public:
  struct Path {
    std::vector<glm::ivec2> waypoints;
    float totalDistance;
    bool found;
    
    Path() : totalDistance(0.0f), found(false) {}
    Path(std::vector<glm::ivec2> wp, float dist)
      : waypoints(wp), totalDistance(dist), found(true) {}
  };
  
  // Find shortest path using Dijkstra's algorithm
  static Path findShortestPath(
    const RoadNetwork& network,
    glm::ivec2 start,
    glm::ivec2 goal
  );
  
  // Find shortest path with congestion penalties
  static Path findShortestPathWithCongestion(
    const RoadNetwork& network,
    glm::ivec2 start,
    glm::ivec2 goal
  );
  
  // Calculate Manhattan distance heuristic
  static float heuristic(glm::ivec2 a, glm::ivec2 b);
};
