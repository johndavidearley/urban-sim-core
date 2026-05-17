#include "DistrictSystem.hpp"
#include <algorithm>

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

DistrictMetrics DistrictSystem::evaluateDistrictMetrics(
  DistrictId id,
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population
) {
  DistrictMetrics metrics;
  metrics.districtId = id;

  const District* district = getDistrictConst(id);
  if (district == nullptr) {
    return metrics;
  }

  metrics.districtName = district->name;

  const auto& buildings = store.getBuildings();
  int64_t totalLandValue = 0;
  uint32_t landValueTileCount = 0;

  for (const auto& [buildingId, building] : buildings) {
    (void)buildingId;
    if (!district->contains(building.position)) {
      continue;
    }

    metrics.buildings++;
    switch (building.type) {
      case BuildingType::Residential:
        metrics.residentialBuildings++;
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

  for (const auto& [groupId, group] : population.getGroups()) {
    (void)groupId;
    metrics.population += group.size;
  }

  EconomyState economy = EconomySystem::calculateEconomy(store, population, district->taxRates);
  metrics.revenue = economy.totalRevenue;
  metrics.expenses = economy.totalExpenses;
  metrics.balance = economy.balance;

  metrics.happiness = 0.65f;
  metrics.serviceCoverage = 0.75f;

  return metrics;
}

std::vector<DistrictMetrics> DistrictSystem::evaluateAllDistricts(
  const CityMap& map,
  const EntityStore& store,
  const PopulationStore& population
) {
  std::vector<DistrictMetrics> result;
  for (const auto& district : districts) {
    result.push_back(evaluateDistrictMetrics(district.id, map, store, population));
  }
  return result;
}

void DistrictSystem::clearDistricts() {
  districts.clear();
  nextDistrictId = 1;
}
