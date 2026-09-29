#pragma once

#include <cstdint>

struct EconomyState;

struct TreasuryFlow {
  int64_t revenue = 0;
  int64_t expenses = 0;
  int64_t net = 0;
  int64_t shortfall = 0;
  int64_t debtIssued = 0;
  int64_t debtRepaid = 0;
  int64_t interestCharged = 0;
  int64_t outstandingDebt = 0;
};

// Unpaid shortfall becomes municipal debt. Optional; applyEconomy with
// debt == nullptr matches the pre-debt cash-only behavior.
struct TreasuryDebt {
  int64_t principal = 0;
  double interestRemainder = 0.0;
};

class TreasurySystem {
public:
  static TreasuryFlow applyEconomy(
    const EconomyState& economy,
    int64_t& funds,
    double tickScale = 0.01,
    int64_t additionalExpenses = 0,
    TreasuryDebt* debt = nullptr,
    double interestRatePerTick = 0.0
  );
};
