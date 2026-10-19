#include "src/gameplay/ServiceTool.hpp"
#include "src/gameplay/CatalogPrices.hpp"
#include "src/world/Zoning.hpp"

#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"

PowerPlantSpec ServiceTool::powerPlant(PowerSourceType source) {
  switch (source) {
    case PowerSourceType::Coal: return {10000, 40, 80.0f, 1000.0f, false};
    case PowerSourceType::NaturalGas: return {catalog::gasCost, 36, 60.0f, 450.0f, false};
    case PowerSourceType::Nuclear: return {catalog::nuclearCost, 90, 200.0f, 12.0f, false};
    case PowerSourceType::Solar: return {catalog::solarCost, 8, 15.0f, 45.0f, false};
    case PowerSourceType::Wind: return {6000, 6, 10.0f, 11.0f, false};
    case PowerSourceType::Hydro: return {catalog::hydroCost, 18, 40.0f, 24.0f, true};
    case PowerSourceType::Substation: return {catalog::substationCost, 4, 0.0f, 0.0f, false};
    case PowerSourceType::Generic:
    default: return {12000, 45, 100.0f, 400.0f, false};
  }
}

int64_t ServiceTool::constructionCost(ServiceType type, bool surfaceWater, PowerSourceType source, bool sewageTreatment) {
  if (type == ServiceType::Power) return powerPlant(source).constructionCost;
  if (surfaceWater && type == ServiceType::Water) return 3000;
  if (surfaceWater && type == ServiceType::Sanitation) return sewageTreatment ? catalog::treatmentCost : 2000;
  switch (type) {
    case ServiceType::Fire: return 5000;
    case ServiceType::Police: return 4500;
    case ServiceType::Health: return 6500;
    case ServiceType::Education: return 5500;
    case ServiceType::Power: return 12000;
    case ServiceType::Water: return 8000;
    case ServiceType::Sanitation: return 7000;
    case ServiceType::Garbage: return 9000;
    case ServiceType::Recycling: return catalog::recyclingCost;
    case ServiceType::Cemetery: return catalog::cemeteryCost;
    case ServiceType::Crematorium: return catalog::crematoriumCost;
    case ServiceType::Incinerator: return catalog::incineratorCost;
    default: return 5000;
  }
}

int64_t ServiceTool::operatingCostPerTick(ServiceType type, bool surfaceWater, PowerSourceType source, bool sewageTreatment) {
  if (type == ServiceType::Power) return powerPlant(source).operatingCost;
  if (surfaceWater && type == ServiceType::Water) return 12;
  if (surfaceWater && type == ServiceType::Sanitation) return sewageTreatment ? 34 : 8;
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
    case ServiceType::Incinerator: return 60;
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
    case ServiceType::Incinerator: return 18;
    default: return 12;
  }
}

