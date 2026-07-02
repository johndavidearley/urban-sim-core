#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/Zoning.hpp"

using DistrictId = uint32_t;

struct ServicePriority {
  float fireWeight = 1.0f;
  float policeWeight = 1.0f;
  float healthWeight = 1.0f;
  float educationWeight = 1.0f;
};

// A named preset bundling a coherent set of zoning ordinances and service
// priorities - a shortcut for common district characters rather than a new
// mechanism of its own. Setting an archetype just populates bannedZoneTypes/
// servicePriorities with that preset's defaults (still overridable
// afterward via the general setters); General applies no restrictions,
// matching a plain createDistrict call.
enum class DistrictArchetype : int {
  General = 0,
  Industrial = 1,  // heavy industry: no housing or offices, prioritizes fire safety
  TechHub = 2       // knowledge-sector: no heavy industry, prioritizes education
};

struct District {
  DistrictId id = 0;
  std::string name;
  glm::ivec2 minCorner{0, 0};
  glm::ivec2 maxCorner{10, 10};

  TaxRates taxRates;
  float serviceAllocation = 0.5f; // 0.0 to 1.0 share of city service budget
  int64_t serviceBudgetCap = -1;  // Negative value means uncapped
  ServicePriority servicePriorities;  // Weight allocation for service types

  std::vector<uint32_t> assignedFacilityIds;  // Service facility IDs assigned to this district

  // Zoning ordinance: zone types autonomous growth (CitySimulator's autoZone)
  // may not assign within this district. Empty means no restriction. This
  // constrains what the city government lets grow on its own - manual
  // zoning commands (--zone-rect) are a sandbox tool and intentionally
  // bypass it.
  std::vector<ZoneType> bannedZoneTypes;
  DistrictArchetype archetype = DistrictArchetype::General;

  bool contains(glm::ivec2 coord) const {
    return coord.x >= minCorner.x && coord.x <= maxCorner.x &&
           coord.y >= minCorner.y && coord.y <= maxCorner.y;
  }

  bool allowsZone(ZoneType zone) const {
    return std::find(bannedZoneTypes.begin(), bannedZoneTypes.end(), zone) == bannedZoneTypes.end();
  }

  int32_t width() const {
    return maxCorner.x - minCorner.x + 1;
  }

  int32_t height() const {
    return maxCorner.y - minCorner.y + 1;
  }

  int64_t area() const {
    return static_cast<int64_t>(width()) * static_cast<int64_t>(height());
  }
};

struct DistrictMetrics {
  DistrictId districtId = 0;
  std::string districtName;

  uint32_t population = 0;
  uint32_t buildings = 0;
  uint32_t residentialBuildings = 0;
  uint32_t commercialBuildings = 0;
  uint32_t industrialBuildings = 0;
  uint32_t officeBuildings = 0;

  int64_t revenue = 0;
  int64_t expenses = 0;
  int64_t balance = 0;
  int64_t serviceBudgetTarget = 0;
  int64_t serviceBudgetAllocated = 0;
  bool serviceBudgetCapApplied = false;
  float economicHealth = 0.5f;
  float serviceCoveragePotential = 0.0f;

  float averageLandValue = 100.0f;
  float serviceCoverage = 0.0f;
  float happiness = 0.5f;
};

// Owns the districts of a single city. Instantiate one per simulated city
// alongside the entity/population stores.
class DistrictSystem {
public:
  // Create a new district
  DistrictId createDistrict(
    const std::string& name,
    glm::ivec2 minCorner,
    glm::ivec2 maxCorner
  );

  // Get all districts
  const std::vector<District>& getDistricts() const;

  // Get district by ID
  District* getDistrict(DistrictId id);
  const District* getDistrictConst(DistrictId id) const;

  // Delete a district
  bool deleteDistrict(DistrictId id);

  // Set tax rates for a district
  bool setDistrictTaxRates(DistrictId id, const TaxRates& rates);

  // Set service allocation percentage for a district
  bool setDistrictServiceAllocation(DistrictId id, float percentage);

  // Set service priorities for a district
  bool setDistrictServicePriorities(DistrictId id, const ServicePriority& priorities);

  // Set absolute service budget cap for a district (negative disables cap)
  bool setDistrictServiceBudgetCap(DistrictId id, int64_t cap);

  // Set the zoning ordinance: zone types autonomous growth may not assign
  // within this district (see District::bannedZoneTypes). Does not change
  // the district's archetype.
  bool setDistrictZoningOrdinance(DistrictId id, const std::vector<ZoneType>& bannedZoneTypes);

  // Apply a named archetype's default ordinance + service priorities (see
  // DistrictArchetype). Overwrites both; call the general setters afterward
  // to customize further.
  bool setDistrictArchetype(DistrictId id, DistrictArchetype archetype);

  // Parse a case-insensitive archetype name ("GENERAL"/"INDUSTRIAL"/"TECHHUB",
  // "TECH_HUB", "TECH-HUB"). Mirrors Zoning::parseZoneType/ServiceSystem::
  // parseServiceType's convention.
  static bool parseArchetype(const std::string& raw, DistrictArchetype& outArchetype);

  // Assign a service facility to a district
  bool assignFacilityToDistrict(DistrictId districtId, uint32_t facilityId);

  // Unassign a service facility from a district
  bool unassignFacilityFromDistrict(DistrictId districtId, uint32_t facilityId);

  // Get facilities assigned to a district
  std::vector<uint32_t> getFacilitiesForDistrict(DistrictId id) const;

  // Evaluate metrics for a single district
  DistrictMetrics evaluateDistrictMetrics(
    DistrictId id,
    const CityMap& map,
    const EntityStore& store,
    const PopulationStore& population,
    const RoadNetwork* roads = nullptr,
    const std::vector<ServiceFacility>* facilities = nullptr
  ) const;

  // Evaluate all district metrics
  std::vector<DistrictMetrics> evaluateAllDistricts(
    const CityMap& map,
    const EntityStore& store,
    const PopulationStore& population,
    const RoadNetwork* roads = nullptr,
    const std::vector<ServiceFacility>* facilities = nullptr,
    int64_t sharedServiceBudgetPool = -1
  ) const;

  // Compute a tuned growth pressure multiplier for a district from balanced metrics.
  static float computeGrowthPressureMultiplier(
    const District& district,
    const DistrictMetrics& metrics
  );

  // Clear all districts
  void clearDistricts();

private:
  std::vector<District> districts;
  DistrictId nextDistrictId = 1;
};
