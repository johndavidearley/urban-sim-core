#pragma once
#include "src/systems/TourismSystem.hpp"
#include "src/gameplay/CityAlerts.hpp"
#include "src/gameplay/CityHistory.hpp"
#include "src/gameplay/BudgetPolicy.hpp"
#include "src/gameplay/TreasurySystem.hpp"

#include "src/systems/MigrationSystem.hpp"
#include "src/systems/RegionalLogistics.hpp"
#include "src/systems/CityDisasterState.hpp"
#include <cstdint>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/TreasurySystem.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/DeathcareSystem.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/systems/WasteSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

// Shared one-tick step for player-driven cities (live visualizer and any host
// that builds roads/zones itself). This is a subset of the autonomous
// CitySimulator phase list: same systems, host-owned construction, plus
// treasury. Optional phases (pollution emitters, land value) default on so a
// playable city sees the same environment model as `--simulate`.
struct PlayableCityTickState {
  BudgetPolicy budgetPolicy;
  TreasuryLedger treasuryLedger;
  CityHistory history;
  CityAlerts alerts;
  TransitReport transitReport;
  std::vector<regional::Connection> regionalConnections;
  regional::Settlement regionalSettlement;
  tourism::Runtime tourism;
  bool regionalLogisticsEnabled = false;
  uint32_t tick = 0;
  CityDisasterState disasters;
  // Last migration request, retained in saves for compatibility; not a cap.
  uint32_t populationTarget = 480;
  MigrationConditions migrationConditions;
  PopulationChange populationChange;
  DeathcareState deathcareState;
  ZoneDemand demand;
  TrafficSummary trafficSummary;
  FreightRouteCache freightRouteCache;
  ServiceCoverageSummary serviceSummary;
  ServiceCoverageCache serviceCache;
  WasteSummary waste;
  DeathcareSummary deathcare;
  EconomyState economy;
  float illnessRate = 0.0f;
  float crimeRate = 0.0f;
  // Previous tick's education coverage. allocate reads it; the playable tick
  // writes serviceSummary.educationCoverage here after services run. A tool
  // refresh updates the service summary and leaves this lag alone. Starts
  // at 0, so the first tick is the uneducated labor split.
  float laggedEducationCoverage = 0.0f;
  int64_t treasuryRevenue = 0;
  int64_t treasuryExpenses = 0;
  int64_t treasuryNet = 0;
  int64_t treasuryShortfall = 0;
  int64_t treasuryDebt = 0;
  int64_t treasuryDebtIssued = 0;
  int64_t treasuryDebtRepaid = 0;
  int64_t treasuryInterestCharged = 0;
  double treasuryInterestRemainder = 0.0;
  // Player-issued; paid before ordinary expenses. Default ticks issue none.
  MunicipalBond bond;
  bool lowFunds = false;
  bool bankrupt = false;
  // Persist across ticks so auto-placed routes accumulate like CitySimulator.
  std::vector<TransitRoute> transitRoutes;
  TransitCoverageCache transitCache;
  TransitSummary transitSummary;
  // Last tick's growth deltas so G-mode can keep ConstructionState::emptyZonedCount in sync.
  int buildingsSpawned = 0;
  int buildingsDemolished = 0;
  // Billed this tick on top of service operating costs. A redevelopment
  // costs more than a new low building. See buildingConstructionCost.
  int64_t constructionSpend = 0;
  // One-tick-lag growth pressure from DistrictSystem, same as CitySimulator.
  std::vector<GrowthChanceModifier> districtGrowthModifiers;
};

struct PlayableCityTickOptions {
  bool enableDisasters = false;
  FireParams fireParams;
  DisasterParams disasterParams;
  float growthChance = 0.18f;
  bool requireUtilities = true;
  double treasuryTickScale = 0.01;
  double treasuryInterestRate = 0.001;
  double bondInterestRate = TreasurySystem::kBondInterestRate;
  // Base seed mixed with tick for deterministic subsystem RNGs.
  uint32_t baseSeed = 1000u;
  bool enableFreight = true;
  bool enableTransit = true;
  float transitCapacityMultiplier = 1.0f;
  // Road-hops a transit stop covers; CitySimulator uses gridSpacing * 3.
  int transitStopCoverageRadius = 12;
  // Same environment phases as CitySimulator; disable only for tests.
  bool refreshPollution = true;
  bool updateLandValues = true;
  // When non-null, ordinances are host-owned (G-mode autoZone) and growth
  // pressure is applied here. The same districts supply height caps to
  // redevelopment. Null matches prior playable behavior.
  const DistrictSystem* districts = nullptr;
};

struct DerivedCityRefreshOptions {
  // Rebuild display/cache data without replacing the saved congestion or land
  // values that govern the next growth step.
  bool preserveCheckpoint = false;
  bool projectVisitorTravel = true;
  bool reallocPopulation = false;
  bool runTraffic = true;
  bool enableFreight = true;
  bool enableTransit = true;
  bool updateLandValues = true;
  bool placeTransit = false;
  uint32_t seed = 0;
  float transitCapacityMultiplier = 1.0f;
  int transitStopCoverageRadius = 12;
};

// Refresh Tile::connectedToPower / connectedToWater from facility coverage.
void updateUtilityConnectivityFromFacilities(
  CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities
);

// Recompute utilities, services, optional traffic, land value, economy, and
// health after a host mutation (tool apply / session load) without advancing
// the tick, growth, waste, deathcare, or treasury.
void refreshDerivedCityState(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  PlayableCityTickState& state,
  const DerivedCityRefreshOptions& options = {}
);

// Advance the city by one playable tick. Mutates map/roads/store/population,
// state (including transit routes/cache), and funds.
void playableCityTick(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  std::vector<ServiceFacility>& facilities,
  PlayableCityTickState& state,
  int64_t& funds,
  const PlayableCityTickOptions& options = PlayableCityTickOptions{}
);

// Hosts without persistent facilities may supply a temporary list.
inline void playableCityTick(CityMap& map, RoadNetwork& roads, EntityStore& store,
  PopulationStore& population, std::vector<ServiceFacility>&& facilities,
  PlayableCityTickState& state, int64_t& funds,
  const PlayableCityTickOptions& options = PlayableCityTickOptions{}) {
  playableCityTick(map, roads, store, population, facilities, state, funds, options);
}
