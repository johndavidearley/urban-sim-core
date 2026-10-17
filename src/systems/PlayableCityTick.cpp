#include "src/systems/TourismAccess.hpp"
#include "src/systems/TourismTravel.hpp"
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
#include "src/systems/NeighborhoodSystem.hpp"
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

void refreshDistrictGrowthModifiers(
  const DistrictSystem* districts,
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities,
  PlayableCityTickState& state
) {
  state.districtGrowthModifiers.clear();
  if (districts == nullptr || districts->getDistricts().empty()) {
    return;
  }
  const std::vector<DistrictMetrics> metrics =
    districts->evaluateAllDistricts(map, store, population, &roads, &facilities);
  const std::vector<District>& allDistricts = districts->getDistricts();
  state.districtGrowthModifiers.reserve(metrics.size());
  for (size_t i = 0; i < metrics.size() && i < allDistricts.size(); ++i) {
    const float multiplier =
      DistrictSystem::computeGrowthPressureMultiplier(allDistricts[i], metrics[i]);
    state.districtGrowthModifiers.push_back(
      {allDistricts[i].minCorner, allDistricts[i].maxCorner, multiplier});
  }
}

void evaluateHealthAndCrime(const CityMap &map, const RoadNetwork &roads,
                            EntityStore &store,
                            const PopulationStore &population,
                            PlayableCityTickState &state) {
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
  const auto local = NeighborhoodSystem::evaluate(
      map, roads, store, state.serviceCache, unemployment, housingDensity,
      state.economy.averageLandValue, tickResPollution);
  state.illnessRate = local.illnessRate;
  state.crimeRate = local.crimeRate;
}

}  // namespace

void updateUtilityConnectivityFromFacilities(
  CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities
) {
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(roads, facilities, cache);
  int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
  activeBounds(map, ax0, ay0, ax1, ay1);
  city_sim::updateUtilityConnectivity(
    map, roads, store, cache, ax0, ay0, ax1, ay1, playablePool());
}

