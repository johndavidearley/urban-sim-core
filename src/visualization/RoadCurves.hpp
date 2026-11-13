#pragma once

#include <array>
#include <cmath>
#include "src/networks/RoadNetwork.hpp"

namespace visualizer {
// Parcel-local cubic Bezier bend. End tangents meet neighboring road edges
// head-on, including when a bend joins another bend or a straight segment.
struct RoadCurve {
  glm::vec2 start{}, end{};
  bool valid=false;
  glm::vec2 point(float t) const {
    const glm::vec2 center{.5f,.5f};
    const auto a=start+(center-start)*.55228475f;
    const auto b=end+(center-end)*.55228475f;
    const float s=1.f-t;
    return start*(s*s*s)+a*(3*s*s*t)+b*(3*s*t*t)+end*(t*t*t);
  }
  glm::vec2 normal(float t) const {
    const glm::vec2 center{.5f,.5f};
    const auto a=start+(center-start)*.55228475f;
    const auto b=end+(center-end)*.55228475f;
    const float s=1.f-t;
    const auto d=(a-start)*(3*s*s)+(b-a)*(6*s*t)+(end-b)*(3*t*t);
    const float length=std::sqrt(d.x*d.x+d.y*d.y);
    return {-d.y/length,d.x/length};
  }
  template<class Emit>
  void ribbon(float width,float offset,Emit emit) const {
    constexpr int segments=16;
    for(int i=0;i<segments;++i) {
      const float a=float(i)/segments,b=float(i+1)/segments;
      const auto p=point(a),q=point(b),n=normal(a),m=normal(b);
      emit(std::array<glm::vec2,4>{{p+n*(offset-width*.5f),q+m*(offset-width*.5f),
                                  q+m*(offset+width*.5f),p+n*(offset+width*.5f)}});
    }
  }
};
inline RoadCurve roadCurve(const CityMap& map,const RoadNetwork* roads,Coord tile) {
  std::array<Coord,2> directions{};
  int count=0;
  for(Coord d:{Coord{1,0},Coord{-1,0},Coord{0,1},Coord{0,-1}}) {
    const Coord n{tile.x+d.x,tile.y+d.y};
    const bool connected=roads ? roads->hasRoad(tile,n) : map.isValid(n)&&map.getTile(n).hasRoad;
    if(!connected) continue;
    if(count==2) return {};
    directions[static_cast<size_t>(count++)]=d;
  }
  if(count!=2 || directions[0].x*directions[1].x+directions[0].y*directions[1].y!=0) return {};
  return {{.5f+.5f*directions[0].x,.5f+.5f*directions[0].y},
          {.5f+.5f*directions[1].x,.5f+.5f*directions[1].y},true};
}
} // namespace visualizer
