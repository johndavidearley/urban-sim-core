#include "src/networks/RoadNetwork.hpp"
#include <algorithm>

RoadNetwork::RoadNetwork(const CityMap& map) : cityMap(map) {
  // Initialize all potential nodes (one per tile)
  glm::ivec2 dims = map.getDimensions();
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      glm::ivec2 coord{x, y};
      nodes[coord] = {makeNodeId(coord), {}, false};
    }
  }
}

RoadNodeId RoadNetwork::makeNodeId(glm::ivec2 coord) const {
  return RoadNodeId{coord};
}

RoadNetwork::EdgeKey RoadNetwork::makeEdgeKey(glm::ivec2 from, glm::ivec2 to) const {
  return EdgeKey{from, to};
}

bool RoadNetwork::isValidCoord(glm::ivec2 coord) const {
  return cityMap.isValid(coord);
}

void RoadNetwork::buildRoad(glm::ivec2 from, glm::ivec2 to) {
  if (!isValidCoord(from) || !isValidCoord(to)) {
    return;
  }
  
  // Only allow adjacent tiles
  int dx = std::abs(from.x - to.x);
  int dy = std::abs(from.y - to.y);
  if ((dx + dy) != 1) {
    return; // Not adjacent
  }
  
  EdgeKey edgeKey = makeEdgeKey(from, to);
  
  if (edges.find(edgeKey) == edges.end()) {
    // Create edge
    Edge newEdge{makeNodeId(from), makeNodeId(to), 1.0f, 10.0f, 0.0f};
    edges[edgeKey] = newEdge;
    
    // Add to adjacency lists
    auto& fromNode = nodes[from];
    auto& toNode = nodes[to];
    
    if (std::find(fromNode.adjacent.begin(), fromNode.adjacent.end(), makeNodeId(to)) 
        == fromNode.adjacent.end()) {
      fromNode.adjacent.push_back(makeNodeId(to));
    }
    
    if (std::find(toNode.adjacent.begin(), toNode.adjacent.end(), makeNodeId(from)) 
        == toNode.adjacent.end()) {
      toNode.adjacent.push_back(makeNodeId(from));
    }
    
    // Update city map
    const_cast<CityMap&>(cityMap).getTile(from).hasRoad = true;
    const_cast<CityMap&>(cityMap).getTile(to).hasRoad = true;

    ++topologyVersion;
  }
}

void RoadNetwork::removeRoad(glm::ivec2 from, glm::ivec2 to) {
  if (!isValidCoord(from) || !isValidCoord(to)) {
    return;
  }
  
  EdgeKey edgeKey = makeEdgeKey(from, to);
  auto it = edges.find(edgeKey);
  if (it != edges.end()) {
    edges.erase(it);
    
    // Remove from adjacency lists
    auto& fromNode = nodes[from];
    auto& toNode = nodes[to];
    
    auto fromIt = std::find(fromNode.adjacent.begin(), fromNode.adjacent.end(), makeNodeId(to));
    if (fromIt != fromNode.adjacent.end()) {
      fromNode.adjacent.erase(fromIt);
    }
    
    auto toIt = std::find(toNode.adjacent.begin(), toNode.adjacent.end(), makeNodeId(from));
    if (toIt != toNode.adjacent.end()) {
      toNode.adjacent.erase(toIt);
    }

    // A tile is visually a road tile only while at least one road segment
    // still touches it. Keeping this synchronized matters for interactive
    // demolition and prevents removed dead ends from remaining painted as
    // roads or blocking zoning.
    CityMap& mutableMap = const_cast<CityMap&>(cityMap);
    mutableMap.getTile(from).hasRoad = !fromNode.adjacent.empty();
    mutableMap.getTile(to).hasRoad = !toNode.adjacent.empty();

    ++topologyVersion;
  }
}

void RoadNetwork::clear() {
  if (!edges.empty()) {
    ++topologyVersion;
  }
  edges.clear();
  CityMap& mutableMap = const_cast<CityMap&>(cityMap);
  for (auto& [coord, node] : nodes) {
    node.adjacent.clear();
    node.connected = false;
    mutableMap.getTile(coord).hasRoad = false;
    mutableMap.getTile(coord).connectedToRoad = false;
  }
}

bool RoadNetwork::hasRoad(glm::ivec2 from, glm::ivec2 to) const {
  EdgeKey edgeKey = makeEdgeKey(from, to);
  return edges.find(edgeKey) != edges.end();
}

