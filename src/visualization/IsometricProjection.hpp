#pragma once

#include "src/world/Tile.hpp"

struct ScreenPoint {
  int x = 0;
  int y = 0;
};

class IsometricProjection {
public:
  IsometricProjection(int tileWidth, int tileHeight, ScreenPoint origin = {});

  ScreenPoint tileTop(Coord tile) const;
  ScreenPoint tileCenter(Coord tile) const;
  Coord screenToTile(ScreenPoint screen) const;

  int tileWidth() const { return tileWidth_; }
  int tileHeight() const { return tileHeight_; }
  ScreenPoint origin() const { return origin_; }

private:
  int tileWidth_ = 2;
  int tileHeight_ = 2;
  ScreenPoint origin_{};
};
