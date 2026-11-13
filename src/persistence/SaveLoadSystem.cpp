#include "src/persistence/UtilityJson.hpp"
#include "src/persistence/EnvironmentJson.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "SaveLoadSystem.hpp"
#include "src/gameplay/UniqueBuildings.hpp"
#include "src/gameplay/Monuments.hpp"

#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <unordered_set>
#include <unordered_map>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
constexpr int kCurrentSnapshotVersion = 1;
constexpr int kMinimumSupportedSnapshotVersion = 0;

bool isTileTypeValid(int type) {
  return type >= 0 && type <= 2;
}

bool isZoneValid(int zone) {
  return zone >= 0 && zone <= 5;
}

bool isBuildingTypeValid(int type) {
  return type >= static_cast<int>(BuildingType::Residential)
      && type <= static_cast<int>(BuildingType::Office);
}

bool isIncomeBandValid(int band) {
  return band >= static_cast<int>(IncomeBand::Low)
      && band <= static_cast<int>(IncomeBand::High);
}

bool isInBounds(int x, int y, int width, int height) {
  return x >= 0 && y >= 0 && x < width && y < height;
}

bool areAdjacentCardinal(const glm::ivec2& a, const glm::ivec2& b) {
  const int dx = std::abs(a.x - b.x);
  const int dy = std::abs(a.y - b.y);
  return (dx + dy) == 1;
}

int64_t tileKey(int x, int y) {
  return (static_cast<int64_t>(x) << 32) ^ static_cast<uint32_t>(y);
}

bool migrateSnapshotJsonToCurrent(
  json& root,
  int& sourceVersion,
  bool& migrationApplied,
  std::string& migrationPath
) {
  if (!root.is_object()) {
    return false;
  }

  sourceVersion = root.value("version", 0);
  migrationApplied = false;
  migrationPath = "none";

  if (sourceVersion < kMinimumSupportedSnapshotVersion || sourceVersion > kCurrentSnapshotVersion) {
    return false;
  }

  if (sourceVersion == 0) {
    if (!root.contains("map")) {
      if (!root.contains("width") || !root.contains("height")) {
        return false;
      }
      root["map"] = {
        {"width", root.at("width")},
        {"height", root.at("height")}
      };
    }

    if (!root.contains("tiles") || !root["tiles"].is_array()) {
      return false;
    }

    for (auto& tileJson : root["tiles"]) {
      if (!tileJson.is_object()) {
        return false;
      }
      if (!tileJson.contains("connectedToPower")) {
        tileJson["connectedToPower"] = true;
      }
      if (!tileJson.contains("connectedToWater")) {
        tileJson["connectedToWater"] = true;
      }
      if (!tileJson.contains("connectedToRoad")) {
        tileJson["connectedToRoad"] = tileJson.value("hasRoad", false);
      }
      if (!tileJson.contains("buildingId")) {
        tileJson["buildingId"] = 0;
      }
    }

    if (!root.contains("buildings")) {
      root["buildings"] = json::array();
    }
    if (!root.contains("populationGroups")) {
      root["populationGroups"] = json::array();
    }
    if (!root.contains("roads")) {
      root["roads"] = json::array();
    }

    root["version"] = kCurrentSnapshotVersion;
    migrationApplied = true;
    migrationPath = "v0->v1";
  }

  return root.value("version", -1) == kCurrentSnapshotVersion;
}

