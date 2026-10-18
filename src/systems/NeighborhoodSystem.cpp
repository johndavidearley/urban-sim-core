#include "src/systems/NeighborhoodSystem.hpp"

#include <algorithm>
#include <vector>

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
  struct PatientRate { Building* building; float raw; float reduction; };
  static thread_local std::vector<PatientRate> patients;
  patients.clear();
  if (cache.hospitalBeds > 0.f)
    patients.reserve(store.idsByBuildingType(BuildingType::Residential).size());
  double population = 0.0;
  for (EntityId id : store.idsByBuildingType(BuildingType::Residential)) {
    auto &building = *store.getBuilding(id);
    Coord anchor;
    const bool anchored = roads.resolveRoadAnchor(building.position, anchor);
    const auto localCoverage = [&](ServiceType type) {
      return anchored ? ServiceSystem::coverageStrength(cache, type, anchor) : 0.0f;
    };
    building.crimeRate =
        CrimeSystem::evaluate(unemployment, localCoverage(ServiceType::Police),
                              averageLandValue, crimeParams)
            .overallRate;
    const float hospital = cache.hospitalBeds > 0.f ? localCoverage(ServiceType::Health) : 0.f;
    const float sewage = localCoverage(ServiceType::Sanitation);
    const float pollution = localHealth ? map.pollution(building.position) : averagePollution;
    if (cache.hospitalBeds > 0.f) patients.push_back({&building, HealthSystem::unmitigatedRate(housingDensity, pollution, sewage, healthParams),
      hospital * healthParams.healthCoverageReduction});
    population += std::max(0, building.occupancy);
    building.illnessRate =
        HealthSystem::evaluate(
            housingDensity,
            pollution, hospital, sewage, healthParams)
            .illnessRate;
  }
  const auto fullCare = summarize(store);
  const auto demand = [&](double illness) {
    return illness > 0.0 ? std::max(1.0, population * illness) : 0.0;
  };
  if (cache.hospitalBeds <= 0.f || cache.hospitalBeds >= demand(fullCare.illnessRate))
    return fullCare;
  // Solve s = min(1, beds / max(1, population * illness(s))). The
  // sufficient-capacity fast path above preserves the M20 float operations.
  // Every probe reads compact coefficients, with no BFS or entity lookups.
  const auto meanAt = [&](double scale) {
    double weighted = 0.0;
    for (const auto& patient : patients)
      weighted += std::clamp(static_cast<double>(patient.raw) *
        (1.0 - patient.reduction * scale), 0.0, 1.0) * std::max(0, patient.building->occupancy);
    return population > 0.0 ? weighted / population : 0.0;
  };
  double lo = 0.0, hi = 1.0;
  for (int i = 0; i < 32; ++i) {
    const double mid = (lo + hi) * 0.5;
    if (mid * demand(meanAt(mid)) < cache.hospitalBeds) lo = mid;
    else hi = mid;
  }
  const float scale = static_cast<float>((lo + hi) * 0.5);
  for (const auto& patient : patients)
    patient.building->illnessRate = std::clamp(patient.raw * (1.f - patient.reduction * scale), 0.f, 1.f);
  return summarize(store);
}
