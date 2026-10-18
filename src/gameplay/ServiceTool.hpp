#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/systems/ServiceSystem.hpp"

class CityMap;
class RoadNetwork;

struct PowerPlantSpec {
  int64_t constructionCost = 0;
  int64_t operatingCost = 0;
  float capacityMW = 0.0f;
  float emissionsKgPerMWh = 0.0f;
  bool shoreline = false;
};

struct ServicePlacementOptions {
  bool requireRoadAccess = true;
  bool surfaceWater = false;
  bool sewageTreatment = false;
  // Used when the placed type is Power. Other services ignore it.
  PowerSourceType powerSource = PowerSourceType::Generic;
  // -1 keeps the type default from coverageDistance().
  int coverageDistanceOverride = -1;
};

struct ServicePlan {
  ServiceFacility facility;
  int64_t cost = 0;
  bool hasSite = false;
  bool valid = false;
  std::string error;
  ServicePlacementOptions options;
};

class ServiceTool {
public:
  static PowerPlantSpec powerPlant(PowerSourceType source);
  static int64_t constructionCost(
    ServiceType type, bool surfaceWater = false,
    PowerSourceType source = PowerSourceType::Generic, bool sewageTreatment = false);
  static int64_t operatingCostPerTick(
    ServiceType type, bool surfaceWater = false,
    PowerSourceType source = PowerSourceType::Generic, bool sewageTreatment = false);
  static int coverageDistance(ServiceType type);

  static ServicePlan plan(
    const CityMap& map,
    const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities,
    ServiceType type,
    Coord position,
    int64_t availableFunds,
    const ServicePlacementOptions& options = {}
  );

  // Replaces any empty developable zoning at the paid civic site.
  static bool build(
    CityMap& map,
    const RoadNetwork& roads,
    std::vector<ServiceFacility>& facilities,
    const ServicePlan& plan,
    int64_t& availableFunds
  );
};
