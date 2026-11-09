#pragma once
#include "src/visualization/Camera3D.hpp"
#include <array>
#include <vector>

namespace visualizer {
// CPU picking uses the same world triangles emitted for rendering. Consecutive
// parcel groups provide a cheap bounding-box pass before exact intersections.
class ScenePicking3D {
public:
  void clear();
  void addTriangle(const std::array<WorldPoint,3>& points,Coord tile);
  Coord pick(const Camera3D& camera,ScreenPoint screen) const;
  size_t triangleCount() const { return triangles_.size(); }
private:
  struct Group {
    Coord tile;
    size_t begin=0,end=0;
    WorldPoint minimum{},maximum{};
  };
  std::vector<std::array<WorldPoint,3>> triangles_;
  std::vector<Group> groups_;
};
}