bool validateSnapshot(const CitySnapshot& snapshot, std::string* errorMessage = nullptr) {
  auto fail = [errorMessage](const std::string& message) {
    if (errorMessage != nullptr) {
      *errorMessage = message;
    }
    return false;
  };
  if (snapshot.version != kCurrentSnapshotVersion) {
    return fail("snapshot version is not the current schema version");
  }

  if (snapshot.width <= 0 || snapshot.height <= 0) {
    return fail("map dimensions must be positive");
  }

  const size_t expectedTileCount = static_cast<size_t>(snapshot.width) * static_cast<size_t>(snapshot.height);
  if (snapshot.tiles.size() != expectedTileCount) {
    return fail("tile count does not match map dimensions");
  }

  if(!validEnvironment(snapshot.environment)) return fail("invalid environment state");
  std::unordered_set<int64_t> seenTiles;
  seenTiles.reserve(snapshot.tiles.size());

  std::unordered_set<uint32_t> buildingIds;
  buildingIds.reserve(snapshot.buildings.size());

  // Hotels and unique civic entities both own an unzoned, single-tile site.
  std::unordered_map<uint32_t,const Building*> hotels;
  std::unordered_set<uint32_t> uniqueKinds;
  std::unordered_set<uint32_t> monumentKinds;
  std::unordered_set<int64_t> hotelPositions;
  std::unordered_set<uint32_t> hotelFootprints;
  for (const Building& building : snapshot.buildings) {
    if (building.id == EntityIdUtils::NullEntity) {
      return fail("building has a null entity ID");
    }
    if (!isBuildingTypeValid(static_cast<int>(building.type))) {
      return fail("building type is out of range");
    }
    if (!isInBounds(building.position.x, building.position.y, snapshot.width, snapshot.height)) {
      return fail("building position is outside the map");
    }
    if (building.capacity < 0 || building.occupancy < 0 || building.occupancy > building.capacity) {
      return fail("building occupancy or capacity is invalid");
    }
    if (!building.hotelBeds && !building.hotelOpen) return fail("hotel policy requires a hotel");
    if (building.hotelBeds && (building.type != BuildingType::Commercial ||
        building.hotelBeds > 1000)) return fail("invalid hotel specialization");
    if (building.uniqueKind && (!uniques::spec(building.uniqueKind) ||
        building.type != BuildingType::Office || building.capacity || building.occupancy ||
        building.hotelBeds || building.monumentKind || !uniqueKinds.insert(building.uniqueKind).second))
      return fail("invalid or duplicate unique building");
    if(building.monumentKind && (!monuments::spec(building.monumentKind) ||
        building.type!=BuildingType::Office || building.capacity || building.occupancy ||
        building.hotelBeds || building.uniqueKind || !monumentKinds.insert(building.monumentKind).second))
      return fail("invalid or duplicate monument");
    if (building.hotelBeds || building.isCivic()) {
      if (!hotels.emplace(building.id,&building).second ||
          !hotelPositions.insert(tileKey(building.position.x,building.position.y)).second)
        return fail("duplicate hotel entity or footprint");
    }
    if (!std::isfinite(building.crimeRate) ||
        !std::isfinite(building.illnessRate) || building.crimeRate < 0.0f ||
        building.crimeRate > 1.0f || building.illnessRate < 0.0f ||
        building.illnessRate > 1.0f) {
      return fail("building neighborhood rates are invalid");
    }
    if (!buildingIds.insert(static_cast<uint32_t>(building.id)).second &&
        (building.hotelBeds || building.isCivic() || hotels.count(building.id)))
      return fail("duplicate hotel entity ID");
  }

  for (const PopulationGroup& group : snapshot.populationGroups) {
    if (group.id == EntityIdUtils::NullEntity) {
      return fail("population group has a null entity ID");
    }
    if (!isIncomeBandValid(static_cast<int>(group.band))) {
      return fail("population income band is out of range");
    }
    if (group.employed > group.size) {
      return fail("population employed count exceeds group size");
    }
  }

  uint64_t minimumNext = 1;
  for (const auto& building : snapshot.buildings)
    minimumNext = std::max(minimumNext, static_cast<uint64_t>(building.id) + 1);
  for (const auto& group : snapshot.populationGroups)
    minimumNext = std::max(minimumNext, static_cast<uint64_t>(group.id) + 1);
  if (snapshot.nextEntityId != 0 &&
      (snapshot.nextEntityId < minimumNext || snapshot.nextEntityId >
       static_cast<uint64_t>(std::numeric_limits<EntityId>::max()) + 1))
    return fail("snapshot has invalid next entity ID");

  for (const SerializedTile& tile : snapshot.tiles) {
    if(tile.utility.waterBooster && (!tile.utility.waterPipe || tile.type==2)) return fail("water booster requires a water pipe on land");
    if(tile.utility.sewageLift && (!tile.utility.sewerPipe || tile.type==2)) return fail("sewer lift requires a sewer pipe on land");
    if(!std::isfinite(tile.utility.sewageBacklog) || tile.utility.sewageBacklog<0 || tile.utility.sewageBacklog>1e15 ||
      (!snapshot.pipeNetworksEnabled && (tile.utility.waterPipe || tile.utility.sewerPipe || tile.utility.sewageBacklog>0))) return fail("invalid underground utility tile");
    if(snapshot.environment.initialized && !validEnvironment(tile.environment,tile.type==2)) return fail("invalid environment tile");
    if (!isInBounds(tile.x, tile.y, snapshot.width, snapshot.height)) {
      return fail("tile position is outside the map");
    }
    if (!isTileTypeValid(tile.type) || !isZoneValid(tile.zone)) {
      return fail("tile type or zone is out of range");
    }
    if (tile.buildingId != 0 && buildingIds.find(tile.buildingId) == buildingIds.end()) {
      return fail("tile references a missing building ID");
    }

    const auto hotel = hotels.find(tile.buildingId);
    if (hotel != hotels.end()) {
      if (hotel->second->position != Coord{tile.x,tile.y} || tile.zone ||
          tile.hasRoad || tile.type == 2 || !hotelFootprints.insert(tile.buildingId).second)
        return fail("hotel does not own an unzoned land footprint");
    }
    const int64_t key = tileKey(tile.x, tile.y);
    if (!seenTiles.insert(key).second) {
      return fail("snapshot contains duplicate tile coordinates");
    }
  }

  if (hotelFootprints.size() != hotels.size()) return fail("hotel footprint is missing");
  for (const SerializedRoad& road : snapshot.roads) {
    if (hotelPositions.count(tileKey(road.from.x,road.from.y)) ||
        hotelPositions.count(tileKey(road.to.x,road.to.y)))
      return fail("hotel footprint overlaps a road edge");
    if (!isInBounds(road.from.x, road.from.y, snapshot.width, snapshot.height)) {
      return fail("road start is outside the map");
    }
    if (!isInBounds(road.to.x, road.to.y, snapshot.width, snapshot.height)) {
      return fail("road end is outside the map");
    }
    const int span=std::abs(road.from.x-road.to.x)+std::abs(road.from.y-road.to.y);
    if(road.elevation < -1 || road.elevation > 1 || !std::isfinite(road.length) ||
       road.fromLevel<0 || road.fromLevel>1 || road.toLevel<0 || road.toLevel>1 ||
       ((road.fromLevel || road.toLevel) && road.elevation!=1) ||
       (road.elevation==0 && (!areAdjacentCardinal(road.from,road.to) || road.length!=1.f)) ||
       (road.elevation!=0 && (span==0 || road.length<1.f || road.length<float(span))))
      return fail("road endpoints, length or elevation are invalid");
    if (!std::isfinite(road.currentLoad) || !std::isfinite(road.freightLoad) || !std::isfinite(road.visitorLoad) ||
        road.freightLoad < 0.0f || road.visitorLoad < 0.0f ||
        road.freightLoad + road.visitorLoad > road.currentLoad ||
        !validRoadClass(road.roadClass)) {
      return fail("road class or freight load is invalid");
    }
    if (road.currentLoad < 0.0f) {
      return fail("road load cannot be negative");
    }
  }

  std::unordered_set<Coord,Vec2Hash> surfaceNodes;
  for(const auto& road:snapshot.roads) {
    if(road.fromLevel==0) surfaceNodes.insert(road.from);
    if(road.toLevel==0) surfaceNodes.insert(road.to);
  }
  for(const auto& alignment:snapshot.roadAlignments)
    if(alignment.path.size()<2 || alignment.footprint.empty() || alignment.curves.empty())
      return fail("road alignment is missing its geometry or footprint");
  if(!snapshot.roadAlignments.empty()) {
    CityMap geometryMap({snapshot.width,snapshot.height});RoadNetwork geometryRoads(geometryMap);
    for(const auto& tile:snapshot.tiles) geometryMap.getTile({tile.x,tile.y}).type=tile.type;
    for(const auto& a:snapshot.roadAlignments) {
      const auto plan=RoadTool::planAlignment(geometryMap,geometryRoads,a,std::numeric_limits<int64_t>::max(),nullptr,false);
      if(!plan.valid || plan.tiles!=a.path || plan.alignment.footprint!=a.footprint)
        return fail("invalid road alignment geometry or footprint");
      for(Coord p:a.footprint) {
        const auto found=std::find_if(snapshot.tiles.begin(),snapshot.tiles.end(),[&](const SerializedTile& t) {
          return t.x==p.x && t.y==p.y;
        });
        if(found==snapshot.tiles.end() || !found->hasRoad || found->buildingId)
          return fail("road alignment footprint is not reserved");
      }
      if(a.elevation!=0) {
        const auto& curve=a.curves.front();const int steps=curve.subdivisions();
        for(int i=0;i<=steps;++i) {
          const float t=float(i)/steps;const auto p=curve.point(t);
          const Coord tile{int(std::floor(p.x)),int(std::floor(p.y))};
          if(tile!=a.path.front() && tile!=a.path.back() && std::abs(a.height(t))<.5f && surfaceNodes.count(tile))
            return fail("road crosses a structure approach without clearance");
        }
        for(const auto& other:snapshot.roadAlignments) {
          if(&a==&other || a.elevation!=other.elevation) continue;
          for(Coord tile:a.footprint) {
            if(tile==a.path.front() || tile==a.path.back() || tile==other.path.front() || tile==other.path.back()) continue;
            if(std::find(other.footprint.begin(),other.footprint.end(),tile)!=other.footprint.end() &&
               !roadDeckMerge(a,other,tile) && !roadDeckMerge(other,a,tile))
              return fail("road structures on the same level overlap");
          }
        }
      }
      const auto route=a.routingPath();const auto indices=a.routingIndices();
      for(size_t i=1;i<route.size();++i) {
        const Coord from=route[i-1],to=route[i];
        const auto edge=std::find_if(snapshot.roads.begin(),snapshot.roads.end(),[&](const SerializedRoad& r) {
          const auto x=roadNode(r.from,r.fromLevel),y=roadNode(r.to,r.toLevel);
          return (x==from && y==to)||(x==to && y==from);
        });
        if(edge==snapshot.roads.end() || edge->elevation!=a.elevation ||
           (a.elevation!=0 && edge->length!=float(indices[i]-indices[i-1])) || (a.oneWay &&
           !(a.roadClass==RoadClass::Ramp && edge->roadClass!=RoadClass::Ramp && !edge->oneWay) &&
           (!edge->oneWay || roadNode(edge->from,edge->fromLevel)!=from)))
          return fail("road alignment references a missing or conflicting road edge");
      }
    }
  }
  for(const auto& a:snapshot.roadAlignments) {
    const auto route=a.routingPath();
    for(Coord endpoint:{route.front(),route.back()}) if(roadLevel(endpoint)==1) {
      bool attached=false;
      for(const auto& host:snapshot.roadAlignments) if(host.hasDeckAt(roadTile(endpoint))) {
        for(size_t index:host.deckJunctions) if(roadNode(host.path[index],1)==endpoint) attached=true;
      }
      if(!attached) return fail("ramp references a missing highway deck junction");
    }
  }
  for(const auto& road:snapshot.roads) if(road.elevation!=0) {
    const auto from=roadNode(road.from,road.fromLevel),to=roadNode(road.to,road.toLevel);
    bool owned=false;
    for(const auto& a:snapshot.roadAlignments) if(a.elevation==road.elevation) {
      const auto route=a.routingPath();
      for(size_t i=1;i<route.size();++i)
        if((route[i-1]==from && route[i]==to)||(route[i-1]==to && route[i]==from)) owned=true;
    }
    if(!owned) return fail("separated road is missing its alignment");
  }
  return true;
}

