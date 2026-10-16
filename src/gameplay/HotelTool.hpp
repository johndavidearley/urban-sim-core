#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "src/world/Tile.hpp"

class CityMap;
class RoadNetwork;
class EntityStore;
struct ServiceFacility;

struct HotelPlan {
  Coord position{0,0};
  int64_t cost = 0;
  bool valid = false;
  std::string error;
};

class HotelTool {
public:
  static constexpr uint32_t kBeds = 32;
  static constexpr int kJobs = 12;
  static constexpr int64_t kConstructionCost = 10000;
  static bool setOpen(EntityStore& store, uint32_t id, bool open);
  // A hotel is one unzoned commercial entity. Existing commercial maintenance,
  // taxes and employment apply once; beds never become worker occupancy.
  static HotelPlan plan(const CityMap& map, const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities, Coord position, int64_t funds);
  static bool build(CityMap& map, const RoadNetwork& roads, EntityStore& store,
    const std::vector<ServiceFacility>& facilities, const HotelPlan& plan, int64_t& funds);
};
