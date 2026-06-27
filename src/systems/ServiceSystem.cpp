#include "ServiceSystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <future>
#include <queue>
#include <string>
#include <unordered_map>

namespace {
int typeIndex(ServiceType type) {
  return static_cast<int>(type);
}

std::string upper(const std::string& raw) {
  std::string normalized = raw;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return normalized;
}

bool hasRoadAdjacency(const RoadNetwork& roads, Coord coord) {
  const RoadNetwork::Node* node = roads.getNode(coord);
  return node != nullptr && !node->adjacent.empty();
}

bool resolveRoadAnchor(const RoadNetwork& roads, Coord coord, Coord& outAnchor) {
  if (hasRoadAdjacency(roads, coord)) {
    outAnchor = coord;
    return true;
  }

  const Coord neighbors[4] = {
    {coord.x + 1, coord.y},
    {coord.x - 1, coord.y},
    {coord.x, coord.y + 1},
    {coord.x, coord.y - 1}
  };

  for (const Coord& n : neighbors) {
    if (hasRoadAdjacency(roads, n)) {
      outAnchor = n;
      return true;
    }
  }

  return false;
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
    if (!resolveRoadAnchor(roads, facility.position, anchor)) {
      continue;
    }
    auto distField = buildDistanceField(roads, anchor, facility.maxTravelDistance);
    if (!distField.empty()) {
      cache.entries.push_back({facility.type, std::move(distField)});
    }
  }
  cache.builtForFacilityCount = facilities.size();
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

  struct Partial {
    uint32_t serviced = 0;
    std::array<uint32_t, 4> byType = {0, 0, 0, 0};
  };

  // Evaluate a contiguous slice of the buildings vector.
  auto evalSlice = [&](size_t begin, size_t end) {
    Partial p;
    for (size_t i = begin; i < end; ++i) {
      Coord anchor;
      if (!resolveRoadAnchor(roads, buildings[i]->position, anchor)) continue;
      std::array<bool, 4> hit = {false, false, false, false};
      bool any = false;
      for (const ServiceCoverageCache::Entry& entry : cache.entries) {
        if (entry.distanceField.count(anchor) == 0) continue;
        hit[typeIndex(entry.type)] = true;
        any = true;
      }
      if (any) ++p.serviced;
      for (int t = 0; t < 4; ++t) { if (hit[t]) ++p.byType[t]; }
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

  std::array<uint32_t, 4> coveredByType = {0, 0, 0, 0};

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
      for (int t = 0; t < 4; ++t) coveredByType[t] += p.byType[t];
    }
  }

  const float denom = static_cast<float>(summary.totalBuildings);
  summary.fireCoverage      = coveredByType[typeIndex(ServiceType::Fire)]      / denom;
  summary.policeCoverage    = coveredByType[typeIndex(ServiceType::Police)]    / denom;
  summary.healthCoverage    = coveredByType[typeIndex(ServiceType::Health)]    / denom;
  summary.educationCoverage = coveredByType[typeIndex(ServiceType::Education)] / denom;
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
