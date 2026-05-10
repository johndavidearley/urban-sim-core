#pragma once

#include <cstdint>
#include <string>

struct CityMetrics {
  uint32_t population = 0;
  uint32_t availableHousing = 0;
  uint32_t availableJobs = 0;
  float unemployment = 0.0f;
  float happiness = 0.5f;
  float pollution = 0.0f;
  float landValueAverage = 100.0f;
  int64_t cityRevenue = 0;
  int64_t cityExpenses = 0;
  float commuteBurden = 0.0f;
  float trafficCongestion = 0.0f;
  
  std::string toString() const;
};