json coordToJson(const glm::ivec2& coord) {
  return json{{"x", coord.x}, {"y", coord.y}};
}

glm::ivec2 coordFromJson(const json& j) {
  return glm::ivec2{j.at("x").get<int>(), j.at("y").get<int>()};
}

json buildingToJson(const Building& building) {
  json result{{"id", building.id},
              {"type", static_cast<int>(building.type)},
              {"position", coordToJson(building.position)},
              {"capacity", building.capacity},
              {"occupancy", building.occupancy},
              {"crimeRate", building.crimeRate},
              {"illnessRate", building.illnessRate}};
  if (building.hotelBeds) {
    result["hotelBeds"] = building.hotelBeds;
    result["hotelOpen"] = building.hotelOpen;
  }
  if (building.uniqueKind) result["uniqueKind"] = building.uniqueKind;
  if (building.monumentKind) result["monumentKind"] = building.monumentKind;
  return result;
}

Building buildingFromJson(const json& j) {
  Building building;
  building.id = j.at("id").get<EntityId>();
  building.type = static_cast<BuildingType>(j.at("type").get<int>());
  building.position = coordFromJson(j.at("position"));
  building.capacity = j.at("capacity").get<int>();
  building.occupancy = j.at("occupancy").get<int>();
  if(j.contains("monumentKind")) {
    const auto& value=j.at("monumentKind");
    if(!value.is_number_integer() || value.get<int64_t>()<1 || value.get<uint64_t>()>monuments::count)
      throw std::runtime_error("invalid monument kind");
    building.monumentKind=value.get<uint32_t>();
  }
  if (j.contains("uniqueKind")) {
    const auto& value = j.at("uniqueKind");
    if (!value.is_number_integer() || value.get<int64_t>() < 1 || value.get<uint64_t>() > uniques::count)
      throw std::runtime_error("invalid unique building kind");
    building.uniqueKind = value.get<uint32_t>();
  }
  if (j.contains("hotelBeds")) {
    const auto& beds = j.at("hotelBeds");
    if (!beds.is_number_integer() || beds.get<int64_t>() < 0 || beds.get<int64_t>() > 1000)
      throw std::runtime_error("hotel beds must be an integer from 0 to 1000");
    building.hotelBeds = beds.get<uint32_t>();
  }
  if (j.contains("hotelOpen")) {
    if (!j.at("hotelOpen").is_boolean() || !building.hotelBeds)
      throw std::runtime_error("hotel policy requires a hotel and boolean value");
    building.hotelOpen=j.at("hotelOpen").get<bool>();
  }
  building.crimeRate = j.value("crimeRate", 0.0f);
  building.illnessRate = j.value("illnessRate", 0.0f);
  return building;
}

