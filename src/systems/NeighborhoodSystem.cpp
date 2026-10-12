#include "src/systems/NeighborhoodSystem.hpp"

#include <algorithm>

namespace {
float coverage(const ServiceCoverageCache &cache, ServiceType type,
               Coord anchor) {
  float result = 0.0f;
  for (const auto &entry : cache.entries) {
    if (entry.type != type)
      continue;
    const auto it = entry.distanceField.find(anchor);
    if (it == entry.distanceField.end())
      continue;
    const float strength =
        1.0f -
        static_cast<float>(it->second) /
            (static_cast<float>(std::max(0, entry.maxTravelDistance)) + 1.0f);
    result = std::max(result, std::clamp(strength, 0.0f, 1.0f));
  }
  return result;
}
} // namespace

NeighborhoodSummary NeighborhoodSystem::summarize(const EntityStore &store) {
  double crime = 0.0, illness = 0.0, residents = 0.0;
  for (EntityId id : store.idsByBuildingType(BuildingType::Residential)) {
    const auto &building = *store.getBuilding(id);
    const double weight = std::max(0, building.occupancy);
    residents += weight;
    crime += building.crimeRate * weight;
    illness += building.illnessRate * weight;
  }
  if (residents == 0.0)
    return {};
  return {static_cast<float>(crime / residents),
          static_cast<float>(illness / residents)};
}

NeighborhoodSummary NeighborhoodSystem::evaluate(
    const CityMap &map, const RoadNetwork &roads, EntityStore &store,
    const ServiceCoverageCache &cache, float unemployment, float housingDensity,
    float averageLandValue, float averagePollution,
    const CrimeParams &crimeParams, const HealthParams &healthParams) {
  // Without health/sewage facilities retain the legacy city pollution input.
  // Once local services exist, pollution and coverage distinguish
  // neighborhoods.
  const bool localHealth = std::any_of(
      cache.entries.begin(), cache.entries.end(), [](const auto &entry) {
        return entry.type == ServiceType::Health ||
               entry.type == ServiceType::Sanitation;
      });
  for (EntityId id : store.idsByBuildingType(BuildingType::Residential)) {
    auto &building = *store.getBuilding(id);
    Coord anchor;
    const bool anchored = roads.resolveRoadAnchor(building.position, anchor);
    const auto localCoverage = [&](ServiceType type) {
      return anchored ? coverage(cache, type, anchor) : 0.0f;
    };
    building.crimeRate =
        CrimeSystem::evaluate(unemployment, localCoverage(ServiceType::Police),
                              averageLandValue, crimeParams)
            .overallRate;
    building.illnessRate =
        HealthSystem::evaluate(
            housingDensity,
            localHealth ? map.pollution(building.position) : averagePollution,
            localCoverage(ServiceType::Health),
            localCoverage(ServiceType::Sanitation), healthParams)
            .illnessRate;
  }
  return summarize(store);
}
