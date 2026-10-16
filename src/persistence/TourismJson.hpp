#pragma once
#include <map>
#include <set>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "src/systems/TourismSystem.hpp"
#include "src/persistence/TourismTravelJson.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/gameplay/TreasurySystem.hpp"

inline void validateTourismAccounting(const tourism::Runtime& visitors,const TreasuryLedger& ledger) {
  const auto& a=visitors.accounting;
  if (a.measured) {
    if (!ledger.valid || ledger.tick!=a.tick || ledger.breakdown.tourismFees!=a.fees() ||
        ledger.breakdown.tourismServices!=a.services)
      throw std::runtime_error("tourism accounting does not match treasury ledger");
  } else if (ledger.breakdown.tourismFees || ledger.breakdown.tourismServices)
    throw std::runtime_error("tourism ledger lacks completed accounting");
}

inline nlohmann::json tourismJson(const tourism::Runtime& runtime,uint32_t cityTick) {
  const auto& s=runtime.state; const auto& r=runtime.report;
  auto stays=nlohmann::json::array(), rows=nlohmann::json::array();
  for (const auto& stay:s.stays) stays.push_back({{"hotelId",stay.hotelId},
    {"guests",stay.guests},{"departureTick",stay.departureTick}});
  for (const auto& row:r.connections) rows.push_back({{"id",row.id},{"day",row.day},
    {"overnight",row.overnight},{"capacity",row.capacity},{"kind",static_cast<int>(row.kind)},
    {"access",static_cast<int>(row.access)}});
  const auto& a=runtime.accounting;
  return {{"accounting",{{"measured",a.measured},{"tick",a.tick},{"dayFees",a.dayFees},
      {"stayFees",a.stayFees},{"services",a.services}}},
    {"travel",tourismTravelJson(runtime.travel)},{"enabled",runtime.enabled},{"state",{{"tick",s.tick ? s.tick : cityTick},
    {"pendingEarlyDepartures",s.pendingEarlyDepartures},{"stays",stays}}},
    {"report",{{"tick",r.tick},{"enabled",r.enabled},
      {"accessibleParks",r.attractions.accessibleParks},{"qualityPercent",r.attractions.qualityPercent},
      {"parkContribution",r.requested.parkContribution},{"qualityContribution",r.requested.qualityContribution},
      {"requestedDay",r.requested.day},{"requestedOvernight",r.requested.overnight},
      {"dayArrivals",r.dayArrivals},{"dayDepartures",r.dayDepartures},{"overnightArrivals",r.overnightArrivals},
      {"scheduledDepartures",r.scheduledDepartures},{"earlyDepartures",r.earlyDepartures},
      {"staying",r.staying},{"unmetDayTransport",r.unmetDayTransport},
      {"unmetOvernightTransport",r.unmetOvernightTransport},{"unmetOvernightBeds",r.unmetOvernightBeds},
      {"connections",rows}}}};
}

