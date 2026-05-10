#pragma once

#include <string>

#include "src/entities/EntityStore.hpp"
#include "src/world/CityMap.hpp"

struct RenderOptions {
  int tilePixels = 8;
  int viewX = 0;
  int viewY = 0;
  int viewWidth = -1;   // -1 means full map
  int viewHeight = -1;  // -1 means full map
};

class MapRenderer {
public:
  // Export a top-down snapshot as a binary PPM (P6) image.
  static bool renderToPPM(
    const std::string& filePath,
    const CityMap& map,
    const EntityStore& store,
    const RenderOptions& options = RenderOptions{}
  );
};
