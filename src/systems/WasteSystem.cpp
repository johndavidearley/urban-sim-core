#include "src/systems/WasteSystem.hpp"

#include <algorithm>
#include <cmath>

#include "src/entities/EntityStore.hpp"

WasteSummary WasteSystem::step(
  const EntityStore& store,
  std::vector<ServiceFacility>& facilities,
  const ServiceCoverageSummary& coverage,
  const std::vector<ServiceFacility>* operatingFacilities
) {
  WasteSummary result;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    result.generated += static_cast<int64_t>(std::ceil(std::max(0, building.occupancy) * 0.25));
  }
  const auto& operating = operatingFacilities ? *operatingFacilities : facilities;
  for (const ServiceFacility& facility : operating) {
    if (facility.type == ServiceType::Incinerator) {
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
  // Burn old stock only after current residual, then deposit current leftovers.
  int64_t spareBurn = result.incinerationCapacity - result.incinerated;
  int64_t toLandfill = remainingReachable - result.incinerated;
  for (auto& facility : facilities) {
    if (facility.type != ServiceType::Garbage) continue;
    facility.landfillLastFill = 0;
    const int64_t stock = std::max<int64_t>(0, 80000 - facility.landfillRemaining);
    const int64_t burned = std::min(stock, spareBurn);
    facility.landfillRemaining += burned;
    facility.landfillLastFill -= burned;
    result.storedIncinerated += burned;
    spareBurn -= burned;
    const auto active = std::find_if(operating.begin(), operating.end(), [&](const auto& f) {
      return f.type == ServiceType::Garbage && f.position == facility.position;
    });
    const int64_t capacity = active == operating.end() ? 0 :
      static_cast<int64_t>(400.0f * std::max(0.0f, active->quality));
    result.collectionCapacity += std::min(capacity, facility.landfillRemaining);
    const int64_t deposited = std::min({toLandfill, capacity, facility.landfillRemaining});
    facility.landfillRemaining -= deposited;
    facility.landfillLastFill += deposited;
    result.landfilled += deposited;
    toLandfill -= deposited;
    result.landfillRemaining += facility.landfillRemaining;
    result.landfillFillRate += facility.landfillLastFill;
    if (facility.landfillRemaining == 0) {
      if (result.fullLandfills == 0) result.firstFullLandfill = facility.position;
      ++result.fullLandfills;
    }
  }
  result.incinerated += result.storedIncinerated;
  result.powerGenerationMW = static_cast<float>(result.incinerated) * 0.02f;
  result.uncollected = std::max<int64_t>(0, result.generated - result.recycled - (result.incinerated - result.storedIncinerated) - result.landfilled);
  if (result.generated > 0) {
    result.collectionRate = static_cast<float>(result.generated - result.uncollected)
      / static_cast<float>(result.generated);
    const float missed = 1.0f - result.collectionRate;
    // Citywide air-pollution increment, bounded at .03 for fully burned waste.
    result.pollutionPenalty = missed * 0.20f
      + std::min(1.0f, static_cast<float>(result.incinerated) / static_cast<float>(result.generated)) * 0.03f;
    result.happinessPenalty = missed * 0.25f;
  }
  if (result.generated == 0 && result.incinerated > 0) result.pollutionPenalty = 0.03f;
  return result;
}

WasteSummary WasteSystem::evaluate(const EntityStore& store,
  const std::vector<ServiceFacility>& facilities, const ServiceCoverageSummary& coverage) {
  auto preview = facilities;
  auto result = step(store, preview, coverage);
  result.landfillRemaining = 0;
  result.landfillFillRate = 0;
  result.fullLandfills = 0;
  for (const auto& facility : facilities) {
    if (facility.type != ServiceType::Garbage) continue;
    result.landfillRemaining += facility.landfillRemaining;
    result.landfillFillRate += facility.landfillLastFill;
    if (facility.landfillRemaining == 0) {
      if (result.fullLandfills == 0) result.firstFullLandfill = facility.position;
      ++result.fullLandfills;
    }
  }
  return result;
}
