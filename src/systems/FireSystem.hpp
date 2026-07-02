#pragma once

#include <cstdint>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/world/CityMap.hpp"

// A tile currently on fire: ignited buildings are destroyed immediately (the
// structure is gone), but the tile keeps burning for a few ticks, posing an
// ongoing spread risk to adjacent occupied tiles - representing the blaze
// itself, not the original structure. ticksRemaining counts down to 0, at
// which point the fire is extinguished/burns out.
struct BurningTile {
  Coord position;
  int ticksRemaining;
};

struct FireParams {
  float baseIgnitionChance = 0.0004f;       // per building per tick, before modifiers
  float industrialRiskMultiplier = 3.0f;    // industrial buildings are far more fire-prone
  float pollutionRiskMultiplier = 2.0f;     // scales risk with local pollution (0-1)
  float spreadChance = 0.20f;               // chance a burning tile ignites each occupied neighbor per tick
  int baseDuration = 4;                     // ticks a fire burns before extinguishing, at 0 fire coverage
  float coverageIgnitionReduction = 0.7f;   // fire coverage cuts ignition chance by up to this fraction
  float coverageSpreadReduction = 0.6f;     // fire coverage cuts spread chance by up to this fraction
  float coverageDurationReduction = 0.75f;  // fire coverage cuts burn duration by up to this fraction (faster response)
};

struct FireSummary {
  uint32_t activeFires = 0;         // burning tiles after this step
  uint32_t newIgnitions = 0;        // buildings newly destroyed by fire this tick (spread + random ignition combined)
  uint32_t buildingsDestroyed = 0;  // same as newIgnitions - kept as a separate, clearer name for callers
};

// Models fire as a per-tick stochastic process over the tile grid rather
// than literal responding vehicles (TrafficMicroSim's emergency-vehicle
// dispatch already covers that fidelity separately, as a standalone demo).
// Fire station coverage (ServiceCoverageSummary::fireCoverage, already
// computed by ServiceSystem every tick CitySimulator runs) is read as a
// single city-wide fraction and reduces ignition chance, spread chance, and
// burn duration alike - a proxy for faster emergency response, not a
// per-building distance lookup (a deliberate fidelity tradeoff to avoid a
// second per-tile BFS pass; ServiceSystem already pays that cost for the
// aggregate coverage number this reuses).
class FireSystem {
public:
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
    const FireParams& params = {}
  );
};
