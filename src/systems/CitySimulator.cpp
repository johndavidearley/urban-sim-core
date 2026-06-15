#include "src/systems/CitySimulator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

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
std::vector<Coord> zonableCandidates(const CityMap& map, Coord center, int extent) {
  const glm::ivec2 dims = map.getDimensions();
  const int x0 = std::max(0, center.x - extent);
  const int x1 = std::min(dims.x - 1, center.x + extent);
  const int y0 = std::max(0, center.y - extent);
  const int y1 = std::min(dims.y - 1, center.y + extent);

  std::vector<Coord> candidates;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const Tile& tile = map.getTile({x, y});
      if (tile.type == 2) continue;                 // water
      if (tile.zone != 0) continue;                 // already zoned
      if (tile.buildingId != 0) continue;           // already built
      if (!hasRoadAccess(map, {x, y})) continue;    // unreachable
      candidates.push_back({x, y});
    }
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
void updatePollution(CityMap& map, const EntityStore& store) {
  const glm::ivec2 dims = map.getDimensions();
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      map.getTile({x, y}).pollution = 0.0f;
    }
  }

  const int radius = 3;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    float emit = 0.0f;
    if (building.type == BuildingType::Industrial) emit = 1.0f;
    else if (building.type == BuildingType::Commercial) emit = 0.25f;
    if (emit <= 0.0f) continue;

    const Coord c = building.position;
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        const int x = c.x + dx;
        const int y = c.y + dy;
        if (!map.isValid({x, y})) continue;
        const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        if (dist > static_cast<float>(radius)) continue;
        const float contribution = emit * (1.0f - dist / static_cast<float>(radius + 1));
        Tile& tile = map.getTile({x, y});
        tile.pollution = std::min(1.0f, tile.pollution + contribution);
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
  const uint32_t popPerFacility = 150;
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
  const int serviceRadius = spacing * 3;

  for (int tick = 0; tick < ticks; ++tick) {
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

    // Refresh the pollution field, then zone land in proportion to demand,
    // steering housing to clean tiles and industry to dirty ones.
    {
      const auto t0 = Clock::now();
      updatePollution(map, store);
      const std::vector<Coord> candidates = zonableCandidates(map, center, extent);
      autoZone(map, candidates, demand, options.zoneBatchPerTick);
      result.timings.zoningMs += elapsedMs(t0, Clock::now());
    }

    // Build on zoned, road-accessible land in proportion to demand.
    {
      const auto t0 = Clock::now();
      GrowthSystem::runStep(map, store, demand, tickSeed, options.buildChance);
      result.timings.growthMs += elapsedMs(t0, Clock::now());
    }

    // Migration: residents move in (or out) gradually rather than instantly
    // filling new housing. The rate scales with city desirability - jobs being
    // plentiful, and traffic not too congested - so housing vacancy is a real
    // signal that paces both migration and further residential construction.
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
        requested = static_cast<float>(prevPop) * 0.98f;  // stagnant city slowly loses residents
      } else {
        requested = static_cast<float>(prevPop) + 0.25f * desirability * headroom + 3.0f * desirability;
      }
      const uint32_t requestedPop = static_cast<uint32_t>(
        std::max(0.0f, std::min(static_cast<float>(cap.resCapacity), requested)));
      PopulationSystem::allocate(store, population, requestedPop, tickSeed + 1u);
      result.timings.populationMs += elapsedMs(t0, Clock::now());
    }

    // Provide public services as the city grows; coverage feeds desirability.
    ServiceCoverageSummary service;
    {
      const auto t0 = Clock::now();
      placeFacilitiesIfNeeded(map, facilities, population.getTotalPopulation(), center, extent, serviceRadius);
      service = ServiceSystem::evaluateCoverage(store, roads, facilities);
      lastServiceSatisfaction = service.satisfaction;
      result.timings.serviceMs += elapsedMs(t0, Clock::now());
    }

    TrafficSummary traffic;
    if (options.runTraffic) {
      const auto t0 = Clock::now();
      traffic = TrafficSystem::simulateCommutes(store, population, roads, tickSeed + 2u);
      result.timings.trafficMs += elapsedMs(t0, Clock::now());
      lastCongestion = traffic.maxEdgeCongestion;
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
    result.rows.push_back(row);
  }

  return result;
}
