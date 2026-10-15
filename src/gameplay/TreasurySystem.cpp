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

TreasuryBreakdown TreasurySystem::breakdown(const EconomyState& economy,
  double tickScale, int64_t serviceCosts, int64_t constructionCosts) {
  TreasuryBreakdown result;
  const double scale = std::max(0.0, tickScale);
  const auto allocate = [scale](const auto& raw, auto& output) {
    int64_t cumulative = 0, previous = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
      cumulative += raw[i];
      const auto rounded = static_cast<int64_t>(std::llround(static_cast<double>(cumulative) * scale));
      output[i] = rounded - previous;
      previous = rounded;
    }
    return previous;
  };
  result.totalRevenue = allocate(std::array<int64_t, 6>{{
    economy.residentialTaxRevenue, economy.commercialTaxRevenue,
    economy.industrialTaxRevenue, economy.officeTaxRevenue,
    economy.incomeTaxRevenue, economy.exportRevenue}}, result.revenue);
  std::array<int64_t, 3> expenses{};
  result.totalExpenses = allocate(std::array<int64_t, 3>{{
    economy.totalMaintenance - economy.parkMaintenance,
    economy.parkMaintenance, economy.importCost}}, expenses);
  std::copy(expenses.begin(), expenses.end(), result.expenses.begin());
  result.expenses[3] = std::max<int64_t>(0, serviceCosts);
  result.expenses[4] = std::max<int64_t>(0, constructionCosts);
  result.totalExpenses += result.expenses[3] + result.expenses[4];
  return result;
}
