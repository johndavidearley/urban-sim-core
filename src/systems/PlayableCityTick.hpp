#pragma once

#include <cstdint>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/TreasurySystem.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/DeathcareSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/systems/WasteSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

// Shared one-tick step for player-driven cities (SDL visualizer and any host
// that builds roads/zones itself). This is a subset of the autonomous
// CitySimulator phase list: same systems, host-owned construction, plus
// treasury. Optional phases (pollution emitters, land value) default on so a
// playable city sees the same environment model as `--simulate`.
struct PlayableCityTickState {
  uint32_t tick = 0;
  uint32_t populationTarget = 480;
  DeathcareState deathcareState;
  ZoneDemand demand;
  TrafficSummary trafficSummary;
  ServiceCoverageSummary serviceSummary;
  ServiceCoverageCache serviceCache;
  WasteSummary waste;
  DeathcareSummary deathcare;
  EconomyState economy;
  float illnessRate = 0.0f;
  float crimeRate = 0.0f;
  int64_t treasuryRevenue = 0;
  int64_t treasuryExpenses = 0;
  int64_t treasuryNet = 0;
  int64_t treasuryShortfall = 0;
  bool lowFunds = false;
  bool bankrupt = false;
  // Persist across ticks so auto-placed routes accumulate like CitySimulator.
  std::vector<TransitRoute> transitRoutes;
  TransitCoverageCache transitCache;
  TransitSummary transitSummary;
  // Last tick's growth deltas so G-mode can keep ConstructionState::emptyZonedCount in sync.
  int buildingsSpawned = 0;
  int buildingsDemolished = 0;
};

struct PlayableCityTickOptions {
  float growthChance = 0.18f;
  bool requireUtilities = true;
  double treasuryTickScale = 0.01;
  // Base seed mixed with tick for deterministic subsystem RNGs.
  uint32_t baseSeed = 1000u;
  bool enableTransit = true;
  float transitCapacityMultiplier = 1.0f;
  // Road-hops a transit stop covers; CitySimulator uses gridSpacing * 3.
  int transitStopCoverageRadius = 12;
  // Same environment phases as CitySimulator; disable only for tests.
  bool refreshPollution = true;
  bool updateLandValues = true;
};

struct DerivedCityRefreshOptions {
  bool reallocPopulation = false;
  bool runTraffic = true;
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
  const std::vector<ServiceFacility>& facilities,
  PlayableCityTickState& state,
  int64_t& funds,
  const PlayableCityTickOptions& options = PlayableCityTickOptions{}
);
