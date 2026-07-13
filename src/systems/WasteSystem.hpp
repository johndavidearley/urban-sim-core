#pragma once

#include <cstdint>
#include <vector>

#include "src/systems/ServiceSystem.hpp"

class EntityStore;

struct WasteSummary {
  int64_t generated = 0;
  int64_t recycled = 0;
  int64_t landfilled = 0;
  int64_t uncollected = 0;
  int64_t collectionCapacity = 0;
  int64_t recyclingCapacity = 0;
  float collectionRate = 1.0f;
  float pollutionPenalty = 0.0f;
  float happinessPenalty = 0.0f;
};

class WasteSystem {
public:
  static WasteSummary evaluate(
    const EntityStore& store,
    const std::vector<ServiceFacility>& facilities,
    const ServiceCoverageSummary& coverage
  );
};
