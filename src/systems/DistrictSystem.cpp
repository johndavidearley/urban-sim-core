#include "DistrictSystem.hpp"
#include <algorithm>
#include <cmath>

namespace {
void applySharedBudgetBalancing(std::vector<DistrictMetrics>& metrics, const std::vector<District>& districts, int64_t sharedServiceBudgetPool) {
  if (sharedServiceBudgetPool < 0 || metrics.empty() || metrics.size() != districts.size()) {
    return;
  }

  std::vector<int64_t> requested(metrics.size(), 0);
  std::vector<int64_t> allocated(metrics.size(), 0);
  std::vector<int64_t> capRemaining(metrics.size(), 0);

  int64_t totalRequested = 0;
  for (size_t i = 0; i < metrics.size(); ++i) {
    requested[i] = std::max<int64_t>(0, metrics[i].serviceBudgetTarget);
    totalRequested += requested[i];

    if (districts[i].serviceBudgetCap < 0) {
      capRemaining[i] = requested[i];
    } else {
      capRemaining[i] = std::max<int64_t>(0, std::min(requested[i], districts[i].serviceBudgetCap));
    }
  }

  int64_t remainingPool = std::max<int64_t>(0, sharedServiceBudgetPool);
  if (remainingPool == 0 || totalRequested == 0) {
    for (size_t i = 0; i < metrics.size(); ++i) {
      metrics[i].serviceBudgetAllocated = 0;
      metrics[i].serviceBudgetCapApplied = (districts[i].serviceBudgetCap >= 0 && requested[i] > districts[i].serviceBudgetCap);
      metrics[i].serviceCoverage = 0.0f;
      metrics[i].happiness = std::max(
        0.0f,
        std::min(1.0f, 0.45f + (metrics[i].economicHealth * 0.35f) + (metrics[i].serviceCoverage * 0.20f))
      );
    }
    return;
  }

  while (remainingPool > 0) {
    int64_t totalUnmet = 0;
    for (size_t i = 0; i < metrics.size(); ++i) {
      const int64_t unmet = std::max<int64_t>(0, std::min(requested[i] - allocated[i], capRemaining[i]));
      totalUnmet += unmet;
    }

    if (totalUnmet == 0) {
      break;
    }

    int64_t distributedThisPass = 0;
    for (size_t i = 0; i < metrics.size() && remainingPool > 0; ++i) {
      const int64_t unmet = std::max<int64_t>(0, std::min(requested[i] - allocated[i], capRemaining[i]));
      if (unmet <= 0) {
        continue;
      }

      int64_t proportional = (remainingPool * unmet) / totalUnmet;
      if (proportional <= 0) {
        proportional = 1;
      }

      const int64_t grant = std::min<int64_t>(proportional, std::min<int64_t>(unmet, remainingPool));
      allocated[i] += grant;
      capRemaining[i] -= grant;
      remainingPool -= grant;
      distributedThisPass += grant;
    }

    if (distributedThisPass == 0) {
      break;
    }
  }

  for (size_t i = 0; i < metrics.size(); ++i) {
    metrics[i].serviceBudgetAllocated = allocated[i];
    metrics[i].serviceBudgetCapApplied =
      (districts[i].serviceBudgetCap >= 0 && requested[i] > districts[i].serviceBudgetCap);

    float budgetFulfillment = 1.0f;
    if (metrics[i].serviceBudgetTarget > 0) {
      budgetFulfillment = static_cast<float>(metrics[i].serviceBudgetAllocated) /
        static_cast<float>(metrics[i].serviceBudgetTarget);
    }
    budgetFulfillment = std::max(0.0f, std::min(1.0f, budgetFulfillment));

    metrics[i].serviceCoverage = std::max(
      0.0f,
      std::min(1.0f, metrics[i].serviceCoveragePotential * budgetFulfillment)
    );

    metrics[i].happiness = std::max(
      0.0f,
      std::min(1.0f, 0.45f + (metrics[i].economicHealth * 0.35f) + (metrics[i].serviceCoverage * 0.20f))
    );
  }
}
} // namespace

