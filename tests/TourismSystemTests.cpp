#include "gtest/gtest.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include "src/systems/TourismSystem.hpp"

namespace {
regional::Offer gateway(uint32_t id, uint32_t capacity,
                        regional::Kind kind = regional::Kind::RoadGateway) {
  regional::Connection connection;
  connection.id = id; connection.kind = kind;
  connection.passengersPerTick = capacity;
  return {connection, regional::Access::Ready};
}
}

TEST(TourismSystemTests, DemandRequiresParksAndCapsQualityAndAttractions) {
  EXPECT_EQ(tourism::demandFor({0,100}).day, 0u);
  const auto small = tourism::demandFor({1,50});
  EXPECT_EQ(small.parkContribution, 20u);
  EXPECT_EQ(small.qualityContribution, 10u);
  EXPECT_EQ(small.day + small.overnight, 30u);
  const auto capped = tourism::demandFor({std::numeric_limits<uint32_t>::max(),100});
  EXPECT_EQ(capped.day + capped.overnight, tourism::kMaximumDemand);
  EXPECT_THROW(tourism::demandFor({1,101}), std::invalid_argument);
}

TEST(TourismSystemTests, RoadAloneServesDayVisitorsWithoutCreatingStays) {
  const auto result = tourism::advance({},1,{1,0},{gateway(1,100)},{});
  EXPECT_EQ(result.report.dayArrivals,15u);
  EXPECT_EQ(result.report.dayDepartures,15u);
  EXPECT_EQ(result.report.overnightArrivals,0u);
  EXPECT_EQ(result.report.unmetOvernightBeds,5u);
  EXPECT_EQ(result.report.unmetOvernightTransport,0u);
  EXPECT_TRUE(result.state.stays.empty());
}

TEST(TourismSystemTests, ThreeModesShareDemandAndRemainOrderIndependent) {
  auto offers = std::vector<regional::Offer>{gateway(9,120,regional::Kind::Airport),
    gateway(1,10),gateway(4,60,regional::Kind::PassengerTerminal)};
  const auto first = tourism::advance({},1,{1,100},offers,{{1,20,true,{1,4,9}}});
  std::reverse(offers.begin(),offers.end());
  const auto second = tourism::advance({},1,{1,100},offers,{{1,20,true,{1,4,9}}});
  EXPECT_EQ(first.report.dayArrivals,30u);
  EXPECT_EQ(first.report.overnightArrivals,10u);
  EXPECT_EQ(first.report.staying,10u);
  EXPECT_EQ(first.report.connections[0].id,1u);
  EXPECT_EQ(first.report.connections[1].day,20u);
  EXPECT_EQ(first.report.connections[1].overnight,10u);
  EXPECT_EQ(first.report.connections[2].day,0u);
  EXPECT_EQ(first.report.connections[2].overnight,0u);
  EXPECT_EQ(second.report.dayArrivals,first.report.dayArrivals);
  EXPECT_EQ(second.state.stays[0].guests,first.state.stays[0].guests);
}

TEST(TourismSystemTests, MoreTransportOnlyHelpsWhenTransportLimitsAdmissions) {
  const std::vector<tourism::Hotel> hotels{{1,20,true,{1,2}}};
  const auto road = tourism::advance({},1,{1,100},{gateway(1,20)},hotels);
  const auto air = tourism::advance({},1,{1,100},
    {gateway(1,20),gateway(2,120,regional::Kind::Airport)},hotels);
  EXPECT_EQ(road.report.dayArrivals,20u);
  EXPECT_EQ(road.report.unmetDayTransport,10u);
  EXPECT_EQ(road.report.unmetOvernightTransport,10u);
  EXPECT_EQ(air.report.dayArrivals,30u);
  EXPECT_EQ(air.report.overnightArrivals,10u);
  const auto moreAir = tourism::advance({},1,{1,100},
    {gateway(1,20),gateway(2,120,regional::Kind::Airport),gateway(3,120,regional::Kind::Airport)},hotels);
  EXPECT_EQ(moreAir.report.overnightArrivals,air.report.overnightArrivals);
}

TEST(TourismSystemTests, HotelPathAndGatewayReadinessAreRequired) {
  auto blocked = gateway(2,100,regional::Kind::Airport);
  blocked.access = regional::Access::NoRoadPath;
  const auto result = tourism::advance({},1,{1,0},
    {gateway(1,100),blocked},{{1,100,true,{2}}});
  EXPECT_EQ(result.report.dayArrivals,15u);
  EXPECT_EQ(result.report.overnightArrivals,0u);
  EXPECT_EQ(result.report.unmetOvernightTransport,5u);
  EXPECT_EQ(result.report.unmetOvernightBeds,0u);
  auto disabled = gateway(1,100); disabled.connection.enabled = false;
  const auto closed = tourism::advance({},1,{1,0},{disabled},{});
  EXPECT_EQ(closed.report.dayArrivals,0u);
}

