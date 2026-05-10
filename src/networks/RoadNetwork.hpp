#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <queue>
#include <cstdint>
#include "src/world/CityMap.hpp"

// Unique ID for road nodes (which are at tile coordinates)
struct RoadNodeId {
  glm::ivec2 coord;
  
  bool operator==(const RoadNodeId& other) const {
    return coord == other.coord;
  }
};

// Hash function for RoadNodeId
struct RoadNodeIdHash {
  size_t operator()(const RoadNodeId& id) const {
    return std::hash<int>()(id.coord.x) ^ (std::hash<int>()(id.coord.y) << 1);
  }
};

// Hash function for glm::ivec2
struct Vec2Hash {
  size_t operator()(const glm::ivec2& v) const {
    return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1);
  }
};

class RoadNetwork {
public:
  struct Edge {
    RoadNodeId from;
    RoadNodeId to;
    float length;      // Always 1.0 for grid movement
    float capacity;    // Max vehicles per tick
    float currentLoad; // Current vehicles using this edge
    
    float getCongestion() const {
      if (capacity == 0) return 0.0f;
      return std::min(1.0f, currentLoad / capacity);
    }
    
    float getTravelTime(float baseSpeed = 1.0f) const {
      float speedMultiplier = 1.0f + (getCongestion() * 0.5f); // Congestion slows travel
      return length / (baseSpeed / speedMultiplier);
    }
  };
  
  struct Node {
    RoadNodeId id;
    std::vector<RoadNodeId> adjacent;
    bool connected;  // Connected to starting node for connectivity checks
  };
  
  // EdgeKey represents a unique edge between two nodes (undirected)
  struct EdgeKey {
    glm::ivec2 a;
    glm::ivec2 b;
    
    bool operator==(const EdgeKey& other) const {
      return (a == other.a && b == other.b) || (a == other.b && b == other.a);
    }
  };
  
  struct EdgeKeyHash {
    size_t operator()(const EdgeKey& key) const {
      // Symmetric hash for undirected edges
      size_t h1 = std::hash<int>()(key.a.x) ^ (std::hash<int>()(key.a.y) << 1);
      size_t h2 = std::hash<int>()(key.b.x) ^ (std::hash<int>()(key.b.y) << 1);
      return std::min(h1, h2) ^ std::max(h1, h2);
    }
  };
  
  RoadNetwork(const CityMap& map);
  
  // Road management
  void buildRoad(glm::ivec2 from, glm::ivec2 to);
  void removeRoad(glm::ivec2 from, glm::ivec2 to);
  bool hasRoad(glm::ivec2 from, glm::ivec2 to) const;
  
  // Connectivity
  void updateConnectivity(glm::ivec2 startNode);
  bool isConnected(glm::ivec2 nodeCoord) const;
  
  // Graph queries
  const Node* getNode(glm::ivec2 coord) const;
  Node* getNode(glm::ivec2 coord);
  bool hasNode(glm::ivec2 coord) const;
  
  // Traffic
  void updateCongestion(glm::ivec2 from, glm::ivec2 to, float load);
  float getCongestion(glm::ivec2 from, glm::ivec2 to) const;
  
  // Stats
  size_t getRoadCount() const;
  std::vector<glm::ivec2> getAllRoadTiles() const;
  
private:
  const CityMap& cityMap;
  std::unordered_map<EdgeKey, Edge, EdgeKeyHash> edges;
  std::unordered_map<glm::ivec2, Node, Vec2Hash> nodes;
  
  RoadNodeId makeNodeId(glm::ivec2 coord) const;
  bool isValidCoord(glm::ivec2 coord) const;
  EdgeKey makeEdgeKey(glm::ivec2 from, glm::ivec2 to) const;
};
