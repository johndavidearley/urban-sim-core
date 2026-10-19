#pragma once

#include <array>
#include <cstdint>

// Store the threshold, rather than the current population or a tier index.
// This is player progression; autonomous CitySimulator runs do not use it.
struct PopulationMilestones {
  inline static constexpr std::array<uint32_t, 6> thresholds{0, 500, 1200, 2500, 5000, 10000};
  uint32_t highestReached = 0;

  static constexpr bool validThreshold(uint32_t threshold) {
    for (auto value : thresholds) if (value == threshold) return true;
    return false;
  }

  // Exact threshold counts as reaching the milestone; skipped tiers are earned.
  bool observe(uint32_t population) {
    const auto previous = highestReached;
    for (auto threshold : thresholds)
      if (population >= threshold && threshold > highestReached) highestReached = threshold;
    return highestReached != previous;
  }

  bool unlocks(uint32_t threshold) const {
    return validThreshold(threshold) && highestReached >= threshold;
  }

  uint32_t nextThreshold() const {
    for (auto threshold : thresholds) if (threshold > highestReached) return threshold;
    return 0; // All milestones reached.
  }
};
