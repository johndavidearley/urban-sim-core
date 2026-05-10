#pragma once

#include <string>

#include "src/entities/EntityStore.hpp"
#include "src/world/CityMap.hpp"

struct GrowthMetrics {
  int zonedResidential = 0;
  int zonedCommercial = 0;
  int zonedIndustrial = 0;

  int builtResidential = 0;
  int builtCommercial = 0;
  int builtIndustrial = 0;

  int totalBuildings = 0;

  float residentialFillRate = 0.0f;
  float commercialFillRate = 0.0f;
  float industrialFillRate = 0.0f;

  std::string toString() const;

  static GrowthMetrics collect(const CityMap& map, const EntityStore& store);
};