void RoadNetwork::updateConnectivity(glm::ivec2 startCoord) {
  if (!isValidCoord(startCoord)) {
    return;
  }
  
  // Reset all connectivity flags
  for (auto& pair : nodes) {
    pair.second.connected = false;
  }
  
  // BFS from start node
  std::queue<glm::ivec2> queue;
  queue.push(startCoord);
  nodes[startCoord].connected = true;
  
  while (!queue.empty()) {
    glm::ivec2 current = queue.front();
    queue.pop();
    
    const Node* currentNode = getNode(current);
    if (!currentNode) continue;
    
    for (const RoadNodeId& neighborId : currentNode->adjacent) {
      glm::ivec2 neighborCoord = neighborId.coord;
      Node* neighborNode = getNode(neighborCoord);
      
      if (neighborNode && !neighborNode->connected) {
        neighborNode->connected = true;
        queue.push(neighborCoord);
        
        // Update city map
        const_cast<CityMap&>(cityMap).getTile(neighborCoord).connectedToRoad = true;
      }
    }
  }
  
  // Mark disconnected tiles
  glm::ivec2 dims = cityMap.getDimensions();
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      glm::ivec2 coord{x, y};
      auto it = nodes.find(coord);
      if (it != nodes.end()) {
        const_cast<CityMap&>(cityMap).getTile(coord).connectedToRoad = it->second.connected;
      }
    }
  }
}

bool RoadNetwork::isConnected(glm::ivec2 nodeCoord) const {
  const Node* node = getNode(nodeCoord);
  return node && node->connected;
}

const RoadNetwork::Node* RoadNetwork::getNode(glm::ivec2 coord) const {
  auto it = nodes.find(coord);
  if (it != nodes.end()) {
    return &it->second;
  }
  return nullptr;
}

RoadNetwork::Node* RoadNetwork::getNode(glm::ivec2 coord) {
  auto it = nodes.find(coord);
  if (it != nodes.end()) {
    return &it->second;
  }
  return nullptr;
}

bool RoadNetwork::hasNode(glm::ivec2 coord) const {
  return nodes.find(coord) != nodes.end();
}

bool RoadNetwork::hasRoadAdjacency(glm::ivec2 coord) const {
  const Node* node = getNode(coord);
  return node != nullptr && !node->adjacent.empty();
}

bool RoadNetwork::resolveRoadAnchor(glm::ivec2 coord, glm::ivec2& outAnchor) const {
  if (hasRoadAdjacency(coord)) {
    outAnchor = coord;
    return true;
  }
  const glm::ivec2 neighbors[4] = {
    {coord.x + 1, coord.y}, {coord.x - 1, coord.y}, {coord.x, coord.y + 1}, {coord.x, coord.y - 1}
  };
  for (const glm::ivec2& n : neighbors) {
    if (hasRoadAdjacency(n)) {
      outAnchor = n;
      return true;
    }
  }
  return false;
}

void RoadNetwork::updateCongestion(glm::ivec2 from, glm::ivec2 to, float load) {
  EdgeKey edgeKey = makeEdgeKey(from, to);
  auto it = edges.find(edgeKey);
  if (it != edges.end()) {
    it->second.currentLoad = std::max(0.0f, it->second.currentLoad + load);
  }
}

float RoadNetwork::getCongestion(glm::ivec2 from, glm::ivec2 to) const {
  EdgeKey edgeKey = makeEdgeKey(from, to);
  auto it = edges.find(edgeKey);
  if (it != edges.end()) {
    return it->second.getCongestion();
  }
  return 0.0f;
}

size_t RoadNetwork::getRoadCount() const {
  return edges.size();
}

std::vector<glm::ivec2> RoadNetwork::getAllRoadTiles() const {
  std::vector<glm::ivec2> roads;
  glm::ivec2 dims = cityMap.getDimensions();
  
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      if (cityMap.getTile({x, y}).hasRoad) {
        roads.push_back({x, y});
      }
    }
  }
  
  return roads;
}

void RoadNetwork::resetCongestion() {
  for (auto& [key, edge] : edges) {
    edge.currentLoad = 0.0f;
  }
}

std::vector<RoadNetwork::EdgeTrafficInfo> RoadNetwork::getAllEdgeTraffic() const {
  std::vector<EdgeTrafficInfo> traffic;
  
  for (const auto& [key, edge] : edges) {
    EdgeTrafficInfo info;
    info.from = key.a;
    info.to = key.b;
    info.congestion = edge.getCongestion();
    info.currentLoad = edge.currentLoad;
    info.capacity = edge.capacity;
    traffic.push_back(info);
  }
  
  return traffic;
}
