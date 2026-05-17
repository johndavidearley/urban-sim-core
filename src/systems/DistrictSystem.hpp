#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/ServiceSystem.hpp"

using DistrictId = uint32_t;

struct ServicePriority {
  float fireWeight = 1.0f;
  float policeWeight = 1.0f;
  float healthWeight = 1.0f;
  float educationWeight = 1.0f;
};

struct District {
  DistrictId id = 0;
  std::string name;
  glm::ivec2 minCorner{0, 0};
  glm::ivec2 maxCorner{10, 10};

  TaxRates taxRates;
  float serviceAllocation = 0.5f; // 0.0 to 1.0 share of city service budget
  ServicePriority servicePriorities;  // Weight allocation for service types

  std::vector<uint32_t> assignedFacilityIds;  // Service facility IDs assigned to this district

  bool contains(glm::ivec2 coord) const {
    return coord.x >= minCorner.x && coord.x <= maxCorner.x &&
           coord.y >= minCorner.y && coord.y <= maxCorner.y;
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

  int64_t revenue = 0;
  int64_t expenses = 0;
  int64_t balance = 0;

  float averageLandValue = 100.0f;
  float serviceCoverage = 0.0f;
  float happiness = 0.5f;
};

class DistrictSystem {
public:
  // Create a new district
  static DistrictId createDistrict(
    const std::string& name,
    glm::ivec2 minCorner,
    glm::ivec2 maxCorner
  );

  // Get all districts
  static const std::vector<District>& getDistricts();

  // Get district by ID
  static District* getDistrict(DistrictId id);
  static const District* getDistrictConst(DistrictId id);

  // Delete a district
  static bool deleteDistrict(DistrictId id);

  // Set tax rates for a district
  static bool setDistrictTaxRates(DistrictId id, const TaxRates& rates);

  // Set service allocation percentage for a district
  static bool setDistrictServiceAllocation(DistrictId id, float percentage);

  // Set service priorities for a district
  static bool setDistrictServicePriorities(DistrictId id, const ServicePriority& priorities);

  // Assign a service facility to a district
  static bool assignFacilityToDistrict(DistrictId districtId, uint32_t facilityId);

  // Unassign a service facility from a district
  static bool unassignFacilityFromDistrict(DistrictId districtId, uint32_t facilityId);

  // Get facilities assigned to a district
  static std::vector<uint32_t> getFacilitiesForDistrict(DistrictId id);

  // Evaluate metrics for a single district
  static DistrictMetrics evaluateDistrictMetrics(
    DistrictId id,
    const CityMap& map,
    const EntityStore& store,
    const PopulationStore& population
  );

  // Evaluate all district metrics
  static std::vector<DistrictMetrics> evaluateAllDistricts(
    const CityMap& map,
    const EntityStore& store,
    const PopulationStore& population
  );

  // Clear all districts
  static void clearDistricts();

private:
  static std::vector<District> districts;
  static DistrictId nextDistrictId;
};
