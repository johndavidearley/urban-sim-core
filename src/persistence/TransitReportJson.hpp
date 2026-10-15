#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <limits>
#include "src/systems/TransitSystem.hpp"

inline nlohmann::json transitReportJson(const TransitReport& report) {
  auto rows = nlohmann::json::array();
  for (const auto& row : report.routes) rows.push_back({{"id",row.id},{"capacity",row.capacity},{"demand",row.demand},{"ridership",row.ridership}});
  return {{"valid",report.valid},{"tick",report.tick},{"commuters",report.commuters},{"routes",rows}};
}
inline TransitReport parseTransitReport(const nlohmann::json& data,uint32_t tick) {
  auto number = [](const nlohmann::json& v) -> uint32_t {
    if (!v.is_number_integer() || v.get<double>() < 0 || v.get<double>() > UINT32_MAX)
      throw std::runtime_error("Invalid transit report integer");
    return v.get<uint32_t>();
  };
  TransitReport r;
  r.valid=data.at("valid").get<bool>(); r.tick=number(data.at("tick")); r.commuters=number(data.at("commuters"));
  const auto& rows=data.at("routes");
  if (!rows.is_array() || rows.size()>4096 || (r.valid && (r.tick==0 || r.tick!=tick)) || (!r.valid && (r.tick || r.commuters || !rows.empty())))
    throw std::runtime_error("Invalid transit report shape");
  std::set<TransitRouteId> ids;
  uint64_t demand=0;
  for (const auto& row : rows) {
    TransitRouteUsage u{number(row.at("id")),number(row.at("capacity")),number(row.at("demand")),number(row.at("ridership"))};
    if (!ids.insert(u.id).second || u.ridership>u.capacity || u.ridership>u.demand)
      throw std::runtime_error("Inconsistent transit route usage");
    demand+=u.demand; r.routes.push_back(u);
  }
  if (demand>r.commuters) throw std::runtime_error("Transit demand exceeds commuters");
  return r;
}