json groupToJson(const PopulationGroup& group) {
  return json{
    {"id", group.id},
    {"band", static_cast<int>(group.band)},
    {"size", group.size},
    {"employed", group.employed}
  };
}

PopulationGroup groupFromJson(const json& j) {
  PopulationGroup group;
  group.id = j.at("id").get<EntityId>();
  group.band = static_cast<IncomeBand>(j.at("band").get<int>());
  group.size = j.at("size").get<uint32_t>();
  group.employed = j.at("employed").get<uint32_t>();
  return group;
}
} // namespace

CitySnapshot SaveLoadSystem::captureSnapshot(
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population
) {
  CitySnapshot snapshot;
  snapshot.version = kCurrentSnapshotVersion;
  snapshot.environment=map.environment();
  snapshot.pipeNetworksEnabled=map.pipeNetworksEnabled();
  snapshot.nextEntityId = EntityIdUtils::nextEntityId();
  const glm::ivec2 dims = map.getDimensions();
  snapshot.width = dims.x;
  snapshot.height = dims.y;

  snapshot.tiles.reserve(static_cast<size_t>(dims.x * dims.y));
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      SerializedTile serialized;
      serialized.x = x;
      serialized.y = y;
      serialized.type = tile.type;serialized.environment=map.environment({x,y});
      serialized.utility=map.utility({x,y});serialized.connectedToSewer=tile.connectedToSewer;
      serialized.zone = tile.zone;
      serialized.landValue = map.landValue({x, y});
      serialized.pollution = map.pollution({x, y});
      serialized.hasRoad = tile.hasRoad;
      serialized.connectedToRoad = tile.connectedToRoad;
      serialized.connectedToPower = tile.connectedToPower;
      serialized.connectedToWater = tile.connectedToWater;
      serialized.buildingId = tile.buildingId;
      snapshot.tiles.push_back(serialized);
    }
  }

  snapshot.buildings.reserve(store.getBuildings().size());
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    snapshot.buildings.push_back(building);
  }

  snapshot.populationGroups.reserve(population.getGroups().size());
  for (const auto& [id, group] : population.getGroups()) {
    (void)id;
    snapshot.populationGroups.push_back(group);
  }
  // Hash-map order depends on insertion and removal history; sort by id so
  // equal stores always write identical snapshots.
  std::sort(snapshot.buildings.begin(), snapshot.buildings.end(),
            [](const Building& a, const Building& b) { return a.id < b.id; });
  std::sort(snapshot.populationGroups.begin(), snapshot.populationGroups.end(),
            [](const auto& a, const auto& b) { return a.id < b.id; });

  const auto edgeTraffic = roads.getAllEdgeTraffic();
  snapshot.roads.reserve(edgeTraffic.size());
  for (const auto& edge : edgeTraffic) {
    snapshot.roads.push_back(SerializedRoad{roadTile(edge.from), roadTile(edge.to),
                                            edge.currentLoad, edge.roadClass,
                                            edge.freightLoad,edge.visitorLoad,edge.oneWay,edge.length,edge.elevation,roadLevel(edge.from),roadLevel(edge.to)});
  }

  snapshot.roadAlignments=roads.alignments();
  return snapshot;
}

