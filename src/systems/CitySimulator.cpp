#include "src/systems/CitySimulator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <thread>
#include <vector>

#include "src/core/ThreadPool.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"

namespace {

using Clock = std::chrono::steady_clock;
double elapsedMs(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

struct CapacitySummary {
  uint32_t resCapacity = 0;
  uint32_t comCapacity = 0;
  uint32_t indCapacity = 0;
  uint32_t resBuildings = 0;
  uint32_t comBuildings = 0;
  uint32_t indBuildings = 0;
};

CapacitySummary summarize(const EntityStore& store) {
  CapacitySummary cap;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    const uint32_t c = static_cast<uint32_t>(std::max(0, building.capacity));
    switch (building.type) {
      case BuildingType::Residential:
        cap.resCapacity += c;
        ++cap.resBuildings;
        break;
      case BuildingType::Commercial:
        cap.comCapacity += c;
        ++cap.comBuildings;
        break;
      case BuildingType::Industrial:
        cap.indCapacity += c;
        ++cap.indBuildings;
        break;
    }
  }
  return cap;
}

float clamp01(float v) {
  return std::max(0.0f, std::min(1.0f, v));
}

// City desirability from unemployment: 1.0 at full employment, falling to 0 as
// unemployment approaches 40% (the population system leaves some structural
// unemployment even when total jobs suffice).
float attractivenessFromUnemployment(float unemployment) {
  return clamp01(1.0f - unemployment / 0.40f);
}

bool isLand(const CityMap& map, int x, int y) {
  return map.isValid({x, y}) && map.getTile({x, y}).type != 2;
}

bool hasRoadAccess(const CityMap& map, Coord pos) {
  if (map.getTile(pos).hasRoad) {
    return true;
  }
  const Coord neighbors[4] = {
    {pos.x + 1, pos.y}, {pos.x - 1, pos.y}, {pos.x, pos.y + 1}, {pos.x, pos.y - 1}
  };
  for (const Coord& n : neighbors) {
    if (map.isValid(n) && map.getTile(n).hasRoad) {
      return true;
    }
  }
  return false;
}

// Map center, nudged to the nearest land tile so the seed grid lands on dry ground.
Coord cityCenter(const CityMap& map) {
  const glm::ivec2 dims = map.getDimensions();
  const Coord c{dims.x / 2, dims.y / 2};
  if (isLand(map, c.x, c.y)) {
    return c;
  }
  for (int r = 1; r < std::max(dims.x, dims.y); ++r) {
    for (int dy = -r; dy <= r; ++dy) {
      for (int dx = -r; dx <= r; ++dx) {
        if (isLand(map, c.x + dx, c.y + dy)) {
          return {c.x + dx, c.y + dy};
        }
      }
    }
  }
  return c;
}

// Lay a grid of roads centered on `center`, within +/- extent, on land only.
// buildRoad is edge-keyed so re-laying existing roads is harmless.
void layRoadGrid(CityMap& map, RoadNetwork& roads, Coord center, int extent, int spacing) {
  const glm::ivec2 dims = map.getDimensions();
  const int x0 = std::max(0, center.x - extent);
  const int x1 = std::min(dims.x - 1, center.x + extent);
  const int y0 = std::max(0, center.y - extent);
  const int y1 = std::min(dims.y - 1, center.y + extent);

  for (int gx = center.x % spacing; gx < dims.x; gx += spacing) {
    if (gx < x0 || gx > x1) continue;
    for (int y = y0; y < y1; ++y) {
      if (isLand(map, gx, y) && isLand(map, gx, y + 1)) {
        roads.buildRoad({gx, y}, {gx, y + 1});
      }
    }
  }
  for (int gy = center.y % spacing; gy < dims.y; gy += spacing) {
    if (gy < y0 || gy > y1) continue;
    for (int x = x0; x < x1; ++x) {
      if (isLand(map, x, gy) && isLand(map, x + 1, gy)) {
        roads.buildRoad({x, gy}, {x + 1, gy});
      }
    }
  }
}

// Empty, buildable, road-adjacent, currently-unzoned tiles within the developed box.
// The scan is read-only on map tiles, so row strips run safely in parallel.
std::vector<Coord> zonableCandidates(const CityMap& map, Coord center, int extent,
                                     ThreadPool& pool) {
  const glm::ivec2 dims = map.getDimensions();
  const int x0 = std::max(0, center.x - extent);
  const int x1 = std::min(dims.x - 1, center.x + extent);
  const int y0 = std::max(0, center.y - extent);
  const int y1 = std::min(dims.y - 1, center.y + extent);

  const int nRows   = y1 - y0 + 1;
  const int minRowsPerChunk = 8;
  const int nChunks = (nRows >= minRowsPerChunk * 2)
    ? std::max(1, std::min(static_cast<int>(pool.threadCount()), nRows / minRowsPerChunk))
    : 1;
  const int rowsPerChunk = (nRows + nChunks - 1) / nChunks;

  std::vector<std::future<std::vector<Coord>>> futs;
  futs.reserve(static_cast<size_t>(nChunks));
  for (int c = 0; c < nChunks; ++c) {
    const int ry0 = y0 + c * rowsPerChunk;
    const int ry1 = std::min(y0 + (c + 1) * rowsPerChunk - 1, y1);
    if (ry0 > ry1) break;
    futs.push_back(pool.submit([&map, x0, x1, ry0, ry1]() {
      std::vector<Coord> partial;
      for (int y = ry0; y <= ry1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          const Tile& tile = map.getTile({x, y});
          if (tile.type == 2 || tile.zone != 0 || tile.buildingId != 0) continue;
          if (!hasRoadAccess(map, {x, y})) continue;
          partial.push_back({x, y});
        }
      }
      return partial;
    }));
  }

