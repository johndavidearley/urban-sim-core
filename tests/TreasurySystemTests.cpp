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
