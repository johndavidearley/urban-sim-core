#include "src/systems/PlayableCityTick.hpp"

#include <algorithm>
#include <thread>

#include "src/core/ThreadPool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/systems/CitySimSupport.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/systems/CrimeSystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/HealthSystem.hpp"
#include "src/systems/LandValueSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/world/Zoning.hpp"

namespace {

ThreadPool& playablePool() {
  static thread_local ThreadPool pool(std::max(1u, std::thread::hardware_concurrency() > 0
    ? std::thread::hardware_concurrency() - 1 : 2u));
  return pool;
}

void activeBounds(const CityMap& map, int& ax0, int& ay0, int& ax1, int& ay1) {
  const Coord dims = map.getDimensions();
  ax0 = 0;
  ay0 = 0;
  ax1 = dims.x - 1;
  ay1 = dims.y - 1;
}

void evaluateHealthAndCrime(const CityMap& map, const EntityStore& store,
                            const PopulationStore& population, PlayableCityTickState& state) {
  const float tickResPollution = city_sim::averageResidentialPollution(map, store);
  const city_sim::CapacitySummary cap = city_sim::summarize(store);
  const uint32_t pop = population.getTotalPopulation();
  const uint32_t employed = population.getTotalEmployed();
  const float housingDensity = cap.resCapacity > 0
    ? city_sim::clamp01(static_cast<float>(pop) / static_cast<float>(cap.resCapacity))
    : 0.0f;
  const float unemployment = pop > 0
    ? city_sim::clamp01(static_cast<float>(pop - employed) / static_cast<float>(pop))
    : 0.0f;
  state.illnessRate = HealthSystem::evaluate(
    housingDensity, tickResPollution, state.serviceSummary.healthCoverage).illnessRate;
  state.crimeRate = CrimeSystem::evaluate(
    unemployment, state.serviceSummary.policeCoverage, state.economy.averageLandValue).overallRate;
}

}  // namespace

void updateUtilityConnectivityFromFacilities(
  CityMap& map,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities
) {
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);
  int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
  activeBounds(map, ax0, ay0, ax1, ay1);
  city_sim::updateUtilityConnectivity(map, roads, cache, ax0, ay0, ax1, ay1, playablePool());
}

void refreshDerivedCityState(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  PlayableCityTickState& state,
  const DerivedCityRefreshOptions& options
) {
  ThreadPool& pool = playablePool();
  int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
  activeBounds(map, ax0, ay0, ax1, ay1);

  if (!ServiceSystem::isCacheValid(roads, facilities, state.serviceCache)) {
    ServiceSystem::buildCache(roads, facilities, state.serviceCache);
  }
  city_sim::updateUtilityConnectivity(
    map, roads, state.serviceCache, ax0, ay0, ax1, ay1, pool);

  if (options.reallocPopulation) {
    PopulationSystem::allocate(
      store, population, population.getTotalPopulation(), options.seed);
  }

  if (options.runTraffic) {
    if (options.enableTransit) {
      if (options.placeTransit) {
        city_sim::placeTransitRoutesIfNeeded(
          roads, store, state.transitRoutes, population.getTotalPopulation(),
          options.transitStopCoverageRadius, options.transitCapacityMultiplier
        );
      }
      if (state.transitRoutes.size() != state.transitCache.builtForRouteCount) {
        TransitSystem::buildCache(roads, state.transitRoutes, state.transitCache);
      }
      TransitOffload offload(state.transitRoutes, state.transitCache);
      state.trafficSummary = TrafficSystem::simulateCommutes(
        store, population, roads, options.seed, &pool, &offload
      );
      state.transitSummary = TransitSystem::summarize(
        state.transitRoutes, offload, state.trafficSummary.commutingPopulation
      );
    } else {
      state.trafficSummary = TrafficSystem::simulateCommutes(
        store, population, roads, options.seed, &pool
      );
      state.transitSummary = TransitSummary{};
    }
  }

  if (!ServiceSystem::isResultCacheValid(store, state.serviceCache)) {
    state.serviceSummary = ServiceSystem::evaluateFromCache(
      store, roads, state.serviceCache, nullptr);
    ServiceSystem::storeCachedResult(store, state.serviceSummary, state.serviceCache);
  } else {
    state.serviceSummary = state.serviceCache.cachedResult;
  }

  if (options.updateLandValues) {
    LandValueSystem::updateLandValues(
      map, roads, store, &state.serviceCache, ax0, ay0, ax1, ay1);
  }

  EconomyLandValueBounds lvBounds;
  lvBounds.useBounds = true;
  lvBounds.x0 = ax0;
  lvBounds.y0 = ay0;
  lvBounds.x1 = ax1;
  lvBounds.y1 = ay1;
  state.economy = EconomySystem::calculateEconomy(
    store, population, TaxRates{}, &map, TradeRates{}, 1.0f, lvBounds);

  evaluateHealthAndCrime(map, store, population, state);
}