  std::vector<Coord> candidates;
  for (auto& f : futs) {
    auto partial = f.get();
    candidates.insert(candidates.end(), partial.begin(), partial.end());
  }

  // Compact growth: zone tiles nearest the center first.
  std::sort(candidates.begin(), candidates.end(), [center](const Coord& a, const Coord& b) {
    const int da = (a.x - center.x) * (a.x - center.x) + (a.y - center.y) * (a.y - center.y);
    const int db = (b.x - center.x) * (b.x - center.x) + (b.y - center.y) * (b.y - center.y);
    if (da != db) return da < db;
    if (a.y != b.y) return a.y < b.y;
    return a.x < b.x;
  });
  return candidates;
}

// Count zoned tiles still waiting for a building (road-accessible).
uint32_t emptyZonedTiles(const CityMap& map, Coord center, int extent) {
  const glm::ivec2 dims = map.getDimensions();
  const int x0 = std::max(0, center.x - extent);
  const int x1 = std::min(dims.x - 1, center.x + extent);
  const int y0 = std::max(0, center.y - extent);
  const int y1 = std::min(dims.y - 1, center.y + extent);

  uint32_t count = 0;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const Tile& tile = map.getTile({x, y});
      if (tile.zone != 0 && tile.zone != static_cast<int>(ZoneType::Park) &&
          tile.buildingId == 0 && tile.type != 2 && hasRoadAccess(map, {x, y})) {
        ++count;
      }
    }
  }
  return count;
}

// Recompute the pollution field from scratch each tick: industry is a heavy
// emitter, commerce a light one, spread over a small radius with linear falloff.
//
// The clear phase is parallelized across row strips (no overlap, no races).
// The scatter phase stays sequential: there are rarely more than ~100 emitters,
// each affecting a 7×7 patch, and concurrent writes to the same tile would
// race.  A precomputed weight LUT removes all sqrt() calls from the hot loop.
void updatePollution(CityMap& map, const EntityStore& store,
                     int x0, int y0, int x1, int y1, ThreadPool& pool) {
  // Precompute per-(dx,dy) falloff weights for radius 3. Values outside the
  // circle are 0; computed once, reused for every emitter this tick.
  constexpr int kRadius = 3;
  constexpr int kDiam   = kRadius * 2 + 1;
  float lut[kDiam][kDiam];
  for (int dy = -kRadius; dy <= kRadius; ++dy) {
    for (int dx = -kRadius; dx <= kRadius; ++dx) {
      const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
      lut[dy + kRadius][dx + kRadius] =
        (d > static_cast<float>(kRadius)) ? 0.0f : (1.0f - d / static_cast<float>(kRadius + 1));
    }
  }

  // Parallel clear: partition rows across pool workers.
  const int nRows   = y1 - y0 + 1;
  // Parallel clear only pays off when there are enough rows to fill each
  // worker's chunk without drowning in task-submission overhead.
  const int minRowsPerChunk = 16;
  const int nChunks = (nRows >= minRowsPerChunk * 2)
    ? std::max(1, std::min(static_cast<int>(pool.threadCount()), nRows / minRowsPerChunk))
    : 1;
  {
    std::vector<std::future<void>> futs;
    futs.reserve(static_cast<size_t>(nChunks));
    const int rowsPerChunk = (nRows + nChunks - 1) / nChunks;
    for (int c = 0; c < nChunks; ++c) {
      const int ry0 = y0 + c * rowsPerChunk;
      const int ry1 = std::min(y0 + (c + 1) * rowsPerChunk - 1, y1);
      if (ry0 > ry1) break;
      futs.push_back(pool.submit([&map, x0, x1, ry0, ry1]() {
        for (int y = ry0; y <= ry1; ++y) {
          for (int x = x0; x <= x1; ++x) {
            map.getTile({x, y}).pollution = 0.0f;
          }
        }
      }));
    }
    for (auto& f : futs) f.get();
  }

  // Sequential scatter using the LUT (no sqrt in the hot loop).
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    float emit = 0.0f;
    if      (building.type == BuildingType::Industrial) emit = 1.0f;
    else if (building.type == BuildingType::Commercial)  emit = 0.25f;
    if (emit <= 0.0f) continue;

    const Coord c = building.position;
    for (int dy = -kRadius; dy <= kRadius; ++dy) {
      for (int dx = -kRadius; dx <= kRadius; ++dx) {
        const float w = lut[dy + kRadius][dx + kRadius];
        if (w <= 0.0f) continue;
        const int tx = c.x + dx;
        const int ty = c.y + dy;
        if (tx < x0 || tx > x1 || ty < y0 || ty > y1) continue;
        Tile& tile = map.getTile({tx, ty});
        tile.pollution = std::min(1.0f, tile.pollution + emit * w);
      }
    }
  }
}

