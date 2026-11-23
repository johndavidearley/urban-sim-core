#pragma once

#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace visualizer {
struct UtilityOverlayGeometry {
  std::vector<std::pair<glm::vec2,glm::vec2>> segments;
  std::vector<glm::vec2> junctions;
};

// Rendering only: retain the real tile network, but display complete roadside
// trunks on their saved continuous road geometry. A missing pipe deliberately
// falls back to the tile network so a smooth stroke cannot conceal a gap.
inline UtilityOverlayGeometry utilityOverlayGeometry(
    const CityMap& map,const RoadNetwork& roads,bool water) {
  UtilityOverlayGeometry result;
  using Key=std::pair<int,int>;
  using Edge=std::array<int,4>;
  const auto key=[](Coord p) {return Key{p.x,p.y};};
  const auto edge=[](Coord a,Coord b) {
    if(a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
    return Edge{a.x,a.y,b.x,b.y};
  };
  const auto present=[&](Coord p) {
    return map.isValid(p) && (water ? map.utility(p).waterPipe : map.utility(p).sewerPipe);
  };
  std::map<Key,glm::vec2> positions;
  std::set<Edge> curvedEdges;
  for(const auto& alignment:roads.alignments()) {
    if(alignment.elevation!=0 || alignment.curves.empty() || alignment.path.size()<2 ||
       !std::all_of(alignment.path.begin(),alignment.path.end(),present)) continue;
    std::vector<glm::vec2> samples;
    for(const auto& curve:alignment.curves) {
      const int steps=std::clamp(curve.subdivisions(),8,128);
      auto a=curve.point(0);
      samples.push_back(a);
      for(int i=1;i<=steps;++i) {
        const auto b=curve.point(float(i)/float(steps));
        result.segments.emplace_back(a,b);samples.push_back(b);a=b;
      }
    }
    for(Coord p:alignment.path) {
      const glm::vec2 centre{p.x+.5f,p.y+.5f};
      const auto nearest=std::min_element(samples.begin(),samples.end(),[&](auto a,auto b) {
        const auto da=a-centre,db=b-centre;
        return da.x*da.x+da.y*da.y < db.x*db.x+db.y*db.y;
      });
      positions.emplace(key(p),*nearest);
    }
    for(size_t i=1;i<alignment.path.size();++i)
      curvedEdges.insert(edge(alignment.path[i-1],alignment.path[i]));
  }
  const auto position=[&](Coord p) {
    const auto found=positions.find(key(p));
    return found==positions.end() ? glm::vec2{p.x+.5f,p.y+.5f} : found->second;
  };
  const auto dims=map.getDimensions();
  for(int y=0;y<dims.y;++y) for(int x=0;x<dims.x;++x) {
    const Coord p{x,y};if(!present(p)) continue;
    bool branch=false;
    for(Coord d:{Coord{1,0},Coord{0,1},Coord{-1,0},Coord{0,-1}}) {
      const Coord n{x+d.x,y+d.y};
      if(!present(n) || curvedEdges.count(edge(p,n))) continue;
      branch=true;
      if(d.x>0 || d.y>0) result.segments.emplace_back(position(p),position(n));
    }
    if(branch || positions.find(key(p))==positions.end()) result.junctions.push_back(position(p));
  }
  return result;
}
} // namespace visualizer
