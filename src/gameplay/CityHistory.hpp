#pragma once

#include <vector>
#include "src/gameplay/BudgetPolicy.hpp"
#include "src/systems/MigrationSystem.hpp"

struct CityHistorySample {
  uint32_t tick = 0;
  uint32_t population = 0;
  PopulationChange migration;
  int64_t revenue = 0;
  int64_t expenses = 0;
  int64_t funds = 0;
  int64_t debt = 0;
  BudgetPolicy policy;
};

// Completed ticks only. Retain a bounded rolling window, independent of UI.
struct CityHistory {
  static constexpr size_t capacity = 240;
  std::vector<CityHistorySample> samples;
  void append(CityHistorySample sample) {
    if (samples.size() == capacity) samples.erase(samples.begin());
    samples.push_back(std::move(sample));
  }
  bool policyChanged(size_t index) const {
    if (index >= samples.size() || index == 0) return false;
    const auto& a = samples[index - 1].policy;
    const auto& b = samples[index].policy;
    return a.taxPercent != b.taxPercent || a.servicePercent != b.servicePercent;
  }
};
