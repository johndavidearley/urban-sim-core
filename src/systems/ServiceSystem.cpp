#include "ServiceSystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <future>
#include <queue>
#include <string>
#include <unordered_map>

namespace {
uint64_t facilitySignature(const std::vector<ServiceFacility>& facilities) {
  // FNV-1a over fixed-width integer fields. Quality is intentionally excluded:
  // current coverage calculations do not use it.
  uint64_t hash = 1469598103934665603ULL;
  auto mix = [&hash](uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      hash ^= static_cast<uint8_t>((value >> shift) & 0xffu);
      hash *= 1099511628211ULL;
    }
  };
  mix(static_cast<uint32_t>(facilities.size()));
  for (const ServiceFacility& facility : facilities) {
    mix(static_cast<uint32_t>(facility.type));
    mix(static_cast<uint32_t>(facility.position.x));
    mix(static_cast<uint32_t>(facility.position.y));
    mix(static_cast<uint32_t>(facility.maxTravelDistance));
    mix(static_cast<uint32_t>(facility.powerSource));
    mix(static_cast<uint32_t>(std::hash<float>{}(facility.powerCapacityMW)));
    mix(static_cast<uint32_t>(std::hash<float>{}(facility.emissionsKgPerMWh)));
  }
  return hash;
}

int typeIndex(ServiceType type) {
  return static_cast<int>(type);
}

std::string upper(const std::string& raw) {
  std::string normalized = raw;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return normalized;
}


// Multi-source BFS: every seed is a facility of the same type and same
// maxTravelDistance. Covered tiles store min hops to the nearest seed.
// Autonomous placement uses one radius for all facilities of a type, so this
// collapses N single-source BFSes into one O(V+E) pass per (type, radius).
std::unordered_map<Coord, int, Vec2Hash> buildMultiSourceDistanceField(
  const RoadNetwork& roads,
  const std::vector<Coord>& sources,
  int maxDistance
) {
  std::unordered_map<Coord, int, Vec2Hash> distance;
  if (maxDistance < 0 || sources.empty()) {
    return distance;
  }

  distance.reserve(static_cast<size_t>(std::max(16, maxDistance * maxDistance * 2))
                   * std::max<size_t>(1, sources.size() / 2));

  std::queue<Coord> frontier;
  for (const Coord& start : sources) {
    if (distance.find(start) != distance.end()) {
      continue;
    }
    distance[start] = 0;
    frontier.push(start);
  }

  while (!frontier.empty()) {
    const Coord current = frontier.front();
    frontier.pop();

    const int currentDistance = distance[current];
    if (currentDistance >= maxDistance) {
      continue;
    }

    const RoadNetwork::Node* node = roads.getNode(current);
    if (node == nullptr) {
      continue;
    }

    for (const RoadNodeId& neighborId : node->adjacent) {
      const Coord next = neighborId.coord;
      if (distance.find(next) != distance.end()) {
        continue;
      }

      const int nextDistance = currentDistance + 1;
      if (nextDistance > maxDistance) {
        continue;
      }

      distance[next] = nextDistance;
      frontier.push(next);
    }
  }

  return distance;
}
} // namespace

bool ServiceSystem::parseServiceType(const std::string& raw, ServiceType& outType) {
  const std::string normalized = upper(raw);

  if (normalized == "FIRE") {
    outType = ServiceType::Fire;
    return true;
  }
  if (normalized == "POLICE") {
    outType = ServiceType::Police;
    return true;
  }
  if (normalized == "HEALTH" || normalized == "HOSPITAL") {
    outType = ServiceType::Health;
    return true;
  }
  if (normalized == "EDUCATION" || normalized == "SCHOOL") {
    outType = ServiceType::Education;
    return true;
  }
  if (normalized == "POWER" || normalized == "POWERPLANT") {
    outType = ServiceType::Power;
    return true;
  }
  if (normalized == "WATER" || normalized == "WATERTOWER") {
    outType = ServiceType::Water;
    return true;
  }
  if (normalized == "SANITATION" || normalized == "SEWAGE" || normalized == "WASTEWATER") {
    outType = ServiceType::Sanitation;
    return true;
  }
  if (normalized == "GARBAGE" || normalized == "LANDFILL" || normalized == "WASTE") {
    outType = ServiceType::Garbage;
    return true;
  }
  if (normalized == "RECYCLING" || normalized == "RECYCLE") {
    outType = ServiceType::Recycling;
    return true;
  }
  if (normalized == "CEMETERY" || normalized == "BURIAL") {
    outType = ServiceType::Cemetery;
    return true;
  }
  if (normalized == "CREMATORIUM" || normalized == "CREMATION") {
    outType = ServiceType::Crematorium;
    return true;
  }

  return false;
}

