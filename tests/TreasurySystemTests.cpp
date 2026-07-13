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
