#include "src/networks/RoadNetwork.hpp"
#include <algorithm>
#include <cmath>

RoadNetwork::RoadNetwork(const CityMap& map) : cityMap(map) {
  // Lazy node creation: only tiles that participate in a road edge are
  // inserted. Pre-registering every map tile made a 500x500 city hold 250k
  // empty hash-map nodes and forced updateConnectivity to reset them all.
}

RoadNodeId RoadNetwork::makeNodeId(glm::ivec2 coord) const {
  return RoadNodeId{coord};
}

RoadNetwork::EdgeKey RoadNetwork::makeEdgeKey(glm::ivec2 from, glm::ivec2 to) const {
  return EdgeKey{from, to};
}

bool RoadNetwork::isValidCoord(glm::ivec2 coord) const {
  const auto tile=roadTile(coord);
  return cityMap.isValid(tile) && roadNode(tile,roadLevel(coord))==coord;
}

RoadNetwork::Node& RoadNetwork::ensureNode(glm::ivec2 coord) {
  auto it = nodes.find(coord);
  if (it == nodes.end()) {
    it = nodes.emplace(coord, Node{makeNodeId(coord), {}, false}).first;
  }
  return it->second;
}

void RoadNetwork::buildRoad(glm::ivec2 from, glm::ivec2 to) {
  if (!cityMap.isValid(from) || !cityMap.isValid(to)) {
    return;
  }
  if (cityMap.getTile(from).regionalConnectionId || cityMap.getTile(to).regionalConnectionId) return;
  
  // Only allow adjacent tiles
  int dx = std::abs(from.x - to.x);
  int dy = std::abs(from.y - to.y);
  if ((dx + dy) != 1) {
    return; // Not adjacent
  }
  
  EdgeKey edgeKey = makeEdgeKey(from, to);
  
  if (edges.find(edgeKey) == edges.end()) {
    // Create edge
    Edge newEdge{makeNodeId(from), makeNodeId(to), 1.0f, kLocalRoadCapacity, 0.0f};
    edges[edgeKey] = newEdge;
    
    // Add to adjacency lists (nodes created on demand)
    auto& fromNode = ensureNode(from);
    auto& toNode = ensureNode(to);
    
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
    ++routingVersion;
  }
}

bool RoadNetwork::setRoadClass(glm::ivec2 from, glm::ivec2 to,
                               RoadClass roadClass) {
  if (!validRoadClass(roadClass))
    return false;
  auto it = edges.find(makeEdgeKey(from, to));
  if (it == edges.end())
    return false;
  if (it->second.roadClass != roadClass) ++routingVersion;
  it->second.roadClass = roadClass;
  it->second.capacity = roadCapacity(roadClass);
  return true;
}
RoadClass RoadNetwork::getRoadClass(glm::ivec2 from, glm::ivec2 to) const {
  auto it = edges.find(makeEdgeKey(from, to));
  return it == edges.end() ? RoadClass::Local : it->second.roadClass;
}
float RoadNetwork::getEdgeCapacity(glm::ivec2 from, glm::ivec2 to) const {
  auto it = edges.find(makeEdgeKey(from, to));
  return it == edges.end() ? 0.0f : it->second.capacity;
}
float RoadNetwork::getFreightLoad(glm::ivec2 from, glm::ivec2 to) const {
  auto it = edges.find(makeEdgeKey(from, to));
  return it == edges.end() ? 0.0f : it->second.freightLoad;
}

void RoadNetwork::addFreightLoad(glm::ivec2 from, glm::ivec2 to, float load) {
  auto it = edges.find(makeEdgeKey(from, to));
  if (it == edges.end())
    return;
  load = std::max(0.0f, load);
  it->second.freightLoad += load;
  it->second.currentLoad += load;
}

