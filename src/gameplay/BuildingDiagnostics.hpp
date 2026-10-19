#pragma once

#include <array>
#include <algorithm>
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/FireSystem.hpp"
#include "src/systems/ServiceSystem.hpp"

class DistrictSystem;

struct NeighborhoodDiagnostics {
  int radius = 4;
  uint32_t tiles = 0, buildings = 0;
  int64_t residents = 0, housingCapacity = 0, filledJobs = 0, jobCapacity = 0;
  float crime = 0.f, illness = 0.f;
  std::array<float, kServiceTypeCount> serviceCoverage{};
};
struct BuildingDiagnostics {
  GrowthSiteDiagnostics growth;
  ServiceCoverageMask services = 0;
  float fireResponse = 0.f;
  std::array<double,3> departmentServed{}, departmentUnserved{};
  int capacity = 0, occupancy = 0;
  bool residential = false;
  NeighborhoodDiagnostics neighborhood;
  static ServiceCoverageMask serviceMask(const RoadNetwork& roads, const ServiceCoverageCache& cache, Coord pos) {
    Coord anchor;
    if (!roads.resolveRoadAnchor(pos,anchor)) return 0;
    const auto found = cache.coverageMask.find(anchor);
    return found == cache.coverageMask.end() ? 0 : found->second;
  }
  static BuildingDiagnostics evaluate(const CityMap& map, const RoadNetwork& roads,
    const EntityStore& store, Coord pos, const ZoneDemand& demand,
    const ServiceCoverageCache& cache, bool burning = false,
    const std::vector<GrowthChanceModifier>* modifiers = nullptr,
    const DistrictSystem* districts = nullptr) {
    BuildingDiagnostics d;
    d.growth = GrowthSystem::diagnoseSite(map,roads,store,pos,demand,&cache,true,burning,modifiers,districts);
    if (!map.isValid(pos)) return d;
    d.services = serviceMask(roads,cache,pos);
    d.fireResponse = FireSystem::responseAt(roads,cache,pos);
    if (const auto* b = store.getBuilding(map.getTile(pos).buildingId)) {
      d.capacity = b->capacity; d.occupancy = b->occupancy;
      d.residential = b->type == BuildingType::Residential;
      const ServiceType types[] = {ServiceType::Fire,ServiceType::Police,ServiceType::Sanitation};
      for (size_t i=0;i<3;++i) {
        d.departmentServed[i] = std::max(0,b->occupancy) * ServiceSystem::departmentFactorAt(cache,types[i],pos);
        d.departmentUnserved[i] = std::max(0,b->occupancy) - d.departmentServed[i];
      }
    }
    auto& n = d.neighborhood;
    double crime = 0, illness = 0;
    for (int y = pos.y - n.radius; y <= pos.y + n.radius; ++y)
      for (int x = pos.x - n.radius; x <= pos.x + n.radius; ++x) {
        if (!map.isValid({x,y})) continue;
        ++n.tiles;
        const auto* b = store.getBuilding(map.getTile({x,y}).buildingId);
        if (!b) continue;
        ++n.buildings;
        if (b->type == BuildingType::Residential) {
          n.residents += std::max(0,b->occupancy); n.housingCapacity += std::max(0,b->capacity);
          crime += std::max(0,b->occupancy) * b->crimeRate;
          illness += std::max(0,b->occupancy) * b->illnessRate;
        } else { n.filledJobs += std::max(0,b->occupancy); n.jobCapacity += std::max(0,b->capacity); }
        const auto mask = serviceMask(roads,cache,{x,y});
        for (size_t i = 0; i < n.serviceCoverage.size(); ++i)
          if (mask & static_cast<ServiceCoverageMask>(1u << i)) n.serviceCoverage[i] += ServiceSystem::departmentFactorAt(cache,static_cast<ServiceType>(i),{x,y});
      }
    if (n.buildings) for (auto& coverage : n.serviceCoverage) coverage /= n.buildings;
    if (n.residents) { n.crime = static_cast<float>(crime / n.residents); n.illness = static_cast<float>(illness / n.residents); }
    return d;
  }
  static const char* advice(const std::string& code) {
    if (code == "burning") return "WAIT FOR THE FIRE TO CLEAR.";
    if (code == "no_road") return "CONNECT AN ADJACENT ROAD.";
    if (code == "no_power") return "CHECK POWER REACH, SUPPLY AND FUNDING.";
    if (code == "no_water") return "CHECK WATER REACH, SUPPLY AND FUNDING.";
    if (code == "low_demand" || code == "low_redevelopment_demand") return "CHECK DEMAND, TAXES AND CITY BALANCE.";
    if (code == "density_cap") return "THIS BUILDING HAS REACHED THE DENSITY CAP.";
    if (code == "height_cap") return "THIS DISTRICT HEIGHT CAP STOPS FURTHER REDEVELOPMENT.";
    if (code == "zero_location_factor") return "CHECK LAND VALUE AND ROAD CONGESTION.";
    if (code == "district_growth_disabled") return "REVIEW THE DISTRICT GROWTH POLICY.";
    if (code == "water_terrain") return "CHOOSE BUILDABLE LAND.";
    if (code == "not_developable_zone") return "ZONE R, C, I OR O TO ENABLE BUILDING GROWTH.";
    return "CITY BALANCE AND SEEDED CHANCE STILL APPLY.";
  }
};