void playableCityTick(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  PlayableCityTickState& state,
  int64_t& funds,
  const PlayableCityTickOptions& options
) {
  const uint32_t tickSeed = options.baseSeed + (state.tick * 31u);
  int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
  activeBounds(map, ax0, ay0, ax1, ay1);
  ThreadPool& pool = playablePool();

  if (store.getBuildingCount() > 0 || population.getTotalPopulation() > 0) {
    state.demand = CitySimulator::evaluateDemand(store, population);
  } else {
    state.demand = Zoning::calculateDemand(tickSeed);
  }

  if (options.refreshPollution) {
    city_sim::updatePollution(map, store, ax0, ay0, ax1, ay1, pool);
  }

  if (!ServiceSystem::isCacheValid(roads, facilities, state.serviceCache)) {
    ServiceSystem::buildCache(roads, facilities, state.serviceCache);
  }
  city_sim::updateUtilityConnectivity(
    map, roads, state.serviceCache, ax0, ay0, ax1, ay1, pool);

  const GrowthStats growth = GrowthSystem::runStep(
    map, store, state.demand, tickSeed + 1u, options.growthChance,
    nullptr, {-1, -1}, {-1, -1}, nullptr, options.requireUtilities
  );
  state.buildingsSpawned = growth.totalSpawned();
  state.buildingsDemolished = growth.totalDemolished();

  PopulationSystem::allocate(store, population, state.populationTarget, tickSeed + 2u);

  DerivedCityRefreshOptions refresh;
  refresh.runTraffic = true;
  refresh.enableTransit = options.enableTransit;
  refresh.placeTransit = options.enableTransit;
  refresh.updateLandValues = options.updateLandValues;
  refresh.seed = tickSeed + 3u;
  refresh.transitCapacityMultiplier = options.transitCapacityMultiplier;
  refresh.transitStopCoverageRadius = options.transitStopCoverageRadius;
  refreshDerivedCityState(map, roads, store, population, facilities, state, refresh);

  float tickResPollution = city_sim::averageResidentialPollution(map, store);
  state.waste = WasteSystem::evaluate(store, facilities, state.serviceSummary);
  if (state.waste.pollutionPenalty > 0.0f) {
    city_sim::applyWastePollution(map, state.waste.pollutionPenalty, ax0, ay0, ax1, ay1);
    tickResPollution = city_sim::averageResidentialPollution(map, store);
  }

  state.deathcare = DeathcareSystem::step(
    population, facilities, state.serviceSummary, state.illnessRate,
    tickResPollution, state.deathcareState
  );
  if (state.deathcare.deaths > 0) {
    state.populationTarget = state.populationTarget > state.deathcare.deaths
      ? state.populationTarget - state.deathcare.deaths : 0;
    PopulationSystem::allocate(
      store, population, population.getTotalPopulation(), tickSeed + 5u
    );
  }

  int64_t serviceOperatingCosts = 0;
  for (const ServiceFacility& facility : facilities) {
    serviceOperatingCosts += ServiceTool::operatingCostPerTick(facility.type);
  }
  const TreasuryFlow flow = TreasurySystem::applyEconomy(
    state.economy, funds, options.treasuryTickScale, serviceOperatingCosts
  );
  state.treasuryRevenue = flow.revenue;
  state.treasuryExpenses = flow.expenses;
  state.treasuryNet = flow.net;
  state.treasuryShortfall = flow.shortfall;
  state.lowFunds = funds > 0 && funds < 5000;
  state.bankrupt = funds == 0 && flow.shortfall > 0;

  ++state.tick;
}
