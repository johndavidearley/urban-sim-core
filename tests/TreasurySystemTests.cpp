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

TEST(TreasurySystemTests, ExplicitVisitorFeesAndServicesSettleOnceAlongsideDebt) {
  EconomyState economy;economy.totalRevenue=100;economy.totalExpenses=300;
  economy.residentialTaxRevenue=100;economy.totalMaintenance=300;
  auto breakdown=TreasurySystem::breakdown(economy,0.01,20,0,40,10);
  int64_t funds=0;TreasuryDebt debt{50,0};
  auto flow=TreasurySystem::applyEconomy(economy,funds,0.01,30,&debt,0,40);
  EXPECT_EQ(flow.revenue,41);EXPECT_EQ(flow.expenses,33);
  EXPECT_EQ(breakdown.totalRevenue,flow.revenue);EXPECT_EQ(breakdown.totalExpenses,flow.expenses);
  EXPECT_EQ(flow.debtRepaid,8);EXPECT_EQ(funds,0);EXPECT_EQ(debt.principal,42);
  EXPECT_EQ(breakdown.tourismFees,40);EXPECT_EQ(breakdown.tourismServices,10);
  EXPECT_EQ(breakdown.revenue[0],1);EXPECT_EQ(breakdown.expenses[0],3);
}

TEST(TreasurySystemTests, BondIssuesOnceFromCatalogAndPaysEqualParts) {
  MunicipalBond bond;
  int64_t funds = 1000;
  EXPECT_FALSE(TreasurySystem::issueBond(bond, funds, 20000, 100));
  EXPECT_FALSE(TreasurySystem::issueBond(bond, funds, 10000, 150));
  EXPECT_EQ(funds, 1000);
  ASSERT_TRUE(TreasurySystem::issueBond(bond, funds, 50000, 400));
  EXPECT_EQ(funds, 51000);
  EXPECT_FALSE(TreasurySystem::issueBond(bond, funds, 10000, 100));
  EXPECT_EQ(funds, 51000);

  const BondPayment due = TreasurySystem::bondDue(bond);
  EXPECT_EQ(due.installment, 125);
  EXPECT_EQ(due.interest, 75);  // 0.15% of 50,000
  const BondPayment first = TreasurySystem::payBond(bond, funds);
  EXPECT_EQ(first.installment, due.installment);
  EXPECT_EQ(first.interest, due.interest);
  EXPECT_EQ(first.paid, 200);
  EXPECT_EQ(first.missed, 0);
  EXPECT_EQ(first.remainingAfter, 49875);
  EXPECT_EQ(funds, 50800);
  EXPECT_EQ(TreasurySystem::bondRemaining(bond), 49875);

  int64_t principalPaid = first.installment;
  while (bond.active()) principalPaid += TreasurySystem::payBond(bond, funds).installment;
  EXPECT_EQ(principalPaid, 50000);
  EXPECT_EQ(bond, MunicipalBond{});
  EXPECT_FALSE(TreasurySystem::bondDue(bond).any());
}

TEST(TreasurySystemTests, BondMissedCashKeepsScheduleAndPayOffClears) {
  MunicipalBond bond;
  int64_t funds = 0;
  ASSERT_TRUE(TreasurySystem::issueBond(bond, funds, 10000, 100));
  funds = 40;
  const BondPayment short_ = TreasurySystem::payBond(bond, funds);
  EXPECT_EQ(short_.installment, 100);
  EXPECT_EQ(short_.interest, 15);
  EXPECT_EQ(short_.paid, 40);
  EXPECT_EQ(short_.missed, 75);
  EXPECT_EQ(funds, 0);
  EXPECT_EQ(bond.ticksElapsed, 1u);
  EXPECT_EQ(TreasurySystem::bondRemaining(bond), 9900);

  funds = 9899;
  EXPECT_FALSE(TreasurySystem::payOffBond(bond, funds));
  EXPECT_EQ(funds, 9899);
  funds = 10000;
  ASSERT_TRUE(TreasurySystem::payOffBond(bond, funds));
  EXPECT_EQ(funds, 100);
  EXPECT_FALSE(bond.active());
  EXPECT_FALSE(TreasurySystem::payOffBond(bond, funds));
}

TEST(TreasurySystemTests, BondValidationAcceptsOnlyCatalogMidSchedule) {
  EXPECT_TRUE(TreasurySystem::validBond({}));
  EXPECT_TRUE(TreasurySystem::validBond({200000, 400, 399, 0.5}));
  EXPECT_FALSE(TreasurySystem::validBond({200000, 400, 400, 0.0}));
  EXPECT_FALSE(TreasurySystem::validBond({20000, 100, 0, 0.0}));
  EXPECT_FALSE(TreasurySystem::validBond({10000, 300, 0, 0.0}));
  EXPECT_FALSE(TreasurySystem::validBond({10000, 100, 0, 1.0}));
  EXPECT_FALSE(TreasurySystem::validBond({0, 100, 0, 0.0}));
}
