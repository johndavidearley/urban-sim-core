#include "src/gameplay/TreasurySystem.hpp"

#include <algorithm>
#include <cmath>

#include "src/systems/EconomySystem.hpp"

TreasuryFlow TreasurySystem::applyEconomy(
  const EconomyState& economy,
  int64_t& funds,
  double tickScale,
  int64_t additionalExpenses,
  TreasuryDebt* debt,
  double interestRatePerTick
) {
  TreasuryFlow flow;
  const double scale = std::max(0.0, tickScale);
  flow.revenue = static_cast<int64_t>(std::llround(
    static_cast<double>(economy.totalRevenue) * scale));
  flow.expenses = static_cast<int64_t>(std::llround(
    static_cast<double>(economy.totalExpenses) * scale)) + std::max<int64_t>(0, additionalExpenses);
  const int64_t previous = funds;
  const int64_t requestedFunds = funds + flow.revenue - flow.expenses;
  flow.shortfall = std::max<int64_t>(0, -requestedFunds);
  funds = std::max<int64_t>(0, requestedFunds);
  flow.net = funds - previous;

  if (debt != nullptr) {
    if (debt->principal < 0) {
      debt->principal = 0;
    }
    if (debt->interestRemainder < 0.0) {
      debt->interestRemainder = 0.0;
    }
    if (flow.shortfall > 0) {
      debt->principal += flow.shortfall;
      flow.debtIssued = flow.shortfall;
    }
    if (funds > 0 && debt->principal > 0) {
      const int64_t repaid = std::min(funds, debt->principal);
      funds -= repaid;
      debt->principal -= repaid;
      flow.debtRepaid = repaid;
      flow.net = funds - previous;
    }
    const double rate = std::max(0.0, interestRatePerTick);
    if (rate > 0.0 && debt->principal > 0) {
      debt->interestRemainder += static_cast<double>(debt->principal) * rate;
      const int64_t charged = static_cast<int64_t>(std::floor(debt->interestRemainder));
      if (charged > 0) {
        debt->principal += charged;
        debt->interestRemainder -= static_cast<double>(charged);
        flow.interestCharged = charged;
      }
    }
    if (debt->principal == 0) {
      debt->interestRemainder = 0.0;
    }
    flow.outstandingDebt = debt->principal;
  }
  return flow;
}
