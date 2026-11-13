#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/glm.hpp>
#include "src/world/Tile.hpp"

enum class RoadClass : int { Local=0, Arterial=1, Highway=2, Ramp=3 };
inline bool validRoadClass(RoadClass c) {return int(c)>=0 && int(c)<=3;}
inline float roadCapacity(RoadClass c) {
  return c==RoadClass::Highway ? 800.f : c==RoadClass::Arterial ? 400.f : c==RoadClass::Ramp ? 200.f : 100.f;
}
inline float roadWidth(RoadClass c) {
  return c==RoadClass::Highway ? .9f : c==RoadClass::Arterial ? .76f : c==RoadClass::Ramp ? .48f : .55f;
}
inline const char* roadClassName(RoadClass c) {
  switch(c) {
    case RoadClass::Arterial:return "ARTERIAL";
    case RoadClass::Highway:return "HIGHWAY";
    case RoadClass::Ramp:return "ONE-WAY RAMP";
    default:return "LOCAL";
  }
}
inline const char* roadClassToken(RoadClass c) {
  return c==RoadClass::Highway ? "highway" : c==RoadClass::Ramp ? "ramp" : c==RoadClass::Arterial ? "arterial" : "local";
}
struct RoadBezier {
  std::array<glm::vec2,4> points{};
  glm::vec2 point(float t) const {
    const float s=1-t;
    return points[0]*(s*s*s)+points[1]*(3*s*s*t)+points[2]*(3*s*t*t)+points[3]*(t*t*t);
  }
  glm::vec2 tangent(float t) const {
    const float s=1-t;
    return (points[1]-points[0])*(3*s*s)+(points[2]-points[1])*(6*s*t)+(points[3]-points[2])*(3*t*t);
  }
  int subdivisions() const {
    float length=0;
    for(size_t i=1;i<4;++i) {
      const auto d=points[i]-points[i-1];length+=std::sqrt(d.x*d.x+d.y*d.y);
    }
    return std::max(8,std::min(4096,int(std::ceil(length*8))));
  }
};
// Graph coordinates encode the level without changing surface tile IDs.
// Never use an encoded node directly to index CityMap.
inline Coord roadNode(Coord tile,int level) {
  return level==0 ? tile : Coord{level>0 ? tile.x : -tile.x-1,-tile.y-1};
}
inline Coord roadTile(Coord node) {return {node.x<0 ? -(node.x+1) : node.x,node.y<0 ? -(node.y+1) : node.y};}
inline int roadLevel(Coord node) {return node.y>=0 ? 0 : node.x>=0 ? 1 : -1;}

struct RoadAlignment {
  std::vector<RoadBezier> curves;
  // Cardinal routing path and reserved swept footprint. Rendering retains
  // the exact continuous geometry; the simulation uses the sampled path.
  std::vector<Coord> path;
  std::vector<Coord> footprint;
  RoadClass roadClass=RoadClass::Local;
  bool snapped=true;
  bool oneWay=false;
  bool roundabout=false;
  int elevation=0; // -1 open-cut underpass, 0 surface, +1 overpass.
  int startLevel=0,endLevel=0;
  std::vector<size_t> deckJunctions; // Indices into the sampled physical path.
  std::vector<size_t> routingIndices() const {
    if(path.empty()) return {};
    if(elevation==0) {
      std::vector<size_t> result;for(size_t i=0;i<path.size();++i) result.push_back(i);return result;
    }
    std::vector<size_t> result{0};result.insert(result.end(),deckJunctions.begin(),deckJunctions.end());
    result.push_back(path.size()-1);return result;
  }
  std::vector<Coord> routingPath() const {
    std::vector<Coord> result;
    for(size_t index:routingIndices()) {
      const int level=index==0 ? startLevel : index+1==path.size() ? endLevel : elevation;
      result.push_back(roadNode(path[index],level));
    }
    return result;
  }
  bool hasDeckAt(Coord tile) const {
    if(elevation!=1 || roadClass!=RoadClass::Highway || curves.size()!=1) return false;
    const auto& curve=curves.front();const int steps=curve.subdivisions();
    for(int i=1;i<steps;++i) {
      const float t=float(i)/steps;const auto p=curve.point(t);
      if(int(std::floor(p.x))==tile.x && int(std::floor(p.y))==tile.y && height(t)>=.799f) return true;
    }
    return false;
  }
  float height(float progress) const {
    if(elevation==0) return 0;
    if(startLevel==1 || endLevel==1) {
      const float p=std::clamp(startLevel==1 ? (progress-.2f)/.8f : progress/.8f,0.f,1.f);
      const float blend=p*p*(3.f-2.f*p);
      return .8f*(startLevel==1 ? 1.f-blend : blend);
    }
    const float ramp=std::clamp(std::min(progress,1.f-progress)*4.f,0.f,1.f);
    return float(elevation)*.8f*ramp*ramp*(3.f-2.f*ramp);
  }
};

inline const char* roadElevationName(int elevation) {
  return elevation>0 ? "OVERPASS" : elevation<0 ? "UNDERPASS" : "GROUND";
}

// Permit the contiguous merge corridor where a ramp leaves/joins its host
// deck. Re-entry elsewhere remains a same-level collision.
inline bool roadDeckMerge(const RoadAlignment& ramp,const RoadAlignment& host,Coord tile) {
  if(ramp.roadClass!=RoadClass::Ramp || ramp.elevation!=1 || host.roadClass!=RoadClass::Highway ||
     host.elevation!=1 || ramp.path.empty()) return false;
  const bool leavesHost=std::any_of(ramp.path.begin(),ramp.path.end(),[&](Coord p) {
    return std::find(host.footprint.begin(),host.footprint.end(),p)==host.footprint.end();
  });
  if(!leavesHost) return false;
  for(bool first:{true,false}) {
    if((first ? ramp.startLevel : ramp.endLevel)!=1) continue;
    const auto endpoint=first ? ramp.path.front() : ramp.path.back();
    if(!host.hasDeckAt(endpoint)) continue;
    for(size_t n=0;n<ramp.path.size();++n) {
      const auto p=ramp.path[first ? n : ramp.path.size()-1-n];
      if(std::find(host.footprint.begin(),host.footprint.end(),p)==host.footprint.end()) break;
      if(std::abs(tile.x-p.x)<=1 && std::abs(tile.y-p.y)<=1) return true;
    }
  }
  return false;
}