std::vector<District> DistrictSystem::districts;
DistrictId DistrictSystem::nextDistrictId = 1;

DistrictId DistrictSystem::createDistrict(
  const std::string& name,
  glm::ivec2 minCorner,
  glm::ivec2 maxCorner
) {
  if (minCorner.x > maxCorner.x || minCorner.y > maxCorner.y) {
    return 0;
  }

  District district;
  district.id = nextDistrictId++;
  district.name = name;
  district.minCorner = minCorner;
  district.maxCorner = maxCorner;
  district.taxRates = TaxRates{};
  district.serviceAllocation = 0.5f;

  districts.push_back(district);
  return district.id;
}

const std::vector<District>& DistrictSystem::getDistricts() {
  return districts;
}

District* DistrictSystem::getDistrict(DistrictId id) {
  for (auto& d : districts) {
    if (d.id == id) {
      return &d;
    }
  }
  return nullptr;
}

const District* DistrictSystem::getDistrictConst(DistrictId id) {
  for (const auto& d : districts) {
    if (d.id == id) {
      return &d;
    }
  }
  return nullptr;
}

bool DistrictSystem::deleteDistrict(DistrictId id) {
  auto it = std::find_if(districts.begin(), districts.end(),
    [id](const District& d) { return d.id == id; });

  if (it != districts.end()) {
    districts.erase(it);
    return true;
  }
  return false;
}

bool DistrictSystem::setDistrictTaxRates(DistrictId id, const TaxRates& rates) {
  District* district = getDistrict(id);
  if (district == nullptr) {
    return false;
  }
  district->taxRates = rates;
  return true;
}

bool DistrictSystem::setDistrictServiceAllocation(DistrictId id, float percentage) {
  District* district = getDistrict(id);
  if (district == nullptr) {
    return false;
  }
  district->serviceAllocation = std::max(0.0f, std::min(1.0f, percentage));
  return true;
}

bool DistrictSystem::setDistrictServicePriorities(DistrictId id, const ServicePriority& priorities) {
  District* district = getDistrict(id);
  if (district == nullptr) {
    return false;
  }
  district->servicePriorities = priorities;
  return true;
}

bool DistrictSystem::setDistrictServiceBudgetCap(DistrictId id, int64_t cap) {
  District* district = getDistrict(id);
  if (district == nullptr) {
    return false;
  }

  district->serviceBudgetCap = cap;
  return true;
}

bool DistrictSystem::assignFacilityToDistrict(DistrictId districtId, uint32_t facilityId) {
  District* district = getDistrict(districtId);
  if (district == nullptr) {
    return false;
  }
  // Prevent duplicate assignment
  auto it = std::find(district->assignedFacilityIds.begin(), district->assignedFacilityIds.end(), facilityId);
  if (it != district->assignedFacilityIds.end()) {
    return false;  // Already assigned
  }
  district->assignedFacilityIds.push_back(facilityId);
  return true;
}

bool DistrictSystem::unassignFacilityFromDistrict(DistrictId districtId, uint32_t facilityId) {
  District* district = getDistrict(districtId);
  if (district == nullptr) {
    return false;
  }
  auto it = std::find(district->assignedFacilityIds.begin(), district->assignedFacilityIds.end(), facilityId);
  if (it == district->assignedFacilityIds.end()) {
    return false;  // Not assigned
  }
  district->assignedFacilityIds.erase(it);
  return true;
}

std::vector<uint32_t> DistrictSystem::getFacilitiesForDistrict(DistrictId id) {
  const District* district = getDistrictConst(id);
  if (district == nullptr) {
    return {};
  }
  return district->assignedFacilityIds;
}

