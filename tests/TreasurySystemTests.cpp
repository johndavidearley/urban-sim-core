#include "gtest/gtest.h"

#include "src/gameplay/TreasurySystem.hpp"
#include "src/systems/EconomySystem.hpp"

TEST(TreasurySystemTests, AppliesScaledRevenueAndExpensesToSharedFunds) {
  EconomyState economy;
  economy.totalRevenue = 12000;
  economy.totalExpenses = 3000;
  int64_t funds = 500;

  const TreasuryFlow flow = TreasurySystem::applyEconomy(economy, funds);

  EXPECT_EQ(flow.revenue, 120);
  EXPECT_EQ(flow.expenses, 30);
  EXPECT_EQ(flow.net, 90);
  EXPECT_EQ(funds, 590);
}

TEST(TreasurySystemTests, DeficitCannotTakeTreasuryBelowZero) {
  EconomyState economy;
  economy.totalExpenses = 100000;
  int64_t funds = 25;

  const TreasuryFlow flow = TreasurySystem::applyEconomy(economy, funds);

  EXPECT_EQ(funds, 0);
  EXPECT_EQ(flow.net, -25);
  EXPECT_EQ(flow.shortfall, 975);
}

TEST(TreasurySystemTests, ShortfallBecomesDebtAndSurplusRepaysIt) {
  EconomyState deficit;
  deficit.totalExpenses = 100000;
  int64_t funds = 25;
  TreasuryDebt debt;

  const TreasuryFlow issued = TreasurySystem::applyEconomy(deficit, funds, 0.01, 0, &debt, 0.0);
  EXPECT_EQ(funds, 0);
  EXPECT_EQ(issued.shortfall, 975);
  EXPECT_EQ(issued.debtIssued, 975);
  EXPECT_EQ(debt.principal, 975);

  EconomyState surplus;
  surplus.totalRevenue = 20000;
  const TreasuryFlow repaid = TreasurySystem::applyEconomy(surplus, funds, 0.01, 0, &debt, 0.0);
  EXPECT_EQ(repaid.revenue, 200);
  EXPECT_EQ(repaid.debtRepaid, 200);
  EXPECT_EQ(debt.principal, 775);
  EXPECT_EQ(funds, 0);
}

TEST(TreasurySystemTests, InterestAccruesOnUnpaidDebtUsingRemainder) {
  EconomyState idle;
  int64_t funds = 0;
  TreasuryDebt debt;
  debt.principal = 1000;

  const TreasuryFlow first = TreasurySystem::applyEconomy(idle, funds, 0.01, 0, &debt, 0.001);
  EXPECT_EQ(first.interestCharged, 1);
  EXPECT_EQ(debt.principal, 1001);
  EXPECT_NEAR(debt.interestRemainder, 0.0, 1e-9);

  debt = {};
  debt.principal = 100;
  const TreasuryFlow tiny = TreasurySystem::applyEconomy(idle, funds, 0.01, 0, &debt, 0.001);
  EXPECT_EQ(tiny.interestCharged, 0);
  EXPECT_EQ(debt.principal, 100);
  EXPECT_NEAR(debt.interestRemainder, 0.1, 1e-9);
}

TEST(TreasurySystemTests, AdditionalOperatingCostsJoinExpensesWithoutScaling) {
  EconomyState economy;
  economy.totalRevenue = 10000;
  economy.totalExpenses = 2000;
  int64_t funds = 500;

  const TreasuryFlow flow = TreasurySystem::applyEconomy(economy, funds, 0.01, 55);

  EXPECT_EQ(flow.revenue, 100);
  EXPECT_EQ(flow.expenses, 75);
  EXPECT_EQ(flow.net, 25);
  EXPECT_EQ(funds, 525);
}

TEST(TreasurySystemTests, BreakdownReconcilesRoundedCategoriesToChargedTotals) {
  EconomyState economy;
  economy.residentialTaxRevenue = 49;
  economy.commercialTaxRevenue = 49;
  economy.industrialTaxRevenue = 49;
  economy.officeTaxRevenue = 49;
  economy.incomeTaxRevenue = 49;
  economy.exportRevenue = 49;
  economy.totalRevenue = 294;
  economy.totalMaintenance = 49;
  economy.parkMaintenance = 24;
  economy.importCost = 49;
  economy.totalExpenses = 98;
  const auto breakdown = TreasurySystem::breakdown(economy, 0.01, 13, 27);
  int64_t funds = 1000;
  const auto flow = TreasurySystem::applyEconomy(economy, funds, 0.01, 40);
  EXPECT_EQ(breakdown.totalRevenue, flow.revenue);
  EXPECT_EQ(breakdown.totalExpenses, flow.expenses);
  int64_t revenue = 0, expenses = 0;
  for (auto value : breakdown.revenue) { EXPECT_GE(value, 0); revenue += value; }
  for (auto value : breakdown.expenses) { EXPECT_GE(value, 0); expenses += value; }
  EXPECT_EQ(revenue, flow.revenue);
  EXPECT_EQ(expenses, flow.expenses);
  EXPECT_EQ(breakdown.expenses[3], 13);
  EXPECT_EQ(breakdown.expenses[4], 27);
}
