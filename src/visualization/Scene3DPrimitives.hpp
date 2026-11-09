#pragma once
#include "src/visualization/Camera3D.hpp"
#include "src/visualization/VisualizerTypes.hpp"
#include <array>
#include "src/visualization/ScenePicking3D.hpp"

// Internal world-space primitives shared by city and architectural models.
namespace visualizer::scene3d {
class PickParcel {
public:
  PickParcel(DrawList* list,Coord tile):list_(list),x_(list->pickTileX),y_(list->pickTileY) {
    list_->pickTileX=tile.x;list_->pickTileY=tile.y;
  }
  ~PickParcel() {list_->pickTileX=x_;list_->pickTileY=y_;}
private:
  DrawList* list_;int x_,y_;
};
class PickCollector {
public:
  PickCollector(DrawList* list,ScenePicking3D* picking):list_(list),previous_(list->scenePicking) {
    list_->scenePicking=picking;if(picking) picking->clear();
  }
  ~PickCollector() {list_->scenePicking=previous_;}
private:
  DrawList* list_;ScenePicking3D* previous_;
};
inline RGB shade(RGB c,float amount) {
  return {static_cast<uint8_t>(c.r*amount),static_cast<uint8_t>(c.g*amount),static_cast<uint8_t>(c.b*amount)};
}
inline void emitTriangle(DrawList* list,const ProjectedPoint& a,const ProjectedPoint& b,
                         const ProjectedPoint& c,bool highlight) {
  const auto start=list->vertices.size();
  list->triangle(a.x,a.y,b.x,b.y,c.x,c.y);
  const ProjectedPoint points[]={a,b,c};
  for(size_t i=0;i<3;++i) {
    list->vertices[start+i].z=highlight ? 0.f : points[i].depth;
    list->vertices[start+i].clipW=points[i].clipW;
  }
}
template<size_t N>
void face(DrawList* list,const Camera3D& camera,const std::array<WorldPoint,N>& points,
          RGB color,uint8_t alpha=255,bool highlight=false) {
  if(camera.perspective && std::all_of(points.begin(),points.end(),[&](WorldPoint p) {
       return camera.viewDepth(p)<Camera3D::nearPlane;
     })) return;
  if(list->scenePicking && !highlight && alpha) {
    for(size_t i=2;i<N;++i)
      list->scenePicking->addTriangle({points[0],points[i-1],points[i]},{list->pickTileX,list->pickTileY});
  }
  setDrawColor(list,color.r,color.g,color.b,alpha);
  if(highlight && camera.perspective) {
    // Overlays bypass depth, so clip them before assigning UI depth.
    std::vector<WorldPoint> clipped;
    clipped.reserve(N+1);
    WorldPoint previous=points.back();
    float previousDepth=camera.viewDepth(previous);
    for(const auto current:points) {
      const float depth=camera.viewDepth(current);
      if((depth>=Camera3D::nearPlane)!=(previousDepth>=Camera3D::nearPlane)) {
        const float t=(Camera3D::nearPlane-previousDepth)/(depth-previousDepth);
        clipped.push_back({previous.x+t*(current.x-previous.x),previous.y+t*(current.y-previous.y),
                           previous.z+t*(current.z-previous.z)});
      }
      if(depth>=Camera3D::nearPlane) clipped.push_back(current);
      previous=current;previousDepth=depth;
    }
    for(size_t i=2;i<clipped.size();++i)
      emitTriangle(list,camera.project(clipped[0]),camera.project(clipped[i-1]),camera.project(clipped[i]),true);
    return;
  }
  std::array<ProjectedPoint,N> projected;
  for(size_t i=0;i<N;++i) projected[i]=camera.project(points[i]);
  for(size_t i=2;i<N;++i) emitTriangle(list,projected[0],projected[i-1],projected[i],highlight);
}
inline void quad(DrawList* l,const Camera3D& c,std::array<WorldPoint,4> p,RGB color,
                 uint8_t alpha=255,bool highlight=false) { face(l,c,p,color,alpha,highlight); }
inline void triangle(DrawList* l,const Camera3D& c,std::array<WorldPoint,3> p,RGB color) { face(l,c,p,color); }
inline void slab(DrawList* l,const Camera3D& c,float x,float z,float w,float d,float y,RGB color) {
  quad(l,c,{{{x,y,z},{x+w,y,z},{x+w,y,z+d},{x,y,z+d}}},color);
}
inline void box(DrawList* l,const Camera3D& c,float x,float z,float w,float d,float base,float h,RGB color) {
  const float y=base+h;
  quad(l,c,{{{x,base,z},{x+w,base,z},{x+w,y,z},{x,y,z}}},shade(color,.65f));
  quad(l,c,{{{x+w,base,z},{x+w,base,z+d},{x+w,y,z+d},{x+w,y,z}}},shade(color,.82f));
  quad(l,c,{{{x+w,base,z+d},{x,base,z+d},{x,y,z+d},{x+w,y,z+d}}},shade(color,.9f));
  quad(l,c,{{{x,base,z+d},{x,base,z},{x,y,z},{x,y,z+d}}},shade(color,.72f));
  slab(l,c,x,z,w,d,y,color);
}
} // namespace visualizer::scene3d
