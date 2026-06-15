#pragma once

#include <cstdint>

struct SimulationTime {
  uint64_t tickCount = 0;           // Current tick (0-based)
  uint32_t ticksPerDay = 24;        // In-game hours per day
  uint32_t ticksPerMonth = 720;     // 30 days
  float simulationSpeed = 1.0f;     // 1.0 = normal, 2.0 = 2x
  
  // Computed properties
  uint32_t getCurrentHour() const {
    return tickCount % ticksPerDay;
  }
  
  uint32_t getCurrentDay() const {
    return (tickCount / ticksPerDay) % 30;
  }
  
  uint32_t getCurrentMonth() const {
    return static_cast<uint32_t>(tickCount / ticksPerMonth);
  }
  
  bool isDayBoundary() const {
    return (tickCount % ticksPerDay) == 0;
  }
  
  bool isMonthBoundary() const {
    return (tickCount % ticksPerMonth) == 0;
  }
  
  void advance() {
    tickCount++;
  }
};
