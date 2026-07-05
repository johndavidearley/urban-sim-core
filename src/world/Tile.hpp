#pragma once

#include <cstdint>
#include <glm/glm.hpp>

using Coord = glm::ivec2;
using Rect = glm::ivec4; // {x1, y1, x2, y2}

// landValue and pollution live in CityMap-owned parallel arrays (see
// CityMap::pollution/landValue), not here - Phase 3 of the Tile-storage
// backlog plan (docs/ROADMAP.md item 10). They're CityMap's two hottest
// per-tick numeric fields; splitting them out lets those loops iterate
// contiguous float arrays instead of striding through this whole struct.
// Always go through CityMap's accessors for them, never a bare Tile.
struct Tile {
  Coord position;
  int type;                  // 0=empty, 1=terrain, 2=water
  int zone;                  // 0=none, 1=residential, 2=commercial, 3=industrial
  bool hasRoad = false;
  bool connectedToRoad = false;
  bool connectedToPower = true;   // Stub: true for MVP
  bool connectedToWater = true;   // Stub: true for MVP
  uint32_t buildingId = 0;        // 0 = no building
};
