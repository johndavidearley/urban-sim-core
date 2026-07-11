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
  }
  return hash;
}

uint64_t buildingCoverageSignature(const EntityStore& store) {
  // Combine per-building hashes commutatively because EntityStore iteration
  // order is intentionally unspecified. Capacity, occupancy, and type do not
  // affect whether a coordinate is covered.
  uint64_t signature = static_cast<uint64_t>(store.getBuildingCount());
  for (const auto& [id, building] : store.getBuildings()) {
    uint64_t item = static_cast<uint64_t>(id) * 0x9e3779b97f4a7c15ULL;
    item ^= static_cast<uint64_t>(static_cast<uint32_t>(building.position.x)) << 32;
    item ^= static_cast<uint32_t>(building.position.y);
    item ^= item >> 30;
    item *= 0xbf58476d1ce4e5b9ULL;
    item ^= item >> 27;
    signature ^= item;
  }
  return signature;
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


std::unordered_map<Coord, int, Vec2Hash> buildDistanceField(
  const RoadNetwork& roads,
  Coord start,
  int maxDistance
) {
  std::unordered_map<Coord, int, Vec2Hash> distance;
  if (maxDistance < 0) {
    return distance;
  }

  std::queue<Coord> frontier;
  frontier.push(start);
  distance[start] = 0;

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
    default:
      return "Unknown";
  }
}

void ServiceSystem::buildCache(
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities,
  ServiceCoverageCache& cache
) {
  cache.entries.clear();
  cache.entries.reserve(facilities.size());
  for (const ServiceFacility& facility : facilities) {
    Coord anchor;
    if (!roads.resolveRoadAnchor(facility.position, anchor)) {
      continue;
    }
    auto distField = buildDistanceField(roads, anchor, facility.maxTravelDistance);
    if (!distField.empty()) {
      cache.entries.push_back({facility.type, std::move(distField)});
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
  for (const ServiceCoverageCache::Entry& entry : cache.entries) {
    mergeInto(cache.nearestAnyDistance, entry.distanceField);
    if (entry.type == ServiceType::Power) {
      mergeInto(cache.nearestPowerDistance, entry.distanceField);
    } else if (entry.type == ServiceType::Water) {
      mergeInto(cache.nearestWaterDistance, entry.distanceField);
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
  return cache.cachedStoreMutationVersion == store.getMutationVersion()
      && cache.cachedBuildingCoverageSignature == buildingCoverageSignature(store);
}

void ServiceSystem::storeCachedResult(
  const EntityStore& store,
  const ServiceCoverageSummary& result,
  ServiceCoverageCache& cache
) {
  cache.cachedStoreMutationVersion = store.getMutationVersion();
  cache.cachedBuildingCoverageSignature = buildingCoverageSignature(store);
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
  summary.totalBuildings = static_cast<uint32_t>(store.getBuildings().size());
  if (summary.totalBuildings == 0) {
    summary.satisfaction = 0.5f;
    return summary;
  }

  // Collect building pointers once for indexed access.
  std::vector<const Building*> buildings;
  buildings.reserve(summary.totalBuildings);
  for (const auto& [id, b] : store.getBuildings()) {
    (void)id;
    buildings.push_back(&b);
  }

  // "Serviced" (and the count feeding it) deliberately covers only the
  // original four types (Fire/Police/Health/Education) - Power/Water are
  // tracked in the same byType array for symmetry but excluded from `any`,
  // so manually placing a utility facility (e.g. via --place-facility
  // POWER) can never change servicedBuildings/overallCoverage for a caller
  // who isn't otherwise using utilities.
  struct Partial {
    uint32_t serviced = 0;
    std::array<uint32_t, 6> byType = {0, 0, 0, 0, 0, 0};
  };

  // Evaluate a contiguous slice of the buildings vector.
  auto evalSlice = [&](size_t begin, size_t end) {
    Partial p;
    for (size_t i = begin; i < end; ++i) {
      Coord anchor;
      if (!roads.resolveRoadAnchor(buildings[i]->position, anchor)) continue;
      std::array<bool, 6> hit = {false, false, false, false, false, false};
      bool any = false;
      for (const ServiceCoverageCache::Entry& entry : cache.entries) {
        if (entry.distanceField.count(anchor) == 0) continue;
        const int t = typeIndex(entry.type);
        hit[t] = true;
        if (t < 4) any = true;
      }
      if (any) ++p.serviced;
      for (int t = 0; t < 6; ++t) { if (hit[t]) ++p.byType[t]; }
    }
    return p;
  };

  const size_t n = buildings.size();
  // Only fan out to threads when there is enough work to amortize task
  // submission overhead (~4k lookup pairs ≈ buildings × facility entries).
  const size_t totalPairs = n * cache.entries.size();
  const size_t nChunks = (pool != nullptr && totalPairs >= 4096)
    ? std::min(static_cast<size_t>(pool->threadCount()), (n + 63) / 64)
    : 1;

  std::array<uint32_t, 6> coveredByType = {0, 0, 0, 0, 0, 0};

  if (nChunks <= 1) {
    const Partial p = evalSlice(0, n);
    summary.servicedBuildings = p.serviced;
    coveredByType = p.byType;
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
      for (int t = 0; t < 6; ++t) coveredByType[t] += p.byType[t];
    }
  }

  const float denom = static_cast<float>(summary.totalBuildings);
  summary.fireCoverage      = coveredByType[typeIndex(ServiceType::Fire)]      / denom;
  summary.policeCoverage    = coveredByType[typeIndex(ServiceType::Police)]    / denom;
  summary.healthCoverage    = coveredByType[typeIndex(ServiceType::Health)]    / denom;
  summary.educationCoverage = coveredByType[typeIndex(ServiceType::Education)] / denom;
  summary.powerCoverage     = coveredByType[typeIndex(ServiceType::Power)]     / denom;
  summary.waterCoverage     = coveredByType[typeIndex(ServiceType::Water)]     / denom;
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
