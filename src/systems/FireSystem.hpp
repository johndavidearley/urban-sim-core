#pragma once

#include <cstdint>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/world/CityMap.hpp"
#include "src/systems/ServiceSystem.hpp"

// A building on fire (M47). It stays standing while it burns and can spread
// fire to occupied neighbors. Each tick, local fire response may put it out.
// If ticksRemaining reaches 0 first, the building burns down.
struct BurningTile {
  Coord position;
  int ticksRemaining;
};

struct FireParams {
  float baseIgnitionChance = 0.00005f;       // per building per tick, before modifiers
  float industrialRiskMultiplier = 3.0f;    // industrial buildings are far more fire-prone
  float pollutionRiskMultiplier = 2.0f;     // scales risk with local pollution (0-1)
  float spreadChance = 0.20f;               // chance a burning tile ignites each occupied neighbor per tick
  int baseDuration = 4;                     // ticks an unfought fire burns before the building is lost
  float coverageIgnitionReduction = 0.7f;   // fire coverage cuts ignition chance by up to this fraction
  float coverageSpreadReduction = 0.6f;     // fire coverage cuts spread chance by up to this fraction
  float extinguishChance = 0.8f;            // per-tick chance to put a fire out at full response
};

struct FireSummary {
  uint32_t activeFires = 0;         // burning buildings after this step
  uint32_t newIgnitions = 0;        // buildings that caught fire this tick (spread + random ignition)
  uint32_t buildingsDestroyed = 0;  // buildings that burned down this tick
  uint32_t extinguished = 0;        // fires put out this tick, building saved
};

// Models fire as a per-tick stochastic process over the tile grid rather
// than literal responding vehicles (TrafficMicroSim's emergency-vehicle
// dispatch already covers that fidelity separately, as a standalone demo).
// Local response reuses service distance fields, with the same falloff as
// neighborhood police response. Without a fire station, use the city fraction.
class FireSystem {
public:
  static float responseAt(const RoadNetwork& roads, const ServiceCoverageCache& cache,
    Coord position, float fallbackCoverage = 0.f);

  // Advances the fire simulation by one tick: existing fires may spread to
  // adjacent occupied tiles and burn down; new ignitions can occur
  // independently, weighted by building type and local pollution. `burning`
  // is caller-owned state that must persist across ticks (like
  // CitySimulator's other cross-tick vectors - transit routes, facilities).
  // Deterministic for a given seed: buildings and burning tiles are
  // processed in a fixed (ID/position-sorted) order.
  static FireSummary step(
    CityMap& map,
    EntityStore& store,
    std::vector<BurningTile>& burning,
    float fireCoverage,  // 0-1, e.g. ServiceCoverageSummary::fireCoverage
    uint32_t seed,
    const FireParams& params = {},
    const RoadNetwork* roads = nullptr,
    const ServiceCoverageCache* cache = nullptr
  );
};
