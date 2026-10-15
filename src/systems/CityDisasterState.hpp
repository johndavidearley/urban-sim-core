#pragma once
#include <utility>
#include "src/systems/FireSystem.hpp"
#include "src/systems/NaturalDisasterSystem.hpp"

// Persistent destructive-system state. Loss counters survive disabling events.
struct CityDisasterState {
  std::vector<BurningTile> burningTiles;
  FireSummary fire;
  DisasterSummary disaster;
  uint32_t buildingsLostToFire = 0;
  uint32_t buildingsLostToDisaster = 0;

  void stopEvents() {
    burningTiles.clear();
    fire = {};
    disaster = {};
  }
};

// A burning lot cannot redevelop until its blaze has expired.
class BurningGrowthGuard {
 public:
  BurningGrowthGuard(CityMap& map, const std::vector<BurningTile>& burning)
      : map_(map) {
    for (const auto& tile : burning) {
      if (!map.isValid(tile.position)) continue;
      zones_.push_back({tile.position, map.getTile(tile.position).zone});
      map.getTile(tile.position).zone = 0;
    }
  }
  ~BurningGrowthGuard() {
    for (const auto& zone : zones_) map_.getTile(zone.first).zone = zone.second;
  }
  BurningGrowthGuard(const BurningGrowthGuard&) = delete;
  BurningGrowthGuard& operator=(const BurningGrowthGuard&) = delete;
 private:
  CityMap& map_;
  std::vector<std::pair<Coord, int>> zones_;
};
