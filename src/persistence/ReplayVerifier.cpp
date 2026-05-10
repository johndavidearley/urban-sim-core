#include "ReplayVerifier.hpp"

#include <algorithm>
#include <array>
#include <tuple>

#include "src/persistence/SaveLoadSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/world/Zoning.hpp"

namespace {
void hashBytes(uint64_t& hash, const unsigned char* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<uint64_t>(data[i]);
    hash *= 1099511628211ULL;
  }
}

template <typename T>
void hashValue(uint64_t& hash, const T& value) {
  hashBytes(hash, reinterpret_cast<const unsigned char*>(&value), sizeof(T));
}

std::array<int, 4> canonicalRoadEndpoints(const SerializedRoad& road) {
  const bool keepOrder = (road.from.x < road.to.x) ||
                         (road.from.x == road.to.x && road.from.y <= road.to.y);
  if (keepOrder) {
    return {road.from.x, road.from.y, road.to.x, road.to.y};
  }
  return {road.to.x, road.to.y, road.from.x, road.from.y};
}

uint64_t runScenarioAndChecksum(const ReplayConfig& config) {
  CityMap map({config.mapSize, config.mapSize});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const int minBound = 2;
  const int maxBound = std::max(3, config.mapSize - 3);
  const int mid = config.mapSize / 2;

  Zoning::applyZoneRect(
    map,
    {minBound, minBound},
    {std::max(minBound, mid - 1), maxBound},
    ZoneType::Residential
  );
  Zoning::applyZoneRect(
    map,
    {std::min(maxBound, mid + 1), minBound},
    {maxBound, maxBound},
    ZoneType::Commercial
  );

  for (int x = minBound; x < maxBound; ++x) {
    roads.buildRoad({x, mid}, {x + 1, mid});
  }
  for (int y = minBound; y < maxBound; ++y) {
    roads.buildRoad({mid, y}, {mid, y + 1});
  }
  roads.updateConnectivity({mid, mid});

  for (int step = 0; step < std::max(0, config.growthSteps); ++step) {
    const ZoneDemand demand = Zoning::calculateDemand(config.seed + static_cast<uint32_t>(step));
    GrowthSystem::runStep(
      map,
      roads,
      store,
      demand,
      config.seed + static_cast<uint32_t>(step),
      0.5f
    );
  }

  if (config.seedPopulation > 0) {
    PopulationSystem::allocate(
      store,
      population,
      static_cast<uint32_t>(config.seedPopulation),
      config.seed
    );

    if (config.runCommutes) {
      TrafficSystem::simulateCommutes(store, population, roads, config.seed);
    }
  }

  if (config.runEconomy) {
    (void)EconomySystem::calculateEconomy(store, population);
  }

  return ReplayVerifier::calculateChecksum(map, roads, store, population);
}
} // namespace

uint64_t ReplayVerifier::calculateChecksum(
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population
) {
  const CitySnapshot snapshot = SaveLoadSystem::captureSnapshot(map, roads, store, population);

  uint64_t hash = 1469598103934665603ULL;
  hashValue(hash, snapshot.version);
  hashValue(hash, snapshot.width);
  hashValue(hash, snapshot.height);

  for (const SerializedTile& tile : snapshot.tiles) {
    hashValue(hash, tile.x);
    hashValue(hash, tile.y);
    hashValue(hash, tile.type);
    hashValue(hash, tile.zone);
    hashValue(hash, tile.landValue);
    hashValue(hash, tile.pollution);
    hashValue(hash, tile.hasRoad);
    hashValue(hash, tile.connectedToRoad);
    hashValue(hash, tile.connectedToPower);
    hashValue(hash, tile.connectedToWater);
  }

  std::vector<Building> buildings = snapshot.buildings;
  std::sort(buildings.begin(), buildings.end(), [](const Building& a, const Building& b) {
    return std::tie(a.type, a.position.x, a.position.y, a.capacity, a.occupancy) <
           std::tie(b.type, b.position.x, b.position.y, b.capacity, b.occupancy);
  });
  for (const Building& building : buildings) {
    hashValue(hash, building.type);
    hashValue(hash, building.position.x);
    hashValue(hash, building.position.y);
    hashValue(hash, building.capacity);
    hashValue(hash, building.occupancy);
  }

  std::vector<PopulationGroup> groups = snapshot.populationGroups;
  std::sort(groups.begin(), groups.end(), [](const PopulationGroup& a, const PopulationGroup& b) {
    return std::tie(a.band, a.size, a.employed) < std::tie(b.band, b.size, b.employed);
  });
  for (const PopulationGroup& group : groups) {
    hashValue(hash, group.band);
    hashValue(hash, group.size);
    hashValue(hash, group.employed);
  }

  std::vector<SerializedRoad> roadsCopy = snapshot.roads;
  std::sort(roadsCopy.begin(), roadsCopy.end(), [](const SerializedRoad& a, const SerializedRoad& b) {
    return canonicalRoadEndpoints(a) < canonicalRoadEndpoints(b);
  });
  for (const SerializedRoad& road : roadsCopy) {
    const auto endpoints = canonicalRoadEndpoints(road);
    for (int value : endpoints) {
      hashValue(hash, value);
    }
    hashValue(hash, road.currentLoad);
  }

  return hash;
}

ReplayResult ReplayVerifier::verifyDeterministicRun(const ReplayConfig& config) {
  ReplayResult result;
  result.firstChecksum = runScenarioAndChecksum(config);
  result.secondChecksum = runScenarioAndChecksum(config);
  result.deterministic = (result.firstChecksum == result.secondChecksum);
  return result;
}
