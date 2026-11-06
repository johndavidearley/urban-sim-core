#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "src/world/Tile.hpp"

class CityMap;
class RoadNetwork;
class EntityStore;
struct ServiceFacility;

namespace uniques {
constexpr size_t count = 6;
struct Spec {
  uint32_t id;
  const char* key;
  const char* name;
  const char* requirement;
  uint64_t threshold;
  int64_t cost, upkeep;
  int radius;
  float landValue;
};
inline constexpr std::array<Spec, count> catalog{{
  {1, "treasury_hall", "TREASURY HALL", "FUNDS HELD", 500000, 300000, 12, 6, 20.f},
  {2, "transit_exchange", "TRANSIT EXCHANGE", "BUS RIDERS CARRIED", 50000, 450000, 24, 7, 30.f},
  {3, "trade_hall", "TRADE HALL", "GOODS EXPORTED", 1000000, 500000, 20, 6, 25.f},
  {4, "visitor_pavilion", "VISITOR PAVILION", "VISITORS ADMITTED", 10000, 400000, 24, 7, 30.f},
  {5, "academy", "CITY ACADEMY", "EDUCATED RESIDENTS", 2500, 650000, 22, 6, 30.f},
  {6, "civic_square", "CIVIC SQUARE", "RESIDENTS", 5000, 400000, 30, 8, 40.f}
}};
inline const Spec* spec(uint32_t id) {
  return id && id <= count ? &catalog[id - 1] : nullptr;
}
inline const Spec* spec(const std::string& key) {
  for (const auto& item : catalog) if (key == item.key) return &item;
  return nullptr;
}

// Peak funds/residents/education; cumulative delivered bus rides, goods and
// arrivals. Clamp to the requirement: bounded, permanent and overflow-safe.
struct Progress {
  std::array<uint64_t, count> values{};
  void peak(size_t index, uint64_t value) {
    values[index] = std::max(values[index], std::min(value, catalog[index].threshold));
  }
  void add(size_t index, uint64_t value) {
    values[index] += std::min(value, catalog[index].threshold - values[index]);
  }
  void observeCurrent(int64_t funds, uint32_t residents, uint32_t educated) {
    peak(0, static_cast<uint64_t>(std::max<int64_t>(0, funds)));
    peak(4, educated);
    peak(5, residents);
  }
  bool unlocked(uint32_t id) const {
    const auto* item = spec(id);
    return item && values[id - 1] >= item->threshold;
  }
};
bool built(const EntityStore& store, uint32_t kind);
int64_t upkeep(const EntityStore& store);
struct Effect { Coord position; int radius; float bonus; };
std::vector<Effect> activeEffects(const CityMap& map, const RoadNetwork& roads, const EntityStore& store);
float landValueBonus(const std::vector<Effect>& effects, Coord tile);
}

struct UniqueBuildingPlan {
  uint32_t kind = 0;
  Coord position{0,0};
  int64_t cost = 0;
  bool valid = false;
  std::string error;
};
class UniqueBuildingTool {
public:
  static UniqueBuildingPlan plan(const CityMap& map, const RoadNetwork& roads,
    const EntityStore& store, const std::vector<ServiceFacility>& facilities,
    const uniques::Progress& progress, uint32_t kind, Coord position, int64_t funds);
  static bool build(CityMap& map, const RoadNetwork& roads, EntityStore& store,
    const std::vector<ServiceFacility>& facilities, const uniques::Progress& progress,
    const UniqueBuildingPlan& proposed, int64_t& funds);
};
