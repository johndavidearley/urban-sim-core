#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/systems/ServiceSystem.hpp"

class CityMap;
class RoadNetwork;

struct ServicePlan {
  ServiceFacility facility;
  int64_t cost = 0;
  bool hasSite = false;
  bool valid = false;
  std::string error;
};

class ServiceTool {
public:
  static int64_t constructionCost(ServiceType type);
  static int64_t operatingCostPerTick(ServiceType type);
  static int coverageDistance(ServiceType type);

  static ServicePlan plan(
    const CityMap& map,
    const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities,
    ServiceType type,
    Coord position,
    int64_t availableFunds
  );

  static bool build(
    const CityMap& map,
    const RoadNetwork& roads,
    std::vector<ServiceFacility>& facilities,
    const ServicePlan& plan,
    int64_t& availableFunds
  );
};
