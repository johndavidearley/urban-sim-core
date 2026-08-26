#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <functional>
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

// Combine two hashes; std::hash<int> is identity on some standard libraries,
// so plain XOR/shift mixing collides heavily on grid coordinates.
inline size_t hashCombine(size_t seed, size_t value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

// Hash function for glm::ivec2
struct Vec2Hash {
  size_t operator()(const glm::ivec2& v) const {
    return hashCombine(std::hash<int>()(v.x), std::hash<int>()(v.y));
  }
};

// Hash function for RoadNodeId
struct RoadNodeIdHash {
  size_t operator()(const RoadNodeId& id) const {
    return Vec2Hash{}(id.coord);
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
      // Symmetric hash for undirected edges: combine endpoint hashes in a
      // canonical (min, max) order so (a,b) and (b,a) hash identically.
      Vec2Hash vecHash;
      const size_t h1 = vecHash(key.a);
      const size_t h2 = vecHash(key.b);
      return hashCombine(std::min(h1, h2), std::max(h1, h2));
    }
  };
  
  RoadNetwork(const CityMap& map);
  
  // Road management
  void buildRoad(glm::ivec2 from, glm::ivec2 to);
  void removeRoad(glm::ivec2 from, glm::ivec2 to);
  void clear();
  bool hasRoad(glm::ivec2 from, glm::ivec2 to) const;
  
  // Connectivity
  void updateConnectivity(glm::ivec2 startNode);
  bool isConnected(glm::ivec2 nodeCoord) const;
  
  // Graph queries
  const Node* getNode(glm::ivec2 coord) const;
  Node* getNode(glm::ivec2 coord);
  bool hasNode(glm::ivec2 coord) const;

  // True if this tile directly touches at least one road edge. Nodes are
  // created lazily on buildRoad, so hasNode() is also false for never-road
  // tiles; prefer this (or resolveRoadAnchor) when checking path access.
  bool hasRoadAdjacency(glm::ivec2 coord) const;

  // Resolves the road tile a building/facility at `coord` should path
  // to/from: `coord` itself if it directly touches a road edge, otherwise
  // the first of its 4 orthogonal neighbors that does (buildings sit next
  // to roads, not necessarily on them - see CitySimulator's hasRoadAccess,
  // which zones a tile as road-accessible under this same self-or-neighbor
  // rule, and LandValueSystem/ServiceSystem, which already resolved this
  // per-caller before it became a shared method here). Returns false
  // (leaving outAnchor unchanged) if neither `coord` nor any neighbor
  // touches a road - genuinely disconnected land.
  bool resolveRoadAnchor(glm::ivec2 coord, glm::ivec2& outAnchor) const;

  // Traffic
  void updateCongestion(glm::ivec2 from, glm::ivec2 to, float load);
  float getCongestion(glm::ivec2 from, glm::ivec2 to) const;
  void resetCongestion();
  
  // Stats
  size_t getRoadCount() const;
  std::vector<glm::ivec2> getAllRoadTiles() const;

  // Bumped exactly when buildRoad/removeRoad actually adds or removes an
  // edge (re-laying an already-present road, as CitySimulator's grid
  // expansion does every tick, is a no-op and does not bump this). Lets
  // callers cache topology-dependent results (e.g. TrafficRouteCache) across
  // ticks and cheaply detect when they've gone stale.
  uint64_t getTopologyVersion() const { return topologyVersion; }
  
  // Traffic queries
  struct EdgeTrafficInfo {
    glm::ivec2 from;
    glm::ivec2 to;
    float congestion;
    float currentLoad;
    float capacity;
  };
  std::vector<EdgeTrafficInfo> getAllEdgeTraffic() const;
  
private:
  const CityMap& cityMap;
  std::unordered_map<EdgeKey, Edge, EdgeKeyHash> edges;
  // Only tiles that currently touch a road edge (lazy). Empty map until
  // the first buildRoad; cleared nodes with no remaining edges are erased
  // on removeRoad so memory tracks the live road network.
  std::unordered_map<glm::ivec2, Node, Vec2Hash> nodes;
  uint64_t topologyVersion = 0;
  
  RoadNodeId makeNodeId(glm::ivec2 coord) const;
  bool isValidCoord(glm::ivec2 coord) const;
  EdgeKey makeEdgeKey(glm::ivec2 from, glm::ivec2 to) const;
  Node& ensureNode(glm::ivec2 coord);
};