float averageResidentialPollution(const CityMap& map, const EntityStore& store) {
  double sum = 0.0;
  uint32_t n = 0;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    if (building.type == BuildingType::Residential) {
      sum += map.getTile(building.position).pollution;
      ++n;
    }
  }
  return n > 0 ? static_cast<float>(sum / n) : 0.0f;
}

// Pick a road-accessible land site for a new service facility: the first sits
// at the center, later ones go to the developed tile farthest from all existing
// facilities, spreading coverage across the city.
Coord chooseFacilitySite(
  const CityMap& map,
  const std::vector<ServiceFacility>& facilities,
  Coord center,
  int extent
) {
  const glm::ivec2 dims = map.getDimensions();
  const int x0 = std::max(0, center.x - extent);
  const int x1 = std::min(dims.x - 1, center.x + extent);
  const int y0 = std::max(0, center.y - extent);
  const int y1 = std::min(dims.y - 1, center.y + extent);

  Coord best = center;
  long bestScore = -1;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (map.getTile({x, y}).type == 2) continue;        // water
      if (!hasRoadAccess(map, {x, y})) continue;
      long score;
      if (facilities.empty()) {
        // Prefer the center for the first facility (smallest distance wins).
        const long dc = (x - center.x) * (x - center.x) + (y - center.y) * (y - center.y);
        score = -dc;
      } else {
        long nearest = std::numeric_limits<long>::max();
        for (const ServiceFacility& f : facilities) {
          const long d = (x - f.position.x) * (x - f.position.x) + (y - f.position.y) * (y - f.position.y);
          nearest = std::min(nearest, d);
        }
        score = nearest;  // maximize distance to the closest existing facility
      }
      if (score > bestScore) {
        bestScore = score;
        best = {x, y};
      }
    }
  }
  return best;
}

// Keep roughly one facility per `popPerFacility` residents, cycling through the
// four service types so coverage of each grows together.
void placeFacilitiesIfNeeded(
  const CityMap& map,
  std::vector<ServiceFacility>& facilities,
  uint32_t population,
  Coord center,
  int extent,
  int coverageRadius
) {
  const uint32_t popPerFacility = 1000;
  const size_t target = population / popPerFacility;
  while (facilities.size() < target) {
    ServiceFacility facility;
    facility.type = static_cast<ServiceType>(facilities.size() % 4);
    facility.position = chooseFacilitySite(map, facilities, center, extent);
    facility.maxTravelDistance = coverageRadius;
    facility.quality = 1.0f;
    facilities.push_back(facility);
  }
}

