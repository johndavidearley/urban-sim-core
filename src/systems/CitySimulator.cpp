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
#include "src/systems/LandValueSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/networks/Pathfinding.hpp"

namespace {

using Clock = std::chrono::steady_clock;
double elapsedMs(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

struct CapacitySummary {
  uint32_t resCapacity = 0;
  uint32_t comCapacity = 0;
  uint32_t indCapacity = 0;
  uint32_t officeCapacity = 0;
  uint32_t resBuildings = 0;
  uint32_t comBuildings = 0;
  uint32_t indBuildings = 0;
  uint32_t officeBuildings = 0;
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
      case BuildingType::Office:
        cap.officeCapacity += c;
        ++cap.officeBuildings;
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

// Sample every `stopSpacing`-th waypoint of a road path (plus the endpoint)
// as a bus stop, so a route has a handful of stops rather than one per tile.
std::vector<Coord> sampleRouteStops(const Pathfinding::Path& path, int stopSpacing) {
  std::vector<Coord> stops;
  if (path.waypoints.empty()) return stops;
  const size_t step = static_cast<size_t>(std::max(1, stopSpacing));
  for (size_t i = 0; i < path.waypoints.size(); i += step) {
    stops.push_back(path.waypoints[i]);
  }
  if (stops.back() != path.waypoints.back()) {
    stops.push_back(path.waypoints.back());
  }
  return stops;
}

// Keep roughly one route of `mode` per `popPerRoute` residents (capped at
// `maxOfMode`, since each route adds a per-tick BFS to the transit coverage
// cache): connect the residential building farthest from existing route
// coverage (of either mode - both compete for the same underserved areas) to
// a job building (commercial/industrial/office) via the road network. Simple
// and deterministic, in the same spirit as placeFacilitiesIfNeeded, though
// routes need an endpoint pair rather than a single site.
//
// Bus and rail share this one placement routine, differing only in
// parameters: bus connects to the *nearest* job (short local hops, denser
// placement, modest capacity), while rail connects to the *farthest* job
// (a long trunk line spanning the city, sparse placement, high capacity) -
// passed in via `connectToFarthestJob`. The BFS coverage/offload mechanism
// downstream doesn't care how a route's stops were chosen.
void placeTransitRoutesOfModeIfNeeded(
  const RoadNetwork& roads,
  const EntityStore& store,
  std::vector<TransitRoute>& routes,
  uint32_t population,
  TransitMode mode,
  uint32_t popPerRoute,
  size_t maxOfMode,
  int stopCoverageRadius,
  int vehicleCount,
  int capacityPerVehicle,
  int stopSpacing,
  bool connectToFarthestJob
) {
  const size_t existingOfMode = static_cast<size_t>(std::count_if(
    routes.begin(), routes.end(), [mode](const TransitRoute& r) { return r.mode == mode; }));
  const size_t target = std::min<size_t>(population / popPerRoute, maxOfMode);
  if (existingOfMode >= target) return;

  std::vector<const Building*> residential;
  std::vector<const Building*> jobs;
  for (const auto& [id, b] : store.getBuildings()) {
    (void)id;
    if (b.type == BuildingType::Residential) {
      residential.push_back(&b);
    } else if (b.type == BuildingType::Commercial || b.type == BuildingType::Industrial ||
               b.type == BuildingType::Office) {
      jobs.push_back(&b);
    }
  }
  if (residential.empty() || jobs.empty()) return;

  // Buildings live in a hash map; every other per-tick decision loop in this
  // file sorts by ID first to keep iteration (and thus RNG-free tie-breaking
  // here) deterministic across platforms.
  auto byId = [](const Building* a, const Building* b) { return a->id < b->id; };
  std::sort(residential.begin(), residential.end(), byId);
  std::sort(jobs.begin(), jobs.end(), byId);

  // Residential buildings proven disconnected from every job building this
  // call are excluded from further "farthest-uncovered" candidacy, so one
  // stranded building (e.g. beyond a water gap the road grid hasn't crossed
  // yet) can't permanently block placement for the whole city.
  std::vector<const Building*> unreachableHomes;
  size_t placed = existingOfMode;

  while (placed < target) {
    // Farthest-from-existing-stop residential building anchors the new
    // route, spreading routes across the city instead of clustering them.
    // Existing stops of either mode count, since both compete to cover the
    // same underserved areas.
    const Building* bestHome = nullptr;
    long bestScore = -1;
    for (const Building* home : residential) {
      if (!roads.hasNode(home->position)) continue;
      if (std::find(unreachableHomes.begin(), unreachableHomes.end(), home) != unreachableHomes.end()) continue;
      long nearestStopDist = std::numeric_limits<long>::max();
      for (const TransitRoute& r : routes) {
        for (const Coord& stop : r.stops) {
          const long dx = home->position.x - stop.x;
          const long dy = home->position.y - stop.y;
          nearestStopDist = std::min(nearestStopDist, dx * dx + dy * dy);
        }
      }
      // Maximize distance to the closest existing stop (least-covered wins);
      // with no routes yet, every candidate ties at "uncovered" and the
      // first in ID order is picked.
      if (nearestStopDist > bestScore) {
        bestScore = nearestStopDist;
        bestHome = home;
      }
    }
    if (bestHome == nullptr) break;

    // Try job buildings nearest-first or farthest-first depending on mode
    // (straight-line distance; cheap ordering heuristic - the route itself
    // follows actual roads via Pathfinding). Falls through to the next
    // candidate if the preferred one turns out unreachable, so one
    // disconnected job doesn't strand this route attempt.
    std::vector<const Building*> jobsOrdered = jobs;
    std::sort(jobsOrdered.begin(), jobsOrdered.end(), [&](const Building* a, const Building* b) {
      const long dax = a->position.x - bestHome->position.x, day = a->position.y - bestHome->position.y;
      const long dbx = b->position.x - bestHome->position.x, dby = b->position.y - bestHome->position.y;
      const long da = dax * dax + day * day, db = dbx * dbx + dby * dby;
      if (da != db) return connectToFarthestJob ? (da > db) : (da < db);
      return a->id < b->id;
    });

    Pathfinding::Path path;
    bool connected = false;
    for (const Building* job : jobsOrdered) {
      if (!roads.hasNode(job->position)) continue;
      path = Pathfinding::findShortestPath(roads, bestHome->position, job->position);
      if (path.found && path.waypoints.size() >= 2) {
        connected = true;
        break;
      }
    }

    if (!connected) {
      unreachableHomes.push_back(bestHome);
      if (unreachableHomes.size() >= residential.size()) break;  // exhausted every candidate
      continue;
    }

    TransitRoute route;
    route.id = static_cast<TransitRouteId>(routes.size() + 1);
    route.mode = mode;
    route.stops = sampleRouteStops(path, stopSpacing);
    route.stopCoverageRadius = stopCoverageRadius;
    route.vehicleCount = vehicleCount;
    route.capacityPerVehicle = capacityPerVehicle;
    routes.push_back(std::move(route));
    ++placed;
  }
}

void placeTransitRoutesIfNeeded(
  const RoadNetwork& roads,
  const EntityStore& store,
  std::vector<TransitRoute>& routes,
  uint32_t population,
  int stopCoverageRadius
) {
  // Bus: short local hops to the nearest job, denser placement, modest
  // per-vehicle capacity - matches TransitRoute's own defaults.
  placeTransitRoutesOfModeIfNeeded(
    roads, store, routes, population, TransitMode::Bus,
    /*popPerRoute=*/1200, /*maxOfMode=*/16, stopCoverageRadius,
    /*vehicleCount=*/2, /*capacityPerVehicle=*/30, /*stopSpacing=*/2,
    /*connectToFarthestJob=*/false
  );

  // Rail: a long trunk line to the farthest job, sparse (a city only
  // justifies a handful of lines), much higher capacity and a wider
  // catchment - riders travel farther to reach a station than a bus stop.
  placeTransitRoutesOfModeIfNeeded(
    roads, store, routes, population, TransitMode::Rail,
    /*popPerRoute=*/4000, /*maxOfMode=*/3, stopCoverageRadius * 2,
    /*vehicleCount=*/6, /*capacityPerVehicle=*/150, /*stopSpacing=*/1,
    /*connectToFarthestJob=*/true
  );
}

// True unless some district containing `coord` has a zoning ordinance
// banning `zone` (see District::bannedZoneTypes). districts == nullptr means
// no restriction, matching prior behavior for every caller that doesn't
// pass one.
bool zoneAllowedAt(const DistrictSystem* districts, Coord coord, ZoneType zone) {
  if (districts == nullptr) return true;
  for (const District& d : districts->getDistricts()) {
    if (d.contains(coord) && !d.allowsZone(zone)) {
      return false;
    }
  }
  return true;
}

// Zone up to `batch` of the nearest candidate tiles, splitting counts by demand
// but placing the cleanest tiles residential and the most polluted industrial,
// so housing and industry self-segregate as the pollution field develops.
// Tiles a district's zoning ordinance blocks for their assigned type are
// left unzoned this tick (they remain candidates and get retried later) -
// counts are not reallocated to another type, so a heavily-restricted
// district genuinely produces less of the banned type city-wide, the same
// as a real ordinance would.
void autoZone(CityMap& map, const std::vector<Coord>& candidates, const ZoneDemand& demand, int batch,
              const DistrictSystem* districts) {
  const float total = std::max(0.0f, demand.residential) +
                      std::max(0.0f, demand.commercial) +
                      std::max(0.0f, demand.industrial) +
                      std::max(0.0f, demand.office);
  const int limit = std::min(static_cast<int>(candidates.size()), batch);
  if (total <= 1e-3f || limit <= 0) {
    return;
  }

  int nR = static_cast<int>(std::lround(limit * std::max(0.0f, demand.residential) / total));
  int nI = static_cast<int>(std::lround(limit * std::max(0.0f, demand.industrial) / total));
  int nO = static_cast<int>(std::lround(limit * std::max(0.0f, demand.office) / total));
  if (nR + nI + nO > limit) {
    nI = std::min(nI, limit);
    nO = std::min(nO, limit - nI);
    nR = std::min(nR, limit - nI - nO);
  }
  const int nC = limit - nR - nI - nO;

  // The nearest `limit` candidates keep growth compact; sort them by pollution
  // so the assignment below segregates dirty industry from clean housing.
  // Order of assignment (cleanest to dirtiest tiles): Residential, Office,
  // Commercial, Industrial - office space is as pollution-sensitive as
  // housing (it commands the highest land value), retail tolerates more, and
  // industry gets whatever's left.
  std::vector<Coord> batchTiles(candidates.begin(), candidates.begin() + limit);
  std::sort(batchTiles.begin(), batchTiles.end(), [&map](const Coord& a, const Coord& b) {
    const float pa = map.getTile(a).pollution;
    const float pb = map.getTile(b).pollution;
    if (pa != pb) return pa < pb;
    if (a.y != b.y) return a.y < b.y;
    return a.x < b.x;
  });

  static constexpr ZoneType kZoneFallbackOrder[4] = {
    ZoneType::Residential, ZoneType::Office, ZoneType::Commercial, ZoneType::Industrial
  };

  for (int i = 0; i < limit; ++i) {
    ZoneType zone;
    if (i < nR) zone = ZoneType::Residential;
    else if (i < nR + nO) zone = ZoneType::Office;
    else if (i < nR + nO + nC) zone = ZoneType::Commercial;
    else zone = ZoneType::Industrial;

    if (!zoneAllowedAt(districts, batchTiles[i], zone)) {
      // Ordinance blocks the demand-driven choice here; fall back to the
      // first still-allowed type in a fixed order rather than leaving the
      // tile stranded. A real ordinance restricts what a parcel becomes, it
      // doesn't make land undevelopable forever - and if it did (e.g. a
      // district banning everything, or one that happens to cover the
      // city's growth origin and bans the only type demand wants yet), that
      // would otherwise deadlock the whole city's bootstrap, not just this
      // district.
      bool found = false;
      for (ZoneType candidate : kZoneFallbackOrder) {
        if (candidate != zone && zoneAllowedAt(districts, batchTiles[i], candidate)) {
          zone = candidate;
          found = true;
          break;
        }
      }
      if (!found) continue;  // every type banned here; leave unzoned
    }

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

  // Office: higher-tier white-collar employment that follows commercial
  // establishment rather than leading it (office towers/parks cluster around
  // an already-formed retail/downtown base, in reality). Targets a smaller
  // share of population than retail/industrial, and is gated by how
  // established commercial capacity already is relative to its own target -
  // a city with no commercial base yet generates no office demand.
  const float officeTarget = 0.25f * pop;
  const float commercialEstablishment = comTarget > 0.0f
    ? clamp01(static_cast<float>(cap.comCapacity) / comTarget) : 0.0f;
  const float officeGap = officeTarget > 0.0f
    ? clamp01((officeTarget - static_cast<float>(cap.officeCapacity)) / officeTarget) : 0.0f;
  demand.office = clamp01(officeGap * commercialEstablishment);

  return demand;
}

SimResult CitySimulator::run(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  uint32_t seed,
  int ticks,
  const SimOptions& options,
  const DistrictSystem* districts
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
  std::vector<TransitRoute> transitRoutes;
  TransitCoverageCache transitCache;
  // Growth chance modifiers derived from the *previous* tick's district
  // metrics (one-tick lag, like lastCongestion/lastServiceSatisfaction below)
  // - empty when districts is null, which GrowthSystem treats as a no-op.
  std::vector<GrowthChanceModifier> districtGrowthModifiers;

  // Thread pool for parallel pathfinding (traffic) and building coverage
  // (services), and for running both concurrently within each tick.
  // Sized to hardware concurrency minus 1 (main thread drives the loop).
  const unsigned int hwc = std::thread::hardware_concurrency();
  ThreadPool pool(std::max(1u, hwc > 0 ? hwc - 1 : 3u));

  for (int tick = 0; infinite || tick < ticks; ++tick) {
    const uint32_t tickSeed = seed + static_cast<uint32_t>(tick);

    const ZoneDemand demand = evaluateDemand(store, population);
    const float overallDemand = std::max({demand.residential, demand.commercial, demand.industrial, demand.office});

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
      autoZone(map, candidates, demand, options.zoneBatchPerTick, districts);
      result.timings.zoningMs += elapsedMs(t0, Clock::now());
    }

    // Build on zoned, road-accessible land in proportion to demand. District
    // growth-pressure modifiers (if any) come from the previous tick's
    // service-budget/density evaluation below.
    {
      const auto t0 = Clock::now();
      GrowthSystem::runStep(map, store, demand, tickSeed, options.buildChance,
                            &districtGrowthModifiers, {ax0, ay0}, {ax1, ay1}, &pool);
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

    // Transit prep: route placement and BFS cache rebuild are state-mutating,
    // like facility placement above, and must complete before traffic uses
    // them. Routes only grow (never removed), so this is cheap once the
    // route count has caught up to its population-based target.
    TransitSummary transitSummary;
    if (options.enableTransit && trafficActive) {
      const auto t0 = Clock::now();
      placeTransitRoutesIfNeeded(roads, store, transitRoutes, population.getTotalPopulation(), serviceRadius);
      if (transitRoutes.size() != transitCache.builtForRouteCount) {
        TransitSystem::buildCache(roads, transitRoutes, transitCache);
      }
      result.timings.transitMs += elapsedMs(t0, Clock::now());
    }

    // Traffic runs on the main thread so it can safely wait for inner pool
    // tasks (parallel Dijkstra) without risking pool deadlock.
    TrafficSummary traffic;
    if (trafficActive) {
      const auto t0 = Clock::now();
      TransitOffload offload(transitRoutes, transitCache);
      TransitOffload* offloadPtr = options.enableTransit ? &offload : nullptr;
      traffic = TrafficSystem::simulateCommutes(store, population, roads, tickSeed + 2u, &pool, offloadPtr);
      result.timings.trafficMs += elapsedMs(t0, Clock::now());
      lastCongestion = traffic.maxEdgeCongestion;
      transitSummary = TransitSystem::summarize(transitRoutes, offload, traffic.commutingPopulation);
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

    // Refresh land value from the freshest available job positions, service
    // cache, and pollution field (all already up to date at this point in the
    // tick), so the economy calculation below sees current values. The
    // job-access BFS traverses the whole road network each call (the costliest
    // part of this pass), so - like services/traffic/population - it is
    // interval-gated; values persist on the map between recomputes, so a
    // skipped tick just means slightly stale figures, not stale-forever ones.
    if (tick % std::max(1, options.landValueInterval) == 0) {
      const auto t0 = Clock::now();
      LandValueSystem::updateLandValues(map, roads, store, &coverageCache, ax0, ay0, ax1, ay1);
      result.timings.landValueMs += elapsedMs(t0, Clock::now());
    }

    EconomyState economy;
    {
      const auto t0 = Clock::now();
      // Compounding price-level index from elapsed ticks; a 0 rate keeps this
      // at exactly 1.0 (default), matching prior behavior for every existing
      // caller that doesn't opt in via options.inflationRatePerTick.
      const float inflationMultiplier = std::pow(1.0f + options.inflationRatePerTick, static_cast<float>(tick));
      economy = EconomySystem::calculateEconomy(store, population, TaxRates{}, &map, TradeRates{}, inflationMultiplier);
      result.timings.economyMs += elapsedMs(t0, Clock::now());
    }

    // District policy: re-evaluate service-budget fulfillment and density per
    // district, feeding next tick's growth-chance modifiers (see
    // districtGrowthModifiers above). Reads roads/facilities/store/population
    // as they stand right now (this tick), so growth for *this* tick already
    // ran against last tick's evaluation - the same one-tick lag pattern used
    // for congestion/service satisfaction elsewhere in this loop.
    if (districts != nullptr && !districts->getDistricts().empty() &&
        (tick % std::max(1, options.districtInterval) == 0)) {
      const auto t0 = Clock::now();
      const std::vector<DistrictMetrics> districtMetrics =
        districts->evaluateAllDistricts(map, store, population, &roads, &facilities);
      const std::vector<District>& allDistricts = districts->getDistricts();
      districtGrowthModifiers.clear();
      districtGrowthModifiers.reserve(districtMetrics.size());
      for (size_t i = 0; i < districtMetrics.size() && i < allDistricts.size(); ++i) {
        const float multiplier = DistrictSystem::computeGrowthPressureMultiplier(allDistricts[i], districtMetrics[i]);
        districtGrowthModifiers.push_back({allDistricts[i].minCorner, allDistricts[i].maxCorner, multiplier});
      }
      result.finalDistrictMetrics = districtMetrics;
      result.timings.districtMs += elapsedMs(t0, Clock::now());
    }

    const CapacitySummary cap = summarize(store);
    SimTickMetrics row;
    row.tick = tick;
    row.demandResidential = demand.residential;
    row.demandCommercial = demand.commercial;
    row.demandIndustrial = demand.industrial;
    row.demandOffice = demand.office;
    row.population = population.getTotalPopulation();
    row.employed = population.getTotalEmployed();
    row.residentialBuildings = cap.resBuildings;
    row.commercialBuildings = cap.comBuildings;
    row.industrialBuildings = cap.indBuildings;
    row.officeBuildings = cap.officeBuildings;
    row.roadTiles = static_cast<uint32_t>(roads.getRoadCount());
    row.budgetBalance = economy.balance;
    row.trafficCongestion = traffic.maxEdgeCongestion;
    row.avgPollution = averageResidentialPollution(map, store);
    row.serviceCoverage = service.overallCoverage;
    row.serviceFacilities = static_cast<uint32_t>(facilities.size());
    row.avgLandValue = economy.averageLandValue;
    row.tradeBalance = economy.tradeBalance;
    row.inflationMultiplier = economy.inflationMultiplier;
    row.transitRoutes = static_cast<uint32_t>(transitRoutes.size());
    row.transitBusRoutes = transitSummary.busRoutes;
    row.transitRailRoutes = transitSummary.railRoutes;
    row.transitRidership = transitSummary.ridership;
    row.transitDemand = transitSummary.demand;
    row.transitModalShare = transitSummary.modalShare;
    if (!infinite) {
      result.rows.push_back(row);
    }
    if (options.tickCallback && !options.tickCallback(row)) {
      break;
    }
  }

  return result;
}