ServicePlan ServiceTool::plan(
  const CityMap& map,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities,
  ServiceType type,
  Coord position,
  int64_t availableFunds,
  const ServicePlacementOptions& options
) {
  ServicePlan result;
  result.hasSite = true;
  result.options = options;
  const int range = options.coverageDistanceOverride >= 0
    ? options.coverageDistanceOverride
    : coverageDistance(type);
  result.facility = ServiceFacility{type, position, range, 1.0f};
  result.facility.surfaceWater = options.surfaceWater;
  result.facility.sewageTreatment = options.sewageTreatment;
  if (options.sewageTreatment && (!options.surfaceWater || type != ServiceType::Sanitation)) {
    result.error = "treatment requires shoreline Sanitation";
    return result;
  }
  const PowerPlantSpec plant = powerPlant(options.powerSource);
  if (type == ServiceType::Power) {
    result.facility.powerSource = options.powerSource;
    result.facility.powerCapacityMW = plant.capacityMW;
    result.facility.emissionsKgPerMWh = plant.emissionsKgPerMWh;
    result.cost = plant.constructionCost;
  } else {
    result.cost = constructionCost(type, options.surfaceWater, options.powerSource, options.sewageTreatment);
  }
  if (options.surfaceWater && type != ServiceType::Water && type != ServiceType::Sanitation) {
    result.error = "surface water mode requires Water or Sanitation";
    return result;
  }
  const int span = facilityFootprintSpan(result.facility);
  for (int dy = 0; dy < span; ++dy) {
    for (int dx = 0; dx < span; ++dx) {
      const Coord cell{position.x + dx, position.y + dy};
      if (!map.isValid(cell)) {
        result.error = span > 1 ? "solar plant needs a clear 2x2" : "site is outside the map";
        return result;
      }
      const Tile& tile = map.getTile(cell);
      if (tile.regionalConnectionId) {
        result.error = "site is reserved by a regional terminal";
        return result;
      }
      if (tile.type == 2) {
        result.error = "cannot build on water";
        return result;
      }
      if (tile.hasRoad) {
        result.error = "cannot build on a road";
        return result;
      }
      if (tile.zone == static_cast<int>(ZoneType::Park)) {
        result.error = "bulldoze the park before placing a service";
        return result;
      }
      if (tile.buildingId != 0) {
        result.error = "site is occupied";
        return result;
      }
      for (const ServiceFacility& facility : facilities) {
        if (facilityCoversTile(facility, cell)) {
          // A paid treatment replacement reuses a raw outlet site.
          if (options.sewageTreatment && facility.type == ServiceType::Sanitation &&
              facility.surfaceWater && !facility.sewageTreatment && facility.position == position) continue;
          result.error = "site already has a service";
          return result;
        }
      }
    }
  }

  const bool needsShore = options.surfaceWater || (type == ServiceType::Power && plant.shoreline);
  if (needsShore) {
    bool shoreline = false;
    for (const Coord delta : {Coord{-1,0}, Coord{1,0}, Coord{0,-1}, Coord{0,1}}) {
      const auto next = Coord{position.x + delta.x, position.y + delta.y};
      shoreline |= map.isValid(next) && map.getTile(next).type == 2;
    }
    if (!shoreline) {
      result.error = type == ServiceType::Power
        ? "hydro plant requires land beside a lake or river"
        : "intake / sewage facility requires land beside a lake or river";
      return result;
    }
  }

  if (options.requireRoadAccess) {
    Coord roadAnchor;
    if (!roads.resolveRoadAnchor(position, roadAnchor)) {
      result.error = "service requires road access";
      return result;
    }
  }
  if (result.cost > availableFunds) {
    result.error = "insufficient funds";
    return result;
  }

  result.valid = true;
  return result;
}

bool ServiceTool::build(
  CityMap& map,
  const RoadNetwork& roads,
  std::vector<ServiceFacility>& facilities,
  const ServicePlan& plan,
  int64_t& availableFunds
) {
  if (!plan.valid || plan.cost > availableFunds) {
    return false;
  }
  const ServicePlan current = ServiceTool::plan(
    map, roads, facilities, plan.facility.type, plan.facility.position,
    availableFunds, plan.options
  );
  if (!current.valid || current.cost != plan.cost) {
    return false;
  }
  auto raw = std::find_if(facilities.begin(), facilities.end(), [&](const auto& facility) {
    return current.facility.sewageTreatment && facility.type == ServiceType::Sanitation &&
      facility.surfaceWater && !facility.sewageTreatment && facility.position == current.facility.position;
  });
  if (raw != facilities.end()) *raw = current.facility;
  else facilities.push_back(current.facility);
  // Civic sites are not entities. Leaving old zoning here lets the next growth
  // tick spawn a private building beneath the newly purchased facility.
  const int span = facilityFootprintSpan(current.facility);
  for (int dy = 0; dy < span; ++dy) {
    for (int dx = 0; dx < span; ++dx) {
      map.setZone(
        {current.facility.position.x + dx, current.facility.position.y + dy},
        static_cast<int>(ZoneType::None));
    }
  }
  availableFunds -= current.cost;
  return true;
}
