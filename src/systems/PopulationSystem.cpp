#include "src/systems/PopulationSystem.hpp"

#include <algorithm>
#include <array>
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

std::array<uint32_t, 3> splitByWeights(uint32_t total, const std::array<uint32_t, 3>& weights) {
  std::array<uint32_t, 3> split{0u, 0u, 0u};
  const uint32_t weightSum = weights[0] + weights[1] + weights[2];
  if (total == 0 || weightSum == 0) {
    return split;
  }

  uint32_t assigned = 0;
  for (size_t i = 0; i < split.size(); ++i) {
    split[i] = (total * weights[i]) / weightSum;
    assigned += split[i];
  }

  size_t idx = 0;
  while (assigned < total) {
    split[idx % split.size()] += 1u;
    ++idx;
    ++assigned;
  }

  return split;
}

void allocateBandToJobTypes(
  uint32_t employed,
  uint32_t preferredCommercialPercent,
  uint32_t& remainingCommercial,
  uint32_t& remainingIndustrial,
  uint32_t& assignedCommercial,
  uint32_t& assignedIndustrial
) {
  if (employed == 0) {
    return;
  }

  uint32_t desiredCommercial = (employed * preferredCommercialPercent) / 100u;
  uint32_t desiredIndustrial = employed - desiredCommercial;

  uint32_t commercial = std::min(desiredCommercial, remainingCommercial);
  uint32_t industrial = std::min(desiredIndustrial, remainingIndustrial);

  uint32_t assigned = commercial + industrial;
  if (assigned < employed) {
    uint32_t remaining = employed - assigned;

    const uint32_t moreIndustrial = std::min(remaining, remainingIndustrial - industrial);
    industrial += moreIndustrial;
    remaining -= moreIndustrial;

    const uint32_t moreCommercial = std::min(remaining, remainingCommercial - commercial);
    commercial += moreCommercial;
  }

  remainingCommercial -= commercial;
  remainingIndustrial -= industrial;
  assignedCommercial += commercial;
  assignedIndustrial += industrial;
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
  const uint32_t commercialCapacity = capacityFor(store, commercial);
  const uint32_t industrialCapacity = capacityFor(store, industrial);
  const uint32_t jobCapacity = commercialCapacity + industrialCapacity;

  const uint32_t housed = std::min(requestedPopulation, housingCapacity);
  const uint32_t employed = std::min(housed, jobCapacity);
  const uint32_t unemployed = housed - employed;

  assignOccupancy(store, residential, housed, seed + 17u);

  // Composition model: low/middle/high income split with deterministic band employment shares.
  const std::array<uint32_t, 3> housedByBand = splitByWeights(housed, {50u, 35u, 15u});
  const std::array<uint32_t, 3> employedByBand = splitByWeights(employed, {30u, 40u, 30u});

  // Job matching constraints by income band.
  // Low income prefers industrial jobs, high income prefers commercial jobs.
  uint32_t remainingCommercial = commercialCapacity;
  uint32_t remainingIndustrial = industrialCapacity;
  uint32_t commercialShare = 0u;
  uint32_t industrialShare = 0u;

  allocateBandToJobTypes(
    employedByBand[0],
    30u,
    remainingCommercial,
    remainingIndustrial,
    commercialShare,
    industrialShare
  );
  allocateBandToJobTypes(
    employedByBand[1],
    50u,
    remainingCommercial,
    remainingIndustrial,
    commercialShare,
    industrialShare
  );
  allocateBandToJobTypes(
    employedByBand[2],
    75u,
    remainingCommercial,
    remainingIndustrial,
    commercialShare,
    industrialShare
  );

  assignOccupancy(store, commercial, commercialShare, seed + 29u);
  assignOccupancy(store, industrial, industrialShare, seed + 43u);

  population.clear();
  if (housedByBand[0] > 0) {
    population.createGroup(IncomeBand::Low, housedByBand[0], std::min(housedByBand[0], employedByBand[0]));
  }
  if (housedByBand[1] > 0) {
    population.createGroup(IncomeBand::Middle, housedByBand[1], std::min(housedByBand[1], employedByBand[1]));
  }
  if (housedByBand[2] > 0) {
    population.createGroup(IncomeBand::High, housedByBand[2], std::min(housedByBand[2], employedByBand[2]));
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

  summary.lowIncomePopulation = housedByBand[0];
  summary.middleIncomePopulation = housedByBand[1];
  summary.highIncomePopulation = housedByBand[2];
  summary.lowIncomeEmployed = std::min(housedByBand[0], employedByBand[0]);
  summary.middleIncomeEmployed = std::min(housedByBand[1], employedByBand[1]);
  summary.highIncomeEmployed = std::min(housedByBand[2], employedByBand[2]);

  return summary;
}

void PopulationSystem::applyToMetrics(const PopulationSummary& summary, CityMetrics& metrics) {
  metrics.population = summary.housedPopulation;
  metrics.availableHousing = summary.availableHousing;
  metrics.availableJobs = summary.availableJobs;
  metrics.unemployment = summary.unemploymentRate;
}