bool SaveLoadSystem::applySnapshot(
  const CitySnapshot& snapshot,
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  std::string* errorMessage
) {
  const glm::ivec2 dims = map.getDimensions();
  if (snapshot.width != dims.x || snapshot.height != dims.y) {
    if (errorMessage != nullptr) {
      *errorMessage = "snapshot dimensions do not match the destination map";
    }
    return false;
  }
  if (!validateSnapshot(snapshot, errorMessage)) {
    return false;
  }

  store.clear();
  population.clear();
  roads.clear();

  map.environment()=snapshot.environment;
  map.pipeNetworksEnabled()=snapshot.pipeNetworksEnabled;
  for (const SerializedTile& serialized : snapshot.tiles) {
    if (!map.isValid({serialized.x, serialized.y})) {
      return false;
    }
    Tile& tile = map.getTile({serialized.x, serialized.y});
    tile.type = serialized.type;map.environment({serialized.x,serialized.y})=serialized.environment;
    map.utility({serialized.x,serialized.y})=serialized.utility;tile.connectedToSewer=serialized.connectedToSewer;
    tile.zone = serialized.zone;
    map.landValue({serialized.x, serialized.y}) = serialized.landValue;
    map.pollution({serialized.x, serialized.y}) = serialized.pollution;
    tile.hasRoad = serialized.hasRoad;
    tile.connectedToRoad = serialized.connectedToRoad;
    tile.connectedToPower = serialized.connectedToPower;
    tile.connectedToWater = serialized.connectedToWater;
    tile.buildingId = serialized.buildingId;
    tile.regionalConnectionId = 0; // Rebuilt by the owning gameplay session.
  }

  for (const Building& building : snapshot.buildings) {
    store.upsertBuilding(building);
  }

  for (const PopulationGroup& group : snapshot.populationGroups) {
    population.upsertGroup(group);
  }

  for (const SerializedRoad& road : snapshot.roads) {
    const auto from=roadNode(road.from,road.fromLevel),to=roadNode(road.to,road.toLevel);
    if(road.elevation!=0) roads.buildSeparatedRoad(from,to,road.length,road.elevation);
    else roads.buildRoad(from,to);
    roads.setRoadClass(from,to,road.roadClass);roads.setOneWay(from,to,road.oneWay);
    if(road.currentLoad>0.f) {
      roads.updateCongestion(from,to,road.currentLoad-road.freightLoad-road.visitorLoad);
      roads.addFreightLoad(from,to,road.freightLoad);roads.addVisitorLoad(from,to,road.visitorLoad);
    }
  }

  for(const auto& alignment:snapshot.roadAlignments) roads.addAlignment(alignment);
  if (!snapshot.roads.empty()) {
    roads.updateConnectivity(roadNode(snapshot.roads.front().from,snapshot.roads.front().fromLevel));
  }

  uint64_t next = snapshot.nextEntityId;
  if (next == 0) {
    next = 1;
    for (const auto& building : snapshot.buildings)
      next = std::max(next, static_cast<uint64_t>(building.id) + 1);
    for (const auto& group : snapshot.populationGroups)
      next = std::max(next, static_cast<uint64_t>(group.id) + 1);
  }
  EntityIdUtils::restoreNextEntityId(next);

  return true;
}

