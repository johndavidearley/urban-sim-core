#include "src/systems/CitySimSupport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <vector>

#include "src/entities/BuildingPartitions.hpp"
#include "src/networks/Pathfinding.hpp"

namespace city_sim {
CapacitySummary summarize(const EntityStore& store) {
  // O(1): EntityStore maintains per-type counts/capacities on structural edits.
  CapacitySummary cap;
  cap.resCapacity = store.capacityOfType(BuildingType::Residential);
  cap.comCapacity = store.capacityOfType(BuildingType::Commercial);
  cap.indCapacity = store.capacityOfType(BuildingType::Industrial);
  cap.officeCapacity = store.capacityOfType(BuildingType::Office);
  cap.resBuildings = store.countOfType(BuildingType::Residential);
  cap.comBuildings = store.countOfType(BuildingType::Commercial);
  cap.indBuildings = store.countOfType(BuildingType::Industrial);
  cap.officeBuildings = store.countOfType(BuildingType::Office);
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

// A buildable, road-adjacent, currently-unzoned tile: the same qualifying
// condition zonableCandidates checked per-tile before this became an
// incremental index (see extendZoningCandidates below).
bool isZoningCandidate(const CityMap& map, Coord pos) {
  const Tile& tile = map.getTile(pos);
  if (tile.type == 2 || tile.zone != 0 || tile.buildingId != 0) return false;
  return hasRoadAccess(map, pos);
}

// Scans an explicit box (not necessarily centered on anything) for candidate
// tiles and inserts any found into `out`. Parallelized across row chunks,
// same approach as the pollution field update above.
void scanBoxForCandidates(const CityMap& map, int x0, int y0, int x1, int y1,
                          ThreadPool& pool, ZoningCandidateIndex& out) {
  if (x0 > x1 || y0 > y1) return;

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
          if (isZoningCandidate(map, {x, y})) partial.push_back({x, y});
        }
      }
      return partial;
    }));
  }

  for (auto& f : futs) {
    auto partial = f.get();
    for (const Coord& c : partial) {
      out.add(c);
    }
  }
}