DistrictMetrics DistrictSystem::evaluateDistrictMetrics(
  DistrictId id,
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork* roads,
  const std::vector<ServiceFacility>* facilities
) {
  DistrictMetrics metrics;
  metrics.districtId = id;

  const District* district = getDistrictConst(id);
  if (district == nullptr) {
    return metrics;
  }

  metrics.districtName = district->name;

  const auto& buildings = store.getBuildings();
  EntityStore districtStore;
  int64_t totalLandValue = 0;
  uint32_t landValueTileCount = 0;
  uint32_t districtResidentialCapacity = 0;
  uint32_t cityResidentialCapacity = 0;

  for (const auto& [buildingId, building] : buildings) {
    (void)buildingId;
    if (building.type == BuildingType::Residential) {
      cityResidentialCapacity += static_cast<uint32_t>(std::max(0, building.capacity));
    }

    if (!district->contains(building.position)) {
      continue;
    }

    districtStore.upsertBuilding(building);
    metrics.buildings++;
    switch (building.type) {
      case BuildingType::Residential:
        metrics.residentialBuildings++;
        districtResidentialCapacity += static_cast<uint32_t>(std::max(0, building.capacity));
        break;
      case BuildingType::Commercial:
        metrics.commercialBuildings++;
        break;
      case BuildingType::Industrial:
        metrics.industrialBuildings++;
        break;
    }
  }

  for (int y = district->minCorner.y; y <= district->maxCorner.y; ++y) {
    for (int x = district->minCorner.x; x <= district->maxCorner.x; ++x) {
      if (!map.isValid({x, y})) {
        continue;
      }
      const Tile& tile = map.getTile({x, y});
      totalLandValue += static_cast<int64_t>(tile.landValue);
      landValueTileCount++;
    }
  }

  if (landValueTileCount > 0) {
    metrics.averageLandValue = static_cast<float>(totalLandValue) / static_cast<float>(landValueTileCount);
  }

  const uint32_t cityPopulation = population.getTotalPopulation();
  const uint32_t cityEmployed = population.getTotalEmployed();
  float districtPopulationShare = 0.0f;

  if (cityResidentialCapacity > 0) {
    districtPopulationShare = static_cast<float>(districtResidentialCapacity) / static_cast<float>(cityResidentialCapacity);
  } else if (!buildings.empty()) {
    districtPopulationShare = static_cast<float>(metrics.buildings) / static_cast<float>(buildings.size());
  }

  districtPopulationShare = std::max(0.0f, std::min(1.0f, districtPopulationShare));

  metrics.population = static_cast<uint32_t>(std::lround(static_cast<double>(cityPopulation) * districtPopulationShare));
  const uint32_t districtEmployed = static_cast<uint32_t>(
    std::lround(static_cast<double>(cityEmployed) * districtPopulationShare)
  );

  PopulationStore districtPopulation;
  if (metrics.population > 0) {
    districtPopulation.createGroup(
      IncomeBand::Middle,
      metrics.population,
      std::min(metrics.population, districtEmployed)
    );
  }

  EconomyState economy = EconomySystem::calculateEconomy(districtStore, districtPopulation, district->taxRates);
  metrics.revenue = economy.totalRevenue;
  metrics.expenses = economy.totalExpenses;
  metrics.balance = economy.balance;
  metrics.economicHealth = economy.economicHealth;

  metrics.serviceBudgetTarget = static_cast<int64_t>(
    std::lround(static_cast<double>(std::max<int64_t>(0, metrics.revenue)) * district->serviceAllocation)
  );
  metrics.serviceBudgetAllocated = metrics.serviceBudgetTarget;
  if (district->serviceBudgetCap >= 0 && metrics.serviceBudgetAllocated > district->serviceBudgetCap) {
    metrics.serviceBudgetAllocated = district->serviceBudgetCap;
    metrics.serviceBudgetCapApplied = true;
  }

  float budgetFulfillment = 1.0f;
  if (metrics.serviceBudgetTarget > 0) {
    budgetFulfillment = static_cast<float>(metrics.serviceBudgetAllocated) /
      static_cast<float>(metrics.serviceBudgetTarget);
  }
  budgetFulfillment = std::max(0.0f, std::min(1.0f, budgetFulfillment));

  metrics.serviceCoverage = 0.0f;
  metrics.serviceCoveragePotential = 0.0f;
  if (roads != nullptr && facilities != nullptr) {
    std::vector<ServiceFacility> districtFacilities;

    if (!district->assignedFacilityIds.empty()) {
      for (uint32_t facilityId : district->assignedFacilityIds) {
        if (facilityId == 0 || facilityId > facilities->size()) {
          continue;
        }
        districtFacilities.push_back((*facilities)[facilityId - 1]);
      }
    } else {
      for (const ServiceFacility& facility : *facilities) {
        if (district->contains(facility.position)) {
          districtFacilities.push_back(facility);
        }
      }
    }

    if (!districtFacilities.empty() && districtStore.getBuildingCount() > 0) {
      const ServiceCoverageSummary coverage = ServiceSystem::evaluateCoverage(
        districtStore,
        *roads,
        districtFacilities
      );

      const float fireWeight = std::max(0.0f, district->servicePriorities.fireWeight);
      const float policeWeight = std::max(0.0f, district->servicePriorities.policeWeight);
      const float healthWeight = std::max(0.0f, district->servicePriorities.healthWeight);
      const float educationWeight = std::max(0.0f, district->servicePriorities.educationWeight);
      const float totalWeight = fireWeight + policeWeight + healthWeight + educationWeight;

      float weightedCoverage = coverage.overallCoverage;
      if (totalWeight > 0.0f) {
        weightedCoverage =
          (coverage.fireCoverage * fireWeight +
           coverage.policeCoverage * policeWeight +
           coverage.healthCoverage * healthWeight +
           coverage.educationCoverage * educationWeight) / totalWeight;
      }

      const float allocationFactor = 0.5f + (0.5f * std::max(0.0f, std::min(1.0f, district->serviceAllocation)));
      metrics.serviceCoveragePotential = std::max(0.0f, std::min(1.0f, weightedCoverage * allocationFactor));
      metrics.serviceCoverage = std::max(0.0f, std::min(1.0f, metrics.serviceCoveragePotential * budgetFulfillment));
    }
  }

  metrics.happiness = std::max(
    0.0f,
    std::min(
      1.0f,
      0.45f + (metrics.economicHealth * 0.35f) + (metrics.serviceCoverage * 0.20f)
    )
  );

  return metrics;
}

