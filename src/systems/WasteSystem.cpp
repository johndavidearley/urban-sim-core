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
    } else if (facility.type == ServiceType::Recycling) {
      result.recyclingCapacity += static_cast<int64_t>(200.0f * std::max(0.0f, facility.quality));
    }
  }

  const int64_t reachable = static_cast<int64_t>(std::floor(
    result.generated * std::max(coverage.garbageCoverage, coverage.recyclingCoverage)));
  const int64_t recyclable = static_cast<int64_t>(std::floor(result.generated * 0.35f));
  result.recycled = std::min({recyclable, result.recyclingCapacity, reachable});
  const int64_t remainingReachable = std::max<int64_t>(0, reachable - result.recycled);
  result.landfilled = std::min(result.collectionCapacity, remainingReachable);
  result.uncollected = std::max<int64_t>(0, result.generated - result.recycled - result.landfilled);
  if (result.generated > 0) {
    result.collectionRate = static_cast<float>(result.generated - result.uncollected)
      / static_cast<float>(result.generated);
    const float missed = 1.0f - result.collectionRate;
    result.pollutionPenalty = missed * 0.20f;
    result.happinessPenalty = missed * 0.25f;
  }
  return result;
}
