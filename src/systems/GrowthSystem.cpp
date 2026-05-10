#include "src/systems/GrowthSystem.hpp"

#include <algorithm>

#include "src/core/Random.hpp"

namespace {
bool hasRoadAccess(const CityMap& map, Coord pos) {
  if (map.getTile(pos).hasRoad) {
    return true;
  }

  const Coord neighbors[4] = {
    {pos.x + 1, pos.y},
    {pos.x - 1, pos.y},
    {pos.x, pos.y + 1},
    {pos.x, pos.y - 1}
  };

  for (const Coord& n : neighbors) {
    if (map.isValid(n) && map.getTile(n).hasRoad) {
      return true;
    }
  }

  return false;
}

float demandForZone(ZoneType zone, const ZoneDemand& demand) {
  switch (zone) {
    case ZoneType::Residential:
      return demand.residential;
    case ZoneType::Commercial:
      return demand.commercial;
    case ZoneType::Industrial:
      return demand.industrial;
    case ZoneType::None:
    case ZoneType::Park:
    default:
      return 0.0f;
  }
}

BuildingType toBuildingType(ZoneType zone) {
  switch (zone) {
    case ZoneType::Residential:
      return BuildingType::Residential;
    case ZoneType::Commercial:
      return BuildingType::Commercial;
    case ZoneType::Industrial:
      return BuildingType::Industrial;
    case ZoneType::None:
    case ZoneType::Park:
    default:
      return BuildingType::Residential;
  }
}

int defaultCapacity(ZoneType zone) {
  switch (zone) {
    case ZoneType::Residential:
      return 8;
    case ZoneType::Commercial:
      return 20;
    case ZoneType::Industrial:
      return 24;
    case ZoneType::None:
    case ZoneType::Park:
    default:
      return 0;
  }
}
} // namespace

GrowthStats GrowthSystem::runStep(
  CityMap& map,
  const RoadNetwork& roads,
  EntityStore& store,
  const ZoneDemand& demand,
  uint32_t seed,
  float baseChance
) {
  (void)roads;

  GrowthStats stats;
  DeterministicRandom rng(seed);

  const glm::ivec2 dims = map.getDimensions();
  const float clampedBaseChance = std::clamp(baseChance, 0.0f, 1.0f);

  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      Tile& tile = map.getTile({x, y});
      const ZoneType zone = static_cast<ZoneType>(tile.zone);

      if (tile.buildingId != EntityIdUtils::NullEntity) {
        continue;
      }
      if (zone == ZoneType::None || zone == ZoneType::Park) {
        continue;
      }
      if (!hasRoadAccess(map, {x, y})) {
        continue;
      }

      ++stats.evaluatedTiles;

      const float chance = std::clamp(clampedBaseChance * demandForZone(zone, demand), 0.0f, 1.0f);
      if (!rng.chance(chance)) {
        continue;
      }

      const EntityId id = store.createBuilding(toBuildingType(zone), {x, y}, defaultCapacity(zone));
      tile.buildingId = id;

      if (zone == ZoneType::Residential) {
        ++stats.spawnedResidential;
      } else if (zone == ZoneType::Commercial) {
        ++stats.spawnedCommercial;
      } else if (zone == ZoneType::Industrial) {
        ++stats.spawnedIndustrial;
      }
    }
  }

  return stats;
}
