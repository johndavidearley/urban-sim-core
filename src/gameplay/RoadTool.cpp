#include "src/gameplay/RoadTool.hpp"

#include "src/networks/RoadNetwork.hpp"
#include "src/world/CityMap.hpp"
#include <set>
#include <tuple>
#include <cmath>

namespace {
void appendAxis(std::vector<Coord>& tiles, Coord& current, int target, bool horizontal) {
  while ((horizontal ? current.x : current.y) != target) {
    const int value = horizontal ? current.x : current.y;
    const int step = target > value ? 1 : -1;
    if (horizontal) {
      current.x += step;
    } else {
      current.y += step;
    }
    tiles.push_back(current);
  }
}
RoadPlan evaluate(const CityMap& map,const RoadNetwork& roads,RoadPlan result,
                  int64_t availableFunds,const std::vector<ServiceFacility>* facilities) {
  for (const Coord tile : (result.hasAlignment ? result.alignment.footprint : result.tiles)) {
    const Tile& mapTile = map.getTile(tile);
    if (mapTile.regionalConnectionId) {
      result.error = "route crosses a regional terminal";
      return result;
    }
    if (mapTile.type == 2) {
      result.error = "route crosses water";
      return result;
    }
    if (mapTile.buildingId != 0 && (result.hasAlignment || !mapTile.hasRoad)) {
      result.error = "route crosses a building";
      return result;
    }
    if (facilities != nullptr) {
      for (const ServiceFacility& facility : *facilities) {
        if ((result.hasAlignment || (facility.type == ServiceType::Power &&
            facility.powerSource == PowerSourceType::Solar)) &&
            facilityCoversTile(facility, tile)) {
          result.error = result.hasAlignment ? "route crosses a service facility" : "route crosses a solar plant";
          return result;
        }
      }
    }
  }

  if(!result.hasAlignment || result.alignment.elevation==0) {
    const auto& footprint=result.hasAlignment ? result.alignment.footprint : result.tiles;
    for(const auto& structure:roads.alignments()) if(structure.elevation!=0) {
      for(const auto& curve:structure.curves) {
        const int steps=curve.subdivisions();
        for(int i=0;i<=steps;++i) {
          const float t=float(i)/steps;const auto p=curve.point(t);
          const Coord tile{int(std::floor(p.x)),int(std::floor(p.y))};
          if(tile==structure.path.front() || tile==structure.path.back()) continue;
          if(std::abs(structure.height(t))<.5f && std::find(footprint.begin(),footprint.end(),tile)!=footprint.end()) {
            result.error="surface road intersects a crossing approach slope";return result;
          }
        }
      }
    }
  }

  if(result.hasAlignment && result.alignment.elevation!=0) {
    const auto route=result.alignment.routingPath();
    if(roads.hasRoad(route.front(),route.back())) {
      result.error="these approaches already have a road link";return result;
    }
    for(Coord node:{route.front(),route.back()}) if(roadLevel(node)==1) {
      for(const auto& host:roads.alignments()) if(host.hasDeckAt(roadTile(node))) {
        const Coord other=node==route.front() ? route.back() : route.front();
        if(roadLevel(other)==0 && (roadTile(other)==host.path.front() || roadTile(other)==host.path.back())) {
          result.error="ramp duplicates an existing highway approach";return result;
        }
      }
    }
    result.newSegments=static_cast<int>(result.tiles.size()-1);
    result.cost=int64_t(result.newSegments)*RoadTool::segmentPrice(result.roadClass)*
      (result.alignment.elevation>0 ? 2 : 3);
    if(result.cost>availableFunds) {result.error="insufficient funds";return result;}
    result.valid=true;return result;
  }

  std::set<std::tuple<int,int,int,int>> counted;
  for (size_t i = 1; i < result.tiles.size(); ++i) {
    Coord a=result.tiles[i-1],b=result.tiles[i];
    if(a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
    if(!counted.emplace(a.x,a.y,b.x,b.y).second) continue;
    if(result.hasAlignment && roads.hasRoad(result.tiles[i-1],result.tiles[i]) &&
       !roads.canTravel(result.tiles[i-1],result.tiles[i])) {
      result.error="route opposes an existing one-way road";return result;
    }
    if(result.hasAlignment && result.alignment.oneWay && result.roadClass!=RoadClass::Ramp && roads.hasRoad(result.tiles[i-1],result.tiles[i]) &&
       roads.canTravel(result.tiles[i],result.tiles[i-1])) {
      result.error="one-way road overlaps an existing two-way road";return result;
    }
    if (!roads.hasRoad(result.tiles[i - 1], result.tiles[i])) {
      ++result.newSegments;
    } else if (result.roadClass == RoadClass::Arterial &&
               roads.getRoadClass(result.tiles[i - 1], result.tiles[i]) ==
                   RoadClass::Local) {
      ++result.upgradedSegments;
    }
  }
  result.cost =
      static_cast<int64_t>(result.newSegments) *
          RoadTool::segmentPrice(result.roadClass) +
      static_cast<int64_t>(result.upgradedSegments) * RoadTool::kUpgradeCostPerSegment;

  if (result.newSegments == 0 && result.upgradedSegments == 0) {
    result.error = "route adds no new road";
    return result;
  }
  if (result.cost > availableFunds) {
    result.error = "insufficient funds";
    return result;
  }

  result.valid = true;
  return result;
}
} // namespace

RoadPlan RoadTool::plan(const CityMap &map, const RoadNetwork &roads,
                        Coord start, Coord end, int64_t availableFunds,
                        RoadClass roadClass,
                        const std::vector<ServiceFacility>* facilities) {
  RoadPlan result;
  result.roadClass = roadClass;
  if (roadClass!=RoadClass::Local && roadClass!=RoadClass::Arterial) {
    result.error = "unsupported road class";
    return result;
  }
  if (!map.isValid(start) || !map.isValid(end)) {
    result.error = "route is outside the map";
    return result;
  }

  result.tiles.push_back(start);
  Coord current = start;
  appendAxis(result.tiles, current, end.x, true);
  appendAxis(result.tiles, current, end.y, false);

  return evaluate(map,roads,std::move(result),availableFunds,facilities);
}

bool RoadTool::build(
  CityMap& map,
  RoadNetwork& roads,
  const RoadPlan& plan,
  int64_t& availableFunds,
  const std::vector<ServiceFacility>* facilities
) {
  if (!plan.valid || plan.cost > availableFunds || plan.tiles.size() < 2) {
    return false;
  }

  const RoadPlan current = plan.hasAlignment
      ? planAlignment(map,roads,plan.alignment,availableFunds,facilities)
      : RoadTool::plan(map, roads, plan.tiles.front(), plan.tiles.back(),
                     availableFunds, plan.roadClass, facilities);
  if (!current.valid || current.roadClass!=plan.roadClass || current.tiles != plan.tiles || current.cost != plan.cost) {
    return false;
  }

  if(plan.hasAlignment) {
    if(plan.alignment.startLevel!=current.alignment.startLevel || plan.alignment.endLevel!=current.alignment.endLevel ||
       plan.alignment.curves.size()!=current.alignment.curves.size()) return false;
    for(size_t i=0;i<plan.alignment.curves.size();++i) for(size_t j=0;j<4;++j) {
      const auto delta=plan.alignment.curves[i].points[j]-current.alignment.curves[i].points[j];
      if(std::abs(delta.x)>.0001f || std::abs(delta.y)>.0001f) return false;
    }
  }
  if(plan.hasAlignment && current.alignment.elevation!=0) {
    const auto route=current.alignment.routingPath();const auto indices=current.alignment.routingIndices();
    for(Coord node:{route.front(),route.back()}) if(roadLevel(node)==1 && !roads.canAttachDeckJunction(node)) return false;
    for(Coord node:{route.front(),route.back()}) if(roadLevel(node)==1 && !roads.attachDeckJunction(node)) return false;
    for(size_t i=1;i<route.size();++i) {
      if(!roads.buildSeparatedRoad(route[i-1],route[i],float(indices[i]-indices[i-1]),current.alignment.elevation)) return false;
      roads.setRoadClass(route[i-1],route[i],plan.roadClass);
      roads.setOneWay(route[i-1],route[i],current.alignment.oneWay);
    }
  } else for (size_t i = 1; i < plan.tiles.size(); ++i) {
    const bool existed=roads.hasRoad(plan.tiles[i-1],plan.tiles[i]);
    roads.buildRoad(plan.tiles[i - 1], plan.tiles[i]);
    if (!existed || plan.roadClass == RoadClass::Arterial)
      roads.setRoadClass(plan.tiles[i - 1], plan.tiles[i], plan.roadClass);
    if(!existed && plan.hasAlignment && plan.alignment.oneWay)
      roads.setOneWay(plan.tiles[i-1],plan.tiles[i],true);
  }
  if(plan.hasAlignment) roads.addAlignment(current.alignment);
  availableFunds -= plan.cost;
  roads.updateConnectivity(plan.hasAlignment ? current.alignment.routingPath().front() : plan.tiles.front());
  return true;
}

int64_t RoadTool::segmentPrice(RoadClass c) {
  return c==RoadClass::Highway ? 2400 : c==RoadClass::Ramp ? 900 : c==RoadClass::Arterial ? kArterialCostPerSegment : kCostPerSegment;
}
namespace {
glm::vec2 placementPoint(glm::vec2 p,bool snapped) {
  if(snapped) return {std::floor(p.x)+.5f,std::floor(p.y)+.5f};
  return p;
}
}
RoadAlignment RoadTool::straight(glm::vec2 start,glm::vec2 end,RoadClass roadClass,bool snapped) {
  start=placementPoint(start,snapped);end=placementPoint(end,snapped);
  RoadAlignment a;a.roadClass=roadClass;a.snapped=snapped;a.oneWay=roadClass==RoadClass::Ramp;
  a.curves.push_back({{{start,start+(end-start)*(1.f/3.f),start+(end-start)*(2.f/3.f),end}}});return a;
}
RoadAlignment RoadTool::curve(glm::vec2 start,glm::vec2 control,glm::vec2 end,RoadClass roadClass,bool snapped) {
  start=placementPoint(start,snapped);control=placementPoint(control,snapped);end=placementPoint(end,snapped);
  RoadAlignment a;a.roadClass=roadClass;a.snapped=snapped;a.oneWay=roadClass==RoadClass::Ramp;
  a.curves.push_back({{{start,start+(control-start)*(2.f/3.f),end+(control-end)*(2.f/3.f),end}}});return a;
}
RoadAlignment RoadTool::roundabout(glm::vec2 center,float radius,RoadClass roadClass,bool snapped) {
  center=placementPoint(center,snapped);
  if(snapped) radius=std::round(radius);
  RoadAlignment a;a.roadClass=roadClass;a.snapped=snapped;a.oneWay=true;a.roundabout=true;
  if(!std::isfinite(radius) || radius<2.f) return a;
  constexpr float k=.55228475f;
  const std::array<glm::vec2,4> d{{{1,0},{0,1},{-1,0},{0,-1}}};
  for(size_t i=0;i<4;++i) {
    const auto p=d[i],q=d[(i+1)%4];
    a.curves.push_back({{{center+p*radius,center+(p+q*k)*radius,
                         center+(q+p*k)*radius,center+q*radius}}});
  }
  return a;
}
RoadPlan RoadTool::planAlignment(const CityMap& map,const RoadNetwork& roads,
    const RoadAlignment& input,int64_t availableFunds,const std::vector<ServiceFacility>* facilities,bool requireConnections) {
  RoadPlan result;result.hasAlignment=true;result.roadClass=input.roadClass;result.alignment=input;
  auto& a=result.alignment;a.path.clear();a.footprint.clear();
  if(!validRoadClass(a.roadClass) || a.curves.empty() || a.curves.size()>4) {
    result.error="choose a valid road alignment (roundabout radius at least 2)";return result;
  }
  if((a.roadClass==RoadClass::Ramp || a.roundabout) && !a.oneWay) {
    result.error="ramps and roundabouts must be one-way";return result;
  }
  if(a.roundabout && (a.curves.size()!=4 || !(a.curves.front().points[0]==a.curves.back().points[3]))) {
    result.error="roundabout must be a closed four-curve loop";return result;
  }
  if(a.elevation < -1 || a.elevation > 1 || (a.elevation!=0 && (a.roundabout || a.curves.size()!=1))) {
    result.error="overpasses and underpasses require a single line or curve";return result;
  }
  if(a.startLevel<0 || a.startLevel>1 || a.endLevel<0 || a.endLevel>1 ||
     ((a.startLevel || a.endLevel) && (a.elevation!=1 || a.roadClass!=RoadClass::Ramp)) ||
     (a.startLevel==1 && a.endLevel==1) || (requireConnections && !a.deckJunctions.empty())) {
    result.error="deck connections require a ramp between ground and an elevated highway";return result;
  }
  bool missingAttachment=false;
  if(!a.roundabout && requireConnections) {
    const auto connectEndpoint=[&](RoadBezier& curve,bool first) {
      const size_t endpoint=first ? 0 : 3,handle=first ? 1 : 2;
      const auto original=curve.points[endpoint];
      const int requested=first ? a.startLevel : a.endLevel;
      int targetLevel=0;
      glm::vec2 target=original,tangent{0,0};float best=.6f*.6f;bool hit=false;
      for(const auto& existing:roads.alignments()) for(const auto& segment:existing.curves) {
        const int steps=segment.subdivisions();
        for(int i=0;i<=steps;++i) {
          const float t=float(i)/steps;
          const bool interior=i!=0 && i!=steps;
          int level=interior ? existing.elevation : i==0 ? existing.startLevel : existing.endLevel;
          if(interior && existing.elevation!=0 && !(existing.roadClass==RoadClass::Highway && existing.elevation==1 && existing.height(t)>=.799f)) continue;
          if(level!=0 && !(a.roadClass==RoadClass::Ramp && a.elevation==1 && level==1)) continue;
          const auto p=segment.point(t),d=p-original;
          const float distance=d.x*d.x+d.y*d.y;
          if(distance<best-.000001f || (distance<=best+.000001f && (!hit || level>targetLevel))) {
            best=distance;target=p;tangent=segment.tangent(t);hit=true;targetLevel=level;
          }
        }
      }
      if(first) a.startLevel=targetLevel;else a.endLevel=targetLevel;
      if(requested==1 && targetLevel!=1) missingAttachment=true;
      if(hit) {
        const auto v=curve.points[handle]-original;
        const float length=std::sqrt(v.x*v.x+v.y*v.y),norm=std::sqrt(tangent.x*tangent.x+tangent.y*tangent.y);
        if(norm>.001f) {
          const float sign=v.x*tangent.x+v.y*tangent.y>=0 ? 1.f : -1.f;
          curve.points[handle]=target+tangent*(sign*length/norm);
        } else curve.points[handle]=curve.points[handle]+(target-original);
        curve.points[endpoint]=target;
      } else {
        const Coord tile{int(std::floor(original.x)),int(std::floor(original.y))};
        if(map.isValid(tile) && roads.hasRoadAdjacency(tile)) {
          target={tile.x+.5f,tile.y+.5f};
          curve.points[handle]=curve.points[handle]+(target-original);curve.points[endpoint]=target;
        }
      }
    };
    connectEndpoint(a.curves.front(),true);connectEndpoint(a.curves.back(),false);
  }
  if(missingAttachment || (a.startLevel==1 && a.endLevel==1)) {
    result.error="ramp must connect one live highway deck to ground";return result;
  }
  const auto dims=map.getDimensions();
  for(size_t i=0;i<a.curves.size();++i) {
    for(const auto p:a.curves[i].points)
      if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0 ||p.y<0 ||p.x>=dims.x||p.y>=dims.y) {
        result.error="road control point is outside the map";return result;
      }
    if(i && !(a.curves[i-1].points[3]==a.curves[i].points[0])) {
      result.error="road curves must meet";return result;
    }
  }
  std::set<std::pair<int,int>> footprint;
  const float half=roadWidth(a.roadClass)*.5f;
  for(const auto& curve:a.curves) {
    const int steps=curve.subdivisions();
    for(int i=0;i<=steps;++i) {
      const auto p=curve.point(float(i)/steps);
      const Coord tile{int(std::floor(p.x)),int(std::floor(p.y))};
      if(a.path.empty()) a.path.push_back(tile);
      Coord current=a.path.back();
      // Sampling is finer than one tile. Join diagonal boundary crossings
      // deterministically so every simulation edge stays cardinal.
      appendAxis(a.path,current,tile.x,true);appendAxis(a.path,current,tile.y,false);
      for(int z=int(std::floor(p.y-half));z<=int(std::floor(p.y+half));++z)
        for(int x=int(std::floor(p.x-half));x<=int(std::floor(p.x+half));++x)
          footprint.emplace(x,z);
    }
  }
  for(Coord tile:a.path) footprint.emplace(tile.x,tile.y);
  for(auto [x,z]:footprint) {
    if(!map.isValid({x,z})) {result.error="road width extends outside the map";return result;}
    a.footprint.push_back({x,z});
  }
  result.tiles=a.path;
  if(a.path.size()<2) {result.error="road is too short";return result;}
  // Self-crossings would create ambiguous one-way paths and junctions.
  std::set<std::pair<int,int>> visited;
  for(size_t i=0;i<a.path.size();++i) {
    if(a.roundabout && i+1==a.path.size() && a.path[i]==a.path.front()) continue;
    if(!visited.emplace(a.path[i].x,a.path[i].y).second) {
      result.error="road doubles back through the same tile";return result;
    }
  }
  if(!std::is_sorted(a.deckJunctions.begin(),a.deckJunctions.end()) ||
     std::adjacent_find(a.deckJunctions.begin(),a.deckJunctions.end())!=a.deckJunctions.end()) {
    result.error="invalid deck junction ordering";return result;
  }
  for(size_t index:a.deckJunctions) if(index==0 || index>=a.path.size()-1 || !a.hasDeckAt(a.path[index])) {
    result.error="deck junction is outside the elevated highway deck";return result;
  }
  if(requireConnections) {
    const auto route=a.routingPath();
    for(Coord node:{route.front(),route.back()}) if(roadLevel(node)==1 && !roads.canAttachDeckJunction(node)) {
      result.error="ramp deck connection no longer exists";return result;
    }
  }
  if(a.elevation!=0) {
    if(a.path.size()<13) {result.error="crossing needs at least 12 routing segments for its approaches";return result;}
    const auto& curve=a.curves.front();const int steps=curve.subdivisions();
    for(int i=0;i<=steps;++i) {
      const float t=float(i)/steps;const auto p=curve.point(t);
      const Coord tile{int(std::floor(p.x)),int(std::floor(p.y))};
      if(tile==a.path.front() || tile==a.path.back()) continue;
      if(roads.hasRoadAdjacency(tile) && std::abs(a.height(t))<.5f) {
        result.error="crossing is too close to an approach; move endpoints farther apart";return result;
      }
    }
  }
  // A surface road added later must also respect an existing crossing's
  // approach slopes. Separated crossings on the same level cannot overlap.
  for(const auto& existing:roads.alignments()) {
    if(a.elevation==0 && existing.elevation==0) continue;
    for(Coord tile:a.footprint) {
      if(tile==a.path.front() || tile==a.path.back() || tile==existing.path.front() || tile==existing.path.back()) continue;
      if(std::find(existing.footprint.begin(),existing.footprint.end(),tile)==existing.footprint.end()) continue;
      if(roadDeckMerge(a,existing,tile) || roadDeckMerge(existing,a,tile)) continue;
      if(a.elevation!=0 && a.elevation==existing.elevation) {
        result.error="structures on the same level overlap";return result;
      }
      const auto& structure=a.elevation!=0 ? a : existing;
      const auto& curve=structure.curves.front();const int steps=curve.subdivisions();
      float clearance=1.f;
      for(int i=0;i<=steps;++i) {
        const float t=float(i)/steps;const auto p=curve.point(t);
        if(int(std::floor(p.x))==tile.x && int(std::floor(p.y))==tile.y)
          clearance=std::min(clearance,std::abs(structure.height(t)));
      }
      if(clearance<.5f) {result.error="road intersects a crossing approach slope";return result;}
    }
  }
  return evaluate(map,roads,std::move(result),availableFunds,facilities);
}
