#include "src/gameplay/ServiceTool.hpp"

#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

int64_t ServiceTool::constructionCost(ServiceType type) {
  switch (type) {
    case ServiceType::Fire: return 5000;
    case ServiceType::Police: return 4500;
    case ServiceType::Health: return 6500;
    case ServiceType::Education: return 5500;
    case ServiceType::Power: return 12000;
    case ServiceType::Water: return 8000;
    case ServiceType::Sanitation: return 7000;
    case ServiceType::Garbage: return 9000;
    case ServiceType::Recycling: return 11000;
    case ServiceType::Cemetery: return 10000;
    case ServiceType::Crematorium: return 14000;
    default: return 5000;
  }
}

int64_t ServiceTool::operatingCostPerTick(ServiceType type) {
  switch (type) {
    case ServiceType::Fire: return 25;
    case ServiceType::Police: return 22;
    case ServiceType::Health: return 35;
    case ServiceType::Education: return 30;
    case ServiceType::Power: return 45;
    case ServiceType::Water: return 28;
    case ServiceType::Sanitation: return 26;
    case ServiceType::Garbage: return 32;
    case ServiceType::Recycling: return 38;
    case ServiceType::Cemetery: return 24;
    case ServiceType::Crematorium: return 42;
    default: return 0;
  }
}

int ServiceTool::coverageDistance(ServiceType type) {
  switch (type) {
    case ServiceType::Fire: return 16;
    case ServiceType::Police: return 14;
    case ServiceType::Health: return 18;
    case ServiceType::Education: return 20;
    case ServiceType::Power: return 22;
    case ServiceType::Water: return 20;
    case ServiceType::Sanitation: return 18;
    case ServiceType::Garbage: return 18;
    case ServiceType::Recycling: return 16;
    case ServiceType::Cemetery: return 18;
    case ServiceType::Crematorium: return 20;
    default: return 12;
  }
}

ServicePlan ServiceTool::plan(
  const CityMap& map,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities,
  ServiceType type,
  Coord position,
  int64_t availableFunds
) {
  ServicePlan result;
  result.hasSite = true;
  result.facility = ServiceFacility{type, position, coverageDistance(type), 1.0f};
  result.cost = constructionCost(type);
  if (!map.isValid(position)) {
    result.error = "site is outside the map";
    return result;
  }

  const Tile& tile = map.getTile(position);
  if (tile.type == 2) {
    result.error = "cannot build on water";
    return result;
  }
  if (tile.hasRoad) {
    result.error = "cannot build on a road";
    return result;
  }
  if (tile.buildingId != 0) {
    result.error = "site is occupied";
    return result;
  }
  for (const ServiceFacility& facility : facilities) {
    if (facility.position == position) {
      result.error = "site already has a service";
      return result;
    }
  }

  Coord roadAnchor;
  if (!roads.resolveRoadAnchor(position, roadAnchor)) {
    result.error = "service requires road access";
    return result;
  }
  if (result.cost > availableFunds) {
    result.error = "insufficient funds";
    return result;
  }

  result.valid = true;
  return result;
}

bool ServiceTool::build(
  const CityMap& map,
  const RoadNetwork& roads,
  std::vector<ServiceFacility>& facilities,
  const ServicePlan& plan,
  int64_t& availableFunds
) {
  if (!plan.valid || plan.cost > availableFunds) {
    return false;
  }
  const ServicePlan current = ServiceTool::plan(
    map, roads, facilities, plan.facility.type, plan.facility.position, availableFunds
  );
  if (!current.valid || current.cost != plan.cost) {
    return false;
  }
  facilities.push_back(current.facility);
  availableFunds -= current.cost;
  return true;
}
