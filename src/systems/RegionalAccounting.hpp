#pragma once
#include <string>
#include "src/systems/RegionalConnectionSystem.hpp"

namespace regional {
struct Delivery { uint32_t id = 0; Demand delivered; };
// Integer quote units per delivered unit (Settlement::amountScale converts to dollars). Facility milestones define
// prices; M31 supplies no new defaults or changes to existing trade prices.
struct Prices {
  int64_t passengerFee = 0, importCost = 0, exportRevenue = 0;
  int64_t operatingCost = 0; // Total operation expense for the measured tick.
};
struct Settlement {
  bool valid = false;
  double amountScale = 1.0; // Raw quote amounts to city dollars; old reports use 1.
  int64_t operatingExpense = 0; // Separate city-dollar port upkeep, already billed by treasury.
  Report allocation;
  std::vector<Delivery> deliveries;
  Demand delivered, undelivered;
  Prices prices;
  int64_t revenue = 0, expenses = 0;
};

// Finalize at most once per increasing tick. Only confirmed local deliveries
// earn fees/trade credits; admission alone earns nothing. Rejection preserves
// the previous settlement. City treasury integration belongs to M32/M34:
// these reconciled amounts are not an additional automatic cash deposit.
bool settle(const Report& allocation,const std::vector<Delivery>& deliveries,
            Prices prices,Settlement& previous,std::string* error = nullptr);
} // namespace regional