void refreshDerivedCityState(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& baseFacilities,
  PlayableCityTickState& state,
  const DerivedCityRefreshOptions& options
) {
  tourism::reconcileRemovedHotels(state.tourism.state,store);
  const auto facilities = state.budgetPolicy.fundedFacilities(baseFacilities);
  ThreadPool& pool = playablePool();
  int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
  activeBounds(map, ax0, ay0, ax1, ay1);

  if (!ServiceSystem::isCacheValid(roads, facilities, state.serviceCache)) {
    ServiceSystem::buildCache(roads, facilities, state.serviceCache);
  }
  city_sim::updateUtilityConnectivity(
    map, roads, store, state.serviceCache, ax0, ay0, ax1, ay1, pool);

  if (options.reallocPopulation) {
    PopulationSystem::allocate(
      store, population, population.getTotalPopulation(), options.seed,
      state.laggedEducationCoverage);
  }

  const auto checkpointLoads =
      options.preserveCheckpoint && options.runTraffic
          ? roads.getAllEdgeTraffic()
          : std::vector<RoadNetwork::EdgeTrafficInfo>{};
  if (options.runTraffic) {
    if (options.enableTransit) {
      if (options.placeTransit) {
        city_sim::placeTransitRoutesIfNeeded(
          roads, store, state.transitRoutes, population.getTotalPopulation(),
          options.transitStopCoverageRadius, options.transitCapacityMultiplier
        );
      }
      if (state.transitRoutes.size() != state.transitCache.builtForRouteCount ||
          state.transitCache.topologyVersion != roads.getTopologyVersion()) {
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

  if (options.runTraffic && options.enableFreight) {
    if (state.regionalLogisticsEnabled) {
      const auto plan=regional::planLogistics(map,roads,state.regionalConnections);
      state.trafficSummary.freight=FreightSystem::simulate(
        map,roads,store,&state.freightRouteCache,{},&plan.gateways);
    } else {
      state.trafficSummary.freight =
          FreightSystem::simulate(map, roads, store, &state.freightRouteCache);
    }
    TrafficSystem::refreshCongestion(roads, state.trafficSummary);
  }
  if (options.runTraffic && !options.preserveCheckpoint && options.projectVisitorTravel && state.tourism.travel.measured) {
    const std::vector<TransitRoute> noRoutes;
    const std::vector<TransitRouteUsage> noResidents;
    (void)tourism::applyTravel(state.tourism.travel.tick,state.tourism.travel.legs,roads,
      options.enableTransit ? state.transitRoutes : noRoutes,state.transitCache,
      options.enableTransit ? state.transitSummary.routeUsage : noResidents);
    TrafficSystem::refreshCongestion(roads,state.trafficSummary);
  }
  if (options.preserveCheckpoint && options.runTraffic) {
    roads.resetCongestion();
    for (const auto &edge : checkpointLoads) {
      roads.updateCongestion(edge.from, edge.to,
                             edge.currentLoad - edge.freightLoad - edge.visitorLoad);
      roads.addFreightLoad(edge.from, edge.to, edge.freightLoad);
      roads.addVisitorLoad(edge.from,edge.to,edge.visitorLoad);
    }
    TrafficSystem::refreshCongestion(roads, state.trafficSummary);
  }

  if (!ServiceSystem::isResultCacheValid(store, state.serviceCache)) {
    state.serviceSummary = ServiceSystem::evaluateFromCache(
      store, roads, state.serviceCache, nullptr);
    ServiceSystem::storeCachedResult(store, state.serviceSummary, state.serviceCache);
  } else {
    state.serviceSummary = state.serviceCache.cachedResult;
  }

  state.waste = WasteSystem::evaluate(store, state.serviceCache.wasteFacilities, state.serviceSummary);

  if (options.updateLandValues && !options.preserveCheckpoint) {
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
    store, population, state.budgetPolicy.rates(), &map, TradeRates{}, 1.0f, lvBounds, EconomyValuation::playable());

  if (state.regionalLogisticsEnabled) {
    if (!options.runTraffic && options.enableFreight) {
      // Refresh an economic projection after utility/tool edits without adding
      // a second freight pass to the live road loads.
      auto projectionRoads=roads;
      const auto plan=regional::planLogistics(map,projectionRoads,state.regionalConnections);
      state.trafficSummary.freight=FreightSystem::simulate(
        map,projectionRoads,store,&state.freightRouteCache,{},&plan.gateways);
    } else if (!options.enableFreight) {
      state.trafficSummary.freight={};
      state.trafficSummary.freight.goodsProduced=state.economy.goodsProduced;
      state.trafficSummary.freight.goodsConsumed=state.economy.goodsConsumed;
    }
    regional::applyFulfilledTrade(state.economy,state.trafficSummary.freight);
  }

  if (options.preserveCheckpoint) {
    const auto local = NeighborhoodSystem::summarize(store);
    state.illnessRate = local.illnessRate;
    state.crimeRate = local.crimeRate;
  } else {
    evaluateHealthAndCrime(map, roads, store, population, state);
  }
}

void playableCityTick(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& baseFacilities,
  PlayableCityTickState& state,
  int64_t& funds,
  const PlayableCityTickOptions& options
) {
  // Only actual cargo construction opts in; passenger/airport configuration
  // and read-only refresh never activate the irreversible logistics transition.
  state.regionalLogisticsEnabled = state.regionalLogisticsEnabled ||
    regional::hasConstructedCargoPort(state.regionalConnections);
  const auto facilities = state.budgetPolicy.fundedFacilities(baseFacilities);
  const uint32_t populationBefore = population.getTotalPopulation();
  const uint32_t tickSeed = options.baseSeed + (state.tick * 31u);
  const float educationNow = state.laggedEducationCoverage;
  int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
  activeBounds(map, ax0, ay0, ax1, ay1);
  ThreadPool& pool = playablePool();

  if (store.getBuildingCount() > 0 || population.getTotalPopulation() > 0) {
    state.demand = CitySimulator::evaluateDemand(store, population);
  } else {
    state.demand = Zoning::calculateDemand(tickSeed);
  }

  state.demand = state.budgetPolicy.adjustedDemand(state.demand);

  if (options.refreshPollution) {
    city_sim::updatePollution(map, store, ax0, ay0, ax1, ay1, pool);
    city_sim::applyPowerPlantPollution(map, roads, facilities, ax0, ay0, ax1, ay1);
  }

  if (!ServiceSystem::isCacheValid(roads, facilities, state.serviceCache)) {
    ServiceSystem::buildCache(roads, facilities, state.serviceCache);
  }
  city_sim::updateUtilityConnectivity(
    map, roads, store, state.serviceCache, ax0, ay0, ax1, ay1, pool);

  const std::vector<GrowthChanceModifier>* growthModifiers =
    state.districtGrowthModifiers.empty() ? nullptr : &state.districtGrowthModifiers;
  if (!options.enableDisasters) state.disasters.stopEvents();
  GrowthStats growth;
  {
    BurningGrowthGuard burningGuard(map, state.disasters.burningTiles);
    growth = GrowthSystem::runStep(
      map, store, state.demand, tickSeed + 1u, options.growthChance,
      growthModifiers, {-1, -1}, {-1, -1}, nullptr, options.requireUtilities,
      &roads, &state.serviceCache
    );
  }
  state.buildingsSpawned = growth.totalSpawned();
  state.buildingsDemolished = growth.totalDemolished();
  state.constructionSpend = growth.constructionSpend;

  MigrationConditions conditions = state.migrationConditions;
  if (!options.requireUtilities)
    conditions.utilityAvailability = 1.0f;
  const auto migration = MigrationSystem::evaluate(
      populationBefore, population.getTotalEmployed(),
      city_sim::summarize(store).resCapacity, conditions,
      state.budgetPolicy.taxAttraction()[0]);
  state.populationTarget = migration.requestedPopulation;
  PopulationSystem::allocate(store, population, migration.requestedPopulation,
                             tickSeed + 2u, educationNow);
  const uint32_t afterMigration = population.getTotalPopulation();
  state.populationChange = {};
  state.populationChange.desirability = migration.desirability;
  state.populationChange.arrivals =
      afterMigration > populationBefore ? afterMigration - populationBefore : 0;
  state.populationChange.departures =
      populationBefore > afterMigration ? populationBefore - afterMigration : 0;

  DerivedCityRefreshOptions refresh;
  refresh.runTraffic = true;
  refresh.projectVisitorTravel = false;
  refresh.enableTransit = options.enableTransit;
  refresh.enableFreight = options.enableFreight;
  refresh.placeTransit = options.enableTransit;
  refresh.updateLandValues = options.updateLandValues;
  refresh.seed = tickSeed + 3u;
  refresh.transitCapacityMultiplier = options.transitCapacityMultiplier;
  refresh.transitStopCoverageRadius = options.transitStopCoverageRadius;
  refreshDerivedCityState(map, roads, store, population, baseFacilities, state, refresh);

  if (options.enableDisasters) {
    auto& events = state.disasters;
    events.fire = FireSystem::step(map, store, events.burningTiles,
                                   state.serviceSummary.fireCoverage,
                                   tickSeed + 6u, options.fireParams);
    events.disaster = NaturalDisasterSystem::step(
        map, store, tickSeed + 7u, options.disasterParams);
    events.buildingsLostToFire += events.fire.buildingsDestroyed;
    events.buildingsLostToDisaster += events.disaster.buildingsDestroyed;
    const auto lost = events.fire.buildingsDestroyed + events.disaster.buildingsDestroyed;
    state.buildingsDemolished += static_cast<int>(lost);
    if (lost > 0) {
      const auto before = population.getTotalPopulation();
      PopulationSystem::allocate(store, population, before, tickSeed + 8u, educationNow);
      state.populationChange.departures += before - population.getTotalPopulation();
      refresh.placeTransit = false;
      refreshDerivedCityState(map, roads, store, population, baseFacilities, state, refresh);
    }
  }

  float tickResPollution = city_sim::averageResidentialPollution(map, store);
  state.waste = WasteSystem::evaluate(store, state.serviceCache.wasteFacilities, state.serviceSummary);
  if (state.waste.pollutionPenalty > 0.0f) {
    city_sim::applyWastePollution(map, state.waste.pollutionPenalty, ax0, ay0, ax1, ay1);
    tickResPollution = city_sim::averageResidentialPollution(map, store);
  }

  state.deathcare = DeathcareSystem::step(
    population, facilities, state.serviceSummary, state.illnessRate,
    tickResPollution, state.deathcareState
  );
  if (state.deathcare.deaths > 0) {
    PopulationSystem::allocate(
      store, population, population.getTotalPopulation(), tickSeed + 5u, educationNow
    );
    const auto survivingResidents = NeighborhoodSystem::summarize(store);
    state.crimeRate = survivingResidents.crimeRate;
    state.illnessRate = survivingResidents.illnessRate;
  }

  refreshDistrictGrowthModifiers(
    options.districts, map, store, population, roads, facilities, state);

  state.populationChange.deaths = state.deathcare.deaths;
  state.populationChange.net =
      static_cast<int64_t>(population.getTotalPopulation()) - populationBefore;
  state.migrationConditions = {
      state.trafficSummary.averageEdgeCongestion,
      tickResPollution,
      state.serviceSummary.satisfaction,
      state.crimeRate,
      state.illnessRate,
      options.requireUtilities
          ? std::min({state.serviceSummary.powerCoverage,
                      state.serviceSummary.waterCoverage,
                      state.serviceSummary.powerSupplyRatio,
                      state.serviceSummary.waterSupplyRatio})
          : 1.0f};
  if (state.regionalLogisticsEnabled && options.enableFreight) {
    state.migrationConditions.serviceSatisfaction *= 0.5f+0.5f*state.economy.goodsAvailability;
    state.regionalSettlement=regional::measureLogistics(state.tick+1,
      regional::planLogistics(map,roads,state.regionalConnections),state.trafficSummary.freight,
      options.treasuryTickScale,regional::regionalOperatingCost(state.regionalConnections));
  }
  if (!state.regionalLogisticsEnabled && regional::hasConstructedPassengerTerminal(state.regionalConnections))
    state.regionalSettlement=regional::measurePassengerReadiness(state.tick+1,map,roads,state.regionalConnections,options.treasuryTickScale);
  // Tourism measures after resident work. Local visitor legs reserve only the
  // transit capacity left by resident commuters and have separate usage reports.
  auto previousVisitors = state.tourism.state;
  if (!previousVisitors.tick && previousVisitors.stays.empty()) previousVisitors.tick = state.tick;
  const auto quality = static_cast<uint32_t>(std::clamp(
    (static_cast<double>(state.serviceSummary.satisfaction) +
     1.0 - state.crimeRate + 1.0 - state.illnessRate) / 3.0, 0.0, 1.0) * 100.0);
  auto visitorPlan = (state.tourism.enabled || !previousVisitors.stays.empty() || previousVisitors.pendingEarlyDepartures) ?
    tourism::planAccess(map,roads,store,state.regionalConnections,quality) : tourism::AccessPlan{};
  auto allocationPlan=visitorPlan;
  if (!state.tourism.enabled) {
    allocationPlan.attractions={};allocationPlan.offers.clear();
    for (auto& hotel:allocationPlan.hotels) hotel.enabled=false;
  }
  const auto visitors = tourism::advance(previousVisitors,state.tick+1,
    allocationPlan.attractions,std::move(allocationPlan.offers),std::move(allocationPlan.hotels));
  state.tourism.state = visitors.state;
  state.tourism.report = visitors.report;
  state.tourism.report.enabled = state.tourism.enabled;
  const std::vector<TransitRoute> noRoutes;
  const std::vector<TransitRouteUsage> noResidents;
  state.tourism.travel=tourism::applyTravel(state.tick+1,
    tourism::planTravel(previousVisitors,visitors.state,visitors.report,visitorPlan,roads),roads,
    options.enableTransit ? state.transitRoutes : noRoutes,state.transitCache,
    options.enableTransit ? state.transitSummary.routeUsage : noResidents);
  TrafficSystem::refreshCongestion(roads,state.trafficSummary);
  state.migrationConditions.congestion=state.trafficSummary.averageEdgeCongestion;
  state.tourism.accounting=tourism::account(state.tourism.report);
  int64_t serviceOperatingCosts = state.budgetPolicy.serviceCost(baseFacilities) + TransitSystem::operatingCost(state.transitRoutes) + regional::regionalOperatingCost(state.regionalConnections);
  state.treasuryLedger = {};
  state.treasuryLedger.valid = true;
  state.treasuryLedger.tick = state.tick + 1;
  state.treasuryLedger.breakdown = TreasurySystem::breakdown(
    state.economy, options.treasuryTickScale, serviceOperatingCosts, growth.constructionSpend,
    state.tourism.accounting.fees(),state.tourism.accounting.services);
  state.treasuryLedger.fundsBefore = funds;
  state.treasuryLedger.debtBefore = state.treasuryDebt;
  serviceOperatingCosts += growth.constructionSpend + state.tourism.accounting.services;
  TreasuryDebt debt;
  debt.principal = state.treasuryDebt;
  debt.interestRemainder = state.treasuryInterestRemainder;
  const TreasuryFlow flow = TreasurySystem::applyEconomy(
    state.economy, funds, options.treasuryTickScale, serviceOperatingCosts,
    &debt, options.treasuryInterestRate, state.tourism.accounting.fees()
  );
  state.treasuryLedger.flow = flow;
  state.treasuryLedger.fundsAfter = funds;
  state.treasuryRevenue = flow.revenue;
  state.treasuryExpenses = flow.expenses;
  state.treasuryNet = flow.net;
  state.treasuryShortfall = flow.shortfall;
  state.treasuryDebt = debt.principal;
  state.treasuryDebtIssued = flow.debtIssued;
  state.treasuryDebtRepaid = flow.debtRepaid;
  state.treasuryInterestCharged = flow.interestCharged;
  state.treasuryInterestRemainder = debt.interestRemainder;
  state.lowFunds = funds > 0 && funds < 5000;
  state.bankrupt = funds == 0 && (flow.shortfall > 0 || state.treasuryDebt > 0);
  state.laggedEducationCoverage = state.serviceSummary.educationCoverage;

  ++state.tick;
  state.transitReport = {true,state.tick,state.trafficSummary.commutingPopulation,state.transitSummary.routeUsage};
  const uint32_t residents = population.getTotalPopulation();
  state.alerts.update(state.tick, {store.getBuildingCount() > 0, residents > 0, options.requireUtilities,
    state.serviceSummary.powerCoverage, state.serviceSummary.powerSupplyRatio,
    state.serviceSummary.waterCoverage, state.serviceSummary.waterSupplyRatio,
    residents ? static_cast<float>(residents - std::min(residents, population.getTotalEmployed())) / residents : 0.f,
    std::min({state.serviceSummary.fireCoverage, state.serviceSummary.policeCoverage,
      state.serviceSummary.healthCoverage, state.serviceSummary.educationCoverage}), flow.revenue, flow.expenses});
  state.history.append({state.tick, population.getTotalPopulation(), state.populationChange,
    flow.revenue, flow.expenses, funds, state.treasuryDebt, state.budgetPolicy});
}
