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

  // Collect building pointers and total capacity in one pass (O(buildings) hash lookups,
  // not O(people) as in per-person round-robin).
  struct Slot { Building* b; };
  std::vector<Slot> slots;
  slots.reserve(ids.size());
  uint32_t totalCap = 0;
  for (EntityId id : ids) {
    Building* b = store.getBuilding(id);
    if (b != nullptr && b->capacity > 0) {
      slots.push_back({b});
      totalCap += static_cast<uint32_t>(b->capacity);
    }
  }
  if (slots.empty() || totalCap == 0) {
    return;
  }

  // Proportional fill: each building gets floor(capped * capacity / totalCap).
  const uint32_t capped = std::min(people, totalCap);
  uint32_t assigned = 0;
  for (const Slot& s : slots) {
    const uint32_t share = (capped * static_cast<uint32_t>(s.b->capacity)) / totalCap;
    s.b->occupancy = static_cast<int>(share);
    assigned += share;
  }

  // Distribute remainder round-robin so total is exact.
  uint32_t remaining = capped - assigned;
  size_t idx = static_cast<size_t>(seed % static_cast<uint32_t>(slots.size()));
  while (remaining > 0) {
    Building* b = slots[idx % slots.size()].b;
    if (b->occupancy < b->capacity) {
      ++b->occupancy;
      --remaining;
    }
    ++idx;
    if (idx == slots.size() * 2) break; // guard against infinite loop when all full
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

// Splits one income band's employed count across job types {commercial,
// industrial, office} by preference weight, then spills any shortfall (a
// preferred type running out of capacity) into whichever types still have
// room, in a fixed order for determinism.
std::array<uint32_t, 3> allocateBandToJobTypes(
  uint32_t employed,
  const std::array<uint32_t, 3>& preferenceWeights,
  std::array<uint32_t, 3>& remainingByType
) {
  std::array<uint32_t, 3> assigned{0u, 0u, 0u};
  if (employed == 0) {
    return assigned;
  }

  const std::array<uint32_t, 3> desired = splitByWeights(employed, preferenceWeights);
  for (size_t i = 0; i < 3; ++i) {
    assigned[i] = std::min(desired[i], remainingByType[i]);
  }

  uint32_t totalAssigned = assigned[0] + assigned[1] + assigned[2];
  if (totalAssigned < employed) {
    uint32_t remainingNeed = employed - totalAssigned;
    // Overflow order (industrial, commercial, office) matches the original
    // two-type model's preference for spilling into industrial first.
    static constexpr size_t kOverflowOrder[3] = {1, 0, 2};
    for (size_t oi = 0; oi < 3 && remainingNeed > 0; ++oi) {
      const size_t i = kOverflowOrder[oi];
      const uint32_t extra = std::min(remainingNeed, remainingByType[i] - assigned[i]);
      assigned[i] += extra;
      remainingNeed -= extra;
    }
  }

  for (size_t i = 0; i < 3; ++i) {
    remainingByType[i] -= assigned[i];
  }
  return assigned;
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
  const std::vector<EntityId> office = collectBuildingIds(store, BuildingType::Office);

  const uint32_t housingCapacity = capacityFor(store, residential);
  const uint32_t commercialCapacity = capacityFor(store, commercial);
  const uint32_t industrialCapacity = capacityFor(store, industrial);
  const uint32_t officeCapacity = capacityFor(store, office);
  const uint32_t jobCapacity = commercialCapacity + industrialCapacity + officeCapacity;

  const uint32_t housed = std::min(requestedPopulation, housingCapacity);
  const uint32_t employed = std::min(housed, jobCapacity);
  const uint32_t unemployed = housed - employed;

  assignOccupancy(store, residential, housed, seed + 17u);

  // Composition model: low/middle/high income split with deterministic band employment shares.
  const std::array<uint32_t, 3> housedByBand = splitByWeights(housed, {50u, 35u, 15u});
  const std::array<uint32_t, 3> employedByBand = splitByWeights(employed, {30u, 40u, 30u});

  // Job matching preferences by income band: {commercial, industrial, office}.
  // Low income skews industrial with little office access; middle income is
  // balanced with a modest office share; high income is office-and-commercial
  // heavy with little industrial - a rough proxy for blue-collar vs.
  // white-collar employment following income.
  std::array<uint32_t, 3> remainingByType{commercialCapacity, industrialCapacity, officeCapacity};
  std::array<uint32_t, 3> assignedByType{0u, 0u, 0u};

  const auto addBand = [&](uint32_t bandEmployed, const std::array<uint32_t, 3>& weights) {
    const std::array<uint32_t, 3> a = allocateBandToJobTypes(bandEmployed, weights, remainingByType);
    for (size_t i = 0; i < 3; ++i) {
      assignedByType[i] += a[i];
    }
  };
  addBand(employedByBand[0], {30u, 70u, 0u});
  addBand(employedByBand[1], {45u, 40u, 15u});
  addBand(employedByBand[2], {35u, 10u, 55u});

  assignOccupancy(store, commercial, assignedByType[0], seed + 29u);
  assignOccupancy(store, industrial, assignedByType[1], seed + 43u);
  assignOccupancy(store, office, assignedByType[2], seed + 53u);

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
