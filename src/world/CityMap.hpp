#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "src/world/Tile.hpp"

class CityMap {
private:
  glm::ivec2 dimensions;
  std::vector<Tile> tiles;
  
  size_t getIndex(Coord coord) const;
  
public:
  CityMap(glm::ivec2 dims);
  
  Tile& getTile(Coord coord);
  const Tile& getTile(Coord coord) const;

  bool isValid(Coord coord) const;

  glm::ivec2 getDimensions() const { return dimensions; }

  size_t getTileCount() const { return tiles.size(); }

  // Phase 1 of the Tile-storage backlog plan (docs/ROADMAP.md, "Next
  // Targets" item 10): accessors for the fields that plan intends to
  // eventually pull into parallel arrays (pollution, land value), plus
  // zone since callers so often touch it alongside them. Pure wrappers
  // over getTile() right now - zero behavior or performance change - so
  // callers can be migrated to these one file at a time (Phase 2) ahead
  // of any actual storage change (Phase 3), instead of in one large,
  // hard-to-verify step.
  float& pollution(Coord coord);
  float pollution(Coord coord) const;
  float& landValue(Coord coord);
  float landValue(Coord coord) const;
  int zone(Coord coord) const;
  void setZone(Coord coord, int zoneValue);
};
