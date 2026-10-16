#include "TourismTravel.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include "src/networks/Pathfinding.hpp"

namespace tourism {
std::vector<TravelLeg> planTravel(const State& previous,const State& current,
    const Report& report,const AccessPlan& access,const RoadNetwork& roads) {
  if (current.tick!=report.tick || static_cast<uint64_t>(previous.tick)+1!=current.tick ||
      previous.stays.size()>4096*kStayTicks || current.stays.size()>4096*kStayTicks ||
      report.dayArrivals>kMaximumDemand || report.overnightArrivals>kMaximumDemand/4 ||
      report.staying>kMaximumDemand/4*kStayTicks)
    throw std::invalid_argument("invalid visitor travel state");
  std::vector<TravelLeg> legs;
  auto add=[&](TripKind kind,Coord from,Coord to,uint32_t count) {
    if (count) legs.push_back({kind,from,to,count,0,0,0,0});
  };
  std::map<uint32_t,const Hotel*> hotels;
  for (const auto& hotel:access.hotels) hotels.emplace(hotel.id,&hotel);
  std::map<uint32_t,const Destination*> destinations;
  for (const auto& destination:access.destinations) destinations.emplace(destination.gatewayId,&destination);
  for (const auto& usage:report.connections) {
    const auto destination=destinations.find(usage.id);
    add(TripKind::DayVisit,destination==destinations.end() ? Coord{-1,-1} : destination->second->gateway,
      destination==destinations.end() ? Coord{-1,-1} : destination->second->attraction,2*usage.day);
  }
  std::map<uint32_t,uint32_t> arrivals,staying;
  std::set<std::pair<uint32_t,uint32_t>> retained;
  for (const auto& stay:current.stays) {
    staying[stay.hotelId]+=stay.guests;retained.emplace(stay.hotelId,stay.departureTick);
    if (static_cast<uint64_t>(stay.departureTick)==static_cast<uint64_t>(report.tick)+kStayTicks)
      arrivals[stay.hotelId]+=stay.guests;
  }
  // Match admissions to hotel beds in the same stable gateway/hotel order used
  // by advance(). This reproduces the allocation without storing itineraries.
  for (const auto& usage:report.connections) {
    auto remaining=usage.overnight;
    const auto gateway=destinations.find(usage.id);
    for (auto& entry:arrivals) {
      const auto hotel=hotels.find(entry.first);
      if (hotel==hotels.end() || std::find(hotel->second->reachableConnections.begin(),
          hotel->second->reachableConnections.end(),usage.id)==hotel->second->reachableConnections.end()) continue;
      const auto count=std::min(remaining,entry.second);
      add(TripKind::HotelAccess,gateway==destinations.end() ? Coord{-1,-1} : gateway->second->gateway,
        hotel->second->roadAnchor,count);
      remaining-=count;entry.second-=count;
    }
    add(TripKind::HotelAccess,{-1,-1},{-1,-1},remaining);
  }
  for (const auto& entry:staying) {
    const auto hotel=hotels.find(entry.first);
    add(TripKind::HotelAttraction,hotel==hotels.end() ? Coord{-1,-1} : hotel->second->roadAnchor,
      hotel==hotels.end() ? Coord{-1,-1} : hotel->second->attractionRoad,2*entry.second);
  }
  std::map<uint32_t,uint32_t> departures;
  for (const auto& stay:previous.stays)
    if (!retained.count({stay.hotelId,stay.departureTick})) departures[stay.hotelId]+=stay.guests;
  for (const auto& entry:departures) {
    const auto hotel=hotels.find(entry.first);
    const auto origin=hotel==hotels.end() ? Coord{-1,-1} : hotel->second->roadAnchor;
    Coord destination{-1,-1};
    for (const auto& gateway:destinations) {
      if (gateway.second->operating && roads.hasRoadAdjacency(origin) &&
          Pathfinding::findShortestPath(roads,origin,gateway.second->gateway).found) {
        destination=gateway.second->gateway;break;
      }
    }
    add(TripKind::Departure,origin,destination,entry.second);
  }
  add(TripKind::Departure,{-1,-1},{-1,-1},previous.pendingEarlyDepartures);
  uint64_t counts[4]={0,0,0,0};
  for (const auto& leg:legs) counts[static_cast<size_t>(leg.kind)]+=leg.requested;
  if (counts[0]!=2ULL*report.dayArrivals || counts[1]!=report.overnightArrivals ||
      counts[2]!=2ULL*report.staying || counts[3]!=static_cast<uint64_t>(report.scheduledDepartures)+report.earlyDepartures)
    throw std::invalid_argument("visitor leg counts do not reconcile");
  return legs;
}

TravelReport applyTravel(uint32_t tick,std::vector<TravelLeg> legs,RoadNetwork& roads,
    const std::vector<TransitRoute>& routes,const TransitCoverageCache& cache,
    const std::vector<TransitRouteUsage>& residents) {
  if (!tick || legs.size()>kMaximumVisitorTrips || routes.size()>4096)
    throw std::invalid_argument("invalid visitor travel bounds");
  uint64_t requested=0;
  for (const auto& leg:legs) {
    if (!leg.requested || static_cast<int>(leg.kind)<0 || static_cast<int>(leg.kind)>3)
      throw std::invalid_argument("invalid visitor leg");
    requested+=leg.requested;
  }
  if (requested>kMaximumVisitorTrips) throw std::invalid_argument("too many visitor trips");
  TransitOffload transit(routes,cache,&residents);
  TravelReport report;report.measured=true;report.tick=tick;
  for (auto& leg:legs) {
    leg.road=leg.transit=leg.walking=leg.unrouted=0;
    const auto path=Pathfinding::findShortestPathWithCongestionWeight(roads,leg.origin,leg.destination,0.0f);
    if (!roads.hasRoadAdjacency(leg.origin) || !roads.hasRoadAdjacency(leg.destination) || !path.found)
      leg.unrouted=leg.requested;
    else if (leg.origin==leg.destination) leg.walking=leg.requested;
    else {
      leg.transit=transit.offloadAcrossRoutes(leg.origin,leg.destination,leg.requested);
      leg.road=leg.requested-leg.transit;
      for (size_t i=1;i<path.waypoints.size();++i)
        roads.addVisitorLoad(path.waypoints[i-1],path.waypoints[i],static_cast<float>(leg.road));
    }
    report.requested+=leg.requested;report.road+=leg.road;report.transit+=leg.transit;
    report.walking+=leg.walking;report.unrouted+=leg.unrouted;
  }
  std::map<uint32_t,uint32_t> reserved;
  for (const auto& row:residents) reserved.emplace(row.id,row.ridership);
  for (const auto& row:transit.routeUsage())
    report.routes.push_back({row.id,row.capacity,reserved[row.id],row.demand,row.ridership});
  report.legs=std::move(legs);
  return report;
}
}
