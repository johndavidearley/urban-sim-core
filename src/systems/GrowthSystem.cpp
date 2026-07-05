#include "src/systems/GrowthSystem.hpp"

#include <algorithm>
#include <future>

#include "src/core/Random.hpp"

namespace {
struct ZoneBalance {
  int zonedResidential = 0;
  int zonedCommercial = 0;
  int zonedIndustrial = 0;
  int zonedOffice = 0;
  int builtResidential = 0;
  int builtCommercial = 0;
  int builtIndustrial = 0;
  int builtOffice = 0;
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
    case ZoneType::Office:
      return demand.office;
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
    case ZoneType::Office:
      return BuildingType::Office;
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
    case ZoneType::Office:
      return 18;
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
  } else if (zone == ZoneType::Office) {
    ++balance.zonedOffice;
  }
}

void incrementBuiltCount(ZoneBalance& balance, ZoneType zone) {
  if (zone == ZoneType::Residential) {
    ++balance.builtResidential;
  } else if (zone == ZoneType::Commercial) {
    ++balance.builtCommercial;
  } else if (zone == ZoneType::Industrial) {
    ++balance.builtIndustrial;
  } else if (zone == ZoneType::Office) {
    ++balance.builtOffice;
  }
}

void decrementBuiltCount(ZoneBalance& balance, ZoneType zone) {
  if (zone == ZoneType::Residential && balance.builtResidential > 0) {
    --balance.builtResidential;
  } else if (zone == ZoneType::Commercial && balance.builtCommercial > 0) {
    --balance.builtCommercial;
  } else if (zone == ZoneType::Industrial && balance.builtIndustrial > 0) {
    --balance.builtIndustrial;
  } else if (zone == ZoneType::Office && balance.builtOffice > 0) {
    --balance.builtOffice;
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
  if (zone == ZoneType::Office) {
    return balance.zonedOffice;
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
  if (zone == ZoneType::Office) {
    return balance.builtOffice;
  }
  return 0;
}

float demandShareForZone(const ZoneDemand& demand, ZoneType zone) {
  const float totalDemand = std::max(0.0f, demand.residential) +
                            std::max(0.0f, demand.commercial) +
                            std::max(0.0f, demand.industrial) +
                            std::max(0.0f, demand.office);
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
  if (zone == ZoneType::Office) {
    return std::max(0.0f, demand.office) / totalDemand;
  }
  return 0.0f;
}

float demandBalancingFactor(ZoneType zone, const ZoneBalance& balance, const ZoneDemand& demand) {
  const float demandShare = demandShareForZone(demand, zone);
  if (demandShare <= 0.0001f) {
    return 0.75f;
  }

  const int totalBuilt = std::max(0, balance.builtResidential + balance.builtCommercial +
                                      balance.builtIndustrial + balance.builtOffice);
  const int zoneBuilt = std::max(0, builtCountForZone(balance, zone));
  const float builtShare = totalBuilt > 0
    ? (static_cast<float>(zoneBuilt) / static_cast<float>(totalBuilt))
    : demandShare;

  const int totalZoned = std::max(0, balance.zonedResidential + balance.zonedCommercial +
                                      balance.zonedIndustrial + balance.zonedOffice);
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
  } else if (zone == ZoneType::Office) {
    ++stats.demolishedOffice;
  }
}

void incrementRedevelopedCount(GrowthStats& stats, ZoneType zone) {
  if (zone == ZoneType::Residential) {
    ++stats.redevelopedResidential;
  } else if (zone == ZoneType::Commercial) {
    ++stats.redevelopedCommercial;
  } else if (zone == ZoneType::Industrial) {
    ++stats.redevelopedIndustrial;
  } else if (zone == ZoneType::Office) {
    ++stats.redevelopedOffice;
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
  } else if (zone == ZoneType::Office) {
    zoned = balance.zonedOffice;
    built = balance.builtOffice;
    demandValue = demand.office;
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
  const std::vector<GrowthChanceModifier>* chanceModifiers,
  Coord activeMin,
  Coord activeMax,
  ThreadPool* pool,
  bool requireUtilities
) {
  GrowthStats stats;
  DeterministicRandom rng(seed);

  const glm::ivec2 dims = map.getDimensions();

  // Resolve scan bounds: default sentinel {-1,-1} means full map.
  const int x0 = (activeMin.x < 0) ? 0           : std::max(0, activeMin.x);
  const int y0 = (activeMin.y < 0) ? 0           : std::max(0, activeMin.y);
  const int x1 = (activeMax.x < 0) ? dims.x - 1 : std::min(dims.x - 1, activeMax.x);
  const int y1 = (activeMax.y < 0) ? dims.y - 1 : std::min(dims.y - 1, activeMax.y);

  // Count zoned/built tiles only within the active region.
  // Pass 1 is read-only: fan it out across pool workers using row strips.
  ZoneBalance balance;
  {
    const int nRows = y1 - y0 + 1;
    constexpr int kMinRowsPerChunk = 16;
    const int nChunks = (pool != nullptr && nRows >= kMinRowsPerChunk * 2)
      ? std::max(1, std::min(static_cast<int>(pool->threadCount()), nRows / kMinRowsPerChunk))
      : 1;

    if (nChunks <= 1) {
      for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          const Tile& tile = map.getTile({x, y});
          const ZoneType zone = static_cast<ZoneType>(tile.zone);
          incrementZonedCount(balance, zone);
          if (tile.buildingId != EntityIdUtils::NullEntity)
            incrementBuiltCount(balance, zone);
        }
      }
    } else {
      std::vector<std::future<ZoneBalance>> futures;
      futures.reserve(nChunks);
      const int rowsPerChunk = (nRows + nChunks - 1) / nChunks;
      for (int c = 0; c < nChunks; ++c) {
        const int rowBegin = y0 + c * rowsPerChunk;
        const int rowEnd   = std::min(y1, rowBegin + rowsPerChunk - 1);
        if (rowBegin > y1) break;
        futures.push_back(pool->submit([&map, x0, x1, rowBegin, rowEnd]() {
          const CityMap& cmap = map;
          ZoneBalance p;
          for (int y = rowBegin; y <= rowEnd; ++y) {
            for (int x = x0; x <= x1; ++x) {
              const Tile& tile = cmap.getTile({x, y});
              const ZoneType zone = static_cast<ZoneType>(tile.zone);
              incrementZonedCount(p, zone);
              if (tile.buildingId != EntityIdUtils::NullEntity)
                incrementBuiltCount(p, zone);
            }
          }
          return p;
        }));
      }
      for (auto& f : futures) {
        const ZoneBalance p = f.get();
        balance.zonedResidential += p.zonedResidential;
        balance.zonedCommercial  += p.zonedCommercial;
        balance.zonedIndustrial  += p.zonedIndustrial;
        balance.zonedOffice      += p.zonedOffice;
        balance.builtResidential += p.builtResidential;
        balance.builtCommercial  += p.builtCommercial;
        balance.builtIndustrial  += p.builtIndustrial;
        balance.builtOffice      += p.builtOffice;
      }
    }
  }

  const float clampedBaseChance = std::clamp(baseChance, 0.0f, 1.0f);

  // Combined mutation pass: demolition → redevelopment → build in a single tile
  // scan. Reduces cache pressure by 3× versus three separate passes; each tile
  // is read once and the appropriate action is taken based on occupancy.
  // Note: RNG draws are interleaved per-tile rather than per-phase, which
  // changes city evolution vs. the three-pass original (still deterministic).
  const float rebuildBaseChance = 0.05f;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      Tile& tile = map.getTile({x, y});
      const ZoneType zone = static_cast<ZoneType>(tile.zone);

      if (tile.buildingId != EntityIdUtils::NullEntity) {
        // Occupied tile: check demolition, then redevelopment if survived.
        if (zone == ZoneType::None || zone == ZoneType::Park) continue;
        if (store.getBuilding(tile.buildingId) == nullptr) continue;

        bool demolished = false;
        const float demolChance = demolitionPressure(zone, balance, demand);
        if (demolChance > 0.0f && rng.chance(demolChance)) {
          if (store.removeBuilding(tile.buildingId)) {
            tile.buildingId = EntityIdUtils::NullEntity;
            decrementBuiltCount(balance, zone);
            incrementDemolishedCount(stats, zone);
            demolished = true;
          }
        }

        if (!demolished) {
          // Redevelopment: when demand is sustained and the zone is saturated,
          // replace at 2× capacity. Capped at 64× default (6 doublings).
          const Building* existing = store.getBuilding(tile.buildingId);
          if (existing != nullptr) {
            const float demandWeight = demandForZone(zone, demand);
            if (demandWeight >= 0.10f) {
              const int maxCap = defaultCapacity(zone) * 64;
              if (existing->capacity < maxCap) {
                const int   zoned      = zonedCountForZone(balance, zone);
                const int   built      = builtCountForZone(balance, zone);
                const float saturation = zoned > 0
                  ? static_cast<float>(built) / static_cast<float>(zoned) : 1.0f;
                if (rng.chance(rebuildBaseChance * demandWeight * saturation)) {
                  const int newCapacity = std::min(maxCap, existing->capacity * 2);
                  if (store.removeBuilding(tile.buildingId)) {
                    tile.buildingId = store.createBuilding(toBuildingType(zone), {x, y}, newCapacity);
                    incrementRedevelopedCount(stats, zone);
                  }
                }
              }
            }
          }
        }
      } else {
        // Empty tile: check for new construction.
        if (tile.type == 2) continue;
        if (zone == ZoneType::None || zone == ZoneType::Park) continue;
        if (!hasRoadAccess(map, {x, y})) continue;
        if (requireUtilities && !(tile.connectedToPower && tile.connectedToWater)) continue;

        ++stats.evaluatedTiles;

        const float demandWeight = demandForZone(zone, demand);
        if (demandWeight < 0.05f) continue;

        const float pressure = coveragePressure(zone, balance, demand);
        if (pressure <= 0.0f) continue;

        const float chance = std::clamp(
          clampedBaseChance * demandWeight * pressure * roadQualityFactor(map, {x, y}) *
            demandBalancingFactor(zone, balance, demand) *
            chanceModifierForCoord({x, y}, chanceModifiers),
          0.0f, 1.0f
        );
        if (!rng.chance(chance)) continue;

        tile.buildingId = store.createBuilding(toBuildingType(zone), {x, y}, defaultCapacity(zone));
        incrementBuiltCount(balance, zone);

        if (zone == ZoneType::Residential)     ++stats.spawnedResidential;
        else if (zone == ZoneType::Commercial) ++stats.spawnedCommercial;
        else if (zone == ZoneType::Industrial) ++stats.spawnedIndustrial;
        else if (zone == ZoneType::Office)     ++stats.spawnedOffice;
      }
    }
  }

  return stats;
}
