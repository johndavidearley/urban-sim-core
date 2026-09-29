#pragma once

#include <string>
#include <vector>

#include "src/systems/DistrictSystem.hpp"
#include "src/world/CityMap.hpp"

struct DistrictPlan {
  Coord minCorner{0, 0};
  Coord maxCorner{0, 0};
  DistrictArchetype archetype = DistrictArchetype::General;
  std::vector<Coord> tiles;
  DistrictId removeId = 0;  // 0 = create; otherwise delete this district
  bool valid = false;
  std::string error;
};

class DistrictTool {
public:
  static DistrictPlan plan(
    const CityMap& map,
    const DistrictSystem& districts,
    Coord start,
    Coord end,
    DistrictArchetype archetype
  );

  static bool apply(DistrictSystem& districts, const CityMap& map, const DistrictPlan& plan);

  // Last district containing `coord` (top-most if they overlap), or 0.
  static DistrictId districtAt(const DistrictSystem& districts, Coord coord);
};
