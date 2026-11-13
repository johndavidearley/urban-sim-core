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

  if(snapshot.environment.initialized) {
    const auto& e=snapshot.environment;
    hashValue(hash,e.seed);hashValue(hash,e.tick);hashValue(hash,e.windX);hashValue(hash,e.windZ);hashValue(hash,e.windPhase);
    for(double v:{e.inflowScale,e.sewageBacklog,e.inflow,e.outflow,e.withdrawal,e.discharge,e.contaminantIn,
                  e.contaminantOut,e.contaminantDecay,e.sewageGenerated,e.sewageHandled}) hashValue(hash,v);
  }
  if(snapshot.pipeNetworksEnabled) hashValue(hash,snapshot.pipeNetworksEnabled);
  for (const SerializedTile& tile : snapshot.tiles) {
    hashValue(hash, tile.x);
    hashValue(hash, tile.y);
    hashValue(hash, tile.type);
    if(snapshot.environment.initialized) {
      const auto& c=tile.environment;
      hashValue(hash,c.terrainMeters);hashValue(hash,c.bedMeters);hashValue(hash,c.water);hashValue(hash,c.contaminant);
      hashValue(hash,c.currentX);hashValue(hash,c.currentZ);hashValue(hash,c.windX);hashValue(hash,c.windZ);
    }
    if(snapshot.pipeNetworksEnabled) {hashValue(hash,tile.utility.waterPipe);hashValue(hash,tile.utility.sewerPipe);if(tile.utility.sewageLift) hashValue(hash,tile.utility.sewageLift);if(tile.utility.waterBooster) hashValue(hash,uint32_t{0x424f4f53});hashValue(hash,tile.utility.sewageBacklog);hashValue(hash,tile.connectedToSewer);}
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
    hashValue(hash, building.crimeRate);
    hashValue(hash, building.illnessRate);
    if (building.uniqueKind) hashValue(hash, building.uniqueKind);
    if (building.monumentKind) hashValue(hash, building.monumentKind);
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
    hashValue(hash, road.roadClass);
    hashValue(hash, road.freightLoad);
    if(road.elevation!=0) {hashValue(hash,road.elevation);hashValue(hash,road.length);}
    if(road.fromLevel || road.toLevel) {hashValue(hash,road.fromLevel);hashValue(hash,road.toLevel);}
    if(road.oneWay) {
      hashValue(hash,road.oneWay);hashValue(hash,road.from.x);hashValue(hash,road.from.y);
    }
  }
  for(const auto& a:snapshot.roadAlignments) {
    if(a.elevation!=0) hashValue(hash,a.elevation);
    if(a.startLevel || a.endLevel) {hashValue(hash,a.startLevel);hashValue(hash,a.endLevel);}
    for(size_t index:a.deckJunctions) hashValue(hash,index);
    hashValue(hash,a.roadClass);hashValue(hash,a.snapped);hashValue(hash,a.oneWay);hashValue(hash,a.roundabout);
    for(const auto& c:a.curves) for(const auto p:c.points) {hashValue(hash,p.x);hashValue(hash,p.y);}
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
