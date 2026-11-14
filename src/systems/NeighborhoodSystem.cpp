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
  const bool localHealth = map.pipeNetworksEnabled() || cache.citywideHealth || std::any_of(
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
      if(type==ServiceType::Sanitation && map.pipeNetworksEnabled()) return map.getTile(building.position).connectedToSewer ? 1.f : 0.f;
      if(type==ServiceType::Health && cache.citywideHealth) return 1.f;
      return anchored ? ServiceSystem::coverageStrength(cache, type, anchor) *
        ServiceSystem::departmentFactorAt(cache, type, building.position) : 0.0f;
    };
    building.crimeRate =
        CrimeSystem::evaluate(unemployment, localCoverage(ServiceType::Police),
                              averageLandValue, crimeParams)
            .overallRate;
    const float hospital = (cache.citywideHealth || cache.hospitalBeds > 0.f) ? localCoverage(ServiceType::Health) : 0.f;
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
  if (cache.clinicVisitCapacity > 0.f && !cache.citywideHealth) {
    // Clinics handle noncritical visits only. Hospital reach and beds remain
    // necessary for the serious share and for outpatient overflow.
    struct Care { Building* b; float raw, clinic, hospital; };
    std::vector<Care> care;
    for (const auto id:store.idsByBuildingType(BuildingType::Residential)) {
      auto& b=*store.getBuilding(id); Coord anchor{-1,-1};
      roads.resolveRoadAnchor(b.position,anchor);
      const float sewage=map.pipeNetworksEnabled() ? (map.getTile(b.position).connectedToSewer ? 1.f : 0.f) :
          ServiceSystem::coverageStrength(cache,ServiceType::Sanitation,anchor);
      care.push_back({&b,HealthSystem::unmitigatedRate(housingDensity,
        localHealth ? map.pollution(b.position) : averagePollution,sewage,healthParams),
        ServiceSystem::healthcareStrength(cache,anchor,true)>0.f ? 1.f : 0.f,
        ServiceSystem::healthcareStrength(cache,anchor,false)});
    }
    double clinicScale=1, hospitalScale=cache.hospitalBeds>0 ? 1 : 0;
    constexpr double serious=ServiceSystem::kCriticalPatientShare;
    for (int iteration=0;iteration<48;++iteration) {
      double visits=0,beds=0;
      for(const auto& p:care) {
        const double outpatient=std::max(p.clinic*clinicScale,p.hospital*hospitalScale);
        const double reduction=(1-serious)*outpatient+serious*p.hospital*hospitalScale;
        const double illness=std::clamp(p.raw*(1-healthParams.healthCoverageReduction*reduction),0.0,1.0);
        const double need=std::max(0,p.b->occupancy)*illness*ServiceSystem::kPatientShare;
        visits+=need*(1-serious)*p.clinic;
        beds+=need*(serious+(1-serious)*(1-p.clinic*clinicScale))*p.hospital;
      }
      const double nextClinic=visits>0 ? std::min(1.0,cache.clinicVisitCapacity/visits) : 1;
      const double nextHospital=beds>0 ? std::min(1.0,cache.hospitalBeds/beds) : (cache.hospitalBeds>0 ? 1 : 0);
      clinicScale=(clinicScale+nextClinic)*.5;
      hospitalScale=(hospitalScale+nextHospital)*.5;
    }
    for(const auto& p:care) {
      const double outpatient=std::max(p.clinic*clinicScale,p.hospital*hospitalScale);
      const double reduction=(1-serious)*outpatient+serious*p.hospital*hospitalScale;
      p.b->illnessRate=static_cast<float>(std::clamp(p.raw*(1-healthParams.healthCoverageReduction*reduction),0.0,1.0));
    }
    return summarize(store);
  }
  const auto fullCare = summarize(store);
  const auto demand = [&](double illness) {
    return illness > 0.0 ? std::max(1.0, population * illness * ServiceSystem::kPatientShare) : 0.0;
  };
  if (cache.citywideHealth || cache.hospitalBeds <= 0.f || cache.hospitalBeds >= demand(fullCare.illnessRate))
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
