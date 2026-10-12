#pragma once

#include "src/systems/CrimeSystem.hpp"
#include "src/systems/HealthSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/CityMap.hpp"

struct NeighborhoodSummary {
  float crimeRate = 0.0f;
  float illnessRate = 0.0f;
};

// Cache derived residential read-outs; no residents or buildings are created,
// removed or harmed. Coverage queries reuse ServiceSystem's distance fields.
class NeighborhoodSystem {
public:
  static NeighborhoodSummary
  evaluate(const CityMap &map, const RoadNetwork &roads, EntityStore &store,
           const ServiceCoverageCache &cache, float unemployment,
           float housingDensity, float averageLandValue, float averagePollution,
           const CrimeParams &crimeParams = {},
           const HealthParams &healthParams = {});
  static NeighborhoodSummary summarize(const EntityStore &store);
};
