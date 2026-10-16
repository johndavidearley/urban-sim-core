#pragma once
#include "src/gameplay/RegionalConnectionTool.hpp"

struct PortPlan {
  regional::Connection connection;
  Coord destination{-1,-1};
  std::vector<Coord> tiles;
  int64_t cost=0;
  bool valid=false;
  std::string error;
};
class PortTool {
public:
  static constexpr int64_t kCargoCost=15000, kPassengerCost=10000, kDemolitionCost=500;
  // Deterministic road entrance and a reachable civic road destination. Read-only;
  // build revalidates the returned connection and destination at execution.
  static PortPlan plan(const CityMap& map,const RoadNetwork& roads,
    const std::vector<regional::Connection>& connections,Coord origin,regional::Kind kind,
    uint64_t nextId,const std::vector<Coord>& civicSites,int64_t funds);
  // Fixed 2x1 land footprint; entrance is an adjacent city road tile.
  static bool build(CityMap& map,const RoadNetwork& roads,
    std::vector<regional::Connection>& connections,regional::Connection connection,
    Coord destination,const std::vector<Coord>& civicSites,int64_t& funds,std::string* error=nullptr);
  static bool remove(CityMap& map,std::vector<regional::Connection>& connections,
    uint32_t id,int64_t& funds,std::string* error=nullptr);
};
