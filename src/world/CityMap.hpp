#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "src/world/Tile.hpp"

class CityMap {
private:
  glm::ivec2 dimensions;
  std::vector<Tile> tiles;

  // Phase 3 of the Tile-storage backlog plan (docs/ROADMAP.md item 10):
  // pollution and land value split out of Tile into their own contiguous
  // arrays, parallel to `tiles` (same index, same size). These are the
  // fields CitySimulator's per-tick pollution field update and
  // LandValueSystem's recompute touch for every tile in the active region,
  // so a plain contiguous float array (rather than striding through the
  // whole Tile struct for one field) is the whole point of this move.
  std::vector<float> pollutions;
  std::vector<float> landValues;

  size_t getIndex(Coord coord) const;
  
public:
  CityMap(glm::ivec2 dims);
  
  Tile& getTile(Coord coord);
  const Tile& getTile(Coord coord) const;

  bool isValid(Coord coord) const;

  glm::ivec2 getDimensions() const { return dimensions; }

  size_t getTileCount() const { return tiles.size(); }

  // Accessors for the fields split into parallel arrays above (docs/
  // ROADMAP.md item 10), plus zone since callers so often touch it
  // alongside them (zone stays on Tile - only pollution/landValue moved).
  // These used to be pure wrappers over getTile() before Phase 3; now
  // they're the *only* way to read or write pollution/landValue at all -
  // Tile itself no longer has these fields.
  float& pollution(Coord coord);
  float pollution(Coord coord) const;
  float& landValue(Coord coord);
  float landValue(Coord coord) const;
  int zone(Coord coord) const;
  void setZone(Coord coord, int zoneValue);
};