// Zone up to `batch` of the nearest candidate tiles, splitting counts by demand
// but placing the cleanest tiles residential and the most polluted industrial,
// so housing and industry self-segregate as the pollution field develops.
void autoZone(CityMap& map, const std::vector<Coord>& candidates, const ZoneDemand& demand, int batch) {
  const float total = std::max(0.0f, demand.residential) +
                      std::max(0.0f, demand.commercial) +
                      std::max(0.0f, demand.industrial);
  const int limit = std::min(static_cast<int>(candidates.size()), batch);
  if (total <= 1e-3f || limit <= 0) {
    return;
  }

  int nR = static_cast<int>(std::lround(limit * std::max(0.0f, demand.residential) / total));
  int nI = static_cast<int>(std::lround(limit * std::max(0.0f, demand.industrial) / total));
  if (nR + nI > limit) {
    nI = std::min(nI, limit);
    nR = std::min(nR, limit - nI);
  }
  const int nC = limit - nR - nI;

  // The nearest `limit` candidates keep growth compact; sort them by pollution
  // so the assignment below segregates dirty industry from clean housing.
  std::vector<Coord> batchTiles(candidates.begin(), candidates.begin() + limit);
  std::sort(batchTiles.begin(), batchTiles.end(), [&map](const Coord& a, const Coord& b) {
    const float pa = map.getTile(a).pollution;
    const float pb = map.getTile(b).pollution;
    if (pa != pb) return pa < pb;
    if (a.y != b.y) return a.y < b.y;
    return a.x < b.x;
  });

  for (int i = 0; i < limit; ++i) {
    ZoneType zone;
    if (i < nR) zone = ZoneType::Residential;
    else if (i < nR + nC) zone = ZoneType::Commercial;
    else zone = ZoneType::Industrial;
    Tile& tile = map.getTile(batchTiles[i]);
    tile.zone = static_cast<int>(zone);
    tile.landValue = Zoning::defaultLandValueForZone(zone);
  }
}

} // namespace

ZoneDemand CitySimulator::evaluateDemand(const EntityStore& store, const PopulationStore& population) {
  const CapacitySummary cap = summarize(store);
  const float pop = static_cast<float>(population.getTotalPopulation());
  const float employed = static_cast<float>(population.getTotalEmployed());
  const float jobCapacity = static_cast<float>(cap.comCapacity + cap.indCapacity);

  (void)jobCapacity;
  ZoneDemand demand;

  // The population system employs everyone up to total job capacity, so a
  // healthy, attractive city needs roughly one job per resident, split between
  // commercial and industrial (~0.5 of population each).
  const float unemployment = pop > 0.0f ? clamp01((pop - employed) / pop) : 0.0f;

  // Residential: build housing when the city is attractive (jobs plentiful) AND
  // existing homes are filling up. Empty housing (low occupancy) suppresses
  // further construction so housing tracks the migrating population rather than
  // racing ahead of it. A startup floor seeds the first homes while tiny.
  const float attractiveness = attractivenessFromUnemployment(unemployment);
  const float housingOccupancy = cap.resCapacity > 0 ? clamp01(pop / static_cast<float>(cap.resCapacity)) : 0.0f;
  const float resStartup = (cap.resCapacity < 40) ? 0.8f : 0.0f;
  demand.residential = clamp01(std::max(resStartup, attractiveness * housingOccupancy));

  // Commercial: retail jobs serving residents. Target ~50% of population.
  const float comTarget = 0.5f * pop;
  demand.commercial = comTarget > 0.0f ? clamp01((comTarget - static_cast<float>(cap.comCapacity)) / comTarget) : 0.0f;

  // Industrial: base/export jobs. Target ~50% of population, with a startup
  // floor so the first jobs appear once anyone has moved in.
  const float indTarget = 0.5f * pop;
  const float indGap = indTarget > 0.0f ? clamp01((indTarget - static_cast<float>(cap.indCapacity)) / indTarget) : 0.0f;
  const float indStartup = (cap.indCapacity < 24 && cap.resCapacity > 0) ? 0.6f : 0.0f;
  demand.industrial = clamp01(std::max(indStartup, indGap));

  return demand;
}