bool SaveLoadSystem::saveToFile(
  const std::string& filePath,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population
) {
  const CitySnapshot snapshot = captureSnapshot(map, roads, store, population);

  json root;
  root["version"] = snapshot.version;
  root["nextEntityId"] = snapshot.nextEntityId;
  if(snapshot.environment.initialized) root["environment"]=snapshot.environment;
  if(snapshot.pipeNetworksEnabled) root["pipeNetworks"]={{"version",1},{"enabled",true}};
  root["map"] = {
    {"width", snapshot.width},
    {"height", snapshot.height}
  };

  root["tiles"] = json::array();
  for (const SerializedTile& tile : snapshot.tiles) {
    root["tiles"].push_back(json{
      {"x", tile.x},
      {"y", tile.y},
      {"type", tile.type},
      {"zone", tile.zone},
      {"landValue", tile.landValue},
      {"pollution", tile.pollution},
      {"hasRoad", tile.hasRoad},
      {"connectedToRoad", tile.connectedToRoad},
      {"connectedToPower", tile.connectedToPower},
      {"connectedToWater", tile.connectedToWater},
      {"buildingId", tile.buildingId}
    });
    if(snapshot.environment.initialized) root["tiles"].back()["environment"]=tile.environment;
    if(snapshot.pipeNetworksEnabled) {root["tiles"].back()["utility"]=tile.utility;root["tiles"].back()["connectedToSewer"]=tile.connectedToSewer;}
  }

  root["buildings"] = json::array();
  for (const Building& building : snapshot.buildings) {
    root["buildings"].push_back(buildingToJson(building));
  }

  root["populationGroups"] = json::array();
  for (const PopulationGroup& group : snapshot.populationGroups) {
    root["populationGroups"].push_back(groupToJson(group));
  }

  root["roads"] = json::array();
  for (const SerializedRoad& road : snapshot.roads) {
    root["roads"].push_back(
        json{{"from", coordToJson(road.from)},
             {"to", coordToJson(road.to)},
             {"currentLoad", road.currentLoad},
             {"roadClass", static_cast<int>(road.roadClass)},
             {"freightLoad", road.freightLoad},{"visitorLoad",road.visitorLoad},{"oneWay",road.oneWay},{"length",road.length},{"elevation",road.elevation},{"fromLevel",road.fromLevel},{"toLevel",road.toLevel}});
  }

  root["roadAlignments"]=json::array();
  for(const auto& a:snapshot.roadAlignments) {
    json curves=json::array(),path=json::array(),footprint=json::array();
    for(const auto& curve:a.curves) {
      json points=json::array();
      for(const auto p:curve.points) points.push_back({p.x,p.y});
      curves.push_back(points);
    }
    for(Coord p:a.path) path.push_back(coordToJson(p));
    for(Coord p:a.footprint) footprint.push_back(coordToJson(p));
    root["roadAlignments"].push_back({{"curves",curves},{"path",path},{"footprint",footprint},
      {"roadClass",int(a.roadClass)},{"snapped",a.snapped},{"oneWay",a.oneWay},{"roundabout",a.roundabout},{"elevation",a.elevation},{"startLevel",a.startLevel},{"endLevel",a.endLevel},{"deckJunctions",a.deckJunctions}});
  }

  std::ofstream out(filePath);
  if (!out.is_open()) {
    return false;
  }

  out << root.dump(2);
  return static_cast<bool>(out);
}

