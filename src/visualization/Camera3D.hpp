#pragma once
#include "src/visualization/IsometricProjection.hpp"
#include <algorithm>
#include <cmath>

namespace visualizer {
struct WorldPoint { float x, y, z; }; // y is height; x/z follow map coordinates.
struct ProjectedPoint { float x, y, depth; };

// Orthographic orbit camera. World geometry and ground picking share one basis.
class Camera3D {
public:
  float yaw = 45.f, pitch = 35.f, scale = 20.f;
  float originX = 0.f, originY = 0.f;
  float depthRange = 512.f;
  ProjectedPoint project(WorldPoint p) const {
    const float a = yaw * radians, b = pitch * radians;
    const float right = std::cos(a)*p.x - std::sin(a)*p.z;
    const float forward = std::sin(a)*p.x + std::cos(a)*p.z;
    return {originX + scale*right,
            originY + scale*(std::sin(b)*forward - std::cos(b)*p.y),
            .5f - (std::cos(b)*forward + std::sin(b)*p.y)/depthRange};
  }
  WorldPoint ground(float x, float y) const {
    const float a = yaw*radians, b = pitch*radians;
    const float right = (x-originX)/scale;
    const float forward = (y-originY)/(scale*std::sin(b));
    return {std::cos(a)*right + std::sin(a)*forward, 0.f,
            -std::sin(a)*right + std::cos(a)*forward};
  }
  Coord screenToTile(ScreenPoint p) const {
    const auto w = ground(static_cast<float>(p.x),static_cast<float>(p.y));
    return {static_cast<int>(std::floor(w.x)),static_cast<int>(std::floor(w.z))};
  }
  ScreenPoint tileCenter(Coord c) const {
    const auto p = project({c.x+.5f,0.f,c.y+.5f});
    return {static_cast<int>(std::lround(p.x)),static_cast<int>(std::lround(p.y))};
  }
  void orbit(float turn, float tilt) {
    yaw = std::fmod(yaw+turn+360.f,360.f);
    pitch = std::clamp(pitch+tilt,20.f,80.f);
  }
private:
  static constexpr float radians = .017453292519943295f;
};
} // namespace visualizer
