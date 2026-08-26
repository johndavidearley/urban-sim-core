#pragma once

#include "src/visualization/VisualizerTypes.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/CityMap.hpp"

namespace visualizer {

void updatePlayableUtilityConnectivity(
  CityMap& map,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities
);

// Recompute utilities/services/land value/economy (and optionally occupancy
// and traffic) after a player tool or session load. Does not advance the tick.
void refreshLiveDerivedState(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& facilities,
  LiveSimulationState& liveState,
  uint32_t seed,
  bool reallocPopulation = false,
  bool runTraffic = true
);

void runSimulationTick(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  std::vector<ServiceFacility>& facilities,
  LiveSimulationState& liveState,
  int64_t& funds
);

// CitySimulator-style construction (expandConstruction) for the playable map.
// Mutates facilities when new civic buildings are auto-placed.
void runAutonomousGrowthStep(
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  std::vector<ServiceFacility>& facilities,
  LiveSimulationState& liveState
);

[[maybe_unused]] bool seedScenario(CityMap& map, RoadNetwork& roads, EntityStore& store, PopulationStore& population);
StartScreenResult runStartScreen(SDL_Renderer* renderer, int windowWidth, int windowHeight);
bool pointInRect(int x, int y, const SDL_Rect& rect);

}  // namespace visualizer
