#pragma once

#include <cstdint>
#include <array>

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

// Display amounts in treasury dollars, with rounding reconciled to the total.
struct TreasuryBreakdown {
  // Residential, commercial, industrial, office, income, exports.
  std::array<int64_t, 6> revenue{};
  // Buildings, parks, imports, service operation, growth construction.
  std::array<int64_t, 5> expenses{};
  int64_t tourismFees = 0, tourismServices = 0;
  int64_t totalRevenue = 0;
  int64_t totalExpenses = 0;
};

struct TreasuryLedger {
  bool valid = false;
  uint32_t tick = 0;
  TreasuryBreakdown breakdown;
  TreasuryFlow flow;
  int64_t fundsBefore = 0;
  int64_t fundsAfter = 0;
  int64_t debtBefore = 0;
};

class TreasurySystem {
public:
  static TreasuryBreakdown breakdown(const EconomyState& economy,
    double tickScale = 0.01, int64_t serviceCosts = 0, int64_t constructionCosts = 0,
    int64_t tourismFees = 0, int64_t tourismServices = 0);
  static TreasuryFlow applyEconomy(
    const EconomyState& economy,
    int64_t& funds,
    double tickScale = 0.01,
    int64_t additionalExpenses = 0,
    TreasuryDebt* debt = nullptr,
    double interestRatePerTick = 0.0,
    int64_t additionalRevenue = 0
  );
};
