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

void decrementBuiltCount(ZoneBalance& balance, ZoneType zone) {
  if (zone == ZoneType::Residential && balance.builtResidential > 0) {
    --balance.builtResidential;
  } else if (zone == ZoneType::Commercial && balance.builtCommercial > 0) {
    --balance.builtCommercial;
  } else if (zone == ZoneType::Industrial && balance.builtIndustrial > 0) {
    --balance.builtIndustrial;
  }
}

int zonedCountForZone(const ZoneBalance& balance, ZoneType zone) {
  if (zone == ZoneType::Residential) {
    return balance.zonedResidential;
  }
  if (zone == ZoneType::Commercial) {
    return balance.zonedCommercial;
  }
  if (zone == ZoneType::Industrial) {
    return balance.zonedIndustrial;
  }
  return 0;
}

int builtCountForZone(const ZoneBalance& balance, ZoneType zone) {
  if (zone == ZoneType::Residential) {
    return balance.builtResidential;
  }
  if (zone == ZoneType::Commercial) {
    return balance.builtCommercial;
  }
  if (zone == ZoneType::Industrial) {
    return balance.builtIndustrial;
  }
  return 0;
}

float demandShareForZone(const ZoneDemand& demand, ZoneType zone) {
  const float totalDemand = std::max(0.0f, demand.residential) +
                            std::max(0.0f, demand.commercial) +
                            std::max(0.0f, demand.industrial);
  if (totalDemand <= 0.0001f) {
    return 0.0f;
  }

  if (zone == ZoneType::Residential) {
    return std::max(0.0f, demand.residential) / totalDemand;
  }
  if (zone == ZoneType::Commercial) {
    return std::max(0.0f, demand.commercial) / totalDemand;
  }
  if (zone == ZoneType::Industrial) {
    return std::max(0.0f, demand.industrial) / totalDemand;
  }
  return 0.0f;
}

float demandBalancingFactor(ZoneType zone, const ZoneBalance& balance, const ZoneDemand& demand) {
  const float demandShare = demandShareForZone(demand, zone);
  if (demandShare <= 0.0001f) {
    return 0.75f;
  }

  const int totalBuilt = std::max(0, balance.builtResidential + balance.builtCommercial + balance.builtIndustrial);
  const int zoneBuilt = std::max(0, builtCountForZone(balance, zone));
  const float builtShare = totalBuilt > 0
    ? (static_cast<float>(zoneBuilt) / static_cast<float>(totalBuilt))
    : demandShare;

  const int totalZoned = std::max(0, balance.zonedResidential + balance.zonedCommercial + balance.zonedIndustrial);
  const int zoneZoned = std::max(0, zonedCountForZone(balance, zone));
  const float zonedShare = totalZoned > 0
    ? (static_cast<float>(zoneZoned) / static_cast<float>(totalZoned))
    : demandShare;

  // Multi-zone balancing baseline:
  // - boost zones underrepresented in built share relative to demand share
  // - apply mild damping when zoning supply is already much below demand share
  const float builtGap = demandShare - builtShare;
  const float supplyGap = demandShare - zonedShare;
  const float factor = 1.0f + (0.60f * builtGap) + (0.20f * supplyGap);
  return std::clamp(factor, 0.70f, 1.30f);
}

float demolitionPressure(ZoneType zone, const ZoneBalance& balance, const ZoneDemand& demand) {
  const int zoned = zonedCountForZone(balance, zone);
  const int built = builtCountForZone(balance, zone);
  if (zoned <= 0 || built <= 0) {
    return 0.0f;
  }

  const float demandWeight = demandForZone(zone, demand);
  if (demandWeight >= 0.20f) {
    return 0.0f;
  }

  const float builtRatio = static_cast<float>(built) / static_cast<float>(zoned);
  const float targetCoverage = std::clamp(0.10f + (demandWeight * 0.85f), 0.10f, 0.95f);
  if (builtRatio <= targetCoverage + 0.10f) {
    return 0.0f;
  }

  const float overbuild = builtRatio - targetCoverage;
  const float lowDemand = 0.20f - demandWeight;
  const float chance = (0.35f * overbuild) + (1.20f * lowDemand);
  return std::clamp(chance, 0.0f, 0.50f);
}

void incrementDemolishedCount(GrowthStats& stats, ZoneType zone) {
  if (zone == ZoneType::Residential) {
    ++stats.demolishedResidential;
  } else if (zone == ZoneType::Commercial) {
    ++stats.demolishedCommercial;
  } else if (zone == ZoneType::Industrial) {
    ++stats.demolishedIndustrial;
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

float chanceModifierForCoord(Coord coord, const std::vector<GrowthChanceModifier>* modifiers) {
  if (modifiers == nullptr || modifiers->empty()) {
    return 1.0f;
  }

  float combined = 1.0f;
  for (const GrowthChanceModifier& modifier : *modifiers) {
    if (!modifier.contains(coord)) {
      continue;
    }
    combined *= std::max(0.0f, modifier.multiplier);
  }

  return std::clamp(combined, 0.0f, 2.0f);
}
} // namespace

GrowthStats GrowthSystem::runStep(
  CityMap& map,
  EntityStore& store,
  const ZoneDemand& demand,
  uint32_t seed,
  float baseChance,
  const std::vector<GrowthChanceModifier>* chanceModifiers
) {
  GrowthStats stats;
  DeterministicRandom rng(seed);
  ZoneBalance balance = collectZoneBalance(map);

  const glm::ivec2 dims = map.getDimensions();
  const float clampedBaseChance = std::clamp(baseChance, 0.0f, 1.0f);

  // Early aging/demolition scaffold: reclaim parcels in low-demand, overbuilt zones.
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      Tile& tile = map.getTile({x, y});
      const ZoneType zone = static_cast<ZoneType>(tile.zone);

      if (tile.buildingId == EntityIdUtils::NullEntity) {
        continue;
      }
      if (zone == ZoneType::None || zone == ZoneType::Park) {
        continue;
      }

      // Preserve snapshot placeholders that are not backed by a managed entity record.
      if (store.getBuilding(tile.buildingId) == nullptr) {
        continue;
      }

      const float chance = demolitionPressure(zone, balance, demand);
      if (chance <= 0.0f) {
        continue;
      }

      if (!rng.chance(chance)) {
        continue;
      }

      if (!store.removeBuilding(tile.buildingId)) {
        continue;
      }

      tile.buildingId = EntityIdUtils::NullEntity;
      decrementBuiltCount(balance, zone);
      incrementDemolishedCount(stats, zone);
    }
  }

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
        clampedBaseChance * demandWeight * pressure * roadQualityFactor(map, {x, y}) *
          demandBalancingFactor(zone, balance, demand) *
          chanceModifierForCoord({x, y}, chanceModifiers),
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
