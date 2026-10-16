#include "TourismSystem.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace tourism {
Accounting account(const Report& report) {
  if (!report.tick || report.dayArrivals>kMaximumDemand || report.staying>kMaximumDemand/4*kStayTicks)
    throw std::invalid_argument("invalid tourism accounting report");
  return {true,report.tick,kDayFee*report.dayArrivals,kStayFee*report.staying,
    kVisitorServiceCost*(static_cast<int64_t>(report.dayArrivals)+report.staying)};
}
Demand demandFor(AttractionInputs inputs) {
  if (inputs.qualityPercent > 100) throw std::invalid_argument("tourism quality exceeds 100");
  Demand demand;
  demand.parkContribution = std::min(inputs.accessibleParks, 50u) * 20;
  // No attraction means no tourism, regardless of the city-quality score.
  demand.qualityContribution = demand.parkContribution * inputs.qualityPercent / 100;
  const auto total = demand.parkContribution + demand.qualityContribution;
  demand.overnight = total / 4;
  demand.day = total - demand.overnight;
  return demand;
}

Result advance(const State& previous, uint32_t tick, AttractionInputs attractions,
               std::vector<regional::Offer> offers, std::vector<Hotel> hotels) {
  if (tick == 0 || tick <= previous.tick || tick - previous.tick != 1 ||
      tick > std::numeric_limits<uint32_t>::max() - kStayTicks)
    throw std::invalid_argument("tourism requires the next completed tick");
  if (hotels.size() > 4096 || previous.stays.size() > 4096 * kStayTicks)
    throw std::invalid_argument("too many tourism records");
  if (offers.size() > 4097 || previous.pendingEarlyDepartures > kMaximumDemand / 4 * kStayTicks)
    throw std::invalid_argument("too many tourism offers or pending departures");
  // One additional slot permits the synthetic road alongside 4096 facilities.
  std::set<uint32_t> offerIds;
  for (const auto& offer : offers) {
    if (!offerIds.insert(offer.connection.id).second)
      throw std::invalid_argument("duplicate tourism gateway ID");
    // Reuse strict regional ID, capacity/mode and access validation.
    (void)regional::allocate(tick, {}, {offer});
  }
  uint64_t previousGuests = previous.pendingEarlyDepartures;
  for (const auto& stay : previous.stays) previousGuests += stay.guests;
  if (previousGuests > kMaximumDemand / 4 * kStayTicks || (!previous.tick && previousGuests))
    throw std::invalid_argument("tourism population exceeds bounded cohorts");
  const auto requested = demandFor(attractions);
  std::sort(offers.begin(), offers.end(), [](const auto& a, const auto& b) {
    return a.connection.id < b.connection.id;
  });
  std::sort(hotels.begin(), hotels.end(), [](const auto& a, const auto& b) {
    return a.id < b.id;
  });
  std::map<uint32_t, uint32_t> occupied;
  std::map<uint32_t, const Hotel*> byId;
  for (const auto& hotel : hotels) {
    if (!hotel.id || hotel.beds > 1000 || !byId.emplace(hotel.id, &hotel).second ||
        hotel.reachableConnections.size() > 4096)
      throw std::invalid_argument("invalid tourism hotel");
    std::set<uint32_t> unique;
    for (auto id : hotel.reachableConnections)
      if (!id || !unique.insert(id).second)
        throw std::invalid_argument("invalid hotel gateway IDs");
  }
  Result result;
  result.state.tick = tick;
  auto& report = result.report;
  report.tick = tick;
  report.attractions = attractions;
  report.earlyDepartures = previous.pendingEarlyDepartures;
  report.requested = requested;
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> survivors;
  std::set<std::pair<uint32_t, uint32_t>> cohortKeys;
  std::map<uint32_t, uint32_t> cohortTotals;
  for (const auto& stay : previous.stays) {
    if (!previous.tick || !stay.hotelId || !stay.guests || stay.guests > 1000 ||
        !cohortKeys.emplace(stay.hotelId, stay.departureTick).second ||
        stay.departureTick <= previous.tick ||
        static_cast<uint64_t>(stay.departureTick) > static_cast<uint64_t>(previous.tick) + kStayTicks)
      throw std::invalid_argument("invalid tourism stay");
    auto& cohortTotal = cohortTotals[stay.departureTick];
    if (stay.guests > kMaximumDemand / 4 - cohortTotal)
      throw std::invalid_argument("tourism cohort exceeds bounded demand");
    cohortTotal += stay.guests;
    auto found = byId.find(stay.hotelId);
    if (stay.departureTick <= tick) report.scheduledDepartures += stay.guests;
    else if (found == byId.end() || !found->second->enabled)
      report.earlyDepartures += stay.guests;
    else {
      auto& count = occupied[stay.hotelId];
      if (stay.guests > found->second->beds - std::min(count, found->second->beds))
        throw std::invalid_argument("hotel occupancy exceeds beds");
      count += stay.guests;
      survivors[{stay.hotelId, stay.departureTick}] += stay.guests;
    }
  }
  uint32_t freeBeds = 0;
  for (const auto& hotel : hotels)
    if (hotel.enabled) freeBeds += hotel.beds - occupied[hotel.id];
  uint32_t dayRemaining = requested.day, overnightRemaining = requested.overnight;
  for (const auto& offer : offers) {
    const auto& connection = offer.connection;
    ConnectionUsage usage; usage.id = connection.id;
    usage.kind = connection.kind;
    usage.access = connection.enabled ? offer.access : regional::Access::Disabled;
    uint32_t capacity = connection.enabled && offer.access == regional::Access::Ready ?
      connection.passengersPerTick : 0;
    usage.capacity = capacity;
    usage.day = std::min(dayRemaining, capacity);
    dayRemaining -= usage.day;
    capacity -= usage.day;
    for (const auto& hotel : hotels) {
      if (!capacity || !overnightRemaining) break;
      if (!hotel.enabled || std::find(hotel.reachableConnections.begin(),
          hotel.reachableConnections.end(), connection.id) == hotel.reachableConnections.end()) continue;
      const auto guests = std::min({overnightRemaining, capacity, hotel.beds - occupied[hotel.id]});
      occupied[hotel.id] += guests;
      capacity -= guests;
      overnightRemaining -= guests;
      usage.overnight += guests;
      if (guests) survivors[{hotel.id, tick + kStayTicks}] += guests;
    }
    report.connections.push_back(usage);
  }
  for (const auto& entry : survivors) {
    result.state.stays.push_back({entry.first.first, entry.second, entry.first.second});
    report.staying += entry.second;
  }
  report.dayArrivals = requested.day - dayRemaining;
  report.dayDepartures = report.dayArrivals;
  report.overnightArrivals = requested.overnight - overnightRemaining;
  report.unmetDayTransport = dayRemaining;
  report.unmetOvernightBeds = std::min(overnightRemaining,
    requested.overnight - std::min(requested.overnight, freeBeds));
  report.unmetOvernightTransport = overnightRemaining - report.unmetOvernightBeds;
  return result;
}
} // namespace tourism
