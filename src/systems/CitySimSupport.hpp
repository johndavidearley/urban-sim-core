#pragma once

#include <cstdint>
#include <vector>

#include "src/core/ThreadPool.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/systems/ZoningCandidateIndex.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

// Internal helpers for CitySimulator orchestration. Not part of the public
// engine API; kept out of CitySimulator.cpp so the tick loop stays readable.
namespace city_sim {

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

CapacitySummary summarize(const EntityStore& store);
float clamp01(float v);
float attractivenessFromUnemployment(float unemployment);

bool isLand(const CityMap& map, int x, int y);
bool hasRoadAccess(const CityMap& map, Coord pos);
Coord cityCenter(const CityMap& map);
void layRoadGrid(CityMap& map, RoadNetwork& roads, Coord center, int extent, int spacing);

void extendZoningCandidates(const CityMap& map, Coord center, int oldExtent, int newExtent,
                            ThreadPool& pool, ZoningCandidateIndex& out);
void autoZone(CityMap& map, const std::vector<Coord>& candidates, const ZoneDemand& demand, int batch,
              const DistrictSystem* districts);

// Persistent construction state shared by CitySimulator and visualizer G-mode.
// `extent == 0` means roads have not been seeded yet.
struct ConstructionState {
  int extent = 0;
  int zoningCandidatesExtent = 0;
  ZoningCandidateIndex zoningCandidates;
  int64_t emptyZonedCount = 0;
};

struct ConstructionOptions {
  int gridSpacing = 4;
  int zoneBatchPerTick = 16;
  bool refreshPollution = true;
  bool placeFacilities = false;
  bool includeUtilities = false;
  bool includeWasteDeathcare = false;
  bool placeTransit = false;
  float transitCapacityMultiplier = 1.0f;
  const DistrictSystem* districts = nullptr;
};

struct ConstructionRegion {
  Coord center{0, 0};
  int extent = 0;
  int ax0 = 0;
  int ay0 = 0;
  int ax1 = 0;
  int ay1 = 0;
  int serviceRadius = 0;
  double roadMs = 0.0;
  double zoningMs = 0.0;
};

inline void applyEmptyZonedDelta(ConstructionState& state, int64_t delta) {
  state.emptyZonedCount += delta;
  if (state.emptyZonedCount < 0) {
    state.emptyZonedCount = 0;
  }
}

// Seed/expand the road grid, refresh pollution, zone a demand-weighted batch,
// and optionally place civic facilities / transit. Both autonomous ticks and
// visualizer G-mode call this instead of duplicating the construction policy.
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
);

void updatePollution(CityMap& map, const EntityStore& store,
                     int x0, int y0, int x1, int y1, ThreadPool& pool);
float averageResidentialPollution(const CityMap& map, const EntityStore& store);

// Add uncollected-waste pollution onto zoned tiles in [x0,y0]-[x1,y1].
void applyWastePollution(CityMap& map, float penalty, int x0, int y0, int x1, int y1);

void updateUtilityConnectivity(
  CityMap& map,
  const RoadNetwork& roads,
  const ServiceCoverageCache& cache,
  int x0, int y0, int x1, int y1,
  ThreadPool& pool
);

void placeFacilitiesIfNeeded(
  const CityMap& map,
  std::vector<ServiceFacility>& facilities,
  uint32_t population,
  Coord center,
  int extent,
  int coverageRadius,
  bool includeUtilities,
  // When true, also auto-place Garbage/Cemetery so waste + deathcare systems
  // have capacity in autonomous runs (player-built cities still place these
  // via the service tool when not using G-mode expansion).
  bool includeWasteDeathcare = false
);

void placeTransitRoutesIfNeeded(
  const RoadNetwork& roads,
  const EntityStore& store,
  std::vector<TransitRoute>& routes,
  uint32_t population,
  int stopCoverageRadius,
  float capacityMultiplier
);

}  // namespace city_sim
