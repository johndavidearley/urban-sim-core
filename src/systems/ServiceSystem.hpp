#pragma once

#include <string>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/metrics/CityMetrics.hpp"
#include "src/networks/RoadNetwork.hpp"

enum class ServiceType : int {
  Fire = 0,
  Police = 1,
  Health = 2,
  Education = 3
};

struct ServiceFacility {
  ServiceType type = ServiceType::Fire;
  Coord position{0, 0};
  int maxTravelDistance = 10; // Road-edge hops
  float quality = 1.0f;
};

struct ServiceCoverageSummary {
  uint32_t totalBuildings = 0;
  uint32_t servicedBuildings = 0;

  float fireCoverage = 0.0f;
  float policeCoverage = 0.0f;
  float healthCoverage = 0.0f;
  float educationCoverage = 0.0f;

  float overallCoverage = 0.0f;
  float satisfaction = 0.5f;
};

class ServiceSystem {
public:
  static bool parseServiceType(const std::string& raw, ServiceType& outType);
  static const char* serviceTypeToString(ServiceType type);

  static ServiceCoverageSummary evaluateCoverage(
    const EntityStore& store,
    const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities
  );

  static void applyToMetrics(const ServiceCoverageSummary& summary, CityMetrics& metrics);
};
