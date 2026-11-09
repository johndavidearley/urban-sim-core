#pragma once
#include "src/visualization/IsometricProjection.hpp"
#include <algorithm>
#include <cmath>

namespace visualizer {
struct WorldPoint {
  float x, y, z; // y is height; x/z follow map coordinates.
  bool valid = true;
};
struct WorldRay { WorldPoint origin,direction; };
struct ProjectedPoint {
  float x, y, depth;
  float clipW = 1.f;
};

// Both projections use the same orbit basis and ground plane. In perspective,
// scale describes pixels per world unit at the center of the view.
class Camera3D {
public:
  float yaw = 45.f, pitch = 35.f, scale = 20.f;
  float originX = 0.f, originY = 0.f;
  float depthRange = 512.f;
  bool perspective = false;
  float centerX = 640.f, centerY = 400.f;
  float focalLength = 800.f;
  static constexpr float nearPlane = .2f;

  float farPlane() const { return std::max(nearPlane + 1.f, depthRange * 2.f); }
  float viewDepth(WorldPoint p) const {
    const auto target = flatGround(centerX, centerY);
    const float a = yaw*radians, b = pitch*radians;
    return focalLength/scale - std::cos(b)*(std::sin(a)*(p.x-target.x) +
           std::cos(a)*(p.z-target.z)) - std::sin(b)*p.y;
  }
  ProjectedPoint project(WorldPoint p) const {
    if (!perspective) return flatProject(p);
    const auto target = flatGround(centerX, centerY);
    const float a = yaw*radians, b = pitch*radians;
    const float dx = p.x-target.x, dz = p.z-target.z;
    const float right = std::cos(a)*dx - std::sin(a)*dz;
    const float forward = std::sin(a)*dx + std::cos(a)*dz;
    float w = focalLength/scale - std::cos(b)*forward - std::sin(b)*p.y;
    // Preserve signed homogeneous W so Vulkan clips triangles crossing the eye
    // and near planes. Guard only the exact eye plane against division by zero.
    if (std::abs(w) < .00001f) w = std::copysign(.00001f, w);
    const float far = farPlane();
    return {centerX + focalLength*right/w,
            centerY + focalLength*(std::sin(b)*forward-std::cos(b)*p.y)/w,
            far/(far-nearPlane) - far*nearPlane/((far-nearPlane)*w), w};
  }
  WorldRay ray(float x,float y) const {
    const float a=yaw*radians,b=pitch*radians;
    const WorldPoint normal{std::cos(b)*std::sin(a),std::sin(b),std::cos(b)*std::cos(a)};
    if(perspective) {
      const auto target=flatGround(centerX,centerY);
      const float distance=focalLength/scale,sx=(x-centerX)/focalLength,sy=(y-centerY)/focalLength;
      return {{target.x+normal.x*distance,normal.y*distance,target.z+normal.z*distance},
              {-normal.x+std::cos(a)*sx+std::sin(b)*std::sin(a)*sy,
               -normal.y-std::cos(b)*sy,
               -normal.z-std::sin(a)*sx+std::sin(b)*std::cos(a)*sy}};
    }
    const auto point=flatGround(x,y);
    const float offset=depthRange*.5f-(normal.x*point.x+normal.z*point.z);
    return {{point.x+normal.x*offset,normal.y*offset,point.z+normal.z*offset},
            {-normal.x,-normal.y,-normal.z}};
  }
  WorldPoint ground(float x, float y) const {
    if (!perspective) return flatGround(x, y);
    const float a = yaw*radians, b = pitch*radians;
    const float sx = (x-centerX)/focalLength, sy = (y-centerY)/focalLength;
    const float rayX = -std::cos(b)*std::sin(a) + std::cos(a)*sx + std::sin(b)*std::sin(a)*sy;
    const float rayY = -std::sin(b) - std::cos(b)*sy;
    const float rayZ = -std::cos(b)*std::cos(a) - std::sin(a)*sx + std::sin(b)*std::cos(a)*sy;
    if (rayY >= -.00001f) return {0,0,0,false}; // Sky or horizon.
    const float distance = focalLength/scale;
    const float t = -distance*std::sin(b)/rayY;
    if (t < nearPlane || t > farPlane()) return {0,0,0,false};
    const auto target = flatGround(centerX, centerY);
    return {target.x + distance*std::cos(b)*std::sin(a) + t*rayX, 0.f,
            target.z + distance*std::cos(b)*std::cos(a) + t*rayZ};
  }
  Coord screenToTile(ScreenPoint p) const {
    const auto w = ground(float(p.x), float(p.y));
    if (!w.valid || !std::isfinite(w.x) || !std::isfinite(w.z) ||
        std::abs(w.x)>1000000.f || std::abs(w.z)>1000000.f) return {-1,-1};
    return {int(std::floor(w.x)), int(std::floor(w.z))};
  }
  ScreenPoint worldToScreen(WorldPoint point) const {
    const auto p = project(point);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x)>100000000.f || std::abs(p.y)>100000000.f ||
        (perspective && (p.clipW<nearPlane || p.clipW>farPlane()))) return {-1000000,-1000000};
    return {int(std::lround(p.x)), int(std::lround(p.y))};
  }
  ScreenPoint tileCenter(Coord c) const { return worldToScreen({c.x+.5f,0.f,c.y+.5f}); }
  void orbit(float turn, float tilt) {
    yaw = std::fmod(yaw+turn,360.f);
    if (yaw < 0.f) yaw += 360.f;
    pitch = std::clamp(pitch+tilt,20.f,80.f);
  }
  void centerOn(WorldPoint anchor, float x, float y) {
    const auto current = ground(x,y);
    if (!anchor.valid || !current.valid) return;
    const auto delta = flatProject({anchor.x-current.x,0.f,anchor.z-current.z});
    originX -= delta.x-originX;
    originY -= delta.y-originY;
  }
  void orbitAt(float x, float y, float turn, float tilt) {
    auto anchor = ground(x,y);
    if (!anchor.valid) { x=centerX; y=centerY; anchor=ground(x,y); }
    orbit(turn,tilt);
    centerOn(anchor,x,y);
  }
  void zoomAt(float x, float y, float newScale) {
    auto anchor = ground(x,y);
    if (!anchor.valid) { x=centerX; y=centerY; anchor=ground(x,y); }
    scale = std::max(.1f,newScale);
    centerOn(anchor,x,y);
  }
private:
  static constexpr float radians = .017453292519943295f;
  ProjectedPoint flatProject(WorldPoint p) const {
    const float a = yaw*radians, b = pitch*radians;
    const float right = std::cos(a)*p.x - std::sin(a)*p.z;
    const float forward = std::sin(a)*p.x + std::cos(a)*p.z;
    return {originX + scale*right,
            originY + scale*(std::sin(b)*forward-std::cos(b)*p.y),
            .5f-(std::cos(b)*forward+std::sin(b)*p.y)/depthRange};
  }
  WorldPoint flatGround(float x, float y) const {
    const float a = yaw*radians, b = pitch*radians;
    const float right = (x-originX)/scale;
    const float forward = (y-originY)/(scale*std::sin(b));
    return {std::cos(a)*right+std::sin(a)*forward, 0.f,
            -std::sin(a)*right+std::cos(a)*forward};
  }
};
} // namespace visualizer
