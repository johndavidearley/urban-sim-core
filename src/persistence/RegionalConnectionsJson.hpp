#pragma once

#include <algorithm>
#include <set>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "src/systems/RegionalConnectionSystem.hpp"
#include "src/gameplay/TerminalTool.hpp"

// Configuration only: no saved reachability cache or fabricated usage report.
inline nlohmann::json regionalConnectionsJson(std::vector<regional::Connection> connections) {
  std::sort(connections.begin(),connections.end(),[](const auto& a,const auto& b) { return a.id<b.id; });
  auto rows=nlohmann::json::array();
  for (const auto& c:connections)
    rows.push_back({{"id",c.id},{"kind",static_cast<int>(c.kind)},
      {"x",c.origin.x},{"y",c.origin.y},{"width",c.width},{"height",c.height},
      {"entranceX",c.entrance.x},{"entranceY",c.entrance.y},{"enabled",c.enabled},
      {"passengersPerTick",c.passengersPerTick},{"goodsPerTick",c.goodsPerTick},
      {"constructed",c.constructed}});
  return rows;
}

inline std::vector<regional::Connection> parseRegionalConnections(
    const nlohmann::json& rows, const CityMap& map) {
  auto invalid=[]() { throw std::runtime_error("Invalid regional connection configuration"); };
  auto integer=[&](const nlohmann::json& value,uint64_t maximum) -> uint32_t {
    if (!value.is_number_integer() || value.get<double>()<0 || value.get<double>()>maximum) invalid();
    return value.get<uint32_t>();
  };
  if (!rows.is_array() || rows.size()>4096) invalid();
  std::set<uint32_t> ids;
  std::vector<regional::Connection> connections;
  const auto size=map.getDimensions();
  auto contains=[](const regional::Connection& c,Coord p) {
    return p.x>=c.origin.x && p.y>=c.origin.y &&
      static_cast<int64_t>(p.x)<static_cast<int64_t>(c.origin.x)+c.width &&
      static_cast<int64_t>(p.y)<static_cast<int64_t>(c.origin.y)+c.height;
  };
  for (const auto& row:rows) {
    regional::Connection c;
    c.id=integer(row.at("id"),UINT32_MAX);
    c.kind=static_cast<regional::Kind>(integer(row.at("kind"),3));
    c.origin={static_cast<int>(integer(row.at("x"),size.x-1)),
              static_cast<int>(integer(row.at("y"),size.y-1))};
    c.width=static_cast<int>(integer(row.at("width"),size.x));
    c.height=static_cast<int>(integer(row.at("height"),size.y));
    c.entrance={static_cast<int>(integer(row.at("entranceX"),size.x-1)),
                static_cast<int>(integer(row.at("entranceY"),size.y-1))};
    if (!row.at("enabled").is_boolean()) invalid();
    c.enabled=row.at("enabled").get<bool>();
    if (row.contains("constructed")) {
      if (!row.at("constructed").is_boolean()) invalid();
      c.constructed=row.at("constructed").get<bool>();
    }
    if (c.constructed) {
      const auto* spec=regional::terminalSpec(c.kind);
      if (!spec || c.width!=spec->width || c.height!=spec->height) invalid();
    }
    c.passengersPerTick=integer(row.at("passengersPerTick"),UINT32_MAX);
    c.goodsPerTick=integer(row.at("goodsPerTick"),UINT32_MAX);
    if (!c.id || !ids.insert(c.id).second || !c.width || !c.height ||
        static_cast<int64_t>(c.origin.x)+c.width>size.x ||
        static_cast<int64_t>(c.origin.y)+c.height>size.y) invalid();
    const bool inside=contains(c,c.entrance);
    if (c.kind==regional::Kind::RoadGateway) {
      if (!inside || (c.entrance.x!=0 && c.entrance.y!=0 &&
          c.entrance.x!=size.x-1 && c.entrance.y!=size.y-1)) invalid();
    } else {
      const bool adjacent=contains(c,{c.entrance.x-1,c.entrance.y}) ||
        contains(c,{c.entrance.x+1,c.entrance.y}) ||
        contains(c,{c.entrance.x,c.entrance.y-1}) ||
        contains(c,{c.entrance.x,c.entrance.y+1});
      if (inside || !adjacent) invalid();
    }
    for (const auto& other:connections) {
      const bool overlap=c.origin.x<other.origin.x+other.width &&
        other.origin.x<c.origin.x+c.width && c.origin.y<other.origin.y+other.height &&
        other.origin.y<c.origin.y+c.height;
      if (overlap || contains(c,other.entrance) || contains(other,c.entrance)) invalid();
    }
    // Reuse the capacity/mode and ID contract; this creates no world activity.
    regional::allocate(0,{},{{c,regional::Access::Disabled}});
    connections.push_back(c);
  }
  std::sort(connections.begin(),connections.end(),[](const auto& a,const auto& b) { return a.id<b.id; });
  return connections;
}
