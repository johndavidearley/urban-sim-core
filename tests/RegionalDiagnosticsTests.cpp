#include <algorithm>
#include "gtest/gtest.h"
#include "src/gameplay/RegionalDiagnostics.hpp"

TEST(RegionalDiagnostics, RequiresCompletedNonzeroMeasurements) {
  regional::Settlement cargo; tourism::Runtime visitors;
  visitors.report.unmetOvernightBeds=5; visitors.travel.unrouted=7;
  cargo.allocation.unmet.imports=8;
  EXPECT_TRUE(regional::diagnose(cargo,visitors).empty());
  visitors.report.tick=10;
  const auto alerts=regional::diagnose(cargo,visitors);
  ASSERT_EQ(alerts.size(),1u);
  EXPECT_EQ(alerts[0].problem,regional::Problem::HotelBeds);
  EXPECT_EQ(alerts[0].tick,10u); EXPECT_EQ(alerts[0].unmet,5u);
  EXPECT_EQ(alerts[0].targetId,0u);
}
TEST(RegionalDiagnostics, UsesCompletedFlowsAndStableTargetsDespitePolicyEdits) {
  tourism::Runtime visitors;
  visitors.enabled=true; visitors.report.tick=11;
  visitors.report.unmetDayTransport=20; visitors.report.unmetOvernightBeds=6;
  visitors.report.connections={{9,0,0,0,regional::Kind::Airport,regional::Access::Disabled},
    {3,10,0,10,regional::Kind::PassengerTerminal,regional::Access::Ready}};
  visitors.state.stays={{9,8,12},{3,8,12}};
  const auto before=regional::diagnose({},visitors);
  ASSERT_EQ(before.size(),2u);
  EXPECT_EQ(before[0].targetId,9u); EXPECT_EQ(before[1].targetId,3u);
  visitors.enabled=false;
  std::reverse(visitors.report.connections.begin(),visitors.report.connections.end());
  const auto after=regional::diagnose({},visitors);
  ASSERT_EQ(after.size(),before.size());
  for (size_t i=0;i<before.size();++i) {
    EXPECT_EQ(before[i].problem,after[i].problem); EXPECT_EQ(before[i].tick,after[i].tick);
    EXPECT_EQ(before[i].unmet,after[i].unmet); EXPECT_EQ(before[i].targetId,after[i].targetId);
  }
  visitors.report.unmetDayTransport=0; visitors.report.unmetOvernightBeds=0;
  EXPECT_TRUE(regional::diagnose({},visitors).empty());
}
TEST(RegionalDiagnostics, SeparatesCargoAdmissionAndDeliveryWithoutOverflow) {
  regional::Settlement cargo; cargo.valid=true;cargo.allocation.tick=14;
  cargo.allocation.unmet.imports=UINT32_MAX;cargo.allocation.unmet.exports=1;
  cargo.undelivered.imports=7;
  cargo.allocation.connections={{9,regional::Access::Ready,0,10,0},
    {3,regional::Access::Ready,0,12,0}};
  cargo.deliveries={{9,{0,10,0}},{3,{0,5,0}}};
  const auto alerts=regional::diagnose(cargo,{});
  ASSERT_EQ(alerts.size(),2u);
  EXPECT_EQ(alerts[0].problem,regional::Problem::CargoCapacity);
  EXPECT_EQ(alerts[0].unmet,static_cast<uint64_t>(UINT32_MAX)+1);
  EXPECT_EQ(alerts[0].targetId,0u);
  EXPECT_EQ(alerts[1].problem,regional::Problem::CargoDelivery);
  EXPECT_EQ(alerts[1].unmet,7u);EXPECT_EQ(alerts[1].targetId,3u);
  cargo.valid=false;EXPECT_TRUE(regional::diagnose(cargo,{}).empty());
}
TEST(RegionalDiagnostics, LocalVisitorTravelRequiresMeasuredReport) {
  tourism::Runtime visitors;visitors.travel.tick=9;visitors.travel.unrouted=2;
  EXPECT_TRUE(regional::diagnose({},visitors).empty());
  visitors.travel.measured=true;
  const auto alerts=regional::diagnose({},visitors);
  ASSERT_EQ(alerts.size(),1u);
  EXPECT_EQ(alerts[0].problem,regional::Problem::VisitorTravel);
  EXPECT_EQ(alerts[0].tick,9u);EXPECT_EQ(alerts[0].unmet,2u);
}
