#pragma once
#include "src/world/CityMap.hpp"
#include <string>
struct PipePlan {
  glm::vec2 start{},end{};
  bool snapped=true,remove=false;
  int kind=3; // 1 water, 2 sewer, 3 combined trench, 4 inline sewer lift, 5 water booster.
  std::vector<Coord> tiles;
  int changed=0;
  int64_t cost=0;
  bool valid=false;
  std::string error;
};
class PipeTool {
public:
  static constexpr int64_t waterCost=40,sewerCost=30,removalCost=10;
  static constexpr int64_t liftCost=2500,liftRemovalCost=100;
  static constexpr int64_t boosterCost=3000,boosterRemovalCost=100;
  static PipePlan plan(const CityMap& map,glm::vec2 start,glm::vec2 end,int kind,
    bool snapped,bool remove,int64_t funds);
  static bool apply(CityMap& map,const PipePlan& plan,int64_t& funds);
};
