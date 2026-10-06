#include "src/systems/PopulationSystem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
// EntityStore already keeps ID-sorted type indices.
const std::vector<EntityId>& collectBuildingIds(const EntityStore& store, BuildingType type) {
  return store.idsByBuildingType(type);
}

uint32_t capacityFor(const EntityStore& store, BuildingType type) {
  return store.capacityOfType(type);
}

void resetAllOccupancy(EntityStore& store) {
  static constexpr BuildingType kTypes[] = {
    BuildingType::Residential, BuildingType::Commercial,
    BuildingType::Industrial, BuildingType::Office
  };
  for (BuildingType type : kTypes) {
    for (EntityId id : store.idsByBuildingType(type)) {
      Building* building = store.getBuilding(id);
      if (building != nullptr) {
        building->occupancy = 0;
      }
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

// Integer lerp for the education weight tables. Endpoints are exact so
// coverage 0 and coverage 1 do not round.
uint32_t lerpU(uint32_t from, uint32_t to, float t) {
  if (t <= 0.0f) {
    return from;
  }
  if (t >= 1.0f) {
    return to;
  }
  const double mixed =
    (1.0 - static_cast<double>(t)) * static_cast<double>(from)
    + static_cast<double>(t) * static_cast<double>(to);
  return static_cast<uint32_t>(std::lround(mixed));
}

// Splits one income band's employed count across job types {commercial,
// industrial, office} by preference weight, then spills any shortfall (a
// preferred type running out of capacity) into whichever types still have
// room, in a fixed order for determinism.
// officeOverflowBudget, when non-null, caps how many of those spilled
// workers may take an office seat. Null means unlimited (coverage 0).
// Preference assignments do not draw from the budget.
std::array<uint32_t, 3> allocateBandToJobTypes(
  uint32_t employed,
  const std::array<uint32_t, 3>& preferenceWeights,
  std::array<uint32_t, 3>& remainingByType,
  uint32_t* officeOverflowBudget
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
      uint32_t room = remainingByType[i] - assigned[i];
      if (i == 2 && officeOverflowBudget != nullptr) {
        room = std::min(room, *officeOverflowBudget);
      }
      const uint32_t extra = std::min(remainingNeed, room);
      assigned[i] += extra;
      remainingNeed -= extra;
      if (i == 2 && officeOverflowBudget != nullptr) {
        *officeOverflowBudget -= extra;
      }
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
  uint32_t seed,
  float educationCoverage
) {
  resetAllOccupancy(store);

  const std::vector<EntityId>& residential = collectBuildingIds(store, BuildingType::Residential);
  const std::vector<EntityId>& commercial = collectBuildingIds(store, BuildingType::Commercial);
  const std::vector<EntityId>& industrial = collectBuildingIds(store, BuildingType::Industrial);
  const std::vector<EntityId>& office = collectBuildingIds(store, BuildingType::Office);

  const uint32_t housingCapacity = capacityFor(store, BuildingType::Residential);
  const uint32_t commercialCapacity = capacityFor(store, BuildingType::Commercial);
  const uint32_t industrialCapacity = capacityFor(store, BuildingType::Industrial);
  const uint32_t officeCapacity = capacityFor(store, BuildingType::Office);
  const uint32_t jobCapacity = commercialCapacity + industrialCapacity + officeCapacity;

  const uint32_t housed = std::min(requestedPopulation, housingCapacity);
  const uint32_t employable = std::min(housed, jobCapacity);

  assignOccupancy(store, residential, housed, seed + 17u);

  // Uneducated baseline (coverage <= 0). These literals are the historical
  // split; coverage 0 must keep them bit-for-bit.
  // Full coverage targets, also summing to 100:
  //   housed {30, 40, 30}, employed {20, 40, 40}
  //   jobs {commercial, industrial, office}:
  //     low {30, 70, 0} (unchanged; low income never prefers office)
  //     middle {25, 30, 45}, high {20, 5, 75}
  // Between the endpoints the weights lerp. There is no cliff at 100%.
  // Office overflow (workers a band could not place by preference) scales
  // from the full office capacity down to zero, so spare office seats stay
  // empty instead of being given to the low band. Industrial, then
  // commercial, absorb the workers office cannot take.
  const bool educated = educationCoverage > 0.0f;
  const float coverage = educated ? std::min(educationCoverage, 1.0f) : 0.0f;

  std::array<uint32_t, 3> housedWeights{50u, 35u, 15u};
  std::array<uint32_t, 3> employedWeights{30u, 40u, 30u};
  const std::array<uint32_t, 3> lowJobs{30u, 70u, 0u};
  std::array<uint32_t, 3> middleJobs{45u, 40u, 15u};
  std::array<uint32_t, 3> highJobs{35u, 10u, 55u};
  uint32_t officeOverflowBudget = 0;
  uint32_t* officeOverflow = nullptr;
  if (educated) {
    housedWeights = std::array<uint32_t, 3>{
      lerpU(50u, 30u, coverage), lerpU(35u, 40u, coverage), lerpU(15u, 30u, coverage)};
    employedWeights = std::array<uint32_t, 3>{
      lerpU(30u, 20u, coverage), lerpU(40u, 40u, coverage), lerpU(30u, 40u, coverage)};
    middleJobs = std::array<uint32_t, 3>{
      lerpU(45u, 25u, coverage), lerpU(40u, 30u, coverage), lerpU(15u, 45u, coverage)};
    highJobs = std::array<uint32_t, 3>{
      lerpU(35u, 20u, coverage), lerpU(10u, 5u, coverage), lerpU(55u, 75u, coverage)};
    officeOverflowBudget = static_cast<uint32_t>(std::lround(
      (1.0 - static_cast<double>(coverage)) * static_cast<double>(officeCapacity)));
    officeOverflow = &officeOverflowBudget;
  }

  const std::array<uint32_t, 3> housedByBand = splitByWeights(housed, housedWeights);
  const std::array<uint32_t, 3> employedByBand = splitByWeights(employable, employedWeights);

  std::array<uint32_t, 3> remainingByType{commercialCapacity, industrialCapacity, officeCapacity};
  std::array<uint32_t, 3> assignedByType{0u, 0u, 0u};
  std::array<uint32_t, 3> placedByBand{0u, 0u, 0u};

  const auto addBand = [&](size_t band, uint32_t bandEmployed, const std::array<uint32_t, 3>& weights) {
    const std::array<uint32_t, 3> a =
      allocateBandToJobTypes(bandEmployed, weights, remainingByType, officeOverflow);
    placedByBand[band] = a[0] + a[1] + a[2];
    for (size_t i = 0; i < 3; ++i) {
      assignedByType[i] += a[i];
    }
  };
  // Low, then middle, then high. Coverage 0 must keep this order: the low
  // band reaches office only through overflow, and later bands see what is left.
  addBand(0, employedByBand[0], lowJobs);
  addBand(1, employedByBand[1], middleJobs);
  addBand(2, employedByBand[2], highJobs);

  assignOccupancy(store, commercial, assignedByType[0], seed + 29u);
  assignOccupancy(store, industrial, assignedByType[1], seed + 43u);
  assignOccupancy(store, office, assignedByType[2], seed + 53u);

  const auto storedEmployed = [&](size_t band) {
    return std::min(housedByBand[band], placedByBand[band]);
  };

  population.clear();
  if (housedByBand[0] > 0) {
    population.createGroup(IncomeBand::Low, housedByBand[0], storedEmployed(0));
  }
  if (housedByBand[1] > 0) {
    population.createGroup(IncomeBand::Middle, housedByBand[1], storedEmployed(1));
  }
  if (housedByBand[2] > 0) {
    population.createGroup(IncomeBand::High, housedByBand[2], storedEmployed(2));
  }

  const uint32_t assignedJobs = assignedByType[0] + assignedByType[1] + assignedByType[2];

  PopulationSummary summary;
  summary.requestedPopulation = requestedPopulation;
  summary.housedPopulation = housed;
  summary.employedPopulation = assignedJobs;
  summary.unemployedPopulation = housed - assignedJobs;
  summary.availableHousing = housingCapacity - housed;
  summary.availableJobs = jobCapacity - assignedJobs;
  if (housed > 0) {
    summary.unemploymentRate =
      static_cast<float>(summary.unemployedPopulation) / static_cast<float>(housed);
  }

  summary.lowIncomePopulation = housedByBand[0];
  summary.middleIncomePopulation = housedByBand[1];
  summary.highIncomePopulation = housedByBand[2];
  summary.lowIncomeEmployed = storedEmployed(0);
  summary.middleIncomeEmployed = storedEmployed(1);
  summary.highIncomeEmployed = storedEmployed(2);

  return summary;
}

PopulationSummary PopulationSystem::summarize(
  const EntityStore& store,
  const PopulationStore& population
) {
  PopulationSummary summary;
  summary.housedPopulation = population.getTotalPopulation();
  summary.employedPopulation = population.getTotalEmployed();
  summary.requestedPopulation = summary.housedPopulation;
  summary.unemployedPopulation = summary.housedPopulation > summary.employedPopulation
    ? summary.housedPopulation - summary.employedPopulation : 0u;

  const uint32_t housingCapacity = store.capacityOfType(BuildingType::Residential);
  const uint32_t jobCapacity =
    store.capacityOfType(BuildingType::Commercial)
    + store.capacityOfType(BuildingType::Industrial)
    + store.capacityOfType(BuildingType::Office);
  summary.availableHousing = housingCapacity > summary.housedPopulation
    ? housingCapacity - summary.housedPopulation : 0u;
  summary.availableJobs = jobCapacity > summary.employedPopulation
    ? jobCapacity - summary.employedPopulation : 0u;
  if (summary.housedPopulation > 0) {
    summary.unemploymentRate =
      static_cast<float>(summary.unemployedPopulation) / static_cast<float>(summary.housedPopulation);
  }

  for (const auto& [id, group] : population.getGroups()) {
    (void)id;
    switch (group.band) {
      case IncomeBand::Low:
        summary.lowIncomePopulation += group.size;
        summary.lowIncomeEmployed += group.employed;
        break;
      case IncomeBand::Middle:
        summary.middleIncomePopulation += group.size;
        summary.middleIncomeEmployed += group.employed;
        break;
      case IncomeBand::High:
        summary.highIncomePopulation += group.size;
        summary.highIncomeEmployed += group.employed;
        break;
    }
  }
  return summary;
}

void PopulationSystem::applyToMetrics(const PopulationSummary& summary, CityMetrics& metrics) {
  metrics.population = summary.housedPopulation;
  metrics.availableHousing = summary.availableHousing;
  metrics.availableJobs = summary.availableJobs;
  metrics.unemployment = summary.unemploymentRate;
  metrics.lowIncomePopulation = summary.lowIncomePopulation;
  metrics.middleIncomePopulation = summary.middleIncomePopulation;
  metrics.highIncomePopulation = summary.highIncomePopulation;
}
