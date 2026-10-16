#pragma once

#include <cstdint>
#include <vector>
#include "RegionalConnectionSystem.hpp"

namespace tourism {
constexpr uint32_t kMaximumDemand = 2000;
constexpr uint32_t kStayTicks = 3;

// Inputs are measured by the host. Only parks reachable from a visitor gateway
// belong here; quality is a bounded score, not an unbounded demand multiplier.
struct AttractionInputs {
  uint32_t accessibleParks = 0;
  uint32_t qualityPercent = 0;
};
struct Demand {
  uint32_t parkContribution = 0, qualityContribution = 0;
  uint32_t day = 0, overnight = 0;
};
Demand demandFor(AttractionInputs inputs);

struct Hotel {
  uint32_t id = 0, beds = 0;
  bool enabled = true;
  // Gateway IDs with a confirmed local route to this hotel. The caller must
  // also diagnose each offer's actual route to an attraction, not its entrance.
  std::vector<uint32_t> reachableConnections;
  Coord roadAnchor{-1,-1};
  Coord attractionRoad{-1,-1};
};
struct Stay {
  uint32_t hotelId = 0, guests = 0, departureTick = 0;
};
struct State {
  uint32_t tick = 0;
  uint32_t pendingEarlyDepartures = 0;
  std::vector<Stay> stays;
};
struct ConnectionUsage {
  uint32_t id = 0, day = 0, overnight = 0, capacity = 0;
  regional::Kind kind = regional::Kind::RoadGateway;
  regional::Access access = regional::Access::Ready;
};
struct Report {
  uint32_t tick = 0;
  bool enabled = false;
  AttractionInputs attractions;
  Demand requested;
  uint32_t dayArrivals = 0, dayDepartures = 0, overnightArrivals = 0;
  uint32_t scheduledDepartures = 0, earlyDepartures = 0;
  uint32_t staying = 0, unmetDayTransport = 0;
  uint32_t unmetOvernightTransport = 0, unmetOvernightBeds = 0;
  std::vector<ConnectionUsage> connections;
};
struct Result { State state; Report report; };
constexpr uint32_t kMaximumVisitorTrips = 4 * kMaximumDemand;
enum class TripKind { DayVisit, HotelAccess, HotelAttraction, Departure };
struct TravelLeg {
  TripKind kind = TripKind::DayVisit;
  Coord origin{-1,-1}, destination{-1,-1};
  uint32_t requested = 0, road = 0, transit = 0, walking = 0, unrouted = 0;
};
struct VisitorRouteUsage {
  uint32_t id = 0, capacity = 0, residentRiders = 0, demand = 0, ridership = 0;
};
struct TravelReport {
  bool measured = false;
  uint32_t tick = 0, requested = 0, road = 0, transit = 0, walking = 0, unrouted = 0;
  std::vector<TravelLeg> legs;
  std::vector<VisitorRouteUsage> routes;
};
struct Accounting {
  bool measured = false;
  uint32_t tick = 0;
  int64_t dayFees = 0, stayFees = 0, services = 0;
  int64_t fees() const { return dayFees+stayFees; }
};
// Treasury dollars per completed tick, separate from private spending/taxes.
constexpr int64_t kDayFee=2, kStayFee=4, kVisitorServiceCost=1;
Accounting account(const Report& report);
struct Runtime {
  bool enabled = false; // Legacy cities opt in explicitly or on hotel construction.
  State state;
  Report report; // Last completed measurement, unchanged by paused policy edits.
  TravelReport travel;
  Accounting accounting;
};

// Pure completed-tick calculation. Residents, money, jobs and road loads are
// deliberately outside this model. All gateways share one remaining demand;
// day visits have first use, then overnight arrivals require reachable beds.
// Stay duration means three completed simulation ticks, not wall-clock days.
// Invalid input throws without mutating the caller's state.
Result advance(const State& previous, uint32_t tick, AttractionInputs attractions,
               std::vector<regional::Offer> offers, std::vector<Hotel> hotels);
} // namespace tourism
