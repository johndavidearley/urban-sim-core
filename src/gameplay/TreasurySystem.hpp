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

// One open municipal bond; principal == 0 means none. Remaining principal
// follows the schedule, so a missed installment still advances it and the
// missed cash becomes overdraft instead.
struct MunicipalBond {
  int64_t principal = 0;
  uint32_t term = 0;
  uint32_t ticksElapsed = 0;
  double interestRemainder = 0.0;
  bool active() const { return principal > 0; }
  bool operator==(const MunicipalBond& other) const {
    return principal == other.principal && term == other.term &&
      ticksElapsed == other.ticksElapsed && interestRemainder == other.interestRemainder;
  }
};

// One tick of bond service. paid + missed == installment + interest.
struct BondPayment {
  int64_t installment = 0;
  int64_t interest = 0;
  int64_t paid = 0;
  int64_t missed = 0;
  int64_t remainingBefore = 0;
  int64_t remainingAfter = 0;
  bool any() const { return remainingBefore > 0; }
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
  BondPayment bond;
};

class TreasurySystem {
public:
  static constexpr double kBondInterestRate = 0.0015;
  static constexpr std::array<int64_t, 3> kBondPrincipals{{10000, 50000, 200000}};
  static constexpr std::array<uint32_t, 3> kBondTerms{{100, 200, 400}};
  static bool validBondPrincipal(int64_t principal);
  static bool validBondTerm(int64_t term);
  // A stored bond is either all zero or a catalog bond mid-schedule.
  static bool validBond(const MunicipalBond& bond);
  static int64_t bondRemaining(const MunicipalBond& bond);
  // What the next tick will charge, without changing the bond.
  static BondPayment bondDue(const MunicipalBond& bond, double interestRatePerTick = kBondInterestRate);
  // Pays this tick's installment and interest from cash. The caller rolls
  // `missed` into overdraft. Clears the bond after the last installment.
  static BondPayment payBond(MunicipalBond& bond, int64_t& funds, double interestRatePerTick = kBondInterestRate);
  static bool issueBond(MunicipalBond& bond, int64_t& funds, int64_t principal, uint32_t term);
  // Pays remaining principal from cash; a fractional interest remainder is forgiven.
  static bool payOffBond(MunicipalBond& bond, int64_t& funds);
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
