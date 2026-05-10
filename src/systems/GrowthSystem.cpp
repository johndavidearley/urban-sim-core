#include "src/systems/GrowthSystem.hpp"

#include <algorithm>

#include "src/core/Random.hpp"

namespace {
struct ZoneBalance {
  int zonedResidential = 0;
  int zonedCommercial = 0;
  int zonedIndustrial = 0;
  int builtResidential = 0;
  int builtCommercial = 0;
  int builtIndustrial = 0;
};

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

float roadQualityFactor(const CityMap& map, Coord pos) {
  if (map.getTile(pos).hasRoad) {
    return 1.15f;
  }
  return 1.0f;
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

void incrementZonedCount(ZoneBalance& balance, ZoneType zone) {
  if (zone == ZoneType::Residential) {
    ++balance.zonedResidential;
  } else if (zone == ZoneType::Commercial) {
    ++balance.zonedCommercial;
  } else if (zone == ZoneType::Industrial) {
    ++balance.zonedIndustrial;
  }
}

void incrementBuiltCount(ZoneBalance& balance, ZoneType zone) {
  if (zone == ZoneType::Residential) {
    ++balance.builtResidential;
  } else if (zone == ZoneType::Commercial) {
    ++balance.builtCommercial;
  } else if (zone == ZoneType::Industrial) {
    ++balance.builtIndustrial;
  }
}

float coveragePressure(ZoneType zone, const ZoneBalance& balance, const ZoneDemand& demand) {
  int zoned = 0;
  int built = 0;
  float demandValue = 0.0f;

  if (zone == ZoneType::Residential) {
    zoned = balance.zonedResidential;
    built = balance.builtResidential;
    demandValue = demand.residential;
  } else if (zone == ZoneType::Commercial) {
    zoned = balance.zonedCommercial;
    built = balance.builtCommercial;
    demandValue = demand.commercial;
  } else if (zone == ZoneType::Industrial) {
    zoned = balance.zonedIndustrial;
    built = balance.builtIndustrial;
    demandValue = demand.industrial;
  }

  if (zoned <= 0) {
    return 0.0f;
  }

  const float builtRatio = static_cast<float>(built) / static_cast<float>(zoned);
  const float targetCoverage = std::clamp(0.10f + (demandValue * 0.85f), 0.10f, 0.95f);
  const float deficit = targetCoverage - builtRatio;

  if (deficit <= 0.0f) {
    return 0.0f;
  }

  return std::clamp(deficit / targetCoverage, 0.0f, 1.0f);
}

ZoneBalance collectZoneBalance(const CityMap& map) {
  ZoneBalance balance;
  const glm::ivec2 dims = map.getDimensions();

  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      const ZoneType zone = static_cast<ZoneType>(tile.zone);

      incrementZonedCount(balance, zone);
      if (tile.buildingId != EntityIdUtils::NullEntity) {
        incrementBuiltCount(balance, zone);
      }
    }
  }

  return balance;
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
  ZoneBalance balance = collectZoneBalance(map);

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

      const float demandWeight = demandForZone(zone, demand);
      if (demandWeight < 0.05f) {
        continue;
      }

      const float pressure = coveragePressure(zone, balance, demand);
      if (pressure <= 0.0f) {
        continue;
      }

      const float chance = std::clamp(
        clampedBaseChance * demandWeight * pressure * roadQualityFactor(map, {x, y}),
        0.0f,
        1.0f
      );
      if (!rng.chance(chance)) {
        continue;
      }

      const EntityId id = store.createBuilding(toBuildingType(zone), {x, y}, defaultCapacity(zone));
      tile.buildingId = id;
      incrementBuiltCount(balance, zone);

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