// Extends a persistent, incrementally-maintained candidate index (a spatial
// hash keyed by tile coordinate, replacing a full O(extent^2) rescan every
// tick) to cover growth from `oldExtent` to `newExtent` around `center`.
// Zoning candidacy can only be lost by autoZone actually zoning a tile (the
// caller removes those directly - see the zoning step in run()) and can
// only be gained by a tile becoming newly road-accessible. Roads in this
// loop only ever grow outward in fixed steps via layRoadGrid, so any tile
// that just became road-accessible lies within the new ring between
// oldExtent and newExtent - shrunk by one tile of buffer on the inside,
// since a tile just inside the old boundary can gain road access from a
// brand new neighbor just outside it. That keeps each call's cost
// proportional to the new ring's area, not the whole developed region.
void extendZoningCandidates(const CityMap& map, Coord center, int oldExtent, int newExtent,
                            ThreadPool& pool, ZoningCandidateIndex& out) {
  if (newExtent <= oldExtent) return;
  const glm::ivec2 dims = map.getDimensions();
  const int ox0 = std::max(0, center.x - newExtent);
  const int ox1 = std::min(dims.x - 1, center.x + newExtent);
  const int oy0 = std::max(0, center.y - newExtent);
  const int oy1 = std::min(dims.y - 1, center.y + newExtent);

  if (oldExtent <= 0) {
    // Nothing scanned yet - no interior to exclude.
    scanBoxForCandidates(map, ox0, oy0, ox1, oy1, pool, out);
    return;
  }

  const int inner = oldExtent - 1;
  const int ix0 = std::max(0, center.x - inner);
  const int ix1 = std::min(dims.x - 1, center.x + inner);
  const int iy0 = std::max(0, center.y - inner);
  const int iy1 = std::min(dims.y - 1, center.y + inner);

  // Picture-frame decomposition of (new box) minus (inner box): top band,
  // bottom band, then left/right bands spanning only the inner y-range so
  // the four pieces never overlap.
  scanBoxForCandidates(map, ox0, oy0, ox1, iy0 - 1, pool, out);  // top
  scanBoxForCandidates(map, ox0, iy1 + 1, ox1, oy1, pool, out);  // bottom
  scanBoxForCandidates(map, ox0, iy0, ix0 - 1, iy1, pool, out);  // left
  scanBoxForCandidates(map, ix1 + 1, iy0, ox1, iy1, pool, out);  // right
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
            map.pollution({x, y}) = 0.0f;
          }
        }
      }));
    }
    for (auto& f : futs) f.get();
  }

  // Sequential scatter using the LUT (no sqrt in the hot loop). Only
  // industrial/commercial emit; use type indices instead of a full store walk.
  auto scatterType = [&](BuildingType type, float emit) {
    for (EntityId id : store.idsByBuildingType(type)) {
      const Building* building = store.getBuilding(id);
      if (building == nullptr) continue;
      const Coord c = building->position;
      for (int dy = -kRadius; dy <= kRadius; ++dy) {
        for (int dx = -kRadius; dx <= kRadius; ++dx) {
          const float w = lut[dy + kRadius][dx + kRadius];
          if (w <= 0.0f) continue;
          const int tx = c.x + dx;
          const int ty = c.y + dy;
          if (tx < x0 || tx > x1 || ty < y0 || ty > y1) continue;
          float& pollution = map.pollution({tx, ty});
          pollution = std::min(1.0f, pollution + emit * w);
        }
      }
    }
  };
  scatterType(BuildingType::Industrial, 1.0f);
  scatterType(BuildingType::Commercial, 0.25f);
}

void applyWastePollution(CityMap& map, float penalty, int x0, int y0, int x1, int y1) {
  if (penalty <= 0.0f || x0 > x1 || y0 > y1) {
    return;
  }
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const Coord coord{x, y};
      if (map.zone(coord) != 0) {
        map.pollution(coord) = std::min(1.0f, map.pollution(coord) + penalty * 0.01f);
      }
    }
  }
}

