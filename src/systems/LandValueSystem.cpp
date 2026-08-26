#include "src/systems/LandValueSystem.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

#include "src/world/Zoning.hpp"

namespace {

// Dense generation-stamped distance field over the full map grid. Only road
// nodes are written (via RoadNetwork adjacency); lookups are O(1) array
// index instead of unordered_map probes. Generation counters avoid O(W*H)
// clears between recomputes.
struct DenseDistanceField {
  int width = 0;
  int height = 0;
  std::vector<int> dist;
  std::vector<uint32_t> stamp;
  uint32_t generation = 0;

  void ensure(int w, int h) {
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (dist.size() < n) {
      dist.assign(n, -1);
      stamp.assign(n, 0);
      generation = 0;
    }
    width = w;
    height = h;
  }

  void begin() {
    ++generation;
    if (generation == 0) {
      std::fill(stamp.begin(), stamp.end(), 0);
      generation = 1;
    }
  }

  size_t index(Coord c) const {
    return static_cast<size_t>(c.y) * static_cast<size_t>(width) + static_cast<size_t>(c.x);
  }

  bool inBounds(Coord c) const {
    return c.x >= 0 && c.y >= 0 && c.x < width && c.y < height;
  }

  // Returns true if `c` was newly marked at distance `d`.
  bool tryVisit(Coord c, int d) {
    if (!inBounds(c)) return false;
    const size_t i = index(c);
    if (stamp[i] == generation) return false;
    stamp[i] = generation;
    dist[i] = d;
    return true;
  }

  int get(Coord c) const {
    if (!inBounds(c)) return -1;
    const size_t i = index(c);
    return stamp[i] == generation ? dist[i] : -1;
  }
};

// Multi-source BFS: distance from every road-reachable tile within
// `maxDistance` to the nearest of `sources`, in one O(V+E) pass (all sources
// pushed at distance 0 up front). Capped at maxDistance because proximityBonus
// is zero beyond it anyway.
void multiSourceDistanceField(
  const RoadNetwork& roads,
  const std::vector<Coord>& sources,
  int maxDistance,
  DenseDistanceField& field
) {
  field.begin();
  if (maxDistance < 0 || sources.empty()) {
    return;
  }

  std::queue<Coord> frontier;
  for (const Coord& s : sources) {
    if (field.tryVisit(s, 0)) {
      frontier.push(s);
    }
  }

  while (!frontier.empty()) {
    const Coord current = frontier.front();
    frontier.pop();
    const int currentDistance = field.get(current);
    if (currentDistance < 0 || currentDistance >= maxDistance) continue;

    const RoadNetwork::Node* node = roads.getNode(current);
    if (node == nullptr) continue;

    for (const RoadNodeId& neighborId : node->adjacent) {
      const Coord next = neighborId.coord;
      if (field.tryVisit(next, currentDistance + 1)) {
        frontier.push(next);
      }
    }
  }
}

// Nearest-facility distance for `coord` from an already-built service cache:
// a single lookup into the cache's precomputed nearestAnyDistance merge
// (built once per cache rebuild in ServiceSystem::buildCache) rather than an
// O(entries) scan repeated for every tile this is called on. Returns -1 if
// uncovered by any facility.
int nearestServiceDistance(const ServiceCoverageCache& cache, Coord coord) {
  const auto it = cache.nearestAnyDistance.find(coord);
  return it != cache.nearestAnyDistance.end() ? it->second : -1;
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
  jobSources.reserve(store.jobIds().size());
  for (EntityId id : store.jobIds()) {
    const Building* building = store.getBuilding(id);
    if (building == nullptr) continue;
    Coord anchor;
    if (roads.resolveRoadAnchor(building->position, anchor)) {
      jobSources.push_back(anchor);
    }
  }

  const glm::ivec2 dims = map.getDimensions();
  // Reused across calls: avoids reallocating W*H vectors every land-value tick.
  static thread_local DenseDistanceField jobField;
  jobField.ensure(dims.x, dims.y);
  if (!jobSources.empty()) {
    multiSourceDistanceField(roads, jobSources, static_cast<int>(params.jobAccessRadius), jobField);
  } else {
    jobField.begin();  // empty generation: all lookups return -1
  }

  const int cx0 = std::max(0, x0);
  const int cy0 = std::max(0, y0);
  const int cx1 = std::min(dims.x - 1, x1);
  const int cy1 = std::min(dims.y - 1, y1);

  for (int y = cy0; y <= cy1; ++y) {
    for (int x = cx0; x <= cx1; ++x) {
      const Tile& tile = map.getTile({x, y});
      if (tile.type == 2) continue;  // water has no land value to speak of
      // Skip unzoned land: the active region is mostly unzoned (only what's
      // been developed matters to the economy), and resolveRoadAnchor below
      // is expensive to run on every one of those tiles - most have no
      // nearby road and cost up to 5 failed lookups each to determine that.
      if (tile.zone == static_cast<int>(ZoneType::None)) continue;

      const float base = Zoning::defaultLandValueForZone(static_cast<ZoneType>(tile.zone));

      Coord anchor;
      const bool anchored = roads.resolveRoadAnchor({x, y}, anchor);

      float jobBonus = 0.0f;
      float serviceBonus = 0.0f;
      if (anchored) {
        const int jobDist = jobSources.empty() ? -1 : jobField.get(anchor);
        jobBonus = proximityBonus(jobDist, params.jobAccessRadius, params.maxJobBonus);

        if (serviceCache != nullptr) {
          const int svcDist = nearestServiceDistance(*serviceCache, anchor);
          serviceBonus = proximityBonus(svcDist, params.serviceAccessRadius, params.maxServiceBonus);
        }
      }

      const float pollutionPenalty = params.maxPollutionPenalty * std::clamp(map.pollution({x, y}), 0.0f, 1.0f);

      map.landValue({x, y}) = std::clamp(
        base + jobBonus + serviceBonus - pollutionPenalty,
        params.minLandValue,
        params.maxLandValue
      );
    }
  }
}

float LandValueSystem::averageLandValue(const CityMap& map) {
  const glm::ivec2 dims = map.getDimensions();
  return averageLandValue(map, 0, 0, dims.x - 1, dims.y - 1);
}

float LandValueSystem::averageLandValue(const CityMap& map, int x0, int y0, int x1, int y1) {
  const glm::ivec2 dims = map.getDimensions();
  const int cx0 = std::max(0, x0);
  const int cy0 = std::max(0, y0);
  const int cx1 = std::min(dims.x - 1, x1);
  const int cy1 = std::min(dims.y - 1, y1);
  if (cx0 > cx1 || cy0 > cy1) {
    return 100.0f;
  }

  double sum = 0.0;
  uint64_t count = 0;
  for (int y = cy0; y <= cy1; ++y) {
    for (int x = cx0; x <= cx1; ++x) {
      // type/zone stay on Tile (only pollution/landValue moved to parallel
      // arrays), so one getTile() call serves both.
      const Tile& tile = map.getTile({x, y});
      if (tile.type == 2 || tile.zone == static_cast<int>(ZoneType::None)) continue;
      sum += map.landValue({x, y});
      ++count;
    }
  }
  return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 100.0f;
}
