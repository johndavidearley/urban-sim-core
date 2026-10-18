#pragma once

#include <cstdint>
#include <vector>

#include "src/systems/ServiceSystem.hpp"

class EntityStore;

struct WasteSummary {
  int64_t landfillRemaining = 0;
  int64_t landfillFillRate = 0;
  int64_t storedIncinerated = 0;
  uint32_t fullLandfills = 0;
  Coord firstFullLandfill{0, 0};
  int64_t generated = 0;
  int64_t recycled = 0;
  int64_t landfilled = 0;
  int64_t incinerated = 0;
  int64_t incinerationCapacity = 0;
  float powerGenerationMW = 0.0f;
  float emissionsKgPerMWh = 650.0f;
  int64_t uncollected = 0;
  int64_t collectionCapacity = 0;
  int64_t recyclingCapacity = 0;
  float collectionRate = 1.0f;
  float pollutionPenalty = 0.0f;
  float happinessPenalty = 0.0f;
};

class WasteSystem {
public:
  // Only completed ticks mutate volume. Evaluation and service-power previews are pure.
  static WasteSummary step(const EntityStore& store, std::vector<ServiceFacility>& facilities,
    const ServiceCoverageSummary& coverage,
    const std::vector<ServiceFacility>* operatingFacilities = nullptr);

  static WasteSummary evaluate(
    const EntityStore& store,
    const std::vector<ServiceFacility>& facilities,
    const ServiceCoverageSummary& coverage
  );
};