std::vector<DistrictMetrics> DistrictSystem::evaluateAllDistricts(
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork* roads,
  const std::vector<ServiceFacility>* facilities,
  int64_t sharedServiceBudgetPool
) {
  std::vector<DistrictMetrics> result;
  for (const auto& district : districts) {
    result.push_back(evaluateDistrictMetrics(district.id, map, store, population, roads, facilities));
  }

  applySharedBudgetBalancing(result, districts, sharedServiceBudgetPool);
  return result;
}

float DistrictSystem::computeGrowthPressureMultiplier(
  const District& district,
  const DistrictMetrics& metrics
) {
  float budgetFulfillment = 1.0f;
  if (metrics.serviceBudgetTarget > 0) {
    budgetFulfillment = static_cast<float>(metrics.serviceBudgetAllocated) /
      static_cast<float>(metrics.serviceBudgetTarget);
  }
  budgetFulfillment = std::max(0.0f, std::min(1.0f, budgetFulfillment));

  const float area = static_cast<float>(std::max<int64_t>(1, district.area()));
  const float density = static_cast<float>(metrics.buildings) / area;
  const float sparseBoost = std::max(0.0f, std::min(1.0f, (0.35f - density) / 0.35f));
  const float capPenalty = metrics.serviceBudgetCapApplied ? 0.08f : 0.0f;

  // Tuned to keep capped districts viable while still reflecting budget pressure.
  const float multiplier = 0.55f + (0.35f * budgetFulfillment) + (0.20f * sparseBoost) - capPenalty;
  return std::max(0.45f, std::min(1.15f, multiplier));
}

void DistrictSystem::clearDistricts() {
  districts.clear();
  nextDistrictId = 1;
}
