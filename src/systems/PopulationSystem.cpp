#include "src/systems/PopulationSystem.hpp"

#include <algorithm>
#include <vector>

namespace {
std::vector<EntityId> collectBuildingIds(const EntityStore& store, BuildingType type) {
  std::vector<EntityId> ids;
  ids.reserve(store.getBuildings().size());

  for (const auto& [id, building] : store.getBuildings()) {
    if (building.type == type) {
      ids.push_back(id);
    }
  }

  std::sort(ids.begin(), ids.end());
  return ids;
}

uint32_t capacityFor(const EntityStore& store, const std::vector<EntityId>& ids) {
  uint32_t total = 0;
  for (EntityId id : ids) {
    const Building* building = store.getBuilding(id);
    if (building != nullptr && building->capacity > 0) {
      total += static_cast<uint32_t>(building->capacity);
    }
  }
  return total;
}

void resetAllOccupancy(EntityStore& store) {
  for (const auto& [id, entry] : store.getBuildings()) {
    (void)entry;
    Building* building = store.getBuilding(id);
    if (building != nullptr) {
      building->occupancy = 0;
    }
  }
}

void assignOccupancy(
  EntityStore& store,
  const std::vector<EntityId>& ids,
  uint32_t people,
  uint32_t seed
) {
  if (ids.empty() || people == 0) {
    return;
  }

  size_t index = static_cast<size_t>(seed % static_cast<uint32_t>(ids.size()));
  uint32_t remaining = people;

  while (remaining > 0) {
    bool placedAny = false;
    for (size_t i = 0; i < ids.size() && remaining > 0; ++i) {
      const size_t slot = (index + i) % ids.size();
      Building* building = store.getBuilding(ids[slot]);
      if (building == nullptr || building->capacity <= building->occupancy) {
        continue;
      }

      ++building->occupancy;
      --remaining;
      placedAny = true;
    }

    if (!placedAny) {
      break;
    }
    index = (index + 1) % ids.size();
  }
}
} // namespace

PopulationSummary PopulationSystem::allocate(
  EntityStore& store,
  PopulationStore& population,
  uint32_t requestedPopulation,
  uint32_t seed
) {
  resetAllOccupancy(store);

  const std::vector<EntityId> residential = collectBuildingIds(store, BuildingType::Residential);
  const std::vector<EntityId> commercial = collectBuildingIds(store, BuildingType::Commercial);
  const std::vector<EntityId> industrial = collectBuildingIds(store, BuildingType::Industrial);

  const uint32_t housingCapacity = capacityFor(store, residential);
  const uint32_t jobCapacity = capacityFor(store, commercial) + capacityFor(store, industrial);

  const uint32_t housed = std::min(requestedPopulation, housingCapacity);
  const uint32_t employed = std::min(housed, jobCapacity);
  const uint32_t unemployed = housed - employed;

  assignOccupancy(store, residential, housed, seed + 17u);

  uint32_t commercialShare = employed / 2u;
  commercialShare = std::min(commercialShare, capacityFor(store, commercial));
  uint32_t industrialShare = employed - commercialShare;
  const uint32_t industrialCapacity = capacityFor(store, industrial);
  if (industrialShare > industrialCapacity) {
    const uint32_t overflow = industrialShare - industrialCapacity;
    industrialShare = industrialCapacity;
    commercialShare = std::min(commercialShare + overflow, capacityFor(store, commercial));
  }

  assignOccupancy(store, commercial, commercialShare, seed + 29u);
  assignOccupancy(store, industrial, industrialShare, seed + 43u);

  population.clear();
  if (housed > 0) {
    population.createGroup(housed, employed);
  }

  PopulationSummary summary;
  summary.requestedPopulation = requestedPopulation;
  summary.housedPopulation = housed;
  summary.employedPopulation = employed;
  summary.unemployedPopulation = unemployed;
  summary.availableHousing = housingCapacity - housed;
  summary.availableJobs = jobCapacity - employed;
  if (housed > 0) {
    summary.unemploymentRate = static_cast<float>(unemployed) / static_cast<float>(housed);
  }

  return summary;
}

void PopulationSystem::applyToMetrics(const PopulationSummary& summary, CityMetrics& metrics) {
  metrics.population = summary.housedPopulation;
  metrics.availableHousing = summary.availableHousing;
  metrics.availableJobs = summary.availableJobs;
  metrics.unemployment = summary.unemploymentRate;
}
