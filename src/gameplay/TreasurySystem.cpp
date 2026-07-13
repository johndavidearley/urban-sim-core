#include "src/gameplay/TreasurySystem.hpp"

#include <algorithm>
#include <cmath>

#include "src/systems/EconomySystem.hpp"

TreasuryFlow TreasurySystem::applyEconomy(
  const EconomyState& economy,
  int64_t& funds,
  double tickScale,
  int64_t additionalExpenses
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
  return flow;
}
