#include "SaveLoadSystem.hpp"

#include <cstdlib>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
constexpr int kCurrentSnapshotVersion = 1;
constexpr int kMinimumSupportedSnapshotVersion = 0;

bool isTileTypeValid(int type) {
  return type >= 0 && type <= 2;
}

bool isZoneValid(int zone) {
  return zone >= 0 && zone <= 4;
}

bool isBuildingTypeValid(int type) {
  return type >= static_cast<int>(BuildingType::Residential)
      && type <= static_cast<int>(BuildingType::Industrial);
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

bool validateSnapshot(const CitySnapshot& snapshot) {
  if (snapshot.version != kCurrentSnapshotVersion) {
    return false;
  }

  if (snapshot.width <= 0 || snapshot.height <= 0) {
    return false;
  }

  const size_t expectedTileCount = static_cast<size_t>(snapshot.width) * static_cast<size_t>(snapshot.height);
  if (snapshot.tiles.size() != expectedTileCount) {
    return false;
  }

  std::unordered_set<int64_t> seenTiles;
  seenTiles.reserve(snapshot.tiles.size());

  std::unordered_set<uint32_t> buildingIds;
  buildingIds.reserve(snapshot.buildings.size());

  for (const Building& building : snapshot.buildings) {
    if (building.id == EntityIdUtils::NullEntity) {
      return false;
    }
    if (!isBuildingTypeValid(static_cast<int>(building.type))) {
      return false;
    }
    if (!isInBounds(building.position.x, building.position.y, snapshot.width, snapshot.height)) {
      return false;
    }
    if (building.capacity < 0 || building.occupancy < 0 || building.occupancy > building.capacity) {
      return false;
    }
    buildingIds.insert(static_cast<uint32_t>(building.id));
  }

  for (const PopulationGroup& group : snapshot.populationGroups) {
    if (group.id == EntityIdUtils::NullEntity) {
      return false;
    }
    if (!isIncomeBandValid(static_cast<int>(group.band))) {
      return false;
    }
    if (group.employed > group.size) {
      return false;
    }
  }

  for (const SerializedTile& tile : snapshot.tiles) {
    if (!isInBounds(tile.x, tile.y, snapshot.width, snapshot.height)) {
      return false;
    }
    if (!isTileTypeValid(tile.type) || !isZoneValid(tile.zone)) {
      return false;
    }
    if (tile.buildingId != 0 && buildingIds.find(tile.buildingId) == buildingIds.end()) {
      return false;
    }

    const int64_t key = tileKey(tile.x, tile.y);
    if (!seenTiles.insert(key).second) {
      return false;
    }
  }

  for (const SerializedRoad& road : snapshot.roads) {
    if (!isInBounds(road.from.x, road.from.y, snapshot.width, snapshot.height)) {
      return false;
    }
    if (!isInBounds(road.to.x, road.to.y, snapshot.width, snapshot.height)) {
      return false;
    }
    if (!areAdjacentCardinal(road.from, road.to)) {
      return false;
    }
    if (road.currentLoad < 0.0f) {
      return false;
    }
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
  snapshot.version = kCurrentSnapshotVersion;
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
  if (!validateSnapshot(snapshot)) {
    return false;
  }

  store.clear();
  population.clear();
  roads.resetCongestion();

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
      snapshot.roads.push_back(road);
    }
  } catch (...) {
    return false;
  }

  const bool valid = validateSnapshot(snapshot);
  if (diagnostics != nullptr) {
    diagnostics->validationPassed = valid;
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

  return applySnapshot(snapshot, map, roads, store, population);
}
