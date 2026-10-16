#pragma once
#include "src/gameplay/RegionalConnectionTool.hpp"

namespace regional {
struct TerminalSpec {
  int width,height;
  int64_t constructionCost,demolitionCost,upkeep;
  uint32_t passengers,goods;
};
const TerminalSpec* terminalSpec(Kind kind);
}
struct TerminalPlan {
  regional::Connection connection;
  Coord destination{-1,-1};
  std::vector<Coord> tiles;
  int64_t cost=0;
  bool valid=false;
  std::string error;
};
class TerminalTool {
public:

  // Deterministic road entrance and a reachable civic road destination. Read-only;
  // build revalidates the returned connection and destination at execution.
  static TerminalPlan plan(const CityMap& map,const RoadNetwork& roads,
    const std::vector<regional::Connection>& connections,Coord origin,regional::Kind kind,
    uint64_t nextId,const std::vector<Coord>& civicSites,int64_t funds);
  // Fixed footprint and initial policy from terminalSpec; revalidates placement.
  static bool build(CityMap& map,const RoadNetwork& roads,
    std::vector<regional::Connection>& connections,regional::Connection connection,
    Coord destination,const std::vector<Coord>& civicSites,int64_t& funds,std::string* error=nullptr);
  static bool remove(CityMap& map,std::vector<regional::Connection>& connections,
    uint32_t id,regional::Kind expectedKind,int64_t& funds,std::string* error=nullptr);
};
