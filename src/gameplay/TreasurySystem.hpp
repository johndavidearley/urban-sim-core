#pragma once

#include <cstdint>

struct EconomyState;

struct TreasuryFlow {
  int64_t revenue = 0;
  int64_t expenses = 0;
  int64_t net = 0;
  int64_t shortfall = 0;
};

class TreasurySystem {
public:
  static TreasuryFlow applyEconomy(
    const EconomyState& economy,
    int64_t& funds,
    double tickScale = 0.01,
    int64_t additionalExpenses = 0
  );
};
