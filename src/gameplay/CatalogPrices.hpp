#pragma once

#include <cstdint>

// Fixed player construction prices calibrated against the retained Phase 13
// journey. These never depend on current cash, population, or income. Upkeep
// and autonomous construction remain separate. See POPULATION_MILESTONES.md.
namespace catalog {
inline constexpr int64_t arterialSegmentCost = 1500;
inline constexpr int64_t arterialUpgradeCost = 1400;
inline constexpr int64_t parkTileCost = 1500;
inline constexpr int64_t officeTileCost = 1500;
inline constexpr int64_t cemeteryCost = 80000;
inline constexpr int64_t recyclingCost = 100000;
inline constexpr int64_t gasCost = 400000;
inline constexpr int64_t solarCost = 300000;
inline constexpr int64_t substationCost = 150000;
inline constexpr int64_t hydroCost = 750000;
inline constexpr int64_t treatmentCost = 600000;
inline constexpr int64_t crematoriumCost = 650000;
inline constexpr int64_t incineratorCost = 900000;
inline constexpr int64_t hotelCost = 1500000;
inline constexpr int64_t cargoPortCost = 1800000;
inline constexpr int64_t passengerTerminalCost = 1500000;
inline constexpr int64_t nuclearCost = 1800000;
inline constexpr int64_t airportCost = 1800000;
} // namespace catalog