void RoadNetwork::removeRoad(glm::ivec2 from, glm::ivec2 to) {
  if (!isValidCoord(from) || !isValidCoord(to)) {
    return;
  }
  
  EdgeKey edgeKey = makeEdgeKey(from, to);
  auto it = edges.find(edgeKey);
  if (it != edges.end()) {
    std::vector<Coord> released;
    alignments_.erase(std::remove_if(alignments_.begin(),alignments_.end(),[&](const RoadAlignment& a) {
      const auto route=a.routingPath();
      for(size_t i=1;i<route.size();++i)
        if(makeEdgeKey(route[i-1],route[i])==edgeKey) {
          released.insert(released.end(),a.footprint.begin(),a.footprint.end());return true;
        }
      return false;
    }),alignments_.end());
    if(it->second.elevation!=0) --separatedRoadCount_;
    edges.erase(it);
    for(Coord tile:released)
      const_cast<CityMap&>(cityMap).getTile(tile).hasRoad=hasRoadAdjacency(tile)||alignmentCovers(tile);
    
    // Remove from adjacency lists
    auto* fromNode = getNode(from);
    auto* toNode = getNode(to);
    if (fromNode != nullptr) {
      auto fromIt = std::find(fromNode->adjacent.begin(), fromNode->adjacent.end(), makeNodeId(to));
      if (fromIt != fromNode->adjacent.end()) {
        fromNode->adjacent.erase(fromIt);
      }
    }
    if (toNode != nullptr) {
      auto toIt = std::find(toNode->adjacent.begin(), toNode->adjacent.end(), makeNodeId(from));
      if (toIt != toNode->adjacent.end()) {
        toNode->adjacent.erase(toIt);
      }
    }

    // A tile is visually a road tile only while at least one road segment
    // still touches it. Keeping this synchronized matters for interactive
    // demolition and prevents removed dead ends from remaining painted as
    // roads or blocking zoning.
    CityMap& mutableMap = const_cast<CityMap&>(cityMap);
    if (fromNode != nullptr) {
      if(roadLevel(from)==0) mutableMap.getTile(from).hasRoad = !fromNode->adjacent.empty() || alignmentCovers(from);
      if (fromNode->adjacent.empty()) {
        nodes.erase(from);
      }
    }
    if (toNode != nullptr) {
      if(roadLevel(to)==0) mutableMap.getTile(to).hasRoad = !toNode->adjacent.empty() || alignmentCovers(to);
      if (toNode->adjacent.empty()) {
        nodes.erase(to);
      }
    }

    ++topologyVersion;
    ++routingVersion;
  }
}

void RoadNetwork::clear() {
  if (!edges.empty() || !nodes.empty()) {
    ++topologyVersion;
    ++routingVersion;
  }
  CityMap& mutableMap = const_cast<CityMap&>(cityMap);
  for (auto& [coord, node] : nodes) {
    (void)node;
    if(roadLevel(coord)==0) {
      mutableMap.getTile(coord).hasRoad = false;
      mutableMap.getTile(coord).connectedToRoad = false;
    }
  }
  for(const auto& alignment:alignments_)
    for(Coord tile:alignment.footprint) mutableMap.getTile(tile).hasRoad=false;
  alignments_.clear();separatedRoadCount_=0;
  edges.clear();
  nodes.clear();
}

bool RoadNetwork::hasRoad(glm::ivec2 from, glm::ivec2 to) const {
  EdgeKey edgeKey = makeEdgeKey(from, to);
  return edges.find(edgeKey) != edges.end();
}

void RoadNetwork::updateConnectivity(glm::ivec2 startCoord) {
  if (!isValidCoord(startCoord)) {
    return;
  }
  
  CityMap& mutableMap = const_cast<CityMap&>(cityMap);

  // Reset only existing road nodes (lazy graph - not the full map).
  for (auto& [coord, node] : nodes) {
    node.connected = false;
    if(roadLevel(coord)==0) mutableMap.getTile(coord).connectedToRoad = false;
  }

  if (nodes.find(startCoord) == nodes.end()) {
    return;
  }
  
  // BFS from start node
  std::queue<glm::ivec2> queue;
  queue.push(startCoord);
  nodes[startCoord].connected = true;
  if(roadLevel(startCoord)==0) mutableMap.getTile(startCoord).connectedToRoad = true;
  
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
        if(roadLevel(neighborCoord)==0) mutableMap.getTile(neighborCoord).connectedToRoad = true;
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
  // Only road-touching nodes exist under lazy creation; collect those with
  // at least one edge rather than scanning the full map for hasRoad flags.
  std::vector<glm::ivec2> roads;
  roads.reserve(nodes.size());
  for (const auto& [coord, node] : nodes) {
    if (!node.adjacent.empty() && roadLevel(coord)==0) {
      roads.push_back(coord);
    }
  }
  return roads;
}

