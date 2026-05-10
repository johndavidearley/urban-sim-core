#pragma once

#include <string>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

struct SerializedTile {
  int x = 0;
  int y = 0;
  int type = 0;
  int zone = 0;
  float landValue = 100.0f;
  float pollution = 0.0f;
  bool hasRoad = false;
  bool connectedToRoad = false;
  bool connectedToPower = true;
  bool connectedToWater = true;
  uint32_t buildingId = 0;
};

struct SerializedRoad {
  glm::ivec2 from{0, 0};
  glm::ivec2 to{0, 0};
  float currentLoad = 0.0f;
};

struct CitySnapshot {
  int version = 1;
  int width = 0;
  int height = 0;
  std::vector<SerializedTile> tiles;
  std::vector<Building> buildings;
  std::vector<PopulationGroup> populationGroups;
  std::vector<SerializedRoad> roads;
};

class SaveLoadSystem {
public:
  static CitySnapshot captureSnapshot(
    const CityMap& map,
    const RoadNetwork& roads,
    const EntityStore& store,
    const PopulationStore& population
  );

  static bool applySnapshot(
    const CitySnapshot& snapshot,
    CityMap& map,
    RoadNetwork& roads,
    EntityStore& store,
    PopulationStore& population
  );

  static bool saveToFile(
    const std::string& filePath,
    const CityMap& map,
    const RoadNetwork& roads,
    const EntityStore& store,
    const PopulationStore& population
  );

  static bool loadSnapshotFromFile(const std::string& filePath, CitySnapshot& snapshot);

  static bool loadFromFile(
    const std::string& filePath,
    CityMap& map,
    RoadNetwork& roads,
    EntityStore& store,
    PopulationStore& population
  );
};
