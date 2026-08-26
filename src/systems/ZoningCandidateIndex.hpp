#pragma once

#include <cstddef>
#include <unordered_set>
#include <vector>

#include "src/networks/RoadNetwork.hpp"
#include "src/world/Tile.hpp"

// Membership set + dense list so zoning can dedupe inserts in O(1) while the
// tick loop partial_sorts the list in place (no full set→vector copy).
struct ZoningCandidateIndex {
  std::unordered_set<Coord, Vec2Hash> membership;
  std::vector<Coord> list;

  void add(Coord c) {
    if (membership.insert(c).second) {
      list.push_back(c);
    }
  }

  void erase(Coord c) {
    membership.erase(c);
  }

  // Drop list entries no longer in membership (after a zoning batch).
  void compact() {
    size_t write = 0;
    for (size_t read = 0; read < list.size(); ++read) {
      if (membership.find(list[read]) != membership.end()) {
        list[write++] = list[read];
      }
    }
    list.resize(write);
  }

  bool empty() const { return list.empty(); }
  size_t size() const { return list.size(); }
};
