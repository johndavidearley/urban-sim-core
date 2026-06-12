#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "src/systems/DistrictSystem.hpp"

// District mutations collected from CLI arguments, applied in declaration
// order before growth so growth-pressure sees the requested district state.
struct DistrictMutationRequests {
  std::vector<std::tuple<std::string, int, int, int, int>> create;            // name, x1, y1, x2, y2
  std::vector<std::tuple<int, std::string, float>> setTax;                    // district_id, tax_type, rate
  std::vector<std::tuple<int, float, float, float, float>> setServicePriorities; // district_id, fire, police, health, edu
  std::vector<std::pair<int, float>> setAllocation;                           // district_id, allocation
  std::vector<std::pair<int, int64_t>> setBudgetCap;                          // district_id, cap
  std::vector<std::pair<int, int>> assignFacility;                            // district_id, facility_id
  std::vector<std::pair<int, int>> unassignFacility;                          // district_id, facility_id

  bool any() const {
    return !create.empty() || !setTax.empty() || !setServicePriorities.empty() ||
           !setAllocation.empty() || !setBudgetCap.empty() ||
           !assignFacility.empty() || !unassignFacility.empty();
  }
};

// Applies all requested mutations, printing per-action confirmation.
// Returns false (after printing to stderr) on the first failure.
bool applyDistrictMutations(DistrictSystem& districtSystem, const DistrictMutationRequests& requests);

void printDistrictList(const DistrictSystem& districtSystem);

// Returns false if the district does not exist.
bool printDistrictSummaryReport(
  const DistrictSystem& districtSystem,
  DistrictId districtId,
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& serviceFacilities
);

void printDistrictBalancing(
  const DistrictSystem& districtSystem,
  int64_t sharedPool,
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& serviceFacilities
);

// Returns false if the district does not exist.
bool printDistrictFacilities(const DistrictSystem& districtSystem, DistrictId districtId);
