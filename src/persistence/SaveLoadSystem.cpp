#include "SaveLoadSystem.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
json coordToJson(const glm::ivec2& coord) {
  return json{{"x", coord.x}, {"y", coord.y}};
}

glm::ivec2 coordFromJson(const json& j) {
  return glm::ivec2{j.at("x").get<int>(), j.at("y").get<int>()};
}

json buildingToJson(const Building& building) {
  return json{
    {"id", building.id},
    {"type", static_cast<int>(building.type)},
    {"position", coordToJson(building.position)},
    {"capacity", building.capacity},
    {"occupancy", building.occupancy}
  };
}

Building buildingFromJson(const json& j) {
  Building building;
  building.id = j.at("id").get<EntityId>();
  building.type = static_cast<BuildingType>(j.at("type").get<int>());
  building.position = coordFromJson(j.at("position"));
  building.capacity = j.at("capacity").get<int>();
  building.occupancy = j.at("occupancy").get<int>();
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
      serialized.type = tile.type;
      serialized.zone = tile.zone;
      serialized.landValue = tile.landValue;
      serialized.pollution = tile.pollution;
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

  const auto edgeTraffic = roads.getAllEdgeTraffic();
  snapshot.roads.reserve(edgeTraffic.size());
  for (const auto& edge : edgeTraffic) {
    snapshot.roads.push_back(SerializedRoad{edge.from, edge.to, edge.currentLoad});
  }

  return snapshot;
}

bool SaveLoadSystem::applySnapshot(
  const CitySnapshot& snapshot,
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population
) {
  const glm::ivec2 dims = map.getDimensions();
  if (snapshot.width != dims.x || snapshot.height != dims.y) {
    return false;
  }

  store.clear();
  population.clear();

  for (const SerializedTile& serialized : snapshot.tiles) {
    if (!map.isValid({serialized.x, serialized.y})) {
      return false;
    }
    Tile& tile = map.getTile({serialized.x, serialized.y});
    tile.type = serialized.type;
    tile.zone = serialized.zone;
    tile.landValue = serialized.landValue;
    tile.pollution = serialized.pollution;
    tile.hasRoad = serialized.hasRoad;
    tile.connectedToRoad = serialized.connectedToRoad;
    tile.connectedToPower = serialized.connectedToPower;
    tile.connectedToWater = serialized.connectedToWater;
    tile.buildingId = serialized.buildingId;
  }

  for (const Building& building : snapshot.buildings) {
    store.upsertBuilding(building);
  }

  for (const PopulationGroup& group : snapshot.populationGroups) {
    population.upsertGroup(group);
  }

  for (const SerializedRoad& road : snapshot.roads) {
    roads.buildRoad(road.from, road.to);
    if (road.currentLoad > 0.0f) {
      roads.updateCongestion(road.from, road.to, road.currentLoad);
    }
  }

  if (!snapshot.roads.empty()) {
    roads.updateConnectivity(snapshot.roads.front().from);
  }

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
    root["roads"].push_back(json{
      {"from", coordToJson(road.from)},
      {"to", coordToJson(road.to)},
      {"currentLoad", road.currentLoad}
    });
  }

  std::ofstream out(filePath);
  if (!out.is_open()) {
    return false;
  }

  out << root.dump(2);
  return static_cast<bool>(out);
}

bool SaveLoadSystem::loadSnapshotFromFile(const std::string& filePath, CitySnapshot& snapshot) {
  std::ifstream in(filePath);
  if (!in.is_open()) {
    return false;
  }

  json root;
  try {
    in >> root;

    snapshot = CitySnapshot{};
    snapshot.version = root.value("version", 1);
    snapshot.width = root.at("map").at("width").get<int>();
    snapshot.height = root.at("map").at("height").get<int>();

    for (const auto& tileJson : root.at("tiles")) {
      SerializedTile tile;
      tile.x = tileJson.at("x").get<int>();
      tile.y = tileJson.at("y").get<int>();
      tile.type = tileJson.at("type").get<int>();
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

    for (const auto& buildingJson : root.at("buildings")) {
      snapshot.buildings.push_back(buildingFromJson(buildingJson));
    }

    for (const auto& groupJson : root.at("populationGroups")) {
      snapshot.populationGroups.push_back(groupFromJson(groupJson));
    }

    for (const auto& roadJson : root.at("roads")) {
      SerializedRoad road;
      road.from = coordFromJson(roadJson.at("from"));
      road.to = coordFromJson(roadJson.at("to"));
      road.currentLoad = roadJson.at("currentLoad").get<float>();
      snapshot.roads.push_back(road);
    }
  } catch (...) {
    return false;
  }

  return true;
}

bool SaveLoadSystem::loadFromFile(
  const std::string& filePath,
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population
) {
  CitySnapshot snapshot;
  if (!loadSnapshotFromFile(filePath, snapshot)) {
    return false;
  }

  return applySnapshot(snapshot, map, roads, store, population);
}
