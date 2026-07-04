#pragma once

#include <cstdint>

#include "src/entities/EntityStore.hpp"
#include "src/world/CityMap.hpp"

struct DisasterParams {
  float earthquakeChancePerTick = 0.0003f;    // rare, uniform citywide risk (any building can be the epicenter)
  int earthquakeRadius = 6;                   // tiles; destruction chance falls off linearly with distance
  float earthquakeDestructionChance = 0.55f;  // at the epicenter itself, before distance falloff

  float floodChancePerTick = 0.0008f;         // slightly more common than earthquakes, but only near water
  int floodRadius = 4;
  int floodProximity = 2;                     // a building must be within this many tiles of a water tile to be flood-eligible
  float floodDestructionChance = 0.65f;       // at the epicenter itself, before distance falloff
};

struct DisasterSummary {
  bool earthquakeOccurred = false;
  bool floodOccurred = false;
  uint32_t buildingsDestroyed = 0;
};

// Models one-off, citywide catastrophic events, unlike FireSystem's
// persistent per-tile spread: each tick independently rolls for an
// earthquake (uniform risk - any building can be the epicenter) and a flood
// (risk restricted to buildings near a water tile). When one triggers, every
// building within its radius has a distance-weighted chance of being
// destroyed immediately - no persistent "damaged" state, no coverage
// mitigation (unlike fire, a police/fire department doesn't reduce whether
// the ground shakes or a river overflows). Deterministic for a given seed:
// buildings are processed in a fixed (ID-sorted) order, same as every other
// subsystem in this codebase.
class NaturalDisasterSystem {
public:
  static DisasterSummary step(
    CityMap& map,
    EntityStore& store,
    uint32_t seed,
    const DisasterParams& params = {}
  );
};
