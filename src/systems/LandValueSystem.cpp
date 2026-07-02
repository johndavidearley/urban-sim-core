#include "src/systems/LandValueSystem.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>

#include "src/world/Zoning.hpp"

namespace {

bool hasRoadAdjacency(const RoadNetwork& roads, Coord coord) {
  const RoadNetwork::Node* node = roads.getNode(coord);
  return node != nullptr && !node->adjacent.empty();
}

// Buildings sit next to roads, not on them; resolve the adjacent road node.
bool resolveRoadAnchor(const RoadNetwork& roads, Coord coord, Coord& outAnchor) {
  if (hasRoadAdjacency(roads, coord)) {
    outAnchor = coord;
    return true;
  }
  const Coord neighbors[4] = {
    {coord.x + 1, coord.y}, {coord.x - 1, coord.y}, {coord.x, coord.y + 1}, {coord.x, coord.y - 1}
  };
  for (const Coord& n : neighbors) {
    if (hasRoadAdjacency(roads, n)) {
      outAnchor = n;
      return true;
    }
  }
  return false;
}

// Multi-source BFS: distance from every road-reachable tile within
// `maxDistance` to the nearest of `sources`, in one O(V+E) pass (all sources
// pushed at distance 0 up front). Capped at maxDistance because proximityBonus
// is zero beyond it anyway - uncapped, this would walk the entire road
// network every call regardless of city size.
std::unordered_map<Coord, int, Vec2Hash> multiSourceDistanceField(
  const RoadNetwork& roads,
  const std::vector<Coord>& sources,
  int maxDistance
) {
  std::unordered_map<Coord, int, Vec2Hash> distance;
  if (maxDistance < 0) {
    return distance;
  }

  std::queue<Coord> frontier;
  for (const Coord& s : sources) {
    if (distance.count(s) != 0) continue;
    distance[s] = 0;
    frontier.push(s);
  }

  while (!frontier.empty()) {
    const Coord current = frontier.front();
    frontier.pop();
    const int currentDistance = distance[current];
    if (currentDistance >= maxDistance) continue;

    const RoadNetwork::Node* node = roads.getNode(current);
    if (node == nullptr) continue;

    for (const RoadNodeId& neighborId : node->adjacent) {
      const Coord next = neighborId.coord;
      if (distance.count(next) != 0) continue;
      distance[next] = currentDistance + 1;
      frontier.push(next);
    }
  }

  return distance;
}

// Nearest-facility distance for `coord` from an already-built service cache:
// the minimum, over all cache entries whose BFS field reaches this tile, of
// that entry's recorded distance. Returns -1 if uncovered by any facility.
int nearestServiceDistance(const ServiceCoverageCache& cache, Coord coord) {
  int best = -1;
  for (const ServiceCoverageCache::Entry& entry : cache.entries) {
    const auto it = entry.distanceField.find(coord);
    if (it == entry.distanceField.end()) continue;
    if (best < 0 || it->second < best) {
      best = it->second;
    }
  }
  return best;
}

// Proximity bonus: full `maxBonus` at distance 0, tapering linearly to 0 at
// `radius`; 0 if unreached or beyond radius.
float proximityBonus(int distance, float radius, float maxBonus) {
  if (distance < 0 || radius <= 0.0f) return 0.0f;
  const float d = static_cast<float>(distance);
  if (d >= radius) return 0.0f;
  return maxBonus * (1.0f - d / radius);
}

} // namespace

void LandValueSystem::updateLandValues(
  CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const ServiceCoverageCache* serviceCache,
  int x0, int y0, int x1, int y1,
  const LandValueParams& params
) {
  std::vector<Coord> jobSources;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    if (building.type != BuildingType::Commercial && building.type != BuildingType::Industrial &&
        building.type != BuildingType::Office) {
      continue;
    }
    Coord anchor;
    if (resolveRoadAnchor(roads, building.position, anchor)) {
      jobSources.push_back(anchor);
    }
  }
  const std::unordered_map<Coord, int, Vec2Hash> jobDistance =
    jobSources.empty() ? std::unordered_map<Coord, int, Vec2Hash>{}
      : multiSourceDistanceField(roads, jobSources, static_cast<int>(params.jobAccessRadius));

  const glm::ivec2 dims = map.getDimensions();
  const int cx0 = std::max(0, x0);
  const int cy0 = std::max(0, y0);
  const int cx1 = std::min(dims.x - 1, x1);
  const int cy1 = std::min(dims.y - 1, y1);

  for (int y = cy0; y <= cy1; ++y) {
    for (int x = cx0; x <= cx1; ++x) {
      Tile& tile = map.getTile({x, y});
      if (tile.type == 2) continue;  // water has no land value to speak of
      // Skip unzoned land: the active region is mostly unzoned (only what's
      // been developed matters to the economy), and resolveRoadAnchor below
      // is expensive to run on every one of those tiles - most have no
      // nearby road and cost up to 5 failed lookups each to determine that.
      if (tile.zone == static_cast<int>(ZoneType::None)) continue;

      const float base = Zoning::defaultLandValueForZone(static_cast<ZoneType>(tile.zone));

      Coord anchor;
      const bool anchored = resolveRoadAnchor(roads, {x, y}, anchor);

      float jobBonus = 0.0f;
      float serviceBonus = 0.0f;
      if (anchored) {
        const auto jobIt = jobDistance.find(anchor);
        const int jobDist = (jobIt != jobDistance.end()) ? jobIt->second : -1;
        jobBonus = proximityBonus(jobDist, params.jobAccessRadius, params.maxJobBonus);

        if (serviceCache != nullptr) {
          const int svcDist = nearestServiceDistance(*serviceCache, anchor);
          serviceBonus = proximityBonus(svcDist, params.serviceAccessRadius, params.maxServiceBonus);
        }
      }

      const float pollutionPenalty = params.maxPollutionPenalty * std::clamp(tile.pollution, 0.0f, 1.0f);

      tile.landValue = std::clamp(
        base + jobBonus + serviceBonus - pollutionPenalty,
        params.minLandValue,
        params.maxLandValue
      );
    }
  }
}

float LandValueSystem::averageLandValue(const CityMap& map) {
  const glm::ivec2 dims = map.getDimensions();
  double sum = 0.0;
  uint64_t count = 0;
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      if (tile.type == 2 || tile.zone == static_cast<int>(ZoneType::None)) continue;
      sum += tile.landValue;
      ++count;
    }
  }
  return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 100.0f;
}
