#pragma once
#include "src/systems/RegionalAccounting.hpp"
#include "src/systems/TourismSystem.hpp"
#include <map>
#include <string>

namespace regional {
// Derived completed-report diagnostics: no timers, policy edits or extra save state.
enum class Problem { PassengerAccess, HotelBeds, CargoCapacity, CargoDelivery, VisitorTravel };
struct Diagnostic {
  Problem problem;
  uint32_t tick=0;
  uint64_t unmet=0;
  uint32_t targetId=0; // Connection, hotel or route; zero means citywide.
};
inline const char* problemName(Problem problem) {
  switch (problem) {
  case Problem::PassengerAccess: return "VISITOR ACCESS";
  case Problem::HotelBeds: return "HOTEL BEDS";
  case Problem::CargoCapacity: return "CARGO ACCESS/CAPACITY";
  case Problem::CargoDelivery: return "LOCAL CARGO DELIVERY";
  case Problem::VisitorTravel: return "LOCAL VISITOR TRAVEL";
  }
  return "UNKNOWN";
}
inline const char* problemAdvice(Problem problem) {
  switch (problem) {
  case Problem::PassengerAccess: return "CHECK PARK ROUTES, UTILITIES AND CAPACITY";
  case Problem::HotelBeds: return "REOPEN HOTELS OR ADD REACHABLE BEDS";
  case Problem::CargoCapacity: return "CHECK TERMINAL ACCESS AND GOODS CAPACITY";
  case Problem::CargoDelivery: return "CHECK LOCAL TRUCK ROUTES TO DESTINATIONS";
  case Problem::VisitorTravel: return "CHECK LOCAL ROUTES; RESIDENTS HAVE PRIORITY";
  }
  return "";
}
inline std::vector<Diagnostic> diagnose(const Settlement& cargo,const tourism::Runtime& visitors) {
  std::vector<Diagnostic> result;
  const auto& report=visitors.report;
  const uint64_t access=static_cast<uint64_t>(report.unmetDayTransport)+report.unmetOvernightTransport;
  if (report.tick && access) {
    uint32_t target=0; int best=0;
    for (const auto& row:report.connections) {
      if (row.kind!=Kind::PassengerTerminal && row.kind!=Kind::Airport) continue;
      const int score=row.access!=Access::Ready ? 2 : (row.day+row.overnight>=row.capacity ? 1 : 0);
      if (score>best || (score && score==best && (!target || row.id<target))) {best=score;target=row.id;}
    }
    result.push_back({Problem::PassengerAccess,report.tick,access,target});
  }
  if (report.tick && report.unmetOvernightBeds) {
    std::map<uint32_t,uint64_t> guests;
    for (const auto& stay:visitors.state.stays) guests[stay.hotelId]+=stay.guests;
    uint32_t target=0; uint64_t largest=0;
    for (const auto& entry:guests) if (entry.second>largest) {target=entry.first;largest=entry.second;}
    result.push_back({Problem::HotelBeds,report.tick,report.unmetOvernightBeds,target});
  }
  if (cargo.valid && cargo.allocation.tick) {
    const uint64_t unmet=static_cast<uint64_t>(cargo.allocation.unmet.imports)+cargo.allocation.unmet.exports;
    const uint64_t undelivered=static_cast<uint64_t>(cargo.undelivered.imports)+cargo.undelivered.exports;
    // Only delivery deficits can be attributed to an admitted connection. Do not
    // invent per-terminal unmet demand from a citywide allocation shortfall.
    uint32_t target=0;
    std::map<uint32_t,uint64_t> deliveredById;
    for (const auto& delivery:cargo.deliveries)
      deliveredById[delivery.id]+=static_cast<uint64_t>(delivery.delivered.imports)+delivery.delivered.exports;
    for (const auto& row:cargo.allocation.connections) {
      const auto delivered=deliveredById[row.id];
      if (static_cast<uint64_t>(row.imports)+row.exports>delivered && (!target || row.id<target)) target=row.id;
    }
    if (unmet) result.push_back({Problem::CargoCapacity,cargo.allocation.tick,unmet,0});
    if (undelivered) result.push_back({Problem::CargoDelivery,cargo.allocation.tick,undelivered,target});
  }
  if (visitors.travel.measured && visitors.travel.tick && visitors.travel.unrouted)
    result.push_back({Problem::VisitorTravel,visitors.travel.tick,visitors.travel.unrouted,0});
  return result;
}
} // namespace regional
