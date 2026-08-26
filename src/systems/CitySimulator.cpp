#include "src/systems/CitySimulator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <thread>
#include <vector>

#include "src/core/ThreadPool.hpp"
#include "src/systems/CitySimSupport.hpp"
#include "src/systems/CrimeSystem.hpp"
#include "src/systems/DeathcareSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/LandValueSystem.hpp"
#include "src/systems/MetricsSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/systems/WasteSystem.hpp"

namespace {

using Clock = std::chrono::steady_clock;
double elapsedMs(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

}  // namespace

ZoneDemand CitySimulator::evaluateDemand(const EntityStore& store, const PopulationStore& population) {
  const city_sim::CapacitySummary cap = city_sim::summarize(store);
  const float pop = static_cast<float>(population.getTotalPopulation());
  const float employed = static_cast<float>(population.getTotalEmployed());
  const float jobCapacity = static_cast<float>(cap.comCapacity + cap.indCapacity);

  (void)jobCapacity;
  ZoneDemand demand;

  // The population system employs everyone up to total job capacity, so a
  // healthy, attractive city needs roughly one job per resident, split between
  // commercial and industrial (~0.5 of population each).
  const float unemployment = pop > 0.0f ? city_sim::clamp01((pop - employed) / pop) : 0.0f;

  // Residential: build housing when the city is attractive (jobs plentiful) AND
  // existing homes are filling up. Empty housing (low occupancy) suppresses
  // further construction so housing tracks the migrating population rather than
  // racing ahead of it. A startup floor seeds the first homes while tiny.
  const float attractiveness = city_sim::attractivenessFromUnemployment(unemployment);
  const float housingOccupancy = cap.resCapacity > 0 ? city_sim::clamp01(pop / static_cast<float>(cap.resCapacity)) : 0.0f;
  const float resStartup = (cap.resCapacity < 40) ? 0.8f : 0.0f;
  demand.residential = city_sim::clamp01(std::max(resStartup, attractiveness * housingOccupancy));

  // Commercial: retail jobs serving residents. Target ~50% of population.
  const float comTarget = 0.5f * pop;
  demand.commercial = comTarget > 0.0f ? city_sim::clamp01((comTarget - static_cast<float>(cap.comCapacity)) / comTarget) : 0.0f;

  // Industrial: base/export jobs. Target ~50% of population, with a startup
  // floor so the first jobs appear once anyone has moved in.
  const float indTarget = 0.5f * pop;
  const float indGap = indTarget > 0.0f ? city_sim::clamp01((indTarget - static_cast<float>(cap.indCapacity)) / indTarget) : 0.0f;
  const float indStartup = (cap.indCapacity < 24 && cap.resCapacity > 0) ? 0.6f : 0.0f;
  demand.industrial = city_sim::clamp01(std::max(indStartup, indGap));

  // Office: higher-tier white-collar employment that follows commercial
  // establishment rather than leading it (office towers/parks cluster around
  // an already-formed retail/downtown base, in reality). Targets a smaller
  // share of population than retail/industrial, and is gated by how
  // established commercial capacity already is relative to its own target -
  // a city with no commercial base yet generates no office demand.
  const float officeTarget = 0.25f * pop;
  const float commercialEstablishment = comTarget > 0.0f
    ? city_sim::clamp01(static_cast<float>(cap.comCapacity) / comTarget) : 0.0f;
  const float officeGap = officeTarget > 0.0f
    ? city_sim::clamp01((officeTarget - static_cast<float>(cap.officeCapacity)) / officeTarget) : 0.0f;
  demand.office = city_sim::clamp01(officeGap * commercialEstablishment);

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

  float lastCongestion = 0.0f;          // previous tick's peak congestion, feeds desirability
  float lastServiceSatisfaction = 0.5f; // previous tick's service satisfaction, feeds desirability
  float lastCrimeRate = 0.0f;           // previous tick's crime rate, feeds desirability
  float lastIllnessRate = 0.0f;         // previous tick's illness rate, feeds desirability
  std::vector<ServiceFacility> facilities;
  ServiceCoverageCache coverageCache;
  std::vector<TransitRoute> transitRoutes;
  TransitCoverageCache transitCache;
  TrafficRouteCache trafficRouteCache;  // topology-only paths, reused across ticks until roads change
  // Growth chance modifiers derived from the *previous* tick's district
  // metrics (one-tick lag, like lastCongestion/lastServiceSatisfaction below)
  // - empty when districts is null, which GrowthSystem treats as a no-op.
  std::vector<GrowthChanceModifier> districtGrowthModifiers;
  std::vector<BurningTile> burningTiles;         // persists only when options.enableDisasters
  uint32_t cumulativeBuildingsLostToFire = 0;
  uint32_t cumulativeBuildingsLostToDisaster = 0;
  city_sim::ConstructionState construction;
  DeathcareState deathcareState;  // fractional deaths + disposition backlog across ticks
  // Hoisted so interval skips reuse last summaries instead of zeroing
  // downstream coverage (crime/health/waste/deathcare/metrics).
  TrafficSummary traffic;
  ServiceCoverageSummary service;
  TransitSummary transitSummary;
  EconomyState economy;
  float lastResPollution = 0.0f;

  // Thread pool for parallel pathfinding (traffic) and building coverage
  // (services), and for running both concurrently within each tick.
  // Sized to hardware concurrency minus 1 (main thread drives the loop).
  const unsigned int hwc = std::thread::hardware_concurrency();
  ThreadPool pool(std::max(1u, hwc > 0 ? hwc - 1 : 3u));

  for (int tick = 0; infinite || tick < ticks; ++tick) {
    const uint32_t tickSeed = seed + static_cast<uint32_t>(tick);

    const ZoneDemand demand = evaluateDemand(store, population);

    city_sim::ConstructionOptions constructionOpts;
    constructionOpts.gridSpacing = spacing;
    constructionOpts.zoneBatchPerTick = options.zoneBatchPerTick;
    constructionOpts.districts = districts;
    const city_sim::ConstructionRegion region = city_sim::expandConstruction(
      map, roads, store, population, facilities, transitRoutes,
      construction, pool, demand, constructionOpts);
    result.timings.roadMs += region.roadMs;
    result.timings.zoningMs += region.zoningMs;

    // Active region bounds — all tile-scanning passes clamp to this box so the
    // work scales with the developed area rather than the full map.
    const Coord center = region.center;
    const int extent = region.extent;
    const int ax0 = region.ax0;
    const int ay0 = region.ay0;
    const int ax1 = region.ax1;
    const int ay1 = region.ay1;
    const int serviceRadius = region.serviceRadius;

    // Build on zoned, road-accessible land in proportion to demand. District
    // growth-pressure modifiers (if any) come from the previous tick's
    // service-budget/density evaluation below.
    {
      const auto t0 = Clock::now();
      const GrowthStats growth = GrowthSystem::runStep(map, store, demand, tickSeed, options.buildChance,
                            &districtGrowthModifiers, {ax0, ay0}, {ax1, ay1}, &pool,
                            options.enableUtilities);
      // Spawn fills empty zoned tiles; demolition frees them. Redevelopment
      // is excluded from both counters inside GrowthSystem.
      city_sim::applyEmptyZonedDelta(
        construction,
        static_cast<int64_t>(growth.totalDemolished())
          - static_cast<int64_t>(growth.totalSpawned()));
      result.timings.growthMs += elapsedMs(t0, Clock::now());
    }

    // Capacity and residential pollution after growth: reused for population,
    // health, and metrics later in the tick so we do not re-walk every
    // building three times. Recomputed only if disasters destroy buildings.
    city_sim::CapacitySummary tickCap = city_sim::summarize(store);
    float tickResPollution = city_sim::averageResidentialPollution(map, store);

    // Migration: residents move in (or out) gradually rather than instantly
    // filling new housing. The rate scales with city desirability - jobs being
    // plentiful, and traffic not too congested - so housing vacancy is a real
    // signal that paces both migration and further residential construction.
    // The desirability computation and requested-population math run every tick
    // (cheap arithmetic); the expensive full allocation only runs on the interval.
    {
      const auto t0 = Clock::now();
      const uint32_t prevPop = population.getTotalPopulation();
      const uint32_t prevEmployed = population.getTotalEmployed();
      const float unemployment = prevPop > 0 ? city_sim::clamp01(static_cast<float>(prevPop - prevEmployed) / prevPop) : 0.0f;
      float desirability = city_sim::attractivenessFromUnemployment(unemployment);
      desirability *= city_sim::clamp01(1.0f - 0.4f * lastCongestion);
      desirability *= city_sim::clamp01(1.0f - 0.5f * tickResPollution);
      desirability *= city_sim::clamp01(0.6f + 0.4f * lastServiceSatisfaction);
      desirability *= city_sim::clamp01(1.0f - 0.25f * lastCrimeRate);
      desirability *= city_sim::clamp01(1.0f - 0.3f * lastIllnessRate);

      const float headroom = tickCap.resCapacity > prevPop
        ? static_cast<float>(tickCap.resCapacity - prevPop) : 0.0f;
      float requested;
      if (desirability < 0.05f && prevPop > 0) {
        requested = static_cast<float>(prevPop) * 0.98f;
      } else {
        requested = static_cast<float>(prevPop) + 0.25f * desirability * headroom + 3.0f * desirability;
      }
      const uint32_t requestedPop = static_cast<uint32_t>(
        std::max(0.0f, std::min(static_cast<float>(tickCap.resCapacity), requested)));

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

    // Service prep: facility placement and BFS cache rebuild are state-mutating
    // and must complete before the concurrent evaluation starts.
    if (serviceActive) {
      const auto t0 = Clock::now();
      city_sim::placeFacilitiesIfNeeded(map, facilities, population.getTotalPopulation(), center, extent, serviceRadius,
                              options.enableUtilities, /*includeWasteDeathcare=*/true);
      if (!ServiceSystem::isCacheValid(roads, facilities, coverageCache)) {
        ServiceSystem::buildCache(roads, facilities, coverageCache);
      }
      if (options.enableUtilities) {
        city_sim::updateUtilityConnectivity(map, roads, coverageCache, ax0, ay0, ax1, ay1, pool);
      }
      result.timings.serviceMs += elapsedMs(t0, Clock::now());
    }

    // Submit service evaluation to a pool worker so it runs concurrently with
    // the traffic pathfinding that follows on the main thread.
    // Skip if neither the building coordinates nor distance fields changed.
    std::future<std::pair<ServiceCoverageSummary, double>> serviceFuture;
    if (serviceActive && !ServiceSystem::isResultCacheValid(store, coverageCache)) {
      serviceFuture = pool.submit([&store, &roads, &coverageCache]() {
        const auto t0 = Clock::now();
        // This task already occupies a pool worker. Do not recursively submit
        // slices to the same pool: a one-worker pool would deadlock waiting for
        // work that cannot start.
        auto svc = ServiceSystem::evaluateFromCache(store, roads, coverageCache, nullptr);
        ServiceSystem::storeCachedResult(store, svc, coverageCache);
        return std::make_pair(svc, elapsedMs(t0, Clock::now()));
      });
    }

    // Transit prep: route placement and BFS cache rebuild are state-mutating,
    // like facility placement above, and must complete before traffic uses
    // them. Routes only grow (never removed), so this is cheap once the
    // route count has caught up to its population-based target.
    if (options.enableTransit && trafficActive) {
      const auto t0 = Clock::now();
      city_sim::placeTransitRoutesIfNeeded(roads, store, transitRoutes, population.getTotalPopulation(), serviceRadius,
                                 options.transitCapacityMultiplier);
      if (transitRoutes.size() != transitCache.builtForRouteCount) {
        TransitSystem::buildCache(roads, transitRoutes, transitCache);
      }
      result.timings.transitMs += elapsedMs(t0, Clock::now());
    }

    // Traffic runs on the main thread so it can safely wait for inner pool
    // tasks (parallel Dijkstra) without risking pool deadlock.
    if (trafficActive) {
      const auto t0 = Clock::now();
      TransitOffload offload(transitRoutes, transitCache);
      TransitOffload* offloadPtr = options.enableTransit ? &offload : nullptr;
      traffic = TrafficSystem::simulateCommutes(store, population, roads, tickSeed + 2u, &pool, offloadPtr, &trafficRouteCache);
      result.timings.trafficMs += elapsedMs(t0, Clock::now());
      lastCongestion = traffic.maxEdgeCongestion;
      transitSummary = TransitSystem::summarize(transitRoutes, offload, traffic.commutingPopulation);
    }

    // Collect the service result (likely already done while traffic ran).
    // If this tick skipped the service pass, `service` keeps the previous
    // tick's summary so crime/health/waste/deathcare do not see zero coverage.
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

    // Disasters: fire ignition/spread, gated off by default (destructive,
    // unlike the additive M12/M13 systems). Reuses service.fireCoverage -
    // already computed above - as a city-wide response-speed proxy instead
    // of a second per-tile BFS pass.
    FireSummary fire;
    DisasterSummary disaster;
    if (options.enableDisasters) {
      const auto t0 = Clock::now();
      fire = FireSystem::step(map, store, burningTiles, service.fireCoverage, tickSeed + 5u, options.fireParams);
      cumulativeBuildingsLostToFire += fire.buildingsDestroyed;
      result.timings.fireMs += elapsedMs(t0, Clock::now());

      const auto t1 = Clock::now();
      disaster = NaturalDisasterSystem::step(map, store, tickSeed + 6u, options.disasterParams);
      cumulativeBuildingsLostToDisaster += disaster.buildingsDestroyed;
      result.timings.disasterMs += elapsedMs(t1, Clock::now());

      // Building set changed: refresh the shared post-growth snapshots so
      // health and metrics rows match the store after destruction.
      if (fire.buildingsDestroyed > 0 || disaster.buildingsDestroyed > 0) {
        city_sim::applyEmptyZonedDelta(
          construction,
          static_cast<int64_t>(fire.buildingsDestroyed)
            + static_cast<int64_t>(disaster.buildingsDestroyed));
        tickCap = city_sim::summarize(store);
        tickResPollution = city_sim::averageResidentialPollution(map, store);
      }
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

    {
      const auto t0 = Clock::now();
      // Compounding price-level index from elapsed ticks; a 0 rate keeps this
      // at exactly 1.0 (default), matching prior behavior for every existing
      // caller that doesn't opt in via options.inflationRatePerTick.
      const float inflationMultiplier = std::pow(1.0f + options.inflationRatePerTick, static_cast<float>(tick));
      EconomyLandValueBounds lvBounds;
      lvBounds.useBounds = true;
      lvBounds.x0 = ax0;
      lvBounds.y0 = ay0;
      lvBounds.x1 = ax1;
      lvBounds.y1 = ay1;
      economy = EconomySystem::calculateEconomy(
        store, population, TaxRates{}, &map, TradeRates{}, inflationMultiplier, lvBounds);
      result.timings.economyMs += elapsedMs(t0, Clock::now());
    }

    // Crime: a pure read-out (no side effects on the map or entity store,
    // unlike FireSystem), so unlike congestion/service satisfaction it's
    // computed unconditionally every tick from this tick's own unemployment,
    // police coverage, and average land value (a poverty proxy). It still
    // feeds desirability one tick lagged (via lastCrimeRate above), the same
    // pattern used for congestion/service satisfaction elsewhere in this loop.
    CrimeSummary crime;
    {
      const auto t0 = Clock::now();
      const uint32_t popNow = population.getTotalPopulation();
      const uint32_t employedNow = population.getTotalEmployed();
      const float unemploymentNow = popNow > 0
        ? city_sim::clamp01(static_cast<float>(popNow - employedNow) / static_cast<float>(popNow)) : 0.0f;
      crime = CrimeSystem::evaluate(unemploymentNow, service.policeCoverage, economy.averageLandValue, options.crimeParams);
      lastCrimeRate = crime.overallRate;
      result.timings.crimeMs += elapsedMs(t0, Clock::now());
    }

    // Health: a pure read-out, exactly like Crime above (no side effects on
    // the map or entity store). Housing crowding (population relative to
    // residential capacity) is a density/contagion proxy, combined with this
    // tick's residential pollution and hospital coverage. Feeds desirability
    // one tick lagged (via lastIllnessRate above), same pattern as crime.
    HealthSummary health;
    {
      const auto t0 = Clock::now();
      const float housingDensity = tickCap.resCapacity > 0
        ? city_sim::clamp01(static_cast<float>(population.getTotalPopulation()) / static_cast<float>(tickCap.resCapacity)) : 0.0f;
      health = HealthSystem::evaluate(housingDensity, tickResPollution, service.healthCoverage, options.healthParams);
      lastIllnessRate = health.illnessRate;
      result.timings.healthMs += elapsedMs(t0, Clock::now());
    }

    // Waste + deathcare: same systems as the playable visualizer path. Safe when
    // no garbage/cemetery facilities exist (collection rate stays 1.0 / no
    // processing capacity). Pollution from uncollected waste updates the map
    // so land value and residential pollution see it next tick.
    WasteSummary waste;
    {
      const auto t0 = Clock::now();
      waste = WasteSystem::evaluate(store, facilities, service);
      if (waste.pollutionPenalty > 0.0f) {
        city_sim::applyWastePollution(map, waste.pollutionPenalty, ax0, ay0, ax1, ay1);
        tickResPollution = city_sim::averageResidentialPollution(map, store);
      }
      result.timings.wasteMs += elapsedMs(t0, Clock::now());
    }

    DeathcareSummary deathcare;
    {
      const auto t0 = Clock::now();
      deathcare = DeathcareSystem::step(
        population, facilities, service, health.illnessRate, tickResPollution, deathcareState
      );
      // applyDeaths already shrunk PopulationStore; re-sync building occupancy
      // to the new total so the next allocate/growth pass sees consistent stock.
      if (deathcare.deaths > 0) {
        PopulationSystem::allocate(
          store, population, population.getTotalPopulation(), tickSeed + 7u
        );
        tickCap = city_sim::summarize(store);
      }
      result.timings.deathcareMs += elapsedMs(t0, Clock::now());
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

    SimTickMetrics row;
    row.tick = tick;
    row.demandResidential = demand.residential;
    row.demandCommercial = demand.commercial;
    row.demandIndustrial = demand.industrial;
    row.demandOffice = demand.office;
    row.population = population.getTotalPopulation();
    row.employed = population.getTotalEmployed();
    row.residentialBuildings = tickCap.resBuildings;
    row.commercialBuildings = tickCap.comBuildings;
    row.industrialBuildings = tickCap.indBuildings;
    row.officeBuildings = tickCap.officeBuildings;
    row.roadTiles = static_cast<uint32_t>(roads.getRoadCount());
    row.budgetBalance = economy.balance;
    row.trafficCongestion = traffic.maxEdgeCongestion;
    lastResPollution = tickResPollution;
    row.avgPollution = tickResPollution;
    row.serviceCoverage = service.overallCoverage;
    row.sanitationCoverage = service.sanitationCoverage;
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
    row.activeFires = fire.activeFires;
    row.buildingsLostToFire = cumulativeBuildingsLostToFire;
    row.crimeRate = crime.overallRate;
    row.illnessRate = health.illnessRate;
    row.earthquakeOccurred = disaster.earthquakeOccurred;
    row.floodOccurred = disaster.floodOccurred;
    row.buildingsLostToDisaster = cumulativeBuildingsLostToDisaster;
    row.wasteCollectionRate = waste.collectionRate;
    row.wasteUncollected = waste.uncollected;
    row.wastePollutionPenalty = waste.pollutionPenalty;
    row.deathsThisTick = deathcare.deaths;
    row.deathcareBacklog = deathcare.awaitingDisposition;
    row.deathcareHappinessPenalty = deathcare.happinessPenalty;
    if (!infinite) {
      result.rows.push_back(row);
    }
    if (options.tickCallback && !options.tickCallback(row)) {
      break;
    }
  }

  result.finalMetrics = MetricsSystem::collectCityMetrics(
    store, population, traffic, economy, &service);
  result.finalMetrics.pollution = lastResPollution;
  return result;
}
