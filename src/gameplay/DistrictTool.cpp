#include "src/gameplay/DistrictTool.hpp"

#include <algorithm>

namespace {
std::string defaultDistrictName(const DistrictSystem& districts, DistrictArchetype archetype) {
  int count = 0;
  for (const District& district : districts.getDistricts()) {
    if (district.archetype == archetype) {
      ++count;
    }
  }
  return std::string(DistrictSystem::archetypeToString(archetype)) + " " + std::to_string(count + 1);
}

bool sameBounds(const District& district, Coord minCorner, Coord maxCorner) {
  return district.minCorner.x == minCorner.x && district.minCorner.y == minCorner.y
      && district.maxCorner.x == maxCorner.x && district.maxCorner.y == maxCorner.y;
}

void fillPlanTiles(DistrictPlan& result) {
  for (int y = result.minCorner.y; y <= result.maxCorner.y; ++y) {
    for (int x = result.minCorner.x; x <= result.maxCorner.x; ++x) {
      result.tiles.push_back({x, y});
    }
  }
}
}  // namespace

DistrictPlan DistrictTool::plan(
  const CityMap& map,
  const DistrictSystem& districts,
  Coord start,
  Coord end,
  DistrictArchetype archetype
) {
  DistrictPlan result;
  result.archetype = archetype;
  result.minCorner = {std::min(start.x, end.x), std::min(start.y, end.y)};
  result.maxCorner = {std::max(start.x, end.x), std::max(start.y, end.y)};
  if (!map.isValid(result.minCorner) || !map.isValid(result.maxCorner)) {
    result.error = "area is outside the map";
    return result;
  }

  for (const District& district : districts.getDistricts()) {
    if (sameBounds(district, result.minCorner, result.maxCorner)) {
      result.removeId = district.id;
      result.archetype = district.archetype;
      fillPlanTiles(result);
      result.valid = true;
      return result;
    }
  }

  fillPlanTiles(result);
  result.valid = true;
  return result;
}

bool DistrictTool::apply(DistrictSystem& districts, const CityMap& map, const DistrictPlan& plan) {
  if (!plan.valid || plan.tiles.empty()) {
    return false;
  }

  const DistrictPlan current = DistrictTool::plan(
    map, districts, plan.minCorner, plan.maxCorner, plan.archetype
  );
  if (!current.valid || current.tiles != plan.tiles || current.removeId != plan.removeId) {
    return false;
  }
  if (plan.removeId != 0) {
    return districts.deleteDistrict(plan.removeId);
  }

  const DistrictId id = districts.createDistrict(
    defaultDistrictName(districts, plan.archetype), plan.minCorner, plan.maxCorner
  );
  if (id == 0) {
    return false;
  }
  return districts.setDistrictArchetype(id, plan.archetype);
}

DistrictId DistrictTool::districtAt(const DistrictSystem& districts, Coord coord) {
  DistrictId id = 0;
  for (const District& district : districts.getDistricts()) {
    if (district.contains(coord)) {
      id = district.id;
    }
  }
  return id;
}