SimResult CitySimulator::run(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  uint32_t seed,
  int ticks,
  const SimOptions& options
) {
  SimResult result;
  const bool infinite = (ticks < 0);
  result.rows.reserve(static_cast<size_t>(std::max(0, ticks)));

  const int spacing = std::max(2, options.gridSpacing);
  const Coord center = cityCenter(map);
  const glm::ivec2 dims = map.getDimensions();
  const int maxExtent = std::max(dims.x, dims.y);

  // Seed an initial neighborhood of roads.
  int extent = std::min(maxExtent, spacing * 2);
  {
    const auto t0 = Clock::now();
    layRoadGrid(map, roads, center, extent, spacing);
    roads.updateConnectivity(center);
    result.timings.roadMs += elapsedMs(t0, Clock::now());
  }

  float lastCongestion = 0.0f;          // previous tick's peak congestion, feeds desirability
  float lastServiceSatisfaction = 0.5f; // previous tick's service satisfaction, feeds desirability
  std::vector<ServiceFacility> facilities;
  ServiceCoverageCache coverageCache;
  const int serviceRadius = spacing * 3;

  // Thread pool for parallel pathfinding (traffic) and building coverage
  // (services), and for running both concurrently within each tick.
  // Sized to hardware concurrency minus 1 (main thread drives the loop).
  const unsigned int hwc = std::thread::hardware_concurrency();
  ThreadPool pool(std::max(1u, hwc > 0 ? hwc - 1 : 3u));

  for (int tick = 0; infinite || tick < ticks; ++tick) {
    const uint32_t tickSeed = seed + static_cast<uint32_t>(tick);

    const ZoneDemand demand = evaluateDemand(store, population);
    const float overallDemand = std::max({demand.residential, demand.commercial, demand.industrial});

    // Expand the road grid outward while demand persists, paced so zoned land
    // does not run too far ahead of what has actually been built.
    {
      const auto t0 = Clock::now();
      const bool wantsRoom = emptyZonedTiles(map, center, extent) < static_cast<uint32_t>(3 * options.zoneBatchPerTick);
      if (overallDemand > 0.12f && wantsRoom && extent < maxExtent) {
        extent = std::min(maxExtent, extent + spacing);
        layRoadGrid(map, roads, center, extent, spacing);
        roads.updateConnectivity(center);
      }
      result.timings.roadMs += elapsedMs(t0, Clock::now());
    }

    // Active region bounds — all tile-scanning passes clamp to this box so the
    // work scales with the developed area rather than the full map.
    const int ax0 = std::max(0, center.x - extent);
    const int ay0 = std::max(0, center.y - extent);
    const int ax1 = std::min(dims.x - 1, center.x + extent);
    const int ay1 = std::min(dims.y - 1, center.y + extent);

    // Refresh the pollution field, then zone land in proportion to demand,
    // steering housing to clean tiles and industry to dirty ones.
    {
      const auto t0 = Clock::now();
      updatePollution(map, store, ax0, ay0, ax1, ay1, pool);
      const std::vector<Coord> candidates = zonableCandidates(map, center, extent, pool);
      autoZone(map, candidates, demand, options.zoneBatchPerTick);
      result.timings.zoningMs += elapsedMs(t0, Clock::now());
    }

    // Build on zoned, road-accessible land in proportion to demand.
    {
      const auto t0 = Clock::now();
      GrowthSystem::runStep(map, store, demand, tickSeed, options.buildChance,
                            nullptr, {ax0, ay0}, {ax1, ay1}, &pool);
      result.timings.growthMs += elapsedMs(t0, Clock::now());
    }

    // Migration: residents move in (or out) gradually rather than instantly
    // filling new housing. The rate scales with city desirability - jobs being
    // plentiful, and traffic not too congested - so housing vacancy is a real
    // signal that paces both migration and further residential construction.
    // The desirability computation and requested-population math run every tick
    // (cheap arithmetic); the expensive full allocation only runs on the interval.
    {
      const auto t0 = Clock::now();
      const CapacitySummary cap = summarize(store);
      const uint32_t prevPop = population.getTotalPopulation();
      const uint32_t prevEmployed = population.getTotalEmployed();
      const float unemployment = prevPop > 0 ? clamp01(static_cast<float>(prevPop - prevEmployed) / prevPop) : 0.0f;
      float desirability = attractivenessFromUnemployment(unemployment);
      desirability *= clamp01(1.0f - 0.4f * lastCongestion);
      desirability *= clamp01(1.0f - 0.5f * averageResidentialPollution(map, store));
      desirability *= clamp01(0.6f + 0.4f * lastServiceSatisfaction);

      const float headroom = cap.resCapacity > prevPop ? static_cast<float>(cap.resCapacity - prevPop) : 0.0f;
      float requested;
      if (desirability < 0.05f && prevPop > 0) {
        requested = static_cast<float>(prevPop) * 0.98f;
      } else {
        requested = static_cast<float>(prevPop) + 0.25f * desirability * headroom + 3.0f * desirability;
      }
      const uint32_t requestedPop = static_cast<uint32_t>(
        std::max(0.0f, std::min(static_cast<float>(cap.resCapacity), requested)));

      if (tick % std::max(1, options.populationInterval) == 0) {
        PopulationSystem::allocate(store, population, requestedPop, tickSeed + 1u);
      }
      result.timings.populationMs += elapsedMs(t0, Clock::now());
    }

    // Services and traffic are independent (services reads road topology;
    // traffic resets/writes edge congestion). Run services as a pool task so
    // it overlaps with traffic's parallel Dijkstra phase on the main thread.
    const bool serviceActive = (tick % std::max(1, options.serviceInterval) == 0);
    const bool trafficActive = options.runTraffic && (tick % std::max(1, options.trafficInterval) == 0);

    // Coverage only changes when buildings are added/removed or facilities
    // change. Redevelopment replaces at the same position so it doesn't
    // invalidate the result. Snapshot the count before service prep so the
    // check is consistent with what's in the cache.
    const size_t currentBuildingCount = store.getBuildings().size();

    // Service prep: facility placement and BFS cache rebuild are state-mutating
    // and must complete before the concurrent evaluation starts.
    if (serviceActive) {
      const auto t0 = Clock::now();
      placeFacilitiesIfNeeded(map, facilities, population.getTotalPopulation(), center, extent, serviceRadius);
      if (facilities.size() != coverageCache.builtForFacilityCount) {
        ServiceSystem::buildCache(roads, facilities, coverageCache);
        coverageCache.cachedBuildingCount = static_cast<size_t>(-1);  // distance fields changed
      }
      result.timings.serviceMs += elapsedMs(t0, Clock::now());
    }

    // Submit service evaluation to a pool worker so it runs concurrently with
    // the traffic pathfinding that follows on the main thread.
    // Skip if neither buildings nor facilities changed since the last evaluation.
    std::future<std::pair<ServiceCoverageSummary, double>> serviceFuture;
    if (serviceActive && currentBuildingCount != coverageCache.cachedBuildingCount) {
      serviceFuture = pool.submit([&store, &roads, &coverageCache, &pool, currentBuildingCount]() {
        const auto t0 = Clock::now();
        auto svc = ServiceSystem::evaluateFromCache(store, roads, coverageCache, &pool);
        coverageCache.cachedBuildingCount = currentBuildingCount;
        coverageCache.cachedResult = svc;
        return std::make_pair(svc, elapsedMs(t0, Clock::now()));
      });
    }

    // Traffic runs on the main thread so it can safely wait for inner pool
    // tasks (parallel Dijkstra) without risking pool deadlock.
    TrafficSummary traffic;
    if (trafficActive) {
      const auto t0 = Clock::now();
      traffic = TrafficSystem::simulateCommutes(store, population, roads, tickSeed + 2u, &pool);
      result.timings.trafficMs += elapsedMs(t0, Clock::now());
      lastCongestion = traffic.maxEdgeCongestion;
    }

    // Collect the service result (likely already done while traffic ran).
    ServiceCoverageSummary service;
    if (serviceFuture.valid()) {
      auto [svc, ms] = serviceFuture.get();
      service = svc;
      lastServiceSatisfaction = service.satisfaction;
      result.timings.serviceMs += ms;
    } else if (serviceActive) {
      // Building set and facilities unchanged: reuse cached result at zero cost.
      service = coverageCache.cachedResult;
      lastServiceSatisfaction = service.satisfaction;
    }

    EconomyState economy;
    {
      const auto t0 = Clock::now();
      economy = EconomySystem::calculateEconomy(store, population);
      result.timings.economyMs += elapsedMs(t0, Clock::now());
    }

    const CapacitySummary cap = summarize(store);
    SimTickMetrics row;
    row.tick = tick;
    row.demandResidential = demand.residential;
    row.demandCommercial = demand.commercial;
    row.demandIndustrial = demand.industrial;
    row.population = population.getTotalPopulation();
    row.employed = population.getTotalEmployed();
    row.residentialBuildings = cap.resBuildings;
    row.commercialBuildings = cap.comBuildings;
    row.industrialBuildings = cap.indBuildings;
    row.roadTiles = static_cast<uint32_t>(roads.getRoadCount());
    row.budgetBalance = economy.balance;
    row.trafficCongestion = traffic.maxEdgeCongestion;
    row.avgPollution = averageResidentialPollution(map, store);
    row.serviceCoverage = service.overallCoverage;
    row.serviceFacilities = static_cast<uint32_t>(facilities.size());
    if (!infinite) {
      result.rows.push_back(row);
    }
    if (options.tickCallback && !options.tickCallback(row)) {
      break;
    }
  }

  return result;
}