TEST(TourismSystemTests, BedsAreSharedAcrossGatewaysAndShortagesStayOvernight) {
  const auto result = tourism::advance({},1,{1,100},
    {gateway(1,34),gateway(2,100)},{{1,6,true,{1,2}}});
  EXPECT_EQ(result.report.dayArrivals,30u);
  EXPECT_EQ(result.report.overnightArrivals,6u);
  EXPECT_EQ(result.report.unmetOvernightBeds,4u);
  EXPECT_EQ(result.report.unmetOvernightTransport,0u);
  ASSERT_EQ(result.state.stays.size(),1u);
  EXPECT_EQ(result.state.stays[0].guests,6u);
}

TEST(TourismSystemTests, StaysConserveGuestsAndReleaseBedsAfterThreeTicks) {
  tourism::State state;
  uint32_t priorGuests = 0;
  for (uint32_t tick=1;tick<=12;++tick) {
    const auto result = tourism::advance(state,tick,{1,0},{gateway(1,100)},{{1,12,true,{1}}});
    EXPECT_EQ(priorGuests + result.report.overnightArrivals,
      result.report.staying + result.report.scheduledDepartures + result.report.earlyDepartures);
    EXPECT_LE(result.report.staying,12u);
    if (tick == 3) { EXPECT_EQ(result.report.overnightArrivals,2u); }
    if (tick == 4) { EXPECT_EQ(result.report.scheduledDepartures,5u); }
    priorGuests = result.report.staying;
    state = result.state;
  }
}

TEST(TourismSystemTests, ClosingOrRemovingHotelReleasesGuestsWithoutGhostStays) {
  const auto first = tourism::advance({},1,{1,0},{gateway(1,100)},{{1,20,true,{1}}});
  for (const auto& hotels : {std::vector<tourism::Hotel>{},
       std::vector<tourism::Hotel>{{1,20,false,{1}}}}) {
    const auto next = tourism::advance(first.state,2,{1,0},{gateway(1,100)},hotels);
    EXPECT_EQ(next.report.earlyDepartures,5u);
    EXPECT_EQ(next.report.staying,0u);
    EXPECT_EQ(next.report.overnightArrivals,0u);
    EXPECT_TRUE(next.state.stays.empty());
    EXPECT_EQ(first.state.stays[0].guests,5u);
  }
}

TEST(TourismSystemTests, ClosedGatewayDoesNotEvictExistingHotelGuests) {
  const auto first = tourism::advance({},1,{1,0},{gateway(1,100)},{{1,20,true,{1}}});
  const auto next = tourism::advance(first.state,2,{1,0},{},{{1,20,true,{}}});
  EXPECT_EQ(next.report.staying,5u);
  EXPECT_EQ(next.report.overnightArrivals,0u);
  EXPECT_EQ(next.report.earlyDepartures,0u);
}

TEST(TourismSystemTests, InvalidStateAndRepeatedOrSkippedTicksAreRejectedWithoutMutation) {
  const auto first = tourism::advance({},1,{1,0},{gateway(1,100)},{{1,20,true,{1}}});
  EXPECT_THROW(tourism::advance(first.state,1,{},{},{}),std::invalid_argument);
  EXPECT_THROW(tourism::advance(first.state,3,{},{},{}),std::invalid_argument);
  EXPECT_THROW(tourism::advance({},1,{},{gateway(1,10),gateway(1,20)},{}),std::invalid_argument);
  EXPECT_THROW(tourism::advance({},1,{},{},{{1,5,true,{}},{1,5,true,{}}}),std::invalid_argument);
  auto invalid = first.state; invalid.stays[0].departureTick = 1;
  EXPECT_THROW(tourism::advance(invalid,2,{},{},{}),std::invalid_argument);
  invalid = first.state; invalid.stays[0].guests = 21;
  EXPECT_THROW(tourism::advance(invalid,2,{},{},{{1,20,true,{1}}}),std::invalid_argument);
  invalid = first.state; invalid.stays.push_back(invalid.stays[0]);
  EXPECT_THROW(tourism::advance(invalid,2,{},{},{{1,20,true,{1}}}),std::invalid_argument);
  EXPECT_EQ(first.state.tick,1u);
  EXPECT_EQ(first.state.stays[0].guests,5u);
}

TEST(TourismSystemTests, MunicipalChargesUseAdmittedDaysAndOccupiedBedsOnly) {
  tourism::Report report;report.tick=1;report.dayArrivals=10;report.staying=6;
  report.overnightArrivals=2;report.requested.day=1000;report.requested.overnight=500;
  const auto a=tourism::account(report);
  EXPECT_TRUE(a.measured);EXPECT_EQ(a.tick,1u);
  EXPECT_EQ(a.dayFees,20);EXPECT_EQ(a.stayFees,24);EXPECT_EQ(a.services,16);
  report.tick=2;report.dayArrivals=0;report.staying=0;
  EXPECT_EQ(tourism::account(report).fees(),0);EXPECT_EQ(tourism::account(report).services,0);
  report.tick=0;EXPECT_THROW(tourism::account(report),std::invalid_argument);
}
