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
  double interestRatePerTick,
  int64_t additionalRevenue
) {
  TreasuryFlow flow;
  const double scale = std::max(0.0, tickScale);
  flow.revenue = static_cast<int64_t>(std::llround(
    static_cast<double>(economy.totalRevenue) * scale)) + std::max<int64_t>(0,additionalRevenue);
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

bool TreasurySystem::validBondPrincipal(int64_t principal) {
  return std::find(kBondPrincipals.begin(), kBondPrincipals.end(), principal) != kBondPrincipals.end();
}

bool TreasurySystem::validBondTerm(int64_t term) {
  return std::find(kBondTerms.begin(), kBondTerms.end(), term) != kBondTerms.end();
}

bool TreasurySystem::validBond(const MunicipalBond& bond) {
  if (!bond.active()) return bond == MunicipalBond{};
  return validBondPrincipal(bond.principal) && validBondTerm(bond.term) &&
    bond.ticksElapsed < bond.term && std::isfinite(bond.interestRemainder) &&
    bond.interestRemainder >= 0.0 && bond.interestRemainder < 1.0;
}

namespace {
// Cumulative rounding keeps equal parts summing exactly to principal.
int64_t scheduledPaid(const MunicipalBond& bond, uint32_t ticks) {
  return bond.principal * static_cast<int64_t>(ticks) / static_cast<int64_t>(bond.term);
}
}

int64_t TreasurySystem::bondRemaining(const MunicipalBond& bond) {
  if (!bond.active() || bond.term == 0) return 0;
  return bond.principal - scheduledPaid(bond, std::min(bond.ticksElapsed, bond.term));
}

BondPayment TreasurySystem::bondDue(const MunicipalBond& bond, double interestRatePerTick) {
  BondPayment due;
  if (!validBond(bond) || !bond.active()) return due;
  due.remainingBefore = bondRemaining(bond);
  due.installment = scheduledPaid(bond, bond.ticksElapsed + 1) - scheduledPaid(bond, bond.ticksElapsed);
  const double accrued = bond.interestRemainder +
    static_cast<double>(due.remainingBefore) * std::max(0.0, interestRatePerTick);
  due.interest = static_cast<int64_t>(std::floor(accrued));
  due.remainingAfter = due.remainingBefore - due.installment;
  return due;
}

BondPayment TreasurySystem::payBond(MunicipalBond& bond, int64_t& funds, double interestRatePerTick) {
  BondPayment payment = bondDue(bond, interestRatePerTick);
  if (!payment.any()) return payment;
  bond.interestRemainder += static_cast<double>(payment.remainingBefore) * std::max(0.0, interestRatePerTick);
  bond.interestRemainder -= static_cast<double>(payment.interest);
  const int64_t due = payment.installment + payment.interest;
  payment.paid = std::min(std::max<int64_t>(0, funds), due);
  payment.missed = due - payment.paid;
  funds -= payment.paid;
  if (++bond.ticksElapsed >= bond.term) bond = {};
  return payment;
}

bool TreasurySystem::issueBond(MunicipalBond& bond, int64_t& funds, int64_t principal, uint32_t term) {
  if (bond.active() || !validBondPrincipal(principal) || !validBondTerm(term)) return false;
  bond = {principal, term, 0, 0.0};
  funds += principal;
  return true;
}

bool TreasurySystem::payOffBond(MunicipalBond& bond, int64_t& funds) {
  const int64_t remaining = bondRemaining(bond);
  if (!bond.active() || funds < remaining) return false;
  funds -= remaining;
  bond = {};
  return true;
}

TreasuryBreakdown TreasurySystem::breakdown(const EconomyState& economy,
  double tickScale, int64_t serviceCosts, int64_t constructionCosts,
  int64_t tourismFees, int64_t tourismServices) {
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
  result.tourismFees=std::max<int64_t>(0,tourismFees);
  result.tourismServices=std::max<int64_t>(0,tourismServices);
  result.totalRevenue+=result.tourismFees;
  result.totalExpenses+=result.tourismServices;
  return result;
}