const char* ServiceSystem::serviceTypeToString(ServiceType type) {
  switch (type) {
    case ServiceType::Fire:
      return "Fire";
    case ServiceType::Police:
      return "Police";
    case ServiceType::Health:
      return "Health";
    case ServiceType::Education:
      return "Education";
    case ServiceType::Power:
      return "Power";
    case ServiceType::Water:
      return "Water";
    case ServiceType::Sanitation:
      return "Sanitation";
    case ServiceType::Garbage:
      return "Garbage";
    case ServiceType::Recycling:
      return "Recycling";
    case ServiceType::Cemetery:
      return "Cemetery";
    case ServiceType::Crematorium:
      return "Crematorium";
    default:
      return "Unknown";
  }
}

bool ServiceSystem::parsePowerSourceType(const std::string& raw, PowerSourceType& outType) {
  const std::string normalized = upper(raw);
  if (normalized == "GENERIC") outType = PowerSourceType::Generic;
  else if (normalized == "COAL") outType = PowerSourceType::Coal;
  else if (normalized == "GAS" || normalized == "NATURAL_GAS" || normalized == "NATURAL-GAS") outType = PowerSourceType::NaturalGas;
  else if (normalized == "NUCLEAR") outType = PowerSourceType::Nuclear;
  else if (normalized == "SOLAR") outType = PowerSourceType::Solar;
  else if (normalized == "WIND") outType = PowerSourceType::Wind;
  else if (normalized == "HYDRO" || normalized == "HYDROELECTRIC") outType = PowerSourceType::Hydro;
  else return false;
  return true;
}

const char* ServiceSystem::powerSourceTypeToString(PowerSourceType type) {
  switch (type) {
    case PowerSourceType::Generic: return "Generic";
    case PowerSourceType::Coal: return "Coal";
    case PowerSourceType::NaturalGas: return "Natural Gas";
    case PowerSourceType::Nuclear: return "Nuclear";
    case PowerSourceType::Solar: return "Solar";
    case PowerSourceType::Wind: return "Wind";
    case PowerSourceType::Hydro: return "Hydro";
    default: return "Unknown";
  }
}

float ServiceSystem::defaultPowerEmissions(PowerSourceType type) {
  switch (type) {
    case PowerSourceType::Coal: return 1000.0f;
    case PowerSourceType::NaturalGas: return 450.0f;
    case PowerSourceType::Generic: return 400.0f;
    case PowerSourceType::Nuclear: return 12.0f;
    case PowerSourceType::Solar: return 45.0f;
    case PowerSourceType::Wind: return 11.0f;
    case PowerSourceType::Hydro: return 24.0f;
    default: return 0.0f;
  }
}

void ServiceSystem::buildCache(
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities,
  ServiceCoverageCache& cache
) {
  cache.entries.clear();
  cache.powerGenerationCapacityMW = 0.0f;
  cache.powerWeightedEmissions = 0.0f;
  cache.entries.reserve(facilities.size());

  // Group road-anchored facilities by (type, maxTravelDistance). Equal-radius
  // groups (the common autonomous-placement case) share one multi-source BFS.
  struct GroupKey {
    ServiceType type = ServiceType::Fire;
    int maxTravelDistance = 0;
    bool operator==(const GroupKey& o) const {
      return type == o.type && maxTravelDistance == o.maxTravelDistance;
    }
  };
  struct GroupKeyHash {
    size_t operator()(const GroupKey& k) const {
      return hashCombine(static_cast<size_t>(k.type), static_cast<size_t>(k.maxTravelDistance));
    }
  };

  std::unordered_map<GroupKey, std::vector<Coord>, GroupKeyHash> groups;
  groups.reserve(facilities.size());

  for (const ServiceFacility& facility : facilities) {
    // Generation exists independently of road-based distribution coverage.
    // A disconnected plant still contributes to the city's installed source
    // mix, while buildings only receive Power coverage through the graph.
    if (facility.type == ServiceType::Power) {
      const float capacity = std::max(0.0f, facility.powerCapacityMW);
      cache.powerGenerationCapacityMW += capacity;
      cache.powerWeightedEmissions += capacity * std::max(0.0f, facility.emissionsKgPerMWh);
    }
    Coord anchor;
    if (!roads.resolveRoadAnchor(facility.position, anchor)) {
      continue;
    }
    groups[{facility.type, facility.maxTravelDistance}].push_back(anchor);
  }

  for (auto& [key, sources] : groups) {
    auto distField = buildMultiSourceDistanceField(roads, sources, key.maxTravelDistance);
    if (!distField.empty()) {
      cache.entries.push_back({key.type, std::move(distField)});
    }
  }

  cache.builtForFacilityCount = facilities.size();
  cache.builtForFacilitySignature = facilitySignature(facilities);
  cache.builtForTopologyVersion = roads.getTopologyVersion();
  cache.cachedStoreMutationVersion = static_cast<uint64_t>(-1);

  // Merge all entries' fields into one nearest-any-facility lookup, done
  // once per rebuild instead of once per query. Also merge Power/Water
  // entries into their own type-restricted lookups (see the header comment
  // on nearestPowerDistance/nearestWaterDistance).
  auto mergeInto = [](std::unordered_map<Coord, int, Vec2Hash>& target,
                      const std::unordered_map<Coord, int, Vec2Hash>& source) {
    for (const auto& [coord, dist] : source) {
      auto it = target.find(coord);
      if (it == target.end()) {
        target.emplace(coord, dist);
      } else if (dist < it->second) {
        it->second = dist;
      }
    }
  };

  cache.nearestAnyDistance.clear();
  cache.nearestPowerDistance.clear();
  cache.nearestWaterDistance.clear();
  cache.coverageMask.clear();
  for (const ServiceCoverageCache::Entry& entry : cache.entries) {
    mergeInto(cache.nearestAnyDistance, entry.distanceField);
    if (entry.type == ServiceType::Power) {
      mergeInto(cache.nearestPowerDistance, entry.distanceField);
    } else if (entry.type == ServiceType::Water) {
      mergeInto(cache.nearestWaterDistance, entry.distanceField);
    }
    const ServiceCoverageMask bit =
      static_cast<ServiceCoverageMask>(1u << typeIndex(entry.type));
    for (const auto& [coord, dist] : entry.distanceField) {
      (void)dist;
      cache.coverageMask[coord] |= bit;
    }
  }
}