bool SaveLoadSystem::loadSnapshotFromFile(
  const std::string& filePath,
  CitySnapshot& snapshot,
  SnapshotLoadDiagnostics* diagnostics
) {
  if (diagnostics != nullptr) {
    *diagnostics = SnapshotLoadDiagnostics{};
  }

  std::ifstream in(filePath);
  if (!in.is_open()) {
    if (diagnostics != nullptr) {
      diagnostics->errorMessage = "unable to open snapshot file";
    }
    return false;
  }

  json root;
  try {
    in >> root;

    int sourceVersion = -1;
    bool migrationApplied = false;
    std::string migrationPath;

    if (!migrateSnapshotJsonToCurrent(root, sourceVersion, migrationApplied, migrationPath)) {
      if (diagnostics != nullptr) {
        diagnostics->sourceVersion = sourceVersion;
        diagnostics->errorMessage = sourceVersion > kCurrentSnapshotVersion
          ? "snapshot schema version is newer than this build supports"
          : "snapshot schema is unsupported or cannot be migrated";
      }
      return false;
    }

    if (diagnostics != nullptr) {
      diagnostics->sourceVersion = sourceVersion;
      diagnostics->targetVersion = root.value("version", -1);
      diagnostics->migrationApplied = migrationApplied;
      diagnostics->migrationPath = migrationPath;
    }

    snapshot = CitySnapshot{};
    snapshot.version = root.value("version", kCurrentSnapshotVersion);
    if (root.contains("nextEntityId")) {
      const auto& next = root.at("nextEntityId");
      if (!next.is_number_integer() || next.get<int64_t>() <= 0 ||
          next.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<EntityId>::max()) + 1)
        throw std::runtime_error("invalid next entity ID");
      snapshot.nextEntityId = next.get<uint64_t>();
    }
    if(root.contains("environment")) snapshot.environment=root.at("environment").get<EnvironmentState>();
    if(root.contains("pipeNetworks")) {
      if(root.at("pipeNetworks").at("version").get<int>()!=1) throw std::runtime_error("unsupported pipe network version");
      snapshot.pipeNetworksEnabled=root.at("pipeNetworks").at("enabled").get<bool>();
    }
    snapshot.width = root.at("map").at("width").get<int>();
    snapshot.height = root.at("map").at("height").get<int>();

    for (const auto& tileJson : root.at("tiles")) {
      SerializedTile tile;
      tile.x = tileJson.at("x").get<int>();
      tile.y = tileJson.at("y").get<int>();
      tile.type = tileJson.at("type").get<int>();
      if(snapshot.pipeNetworksEnabled) {tile.utility=tileJson.at("utility").get<UtilityCell>();tile.connectedToSewer=tileJson.at("connectedToSewer").get<bool>();}
      if(snapshot.environment.initialized) tile.environment=tileJson.at("environment").get<EnvironmentCell>();
      tile.zone = tileJson.at("zone").get<int>();
      tile.landValue = tileJson.at("landValue").get<float>();
      tile.pollution = tileJson.at("pollution").get<float>();
      tile.hasRoad = tileJson.at("hasRoad").get<bool>();
      tile.connectedToRoad = tileJson.at("connectedToRoad").get<bool>();
      tile.connectedToPower = tileJson.at("connectedToPower").get<bool>();
      tile.connectedToWater = tileJson.at("connectedToWater").get<bool>();
      tile.buildingId = tileJson.at("buildingId").get<uint32_t>();
      snapshot.tiles.push_back(tile);
    }

    for (const auto& buildingJson : root.value("buildings", json::array())) {
      snapshot.buildings.push_back(buildingFromJson(buildingJson));
    }

    for (const auto& groupJson : root.value("populationGroups", json::array())) {
      snapshot.populationGroups.push_back(groupFromJson(groupJson));
    }

    for (const auto& roadJson : root.value("roads", json::array())) {
      SerializedRoad road;
      road.from = coordFromJson(roadJson.at("from"));
      road.to = coordFromJson(roadJson.at("to"));
      road.currentLoad = roadJson.at("currentLoad").get<float>();
      road.roadClass = static_cast<RoadClass>(roadJson.value("roadClass", 0));
      road.freightLoad = roadJson.value("freightLoad", 0.0f);
      road.visitorLoad = roadJson.value("visitorLoad",0.0f);
      road.oneWay = roadJson.value("oneWay",false);
      road.length = roadJson.value("length",1.f);road.elevation=roadJson.value("elevation",0);
      road.fromLevel=roadJson.value("fromLevel",0);road.toLevel=roadJson.value("toLevel",0);
      snapshot.roads.push_back(road);
    }
    for(const auto& item:root.value("roadAlignments",json::array())) {
      RoadAlignment a;a.roadClass=static_cast<RoadClass>(item.at("roadClass").get<int>());
      a.snapped=item.at("snapped").get<bool>();a.oneWay=item.at("oneWay").get<bool>();
      a.roundabout=item.at("roundabout").get<bool>();a.elevation=item.value("elevation",0);
      a.startLevel=item.value("startLevel",0);a.endLevel=item.value("endLevel",0);
      a.deckJunctions=item.value("deckJunctions",std::vector<size_t>{});
      for(const auto& curve:item.at("curves")) {
        if(!curve.is_array() || curve.size()!=4) throw std::runtime_error("road Bezier needs four points");
        RoadBezier b;
        for(size_t i=0;i<4;++i) {
          if(!curve[i].is_array() || curve[i].size()!=2) throw std::runtime_error("invalid road control point");
          b.points[i]={curve[i][0].get<float>(),curve[i][1].get<float>()};
        }
        a.curves.push_back(b);
      }
      for(const auto& p:item.at("path")) a.path.push_back(coordFromJson(p));
      for(const auto& p:item.at("footprint")) a.footprint.push_back(coordFromJson(p));
      snapshot.roadAlignments.push_back(std::move(a));
    }
  } catch (const std::exception& ex) {
    if (diagnostics != nullptr) {
      diagnostics->errorMessage = std::string("malformed snapshot: ") + ex.what();
    }
    return false;
  } catch (...) {
    if (diagnostics != nullptr) {
      diagnostics->errorMessage = "malformed snapshot: unknown parsing error";
    }
    return false;
  }

  std::string validationError;
  const bool valid = validateSnapshot(snapshot, &validationError);
  if (diagnostics != nullptr) {
    diagnostics->validationPassed = valid;
    if (!valid) {
      diagnostics->errorMessage = validationError;
    }
  }
  return valid;
}

bool SaveLoadSystem::loadFromFile(
  const std::string& filePath,
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  SnapshotLoadDiagnostics* diagnostics
) {
  CitySnapshot snapshot;
  if (!loadSnapshotFromFile(filePath, snapshot, diagnostics)) {
    return false;
  }

  std::string applyError;
  const bool applied = applySnapshot(snapshot, map, roads, store, population, &applyError);
  if (!applied && diagnostics != nullptr) {
    diagnostics->errorMessage = applyError;
  }
  return applied;
}