void RoadNetwork::addVisitorLoad(glm::ivec2 from,glm::ivec2 to,float load) {
  if (!std::isfinite(load) || load<=0.0f) return;
  const auto found=edges.find(makeEdgeKey(from,to));
  if (found==edges.end()) return;
  found->second.visitorLoad+=load;found->second.currentLoad+=load;
}

void RoadNetwork::resetCongestion() {
  for (auto& [key, edge] : edges) {
    edge.currentLoad = 0.0f;
    edge.freightLoad = 0.0f;
    edge.visitorLoad = 0.0f;
  }
}

std::vector<RoadNetwork::EdgeTrafficInfo> RoadNetwork::getAllEdgeTraffic() const {
  std::vector<EdgeTrafficInfo> traffic;
  traffic.reserve(edges.size());
  
  for (const auto& [key, edge] : edges) {
    EdgeTrafficInfo info;
    info.from = edge.from.coord;
    info.to = edge.to.coord;
    info.oneWay = edge.oneWay;
    info.length = edge.length;info.elevation=edge.elevation;
    (void)key;
    info.congestion = edge.getCongestion();
    info.currentLoad = edge.currentLoad;
    info.capacity = edge.capacity;
    info.roadClass = edge.roadClass;
    info.freightLoad = edge.freightLoad;
    info.visitorLoad = edge.visitorLoad;
    traffic.push_back(info);
  }

  std::sort(traffic.begin(), traffic.end(),
            [](const EdgeTrafficInfo &a, const EdgeTrafficInfo &b) {
              if (a.from.x != b.from.x)
                return a.from.x < b.from.x;
              if (a.from.y != b.from.y)
                return a.from.y < b.from.y;
              if (a.to.x != b.to.x)
                return a.to.x < b.to.x;
              return a.to.y < b.to.y;
            });
  return traffic;
}

bool RoadNetwork::canTravel(Coord from,Coord to) const {
  const auto it=edges.find(makeEdgeKey(from,to));
  return it!=edges.end() && (!it->second.oneWay || it->second.from.coord==from);
}
bool RoadNetwork::setOneWay(Coord from,Coord to,bool enabled) {
  auto it=edges.find(makeEdgeKey(from,to));
  if(it==edges.end()) return false;
  auto& edge=it->second;
  if(edge.oneWay!=enabled || (enabled && edge.from.coord!=from)) {
    edge.from=makeNodeId(from);edge.to=makeNodeId(to);edge.oneWay=enabled;++routingVersion;
  }
  return true;
}
void RoadNetwork::addAlignment(const RoadAlignment& alignment) {
  alignments_.push_back(alignment);
  for(Coord tile:alignment.footprint) const_cast<CityMap&>(cityMap).getTile(tile).hasRoad=true;
}
bool RoadNetwork::alignmentCovers(Coord tile) const {
  for(const auto& a:alignments_)
    if(std::find(a.footprint.begin(),a.footprint.end(),tile)!=a.footprint.end()) return true;
  return false;
}

bool RoadNetwork::alignmentOwnsEdge(Coord from,Coord to) const {
  for(const auto& a:alignments_) {
    const auto route=a.routingPath();
    for(size_t i=1;i<route.size();++i)
      if((route[i-1]==from && route[i]==to)||(route[i-1]==to && route[i]==from)) return true;
  }
  return false;
}
bool RoadNetwork::hasLegacyRoadAt(Coord tile) const {
  if(!alignmentCovers(tile)) return true;
  const auto* node=getNode(tile);
  if(node) for(const auto& n:node->adjacent) if(!alignmentOwnsEdge(tile,n.coord)) return true;
  return false;
}

bool RoadNetwork::buildSeparatedRoad(Coord from,Coord to,float length,int elevation) {
  const auto a=roadTile(from),b=roadTile(to);
  const float minimum=float(std::abs(a.x-b.x)+std::abs(a.y-b.y));
  if(!isValidCoord(from)||!isValidCoord(to)||from==to || !std::isfinite(length) ||
     length<minimum || length<1 || length>float(cityMap.getTileCount()) || (elevation!=-1 && elevation!=1) || hasRoad(from,to) ||
     cityMap.getTile(a).regionalConnectionId || cityMap.getTile(b).regionalConnectionId) return false;
  Edge edge{makeNodeId(from),makeNodeId(to),length,kLocalRoadCapacity,0.f};edge.elevation=elevation;
  edges.emplace(makeEdgeKey(from,to),edge);++separatedRoadCount_;
  ensureNode(from).adjacent.push_back(makeNodeId(to));
  ensureNode(to).adjacent.push_back(makeNodeId(from));
  if(roadLevel(from)==0) const_cast<CityMap&>(cityMap).getTile(from).hasRoad=true;
  if(roadLevel(to)==0) const_cast<CityMap&>(cityMap).getTile(to).hasRoad=true;
  ++topologyVersion;++routingVersion;return true;
}
float RoadNetwork::edgeLength(Coord from,Coord to) const {
  const auto found=edges.find(makeEdgeKey(from,to));return found==edges.end() ? 1.f : found->second.length;
}
int RoadNetwork::edgeElevation(Coord from,Coord to) const {
  const auto found=edges.find(makeEdgeKey(from,to));return found==edges.end() ? 0 : found->second.elevation;
}

