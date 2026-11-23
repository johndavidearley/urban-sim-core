#pragma once
#include "src/visualization/CityActivity.hpp"
#include "src/visualization/Scene3DPrimitives.hpp"
#include "src/visualization/VisualizerRender.hpp"

namespace visualizer {
inline RGB activityColor(const CityActivity::Actor& a) {
  constexpr RGB colors[]={{224,190,65},{102,174,208},{202,87,74},{223,226,215},{91,130,112}};
  return colors[a.color%5];
}
inline void activityBox(DrawList* list,const Camera3D& camera,const CityActivity::Actor& a,
                        float length,float width,float base,float height,RGB color) {
  const auto point=[&](float forward,float side,float y) {
    return WorldPoint{a.x+a.dx*forward-a.dz*side,a.y+y,a.z+a.dz*forward+a.dx*side};
  };
  std::array<WorldPoint,4> bottom{{point(-length/2,-width/2,base),point(length/2,-width/2,base),
    point(length/2,width/2,base),point(-length/2,width/2,base)}};
  auto top=bottom;for(auto& p:top) p.y+=height;
  for(size_t i=0;i<4;++i) {const size_t j=(i+1)%4;
    scene3d::quad(list,camera,{{bottom[i],bottom[j],top[j],top[i]}},scene3d::shade(color,.7f+.05f*float(i)));
  }
  scene3d::quad(list,camera,top,color);
}
inline void drawCityActivity3D(DrawList* list,const Camera3D& camera,
                               const std::vector<CityActivity::Actor>& actors) {
  // Moving visual proxies must not intercept building/ground inspection.
  scene3d::PickCollector noPicking(list,nullptr);
  for(const auto& a:actors) {
    if(camera.perspective && camera.viewDepth({a.x,a.y+.1f,a.z})<Camera3D::nearPlane) continue;
    const auto p=camera.project({a.x,a.y+.1f,a.z});
    if(p.x< -32 || p.y< -32 || p.x>list->viewportWidth+32 || p.y>list->viewportHeight+32) continue;
    if(a.kind==CityActivity::Kind::Pedestrian) {
      scene3d::box(list,camera,a.x-.035f,a.z-.035f,.07f,.07f,a.y+.025f,.13f,activityColor(a));
      scene3d::box(list,camera,a.x-.03f,a.z-.03f,.06f,.06f,a.y+.155f,.055f,{222,178,139});
    } else {
      const bool truck=a.kind==CityActivity::Kind::Truck;
      const float length=truck ? .42f : .30f,width=.15f;
      activityBox(list,camera,a,length,width,.035f,truck ? .16f : .10f,activityColor(a));
      activityBox(list,camera,a,length*.5f,width*.8f,truck ? .195f : .135f,.045f,{53,77,91});
    }
  }
}
inline void drawCityActivityTopDown(DrawList* list,const std::vector<CityActivity::Actor>& actors,
                                   int viewX,int viewY,int scale) {
  for(const auto& a:actors) {
    const int x=int((a.x-viewX)*scale),y=int((a.z-viewY)*scale);
    if(x<0 || y<0 || x>list->viewportWidth || y>list->viewportHeight) continue;
    const bool person=a.kind==CityActivity::Kind::Pedestrian;
    const float length=person ? .08f : a.kind==CityActivity::Kind::Truck ? .42f : .30f;
    const float width=person ? .08f : .15f;
    const auto point=[&](float forward,float side) {
      return glm::vec2{(a.x+a.dx*forward-a.dz*side-viewX)*scale,
                       (a.z+a.dz*forward+a.dx*side-viewY)*scale};
    };
    const auto p=point(-length/2,-width/2),q=point(length/2,-width/2),
               r=point(length/2,width/2),s=point(-length/2,width/2);
    const auto color=activityColor(a);setDrawColor(list,color.r,color.g,color.b,255);
    list->triangle(p.x,p.y,q.x,q.y,r.x,r.y);list->triangle(p.x,p.y,r.x,r.y,s.x,s.y);
  }
}
}
