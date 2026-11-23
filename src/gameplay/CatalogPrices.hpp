#pragma once

#include <cstdint>

// Fixed construction prices for the 10k reserve target. Quotes never depend on
// current cash, population, or income. Upkeep and autonomous construction remain
// separate. See docs/POPULATION_MILESTONES.md.
namespace catalog {
inline constexpr int64_t arterialSegmentCost = 400;
inline constexpr int64_t arterialUpgradeCost = 300;
inline constexpr int64_t parkTileCost = 250;
inline constexpr int64_t officeTileCost = 250;
inline constexpr int64_t cemeteryCost = 16000;
inline constexpr int64_t recyclingCost = 20000;
inline constexpr int64_t gasCost = 80000;
inline constexpr int64_t solarCost = 60000;
inline constexpr int64_t substationCost = 30000;
inline constexpr int64_t hydroCost = 150000;
inline constexpr int64_t treatmentCost = 120000;
inline constexpr int64_t crematoriumCost = 65000;
inline constexpr int64_t incineratorCost = 90000;
inline constexpr int64_t hotelCost = 250000;
inline constexpr int64_t cargoPortCost = 350000;
inline constexpr int64_t passengerTerminalCost = 300000;
inline constexpr int64_t nuclearCost = 350000;
inline constexpr int64_t airportCost = 350000;
} // namespace catalog