bool RoadNetwork::hasSeparatedRoads() const {
  return separatedRoadCount_!=0;
}
std::unordered_map<Coord,int,Vec2Hash> RoadNetwork::distanceField(const std::vector<Coord>& sources,int maxDistance) const {
  std::unordered_map<Coord,int,Vec2Hash> distance;
  if(maxDistance<0) return distance;
  std::queue<Coord> pending;
  for(Coord source:sources) if(distance.emplace(source,0).second) pending.push(source);
  while(!pending.empty()) {
    const auto current=pending.front();pending.pop();
    const auto* node=getNode(current);if(!node) continue;
    for(const auto& n:node->adjacent) {
      const int next=distance[current]+int(std::ceil(edgeLength(current,n.coord)));
      if(next>maxDistance) continue;
      const auto found=distance.find(n.coord);
      if(found!=distance.end() && found->second<=next) continue;
      distance[n.coord]=next;pending.push(n.coord);
    }
  }
  return distance;
}

bool RoadNetwork::canAttachDeckJunction(Coord node) const {
  if(roadLevel(node)!=1) return false;
  const auto tile=roadTile(node);
  for(const auto& a:alignments_) if(a.hasDeckAt(tile)) {
    const auto found=std::find(a.path.begin(),a.path.end(),tile);
    if(found!=a.path.end() && found!=a.path.begin() && found+1!=a.path.end()) return true;
  }
  return false;
}
bool RoadNetwork::attachDeckJunction(Coord node) {
  if(!canAttachDeckJunction(node)) return false;
  const auto tile=roadTile(node);
  for(auto& alignment:alignments_) if(alignment.hasDeckAt(tile)) {
    const auto at=std::find(alignment.path.begin(),alignment.path.end(),tile);
    if(at==alignment.path.end()) continue;
    const size_t index=static_cast<size_t>(at-alignment.path.begin());
    if(std::find(alignment.deckJunctions.begin(),alignment.deckJunctions.end(),index)!=alignment.deckJunctions.end()) return true;
    const auto route=alignment.routingPath();const auto indices=alignment.routingIndices();
    for(size_t i=1;i<indices.size();++i) if(indices[i-1]<index && index<indices[i]) {
      const Coord from=route[i-1],to=route[i];const auto key=makeEdgeKey(from,to);
      const auto found=edges.find(key);if(found==edges.end()) return false;
      const Edge original=found->second;
      const bool forward=original.from.coord==from;
      const auto unlink=[&](Coord a,Coord b) {
        auto& adjacent=nodes.at(a).adjacent;
        adjacent.erase(std::remove(adjacent.begin(),adjacent.end(),makeNodeId(b)),adjacent.end());
      };
      unlink(from,to);unlink(to,from);edges.erase(found);--separatedRoadCount_;
      const auto insert=[&](Coord a,Coord b,float length) {
        Edge edge=original;edge.from=makeNodeId(forward ? a : b);edge.to=makeNodeId(forward ? b : a);edge.length=length;
        edges.emplace(makeEdgeKey(a,b),edge);++separatedRoadCount_;
        ensureNode(a).adjacent.push_back(makeNodeId(b));ensureNode(b).adjacent.push_back(makeNodeId(a));
      };
      insert(from,node,float(index-indices[i-1]));insert(node,to,float(indices[i]-index));
      alignment.deckJunctions.push_back(index);std::sort(alignment.deckJunctions.begin(),alignment.deckJunctions.end());
      ++topologyVersion;++routingVersion;return true;
    }
  }
  return false;
}
