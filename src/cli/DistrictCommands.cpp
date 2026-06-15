#include "src/cli/DistrictCommands.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>

bool applyDistrictMutations(DistrictSystem& districtSystem, const DistrictMutationRequests& requests) {
  for (const auto& [name, x1, y1, x2, y2] : requests.create) {
    DistrictId districtId = districtSystem.createDistrict(
      name,
      {x1, y1},
      {x2, y2}
    );
    if (districtId == 0) {
      std::cerr << "Error: Failed to create district '" << name << "' (invalid bounds?)\n";
      return false;
    }
    std::cout << "Created district '" << name << "' with ID " << districtId << "\n";
    std::cout << "  Bounds: (" << x1 << "," << y1 << ") to (" << x2 << "," << y2 << ")\n";
  }

  for (const auto& [districtId, buildingType, rate] : requests.setTax) {
    District* district = districtSystem.getDistrict(static_cast<DistrictId>(districtId));
    if (district == nullptr) {
      std::cerr << "Error: District " << districtId << " not found\n";
      return false;
    }

    std::string typeUpper = buildingType;
    std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(),
                  [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (typeUpper == "RESIDENTIAL") {
      district->taxRates.residentialRate = std::max(0.0f, std::min(1.0f, rate));
    } else if (typeUpper == "COMMERCIAL") {
      district->taxRates.commercialRate = std::max(0.0f, std::min(1.0f, rate));
    } else if (typeUpper == "INDUSTRIAL") {
      district->taxRates.industrialRate = std::max(0.0f, std::min(1.0f, rate));
    } else if (typeUpper == "INCOME") {
      district->taxRates.incomeRate = std::max(0.0f, std::min(1.0f, rate));
    } else {
      std::cerr << "Error: Unknown building type '" << buildingType << "'\n";
      return false;
    }

    std::cout << "Set " << buildingType << " tax rate to " << std::fixed << std::setprecision(1)
              << (rate * 100.0f) << "% for district " << districtId << "\n";
  }

  for (const auto& [districtId, fireW, policeW, healthW, eduW] : requests.setServicePriorities) {
    District* district = districtSystem.getDistrict(static_cast<DistrictId>(districtId));
    if (district == nullptr) {
      std::cerr << "Error: District " << districtId << " not found\n";
      return false;
    }

    ServicePriority priorities;
    priorities.fireWeight = std::max(0.0f, fireW);
    priorities.policeWeight = std::max(0.0f, policeW);
    priorities.healthWeight = std::max(0.0f, healthW);
    priorities.educationWeight = std::max(0.0f, eduW);

    district->servicePriorities = priorities;
    std::cout << "Set service priorities for district " << districtId << ":\n";
    std::cout << "  Fire: " << std::fixed << std::setprecision(2) << fireW << "\n";
    std::cout << "  Police: " << std::fixed << std::setprecision(2) << policeW << "\n";
    std::cout << "  Health: " << std::fixed << std::setprecision(2) << healthW << "\n";
    std::cout << "  Education: " << std::fixed << std::setprecision(2) << eduW << "\n";
  }

  for (const auto& [districtId, allocation] : requests.setAllocation) {
    bool success = districtSystem.setDistrictServiceAllocation(
      static_cast<DistrictId>(districtId),
      allocation
    );
    if (!success) {
      std::cerr << "Error: District " << districtId << " not found\n";
      return false;
    }

    const District* district = districtSystem.getDistrictConst(static_cast<DistrictId>(districtId));
    std::cout << "Set service allocation for district " << districtId << " to "
              << std::fixed << std::setprecision(1)
              << ((district != nullptr ? district->serviceAllocation : allocation) * 100.0f)
              << "%\n";
  }

  for (const auto& [districtId, cap] : requests.setBudgetCap) {
    bool success = districtSystem.setDistrictServiceBudgetCap(
      static_cast<DistrictId>(districtId),
      cap
    );
    if (!success) {
      std::cerr << "Error: District " << districtId << " not found\n";
      return false;
    }

    if (cap < 0) {
      std::cout << "Disabled service budget cap for district " << districtId << "\n";
    } else {
      std::cout << "Set service budget cap for district " << districtId << " to " << cap << "\n";
    }
  }

  for (const auto& [districtId, facilityId] : requests.assignFacility) {
    bool success = districtSystem.assignFacilityToDistrict(
      static_cast<DistrictId>(districtId),
      static_cast<uint32_t>(facilityId)
    );
    if (!success) {
      std::cerr << "Error: Failed to assign facility " << facilityId << " to district " << districtId << "\n";
      return false;
    }
    std::cout << "Assigned facility " << facilityId << " to district " << districtId << "\n";
  }

  for (const auto& [districtId, facilityId] : requests.unassignFacility) {
    bool success = districtSystem.unassignFacilityFromDistrict(
      static_cast<DistrictId>(districtId),
      static_cast<uint32_t>(facilityId)
    );
    if (!success) {
      std::cerr << "Error: Failed to unassign facility " << facilityId
                << " from district " << districtId << "\n";
      return false;
    }
    std::cout << "Unassigned facility " << facilityId << " from district " << districtId << "\n";
  }

  return true;
}

void printDistrictList(const DistrictSystem& districtSystem) {
  const auto& districts = districtSystem.getDistricts();
  if (districts.empty()) {
    std::cout << "No districts created.\n";
    return;
  }

  std::cout << "Districts (" << districts.size() << "):\n";
  for (const auto& district : districts) {
    std::cout << "  [" << district.id << "] " << district.name << "\n";
    std::cout << "    Bounds: (" << district.minCorner.x << "," << district.minCorner.y
              << ") to (" << district.maxCorner.x << "," << district.maxCorner.y << ")\n";
    std::cout << "    Area: " << district.area() << " tiles\n";
    std::cout << "    Tax Rates: Res=" << std::fixed << std::setprecision(2)
              << (district.taxRates.residentialRate * 100.0f) << "%  "
              << "Com=" << (district.taxRates.commercialRate * 100.0f) << "%  "
              << "Ind=" << (district.taxRates.industrialRate * 100.0f) << "%  "
              << "Inc=" << (district.taxRates.incomeRate * 100.0f) << "%\n";
    std::cout << "    Service Allocation: " << (district.serviceAllocation * 100.0f) << "%\n";
    std::cout << "    Service Budget Cap: ";
    if (district.serviceBudgetCap < 0) {
      std::cout << "none\n";
    } else {
      std::cout << district.serviceBudgetCap << "\n";
    }
    std::cout << "    Service Priorities: Fire=" << district.servicePriorities.fireWeight
              << " Police=" << district.servicePriorities.policeWeight
              << " Health=" << district.servicePriorities.healthWeight
              << " Education=" << district.servicePriorities.educationWeight << "\n";
    std::cout << "    Assigned Facilities: ";
    if (district.assignedFacilityIds.empty()) {
      std::cout << "none\n";
    } else {
      for (size_t idx = 0; idx < district.assignedFacilityIds.size(); ++idx) {
        if (idx > 0) {
          std::cout << ", ";
        }
        std::cout << district.assignedFacilityIds[idx];
      }
      std::cout << "\n";
    }
  }
}

bool printDistrictSummaryReport(
  const DistrictSystem& districtSystem,
  DistrictId districtId,
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& serviceFacilities
) {
  DistrictMetrics metrics = districtSystem.evaluateDistrictMetrics(
    districtId,
    map, store, population,
    &roads,
    &serviceFacilities
  );

  if (metrics.districtId == 0) {
    std::cerr << "Error: District " << districtId << " not found\n";
    return false;
  }

  std::cout << "District Summary: " << metrics.districtName << "\n";
  std::cout << "  Population: " << metrics.population << "\n";
  std::cout << "  Buildings: " << metrics.buildings << " (Res: " << metrics.residentialBuildings
            << ", Com: " << metrics.commercialBuildings << ", Ind: " << metrics.industrialBuildings << ")\n";
  std::cout << "  Average Land Value: " << std::fixed << std::setprecision(1) << metrics.averageLandValue << "\n";
  std::cout << "  Budget: Revenue=" << metrics.revenue << ", Expenses=" << metrics.expenses
            << ", Balance=" << metrics.balance << "\n";
  std::cout << "  Service Budget: Target=" << metrics.serviceBudgetTarget
            << ", Allocated=" << metrics.serviceBudgetAllocated
            << ", CapApplied=" << (metrics.serviceBudgetCapApplied ? "yes" : "no") << "\n";
  std::cout << "  Service Coverage: " << std::fixed << std::setprecision(1) << (metrics.serviceCoverage * 100.0f) << "%\n";
  std::cout << "  Happiness: " << std::fixed << std::setprecision(1) << (metrics.happiness * 100.0f) << "%\n";
  return true;
}

void printDistrictBalancing(
  const DistrictSystem& districtSystem,
  int64_t sharedPool,
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& serviceFacilities
) {
  const std::vector<DistrictMetrics> balancedMetrics = districtSystem.evaluateAllDistricts(
    map,
    store,
    population,
    &roads,
    &serviceFacilities,
    sharedPool
  );

  std::cout << "District Balancing (shared pool=" << sharedPool << "):\n";
  if (balancedMetrics.empty()) {
    std::cout << "  No districts created.\n";
    return;
  }

  int64_t totalAllocated = 0;
  for (const DistrictMetrics& metrics : balancedMetrics) {
    totalAllocated += metrics.serviceBudgetAllocated;
    std::cout << "  [" << metrics.districtId << "] " << metrics.districtName
              << " target=" << metrics.serviceBudgetTarget
              << " allocated=" << metrics.serviceBudgetAllocated
              << " coverage=" << std::fixed << std::setprecision(1)
              << (metrics.serviceCoverage * 100.0f) << "%"
              << " capApplied=" << (metrics.serviceBudgetCapApplied ? "yes" : "no")
              << "\n";
  }
  std::cout << "  Total Allocated: " << totalAllocated << "\n";
}

bool printDistrictFacilities(const DistrictSystem& districtSystem, DistrictId districtId) {
  const District* district = districtSystem.getDistrictConst(districtId);
  if (district == nullptr) {
    std::cerr << "Error: District " << districtId << " not found\n";
    return false;
  }

  std::cout << "District Facilities: [" << district->id << "] " << district->name << "\n";
  if (district->assignedFacilityIds.empty()) {
    std::cout << "  none\n";
  } else {
    for (uint32_t id : district->assignedFacilityIds) {
      std::cout << "  " << id << "\n";
    }
  }
  return true;
}
