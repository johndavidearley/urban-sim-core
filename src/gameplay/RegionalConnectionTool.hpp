#pragma once
#include <string>
#include "src/systems/RegionalConnectionSystem.hpp"

// Foundation API for terminal tools in M32/M33. The registry owns footprint
// reservations; this does not create buildings or mutate terrain/road tiles.
class RegionalConnectionTool {
public:
  static bool canPlace(const regional::Connection& connection,const CityMap& map,
    const RoadNetwork& roads,Coord destination,const std::vector<Coord>& civicSites,
    int64_t constructionCost,const std::vector<regional::Connection>& registry,
    int64_t funds,std::string* error=nullptr);
  // Rebuilds water reachability at execution, not from a potentially stale plan.
  static bool place(const regional::Connection& connection, const CityMap& map,
    const RoadNetwork& roads, Coord destination,
    const std::vector<Coord>& civicSites,
    int64_t constructionCost, std::vector<regional::Connection>& registry,
    int64_t& funds, std::string* error = nullptr);
  // Removal needs no functioning access. No implicit construction refund.
  static bool remove(uint32_t id, int64_t demolitionCost,
    std::vector<regional::Connection>& registry, int64_t& funds,
    std::string* error = nullptr);
};