bool ServiceSystem::isCacheValid(
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities,
  const ServiceCoverageCache& cache
) {
  return cache.builtForFacilityCount == facilities.size()
      && cache.builtForFacilitySignature == facilitySignature(facilities)
      && cache.builtForTopologyVersion == roads.getTopologyVersion();
}

bool ServiceSystem::isResultCacheValid(
  const EntityStore& store,
  const ServiceCoverageCache& cache
) {
  // O(1): EntityStore advances mutationVersion on every structural edit
  // (create/remove/clear/upsert). A full building-position fingerprint was
  // previously rehashed on every validity check and dominated large cities
  // even on pure cache hits.
  return cache.cachedStoreMutationVersion == store.getMutationVersion();
}

void ServiceSystem::storeCachedResult(
  const EntityStore& store,
  const ServiceCoverageSummary& result,
  ServiceCoverageCache& cache
) {
  cache.cachedStoreMutationVersion = store.getMutationVersion();
  cache.cachedResult = result;
}

ServiceCoverageSummary ServiceSystem::evaluateCoverage(
  const EntityStore& store,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities
) {
  ServiceCoverageCache cache;
  buildCache(roads, facilities, cache);
  return evaluateFromCache(store, roads, cache);
}

ServiceCoverageSummary ServiceSystem::evaluateFromCache(
  const EntityStore& store,
  const RoadNetwork& roads,
  const ServiceCoverageCache& cache,
  ThreadPool* pool
) {
  ServiceCoverageSummary summary;
  summary.totalBuildings = static_cast<uint32_t>(store.getBuildingCount());
  summary.powerGenerationMW = cache.powerGenerationCapacityMW;
  summary.powerEmissionsKgPerMWh = summary.powerGenerationMW > 0.0f
    ? cache.powerWeightedEmissions / summary.powerGenerationMW
    : 0.0f;
  if (summary.totalBuildings == 0) {
    summary.satisfaction = 0.5f;
    return summary;
  }

  // Collect building pointers from type indices (no full hash-map walk).
  std::vector<const Building*> buildings;
  buildings.reserve(summary.totalBuildings);
  static constexpr BuildingType kTypes[] = {
    BuildingType::Residential, BuildingType::Commercial,
    BuildingType::Industrial, BuildingType::Office
  };
  for (BuildingType type : kTypes) {
    for (EntityId id : store.idsByBuildingType(type)) {
      if (const Building* b = store.getBuilding(id)) {
        buildings.push_back(b);
      }
    }
  }

  // "Serviced" (and the count feeding it) deliberately covers only the
  // original four types (Fire/Police/Health/Education) - utilities are
  // tracked in the same byType array for symmetry but excluded from `any`,
  // so manually placing a utility facility (e.g. via --place-facility
  // POWER or SANITATION) can never change servicedBuildings/overallCoverage for a caller
  // who isn't otherwise using utilities.
  // Civic types occupy the low 4 bits of ServiceCoverageMask (Fire..Education).
  constexpr ServiceCoverageMask kCivicCoverageMask = 0x0Fu;

  struct Partial {
    uint32_t serviced = 0;
    std::array<uint32_t, kServiceTypeCount> byType{};
    float powerDemandMW = 0.0f;
  };

  // Evaluate a contiguous slice of the buildings vector. One coverageMask
  // lookup per building replaces scanning every facility distance field.
  auto evalSlice = [&](size_t begin, size_t end) {
    Partial p;
    for (size_t i = begin; i < end; ++i) {
      const Building* building = buildings[i];
      const float occupants = static_cast<float>(std::max(0, building->occupancy));
      p.powerDemandMW += occupants *
        (building->type == BuildingType::Residential ? 0.002f : 0.004f);

      Coord anchor;
      if (!roads.resolveRoadAnchor(building->position, anchor)) continue;
      const auto maskIt = cache.coverageMask.find(anchor);
      if (maskIt == cache.coverageMask.end()) continue;
      const ServiceCoverageMask bits = maskIt->second;
      if ((bits & kCivicCoverageMask) != 0) {
        ++p.serviced;
      }
      for (size_t t = 0; t < kServiceTypeCount; ++t) {
        if ((bits & static_cast<ServiceCoverageMask>(1u << t)) != 0) {
          ++p.byType[t];
        }
      }
    }
    return p;
  };

  const size_t n = buildings.size();
  // Work is O(buildings) now (one mask lookup each); fan out once the set is
  // large enough to amortize pool submission.
  const size_t nChunks = (pool != nullptr && n >= 2048)
    ? std::min(static_cast<size_t>(pool->threadCount()), (n + 255) / 256)
    : 1;

  std::array<uint32_t, kServiceTypeCount> coveredByType{};

  if (nChunks <= 1) {
    const Partial p = evalSlice(0, n);
    summary.servicedBuildings = p.serviced;
    coveredByType = p.byType;
    summary.powerDemandMW = p.powerDemandMW;
  } else {
    std::vector<std::future<Partial>> futures;
    futures.reserve(nChunks);
    const size_t chunkSize = (n + nChunks - 1) / nChunks;
    for (size_t c = 0; c < nChunks; ++c) {
      const size_t begin = c * chunkSize;
      const size_t end = std::min(begin + chunkSize, n);
      if (begin >= end) break;
      futures.push_back(pool->submit([=, &evalSlice]() { return evalSlice(begin, end); }));
    }
    for (auto& f : futures) {
      const Partial p = f.get();
      summary.servicedBuildings += p.serviced;
      summary.powerDemandMW += p.powerDemandMW;
      for (size_t t = 0; t < kServiceTypeCount; ++t) coveredByType[t] += p.byType[t];
    }
  }

  const float denom = static_cast<float>(summary.totalBuildings);
  summary.fireCoverage      = coveredByType[typeIndex(ServiceType::Fire)]      / denom;
  summary.policeCoverage    = coveredByType[typeIndex(ServiceType::Police)]    / denom;
  summary.healthCoverage    = coveredByType[typeIndex(ServiceType::Health)]    / denom;
  summary.educationCoverage = coveredByType[typeIndex(ServiceType::Education)] / denom;
  summary.powerCoverage     = coveredByType[typeIndex(ServiceType::Power)]     / denom;
  summary.waterCoverage     = coveredByType[typeIndex(ServiceType::Water)]     / denom;
  summary.sanitationCoverage = coveredByType[typeIndex(ServiceType::Sanitation)] / denom;
  summary.garbageCoverage    = coveredByType[typeIndex(ServiceType::Garbage)] / denom;
  summary.recyclingCoverage  = coveredByType[typeIndex(ServiceType::Recycling)] / denom;
  summary.cemeteryCoverage   = coveredByType[typeIndex(ServiceType::Cemetery)] / denom;
  summary.crematoriumCoverage = coveredByType[typeIndex(ServiceType::Crematorium)] / denom;
  summary.powerSupplyRatio = summary.powerDemandMW > 0.0f
    ? std::min(1.0f, summary.powerGenerationMW / summary.powerDemandMW)
    : 1.0f;
  // Deliberately unchanged: only the original four types feed
  // overallCoverage/satisfaction (see the Partial/hit comment above).
  summary.overallCoverage   = (summary.fireCoverage + summary.policeCoverage +
                                summary.healthCoverage + summary.educationCoverage) / 4.0f;
  summary.satisfaction = std::clamp(0.25f + summary.overallCoverage * 0.75f, 0.0f, 1.0f);
  return summary;
}

void ServiceSystem::applyToMetrics(const ServiceCoverageSummary& summary, CityMetrics& metrics) {
  metrics.serviceCoverage = summary.overallCoverage;
  metrics.serviceSatisfaction = summary.satisfaction;

  const float happinessDelta = (summary.satisfaction - 0.5f) * 0.25f;
  metrics.happiness = std::clamp(metrics.happiness + happinessDelta, 0.0f, 1.0f);
}
