#pragma once

#include <glm/glm.hpp>

using Coord = glm::ivec2;
using Rect = glm::ivec4; // {x1, y1, x2, y2}

struct Tile {
  Coord position;
  int type;                  // 0=empty, 1=terrain, 2=water
  int zone;                  // 0=none, 1=residential, 2=commercial, 3=industrial
  float landValue = 100.0f;
  float pollution = 0.0f;
  bool hasRoad = false;
  bool connectedToRoad = false;
  bool connectedToPower = true;   // Stub: true for MVP
  bool connectedToWater = true;   // Stub: true for MVP
  uint32_t buildingId = 0;        // 0 = no building
};