float averageResidentialPollution(const CityMap& map, const EntityStore& store) {
  double sum = 0.0;
  uint32_t n = 0;
  for (EntityId id : store.idsByBuildingType(BuildingType::Residential)) {
    const Building* building = store.getBuilding(id);
    if (building == nullptr) continue;
    sum += map.pollution(building->position);
    ++n;
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

// Keep roughly one facility per `popPerFacility` residents per type, cycling
// through the service types so coverage of each grows together. Power/Water/
// Sanitation
// are opt-in (includeUtilities, SimOptions::enableUtilities) - when off, the
// type cycle and total count are exactly what they were before Power/Water
// existed. When on, the target count scales up proportionally (7/4 of the
// base) so Fire/Police/Health/Education density is unaffected - utilities
// are added on top, not diluted from the original four's share.
void placeFacilitiesIfNeeded(
  const CityMap& map,
  std::vector<ServiceFacility>& facilities,
  uint32_t population,
  Coord center,
  int extent,
  int coverageRadius,
  bool includeUtilities,
  bool includeWasteDeathcare
) {
  const uint32_t popPerFacility = 1000;
  // Civic type cycle stays Fire/Police/Health/Education (+ utilities when
  // opted in). Waste/deathcare are layered on separately so enabling them
  // does not dilute the original four's share of the population budget —
  // only cycle types (0..typeCount-1) count toward the placement target.
  const int typeCount = includeUtilities ? 7 : 4;
  const size_t target = static_cast<size_t>(population / popPerFacility) * static_cast<size_t>(typeCount) / 4;
  const auto countType = [&](ServiceType type) {
    return static_cast<size_t>(std::count_if(facilities.begin(), facilities.end(),
      [type](const ServiceFacility& f) { return f.type == type; }));
  };
  const auto countCycleTypes = [&]() {
    size_t n = 0;
    for (const ServiceFacility& f : facilities) {
      if (static_cast<int>(f.type) >= 0 && static_cast<int>(f.type) < typeCount) {
        ++n;
      }
    }
    return n;
  };
  const auto placeType = [&](ServiceType type) {
    ServiceFacility facility;
    facility.type = type;
    facility.position = chooseFacilitySite(map, facilities, center, extent);
    facility.maxTravelDistance = coverageRadius;
    facility.quality = 1.0f;
    facilities.push_back(facility);
  };
  const auto ensureType = [&](ServiceType type) {
    if (countType(type) == 0) {
      placeType(type);
    }
  };

  while (countCycleTypes() < target) {
    placeType(static_cast<ServiceType>(countCycleTypes() % static_cast<size_t>(typeCount)));
  }

  // Fire/Police/Health/Education are soft coverage stats, so it's harmless
  // for them to wait for population to reach popPerFacility before the first
  // one appears. Power/Water are a hard growth gate (see GrowthSystem's
  // requireUtilities check) - if they waited on the same threshold, a
  // cold-start city could never grow past its first few buildings, since
  // population can't reach popPerFacility without more buildings. Guarantee
  // at least one of each utility type exists from the very first placement
  // pass, independent of population.
  if (includeUtilities) {
    ensureType(ServiceType::Power);
    ensureType(ServiceType::Water);
    ensureType(ServiceType::Sanitation);
  }

  // Garbage/Cemetery scale with population so waste + deathcare systems have
  // real capacity in autonomous CLI/G-mode runs. Recycling and Crematorium
  // remain player-placed extras.
  //
  // Delay until the civic cycle has started (same population gate as the
  // first Fire station). Otherwise Garbage claims the preferred center site
  // first and later civic facilities are pushed to the map edge with no
  // road coverage of the developed core.
  if (includeWasteDeathcare && countCycleTypes() > 0) {
    const size_t garbageTarget =
      std::max<size_t>(1u, static_cast<size_t>(population / 1500u));
    const size_t cemeteryTarget =
      std::max<size_t>(1u, static_cast<size_t>(population / 3000u));
    while (countType(ServiceType::Garbage) < garbageTarget) {
      placeType(ServiceType::Garbage);
    }
    while (countType(ServiceType::Cemetery) < cemeteryTarget) {
      placeType(ServiceType::Cemetery);
    }
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

// Updates Tile::connectedToPower/connectedToWater for every tile in the
// active region from the coverage cache's type-restricted merges (see
// ServiceCoverageCache::nearestPowerDistance/nearestWaterDistance) - only
// called when options.enableUtilities is set, so a caller that never opts
// in leaves both at their CityMap-constructor default (true, the M7
// utility stub) forever. Parallelized across row chunks like
// updatePollution above - each tile's result is independent.
void updateUtilityConnectivity(
  CityMap& map,
  const RoadNetwork& roads,
  const ServiceCoverageCache& cache,
  int x0, int y0, int x1, int y1,
  ThreadPool& pool
) {
  const int nRows = y1 - y0 + 1;
  const int minRowsPerChunk = 16;
  const int nChunks = (nRows >= minRowsPerChunk * 2)
    ? std::max(1, std::min(static_cast<int>(pool.threadCount()), nRows / minRowsPerChunk))
    : 1;
  const int rowsPerChunk = (nRows + nChunks - 1) / nChunks;

  std::vector<std::future<void>> futs;
  futs.reserve(static_cast<size_t>(nChunks));
  for (int c = 0; c < nChunks; ++c) {
    const int ry0 = y0 + c * rowsPerChunk;
    const int ry1 = std::min(y0 + (c + 1) * rowsPerChunk - 1, y1);
    if (ry0 > ry1) break;
    futs.push_back(pool.submit([&map, &roads, &cache, x0, x1, ry0, ry1]() {
      for (int y = ry0; y <= ry1; ++y) {
        for (int x = x0; x <= x1; ++x) {
          Tile& tile = map.getTile({x, y});
          if (tile.type == 2) continue;  // water tiles have no utility concept
          Coord anchor;
          const bool anchored = roads.resolveRoadAnchor({x, y}, anchor);
          tile.connectedToPower = anchored && cache.nearestPowerDistance.count(anchor) != 0;
          tile.connectedToWater = anchored && cache.nearestWaterDistance.count(anchor) != 0;
        }
      }
    }));
  }
  for (auto& f : futs) f.get();
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

  const BuildingPartitions parts = BuildingPartitions::fromStore(store, /*sortById=*/true);
  const std::vector<const Building*>& residential = parts.residential;
  const std::vector<const Building*>& jobs = parts.jobs;
  if (residential.empty() || jobs.empty()) return;

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
    Coord bestHomeAnchor{};
    long bestScore = -1;
    for (const Building* home : residential) {
      Coord homeAnchor;
      if (!roads.resolveRoadAnchor(home->position, homeAnchor)) continue;
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
        bestHomeAnchor = homeAnchor;
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

    // Buildings sit next to roads, not necessarily on them (see
    // hasRoadAccess's self-or-neighbor rule) - route between resolved road
    // anchors, not raw building positions, so a route can actually connect
    // through a building whose own tile has no road edge.
    Pathfinding::Path path;
    bool connected = false;
    for (const Building* job : jobsOrdered) {
      Coord jobAnchor;
      if (!roads.resolveRoadAnchor(job->position, jobAnchor)) continue;
      path = Pathfinding::findShortestPath(roads, bestHomeAnchor, jobAnchor);
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
  int stopCoverageRadius,
  float capacityMultiplier
) {
  // Scales both vehicleCount and capacityPerVehicle for newly-placed routes
  // (a route already placed at the old capacity is not retroactively
  // resized - this only affects routes placed from here on). 1.0 reproduces
  // the literal constants below exactly, matching prior behavior for every
  // existing caller.
  const auto scale = [capacityMultiplier](int base) {
    return std::max(1, static_cast<int>(std::lround(base * capacityMultiplier)));
  };

  // Bus: short local hops to the nearest job, denser placement, modest
  // per-vehicle capacity - matches TransitRoute's own defaults.
  placeTransitRoutesOfModeIfNeeded(
    roads, store, routes, population, TransitMode::Bus,
    /*popPerRoute=*/1200, /*maxOfMode=*/16, stopCoverageRadius,
    /*vehicleCount=*/scale(2), /*capacityPerVehicle=*/scale(30), /*stopSpacing=*/2,
    /*connectToFarthestJob=*/false
  );

  // Rail: a long trunk line to the farthest job, sparse (a city only
  // justifies a handful of lines), much higher capacity and a wider
  // catchment - riders travel farther to reach a station than a bus stop.
  placeTransitRoutesOfModeIfNeeded(
    roads, store, routes, population, TransitMode::Rail,
    /*popPerRoute=*/4000, /*maxOfMode=*/3, stopCoverageRadius * 2,
    /*vehicleCount=*/scale(6), /*capacityPerVehicle=*/scale(150), /*stopSpacing=*/1,
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
    const float pa = map.pollution(a);
    const float pb = map.pollution(b);
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

    map.setZone(batchTiles[i], static_cast<int>(zone));
    map.landValue(batchTiles[i]) = Zoning::defaultLandValueForZone(zone);
  }
}

ConstructionRegion expandConstruction(
  CityMap& map,
  RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population,
  std::vector<ServiceFacility>& facilities,
  std::vector<TransitRoute>& transitRoutes,
  ConstructionState& state,
  ThreadPool& pool,
  const ZoneDemand& demand,
  const ConstructionOptions& options
) {
  using Clock = std::chrono::steady_clock;
  const auto elapsedMs = [](Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };

  const int spacing = std::max(2, options.gridSpacing);
  const Coord center = cityCenter(map);
  const glm::ivec2 dims = map.getDimensions();
  const int maxExtent = std::max(dims.x, dims.y);

  if (state.extent < state.zoningCandidatesExtent) {
    state.zoningCandidates = ZoningCandidateIndex{};
    state.zoningCandidatesExtent = 0;
  }

  ConstructionRegion region;
  region.center = center;
  region.serviceRadius = spacing * 3;

  {
    const auto t0 = Clock::now();
    if (state.extent <= 0) {
      state.extent = std::min(maxExtent, spacing * 2);
      layRoadGrid(map, roads, center, state.extent, spacing);
      roads.updateConnectivity(center);
    }

    const float overallDemand = std::max({
      demand.residential, demand.commercial, demand.industrial, demand.office});
    const bool wantsRoom =
      state.emptyZonedCount < static_cast<int64_t>(3 * options.zoneBatchPerTick);
    if (overallDemand > 0.12f && wantsRoom && state.extent < maxExtent) {
      state.extent = std::min(maxExtent, state.extent + spacing);
      layRoadGrid(map, roads, center, state.extent, spacing);
      roads.updateConnectivity(center);
    }
    region.roadMs = elapsedMs(t0, Clock::now());
  }

  region.extent = state.extent;
  region.ax0 = std::max(0, center.x - state.extent);
  region.ay0 = std::max(0, center.y - state.extent);
  region.ax1 = std::min(dims.x - 1, center.x + state.extent);
  region.ay1 = std::min(dims.y - 1, center.y + state.extent);

  {
    const auto t0 = Clock::now();
    if (options.refreshPollution) {
      updatePollution(map, store, region.ax0, region.ay0, region.ax1, region.ay1, pool);
    }

    extendZoningCandidates(
      map, center, state.zoningCandidatesExtent, state.extent, pool, state.zoningCandidates);
    state.zoningCandidatesExtent = state.extent;

    std::vector<Coord>& candidates = state.zoningCandidates.list;
    const int batchLimit = std::min(static_cast<int>(candidates.size()), options.zoneBatchPerTick);
    if (batchLimit > 0) {
      std::partial_sort(candidates.begin(), candidates.begin() + batchLimit, candidates.end(),
        [center](const Coord& a, const Coord& b) {
          const int da = (a.x - center.x) * (a.x - center.x) + (a.y - center.y) * (a.y - center.y);
          const int db = (b.x - center.x) * (b.x - center.x) + (b.y - center.y) * (b.y - center.y);
          if (da != db) return da < db;
          if (a.y != b.y) return a.y < b.y;
          return a.x < b.x;
        });
    }

    autoZone(map, candidates, demand, options.zoneBatchPerTick, options.districts);

    bool removed = false;
    for (int i = 0; i < batchLimit; ++i) {
      if (map.zone(candidates[i]) != 0) {
        state.zoningCandidates.erase(candidates[i]);
        removed = true;
        ++state.emptyZonedCount;
      }
    }
    if (removed) {
      state.zoningCandidates.compact();
    }

    if (options.placeFacilities) {
      placeFacilitiesIfNeeded(
        map, facilities, population.getTotalPopulation(), center, state.extent,
        region.serviceRadius, options.includeUtilities, options.includeWasteDeathcare);
    }
    if (options.placeTransit) {
      placeTransitRoutesIfNeeded(
        roads, store, transitRoutes, population.getTotalPopulation(),
        region.serviceRadius, options.transitCapacityMultiplier);
    }
    region.zoningMs = elapsedMs(t0, Clock::now());
  }

  return region;
}

}  // namespace city_sim
