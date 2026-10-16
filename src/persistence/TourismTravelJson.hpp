#pragma once
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <stdexcept>
#include "src/systems/TourismSystem.hpp"
#include "src/systems/TransitSystem.hpp"

inline nlohmann::json tourismTravelJson(const tourism::TravelReport& r) {
  auto legs=nlohmann::json::array(),routes=nlohmann::json::array();
  for (const auto& leg:r.legs) legs.push_back({{"kind",static_cast<int>(leg.kind)},
    {"origin",{{"x",leg.origin.x},{"y",leg.origin.y}}},
    {"destination",{{"x",leg.destination.x},{"y",leg.destination.y}}},
    {"requested",leg.requested},{"road",leg.road},{"transit",leg.transit},
    {"walking",leg.walking},{"unrouted",leg.unrouted}});
  for (const auto& route:r.routes) routes.push_back({{"id",route.id},{"capacity",route.capacity},
    {"residentRiders",route.residentRiders},{"demand",route.demand},{"ridership",route.ridership}});
  return {{"measured",r.measured},{"tick",r.tick},{"requested",r.requested},{"road",r.road},
    {"transit",r.transit},{"walking",r.walking},{"unrouted",r.unrouted},{"legs",legs},{"routes",routes}};
}

inline tourism::TravelReport parseTourismTravel(const nlohmann::json& data,
    const tourism::Report& tourismReport,int width,int height,const TransitReport* residents=nullptr) {
  auto require=[](bool valid) {if (!valid) throw std::runtime_error("inconsistent visitor travel report");};
  auto number=[](const nlohmann::json& v,uint32_t maximum=UINT32_MAX) -> uint32_t {
    if (!v.is_number_integer() || (!v.is_number_unsigned() && v.get<int64_t>()<0) ||
        v.get<uint64_t>()>maximum) throw std::runtime_error("invalid visitor travel integer");
    return v.get<uint32_t>();
  };
  auto coord=[&](const nlohmann::json& value) {
    auto integer=[](const nlohmann::json& v,int maximum) {
      if (!v.is_number_integer() || v.get<double>() < -1 || v.get<double>()>=maximum)
        throw std::runtime_error("invalid visitor travel coordinate");
      return v.get<int>();
    };
    return Coord{integer(value.at("x"),width),integer(value.at("y"),height)};
  };
  tourism::TravelReport r;
  r.measured=data.at("measured").get<bool>();r.tick=number(data.at("tick"));
  r.requested=number(data.at("requested"),tourism::kMaximumVisitorTrips);
  r.road=number(data.at("road"),tourism::kMaximumVisitorTrips);
  r.transit=number(data.at("transit"),tourism::kMaximumVisitorTrips);
  r.walking=number(data.at("walking"),tourism::kMaximumVisitorTrips);
  r.unrouted=number(data.at("unrouted"),tourism::kMaximumVisitorTrips);
  const auto& legs=data.at("legs");const auto& routes=data.at("routes");
  require(legs.is_array() && legs.size()<=tourism::kMaximumVisitorTrips && routes.is_array() && routes.size()<=4096);
  uint64_t requested=0,road=0,transit=0,walking=0,unrouted=0,kinds[4]={0,0,0,0};
  for (const auto& item:legs) {
    tourism::TravelLeg leg;
    leg.kind=static_cast<tourism::TripKind>(number(item.at("kind"),3));
    leg.origin=coord(item.at("origin"));leg.destination=coord(item.at("destination"));
    leg.requested=number(item.at("requested"),tourism::kMaximumVisitorTrips);
    leg.road=number(item.at("road"),tourism::kMaximumVisitorTrips);
    leg.transit=number(item.at("transit"),tourism::kMaximumVisitorTrips);
    leg.walking=number(item.at("walking"),tourism::kMaximumVisitorTrips);
    leg.unrouted=number(item.at("unrouted"),tourism::kMaximumVisitorTrips);
    require(leg.requested && leg.road+leg.transit+leg.walking+leg.unrouted==leg.requested);
    const bool missingOrigin=leg.origin==Coord{-1,-1},missingDestination=leg.destination==Coord{-1,-1};
    require((leg.origin.x>=0 && leg.origin.y>=0) || missingOrigin);
    require((leg.destination.x>=0 && leg.destination.y>=0) || missingDestination);
    if (missingOrigin || missingDestination) require(leg.unrouted==leg.requested);
    else if (leg.origin==leg.destination) require(leg.walking==leg.requested || leg.unrouted==leg.requested);
    else require(!leg.walking);
    requested+=leg.requested;road+=leg.road;transit+=leg.transit;walking+=leg.walking;unrouted+=leg.unrouted;
    kinds[static_cast<size_t>(leg.kind)]+=leg.requested;r.legs.push_back(leg);
  }
  require(requested==r.requested && road==r.road && transit==r.transit && walking==r.walking && unrouted==r.unrouted);
  uint64_t riders=0,demand=0;std::set<uint32_t> ids;
  std::map<uint32_t,TransitRouteUsage> reserved;
  if (residents) for (const auto& row:residents->routes) reserved.emplace(row.id,row);
  for (const auto& item:routes) {
    tourism::VisitorRouteUsage row{number(item.at("id")),number(item.at("capacity")),
      number(item.at("residentRiders")),number(item.at("demand"),tourism::kMaximumVisitorTrips),
      number(item.at("ridership"),tourism::kMaximumVisitorTrips)};
    require(ids.insert(row.id).second && row.residentRiders<=row.capacity &&
      row.ridership<=row.capacity-row.residentRiders && row.ridership<=row.demand);
    if (residents) {
      const auto found=reserved.find(row.id);
      require(residents->valid && residents->tick==r.tick && found!=reserved.end() &&
        found->second.capacity==row.capacity && found->second.ridership==row.residentRiders);
    }
    riders+=row.ridership;demand+=row.demand;r.routes.push_back(row);
  }
  require(riders==r.transit && demand<=r.road+r.transit);
  if (r.measured) {
    require(r.tick && r.tick==tourismReport.tick && kinds[0]==2ULL*tourismReport.dayArrivals &&
      kinds[1]==tourismReport.overnightArrivals && kinds[2]==2ULL*tourismReport.staying &&
      kinds[3]==static_cast<uint64_t>(tourismReport.scheduledDepartures)+tourismReport.earlyDepartures);
    if (residents) require(residents->routes.size()==r.routes.size());
  } else require(data==tourismTravelJson({}));
  return r;
}
