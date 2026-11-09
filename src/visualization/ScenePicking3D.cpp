#include "src/visualization/ScenePicking3D.hpp"
#include <limits>

namespace visualizer {
namespace {
WorldPoint difference(WorldPoint a,WorldPoint b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
WorldPoint cross(WorldPoint a,WorldPoint b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float dot(WorldPoint a,WorldPoint b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
bool intersectsBox(const WorldRay& ray,WorldPoint minimum,WorldPoint maximum,float nearest) {
  const float origin[]={ray.origin.x,ray.origin.y,ray.origin.z};
  const float direction[]={ray.direction.x,ray.direction.y,ray.direction.z};
  const float low[]={minimum.x,minimum.y,minimum.z},high[]={maximum.x,maximum.y,maximum.z};
  float entry=0,exit=nearest;
  for(int axis=0;axis<3;++axis) {
    if(std::abs(direction[axis])<1e-8f) {
      if(origin[axis]<low[axis] || origin[axis]>high[axis]) return false;
    } else {
      float a=(low[axis]-origin[axis])/direction[axis],b=(high[axis]-origin[axis])/direction[axis];
      if(a>b) std::swap(a,b);
      entry=std::max(entry,a);exit=std::min(exit,b);
      if(exit<entry) return false;
    }
  }
  return true;
}
bool intersectTriangle(const WorldRay& ray,const std::array<WorldPoint,3>& triangle,float& distance) {
  const auto edge1=difference(triangle[1],triangle[0]),edge2=difference(triangle[2],triangle[0]);
  const auto p=cross(ray.direction,edge2);
  const float determinant=dot(edge1,p);
  if(std::abs(determinant)<1e-8f) return false;
  const float inverse=1.f/determinant;
  const auto offset=difference(ray.origin,triangle[0]);
  const float u=dot(offset,p)*inverse;
  if(u<0 || u>1) return false;
  const auto q=cross(offset,edge1);
  const float v=dot(ray.direction,q)*inverse;
  if(v<0 || u+v>1) return false;
  distance=dot(edge2,q)*inverse;
  return distance>=0;
}
}
void ScenePicking3D::clear() {triangles_.clear();groups_.clear();}
void ScenePicking3D::addTriangle(const std::array<WorldPoint,3>& points,Coord tile) {
  if(groups_.empty() || groups_.back().tile.x!=tile.x || groups_.back().tile.y!=tile.y)
    groups_.push_back({tile,triangles_.size(),triangles_.size(),points[0],points[0]});
  auto& group=groups_.back();
  for(const auto p:points) {
    group.minimum.x=std::min(group.minimum.x,p.x);group.maximum.x=std::max(group.maximum.x,p.x);
    group.minimum.y=std::min(group.minimum.y,p.y);group.maximum.y=std::max(group.maximum.y,p.y);
    group.minimum.z=std::min(group.minimum.z,p.z);group.maximum.z=std::max(group.maximum.z,p.z);
  }
  triangles_.push_back(points);group.end=triangles_.size();
}
Coord ScenePicking3D::pick(const Camera3D& camera,ScreenPoint screen) const {
  const auto ray=camera.ray(float(screen.x),float(screen.y));
  float nearest=std::numeric_limits<float>::max();
  Coord result{-1,-1};
  for(const auto& group:groups_) {
    if(!intersectsBox(ray,group.minimum,group.maximum,nearest)) continue;
    for(size_t i=group.begin;i<group.end;++i) {
      float distance=0;
      if(!intersectTriangle(ray,triangles_[i],distance) || distance>nearest) continue;
      const WorldPoint hit{ray.origin.x+distance*ray.direction.x,ray.origin.y+distance*ray.direction.y,
                           ray.origin.z+distance*ray.direction.z};
      if(camera.perspective) {
        const float depth=camera.viewDepth(hit);
        if(depth<Camera3D::nearPlane || depth>camera.farPlane()) continue;
      } else {
        const float depth=camera.project(hit).depth;
        if(depth<0 || depth>1) continue;
      }
      nearest=distance;result=group.tile;
    }
  }
  return result;
}
}