inline tourism::Runtime parseTourism(const nlohmann::json& data,uint32_t cityTick,
                                     const CitySnapshot& snapshot,const TransitReport* residents=nullptr) {
  auto number=[](const nlohmann::json& v,uint32_t maximum=UINT32_MAX) -> uint32_t {
    if (!v.is_number_integer() || (!v.is_number_unsigned() && v.get<int64_t>()<0) ||
        v.get<uint64_t>()>maximum) throw std::runtime_error("invalid tourism integer");
    return v.get<uint32_t>();
  };
  auto require=[](bool valid) {if (!valid) throw std::runtime_error("inconsistent tourism state/report");};
  constexpr uint32_t maximumStaying=tourism::kMaximumDemand/4*tourism::kStayTicks;
  tourism::Runtime result; result.enabled=data.at("enabled").get<bool>();
  const auto& saved=data.at("state"); auto& s=result.state;
  s.tick=number(saved.at("tick")); require(s.tick==cityTick);
  s.pendingEarlyDepartures=number(saved.at("pendingEarlyDepartures"),maximumStaying);
  const auto& stays=saved.at("stays"); require(stays.is_array() && stays.size()<=4096*tourism::kStayTicks);
  std::map<uint32_t,const Building*> hotels;
  for (const auto& b:snapshot.buildings) if (b.hotelBeds) hotels.emplace(b.id,&b);
  require(hotels.size()<=4096);
  std::set<std::pair<uint32_t,uint32_t>> keys;
  std::map<uint32_t,uint32_t> occupancy, departureTotals;
  uint64_t active=0;
  for (const auto& item:stays) {
    tourism::Stay stay{number(item.at("hotelId")),number(item.at("guests"),1000),number(item.at("departureTick"))};
    const auto hotel=hotels.find(stay.hotelId);
    require(s.tick && stay.guests && stay.departureTick>s.tick &&
      static_cast<uint64_t>(stay.departureTick)<=static_cast<uint64_t>(s.tick)+tourism::kStayTicks &&
      hotel!=hotels.end() && hotel->second->type==BuildingType::Commercial &&
      keys.emplace(stay.hotelId,stay.departureTick).second);
    occupancy[stay.hotelId]+=stay.guests; departureTotals[stay.departureTick]+=stay.guests;
    require(occupancy[stay.hotelId]<=hotel->second->hotelBeds &&
      departureTotals[stay.departureTick]<=tourism::kMaximumDemand/4);
    active+=stay.guests; s.stays.push_back(stay);
  }
  require(active+s.pendingEarlyDepartures<=maximumStaying);
  const auto& savedReport=data.at("report"); auto& r=result.report;
  r.tick=number(savedReport.at("tick")); r.enabled=savedReport.at("enabled").get<bool>();
  r.attractions={number(savedReport.at("accessibleParks")),number(savedReport.at("qualityPercent"),100)};
  r.requested={number(savedReport.at("parkContribution"),tourism::kMaximumDemand),
    number(savedReport.at("qualityContribution"),tourism::kMaximumDemand),
    number(savedReport.at("requestedDay"),tourism::kMaximumDemand),
    number(savedReport.at("requestedOvernight"),tourism::kMaximumDemand/4)};
  const auto expected=tourism::demandFor(r.attractions);
  require(expected.parkContribution==r.requested.parkContribution &&
    expected.qualityContribution==r.requested.qualityContribution &&
    expected.day==r.requested.day && expected.overnight==r.requested.overnight);
  r.dayArrivals=number(savedReport.at("dayArrivals"),tourism::kMaximumDemand);
  r.dayDepartures=number(savedReport.at("dayDepartures"),tourism::kMaximumDemand);
  r.overnightArrivals=number(savedReport.at("overnightArrivals"),tourism::kMaximumDemand/4);
  r.scheduledDepartures=number(savedReport.at("scheduledDepartures"),maximumStaying);
  r.earlyDepartures=number(savedReport.at("earlyDepartures"),maximumStaying);
  r.staying=number(savedReport.at("staying"),maximumStaying);
  r.unmetDayTransport=number(savedReport.at("unmetDayTransport"),tourism::kMaximumDemand);
  r.unmetOvernightTransport=number(savedReport.at("unmetOvernightTransport"),tourism::kMaximumDemand/4);
  r.unmetOvernightBeds=number(savedReport.at("unmetOvernightBeds"),tourism::kMaximumDemand/4);
  require((r.tick==cityTick || !r.tick) && r.dayArrivals==r.dayDepartures &&
    r.dayArrivals+r.unmetDayTransport==r.requested.day &&
    r.overnightArrivals+r.unmetOvernightBeds+r.unmetOvernightTransport==r.requested.overnight &&
    r.staying==active+s.pendingEarlyDepartures && r.overnightArrivals<=r.staying &&
    r.scheduledDepartures+r.earlyDepartures<=maximumStaying);
  const auto& rows=savedReport.at("connections"); require(rows.is_array() && rows.size()<=4097);
  uint64_t day=0, overnight=0; std::set<uint32_t> ids;
  for (const auto& item:rows) {
    tourism::ConnectionUsage row;
    row.id=number(item.at("id")); row.day=number(item.at("day"),tourism::kMaximumDemand);
    row.overnight=number(item.at("overnight"),tourism::kMaximumDemand/4); row.capacity=number(item.at("capacity"));
    row.kind=static_cast<regional::Kind>(number(item.at("kind"),3));
    row.access=static_cast<regional::Access>(number(item.at("access"),9));
    require(row.id && ids.insert(row.id).second && row.day+row.overnight<=row.capacity &&
      (row.access==regional::Access::Ready || (!row.capacity && !row.day && !row.overnight)) &&
      (row.kind!=regional::Kind::CargoPort || (!row.capacity && !row.day && !row.overnight)));
    day+=row.day; overnight+=row.overnight; r.connections.push_back(row);
  }
  require(day==r.dayArrivals && overnight==r.overnightArrivals);
  if (!r.enabled) require(!r.requested.day && !r.requested.overnight && !r.attractions.accessibleParks &&
    !r.attractions.qualityPercent && !r.staying && rows.empty());
  if (!r.tick) {
    const auto empty=tourismJson(tourism::Runtime{},cityTick).at("report");
    require(savedReport==empty && !active && !s.pendingEarlyDepartures);
  }
  if (data.contains("travel"))
    result.travel=parseTourismTravel(data.at("travel"),result.report,snapshot.width,snapshot.height,residents);
  if (data.contains("accounting")) {
    const auto& a=data.at("accounting");auto& value=result.accounting;
    value.measured=a.at("measured").get<bool>();value.tick=number(a.at("tick"));
    value.dayFees=number(a.at("dayFees"),tourism::kMaximumDemand*tourism::kDayFee);
    value.stayFees=number(a.at("stayFees"),maximumStaying*tourism::kStayFee);
    value.services=number(a.at("services"),(tourism::kMaximumDemand+maximumStaying)*tourism::kVisitorServiceCost);
    if (value.measured) {
      const auto expectedAccounting=tourism::account(result.report);
      require(value.tick==expectedAccounting.tick && value.dayFees==expectedAccounting.dayFees &&
        value.stayFees==expectedAccounting.stayFees && value.services==expectedAccounting.services);
    } else require(!value.tick && !value.dayFees && !value.stayFees && !value.services);
  }
  return result;
}
