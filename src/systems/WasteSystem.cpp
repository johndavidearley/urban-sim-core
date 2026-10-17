#include "src/systems/WasteSystem.hpp"

#include <algorithm>
#include <cmath>

#include "src/entities/EntityStore.hpp"

WasteSummary WasteSystem::evaluate(
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  const ServiceCoverageSummary& coverage
) {
  WasteSummary result;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    result.generated += static_cast<int64_t>(std::ceil(std::max(0, building.occupancy) * 0.25));
  }
  for (const ServiceFacility& facility : facilities) {
    if (facility.type == ServiceType::Garbage) {
      result.collectionCapacity += static_cast<int64_t>(400.0f * std::max(0.0f, facility.quality));
    } else if (facility.type == ServiceType::Incinerator) {
      result.incinerationCapacity += static_cast<int64_t>(300.0f * std::clamp(facility.quality, 0.0f, 1.0f));
    } else if (facility.type == ServiceType::Recycling) {
      result.recyclingCapacity += static_cast<int64_t>(200.0f * std::max(0.0f, facility.quality));
    }
  }

  const int64_t reachable = static_cast<int64_t>(std::floor(
    result.generated * std::max({coverage.garbageCoverage, coverage.recyclingCoverage, coverage.incineratorCoverage})));
  const int64_t recyclable = static_cast<int64_t>(std::floor(result.generated * 0.35f));
  result.recycled = std::min({recyclable, result.recyclingCapacity, reachable});
  const int64_t remainingReachable = std::max<int64_t>(0, reachable - result.recycled);
  const int64_t burnable = std::min(remainingReachable, static_cast<int64_t>(
    std::floor(result.generated * coverage.incineratorCoverage)));
  result.incinerated = std::min(result.incinerationCapacity, burnable);
  result.powerGenerationMW = static_cast<float>(result.incinerated) * 0.02f;
  result.landfilled = std::min(result.collectionCapacity, remainingReachable - result.incinerated);
  result.uncollected = std::max<int64_t>(0, result.generated - result.recycled - result.incinerated - result.landfilled);
  if (result.generated > 0) {
    result.collectionRate = static_cast<float>(result.generated - result.uncollected)
      / static_cast<float>(result.generated);
    const float missed = 1.0f - result.collectionRate;
    // Citywide air-pollution increment, bounded at .03 for fully burned waste.
    result.pollutionPenalty = missed * 0.20f
      + static_cast<float>(result.incinerated) / static_cast<float>(result.generated) * 0.03f;
    result.happinessPenalty = missed * 0.25f;
  }
  return result;
}
