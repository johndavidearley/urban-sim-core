#pragma once
#include "src/gameplay/CatalogPrices.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "src/networks/RoadNetwork.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/Tile.hpp"

class CityMap;
class RoadNetwork;

struct RoadPlan {
  std::vector<Coord> tiles;
  RoadAlignment alignment;
  bool hasAlignment = false;
  int newSegments = 0;
  int upgradedSegments = 0;
  RoadClass roadClass = RoadClass::Local;
  int64_t cost = 0;
  bool valid = false;
  std::string error;
};

class RoadTool {
public:
  static constexpr int64_t kCostPerSegment = 100;
  static constexpr int64_t kArterialCostPerSegment = catalog::arterialSegmentCost;
  static constexpr int64_t kUpgradeCostPerSegment = catalog::arterialUpgradeCost;

  static RoadPlan plan(const CityMap &map, const RoadNetwork &roads,
                       Coord start, Coord end, int64_t availableFunds,
                       RoadClass roadClass = RoadClass::Local,
                       const std::vector<ServiceFacility>* facilities = nullptr);

  static RoadAlignment straight(glm::vec2 start,glm::vec2 end,RoadClass roadClass,bool snapped);
  static RoadAlignment curve(glm::vec2 start,glm::vec2 control,glm::vec2 end,RoadClass roadClass,bool snapped);
  static RoadAlignment roundabout(glm::vec2 center,float radius,RoadClass roadClass,bool snapped);
  static RoadPlan planAlignment(const CityMap& map,const RoadNetwork& roads,
    const RoadAlignment& alignment,int64_t availableFunds,
    const std::vector<ServiceFacility>* facilities=nullptr,bool requireConnections=true);
  static int64_t segmentPrice(RoadClass roadClass);

  static bool build(CityMap& map, RoadNetwork& roads, const RoadPlan& plan, int64_t& availableFunds,
                    const std::vector<ServiceFacility>* facilities = nullptr);
};
