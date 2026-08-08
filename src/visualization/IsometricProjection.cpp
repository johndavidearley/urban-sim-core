#include "src/visualization/IsometricProjection.hpp"

#include <algorithm>
#include <cmath>

IsometricProjection::IsometricProjection(int tileWidth, int tileHeight, ScreenPoint origin)
  : tileWidth_(std::max(2, tileWidth)),
    tileHeight_(std::max(2, tileHeight)),
    origin_(origin) {}

ScreenPoint IsometricProjection::tileTop(Coord tile) const {
  const int halfWidth = tileWidth_ / 2;
  const int halfHeight = tileHeight_ / 2;
  return {
    origin_.x + (tile.x - tile.y) * halfWidth,
    origin_.y + (tile.x + tile.y) * halfHeight,
  };
}

ScreenPoint IsometricProjection::tileCenter(Coord tile) const {
  ScreenPoint point = tileTop(tile);
  point.y += tileHeight_ / 2;
  return point;
}

Coord IsometricProjection::screenToTile(ScreenPoint screen) const {
  const double halfWidth = static_cast<double>(tileWidth_) / 2.0;
  const double halfHeight = static_cast<double>(tileHeight_) / 2.0;
  const double projectedX = static_cast<double>(screen.x - origin_.x) / halfWidth;
  const double projectedY = static_cast<double>(screen.y - origin_.y) / halfHeight;

  // Subtract one projected y unit because tileTop() names the upper diamond
  // vertex while selection is based on the diamond's center.
  const double centeredY = projectedY - 1.0;
  return {
    static_cast<int>(std::floor((projectedX + centeredY) / 2.0 + 0.5)),
    static_cast<int>(std::floor((centeredY - projectedX) / 2.0 + 0.5)),
  };
}
