#include "src/gameplay/PipeTool.hpp"
#include <algorithm>
#include <cmath>

PipePlan PipeTool::plan(const CityMap& map,glm::vec2 start,glm::vec2 end,int kind,
    bool snapped,bool remove,int64_t funds) {
  PipePlan result;result.start=start;result.end=end;result.kind=kind;result.snapped=snapped;result.remove=remove;
  const auto dims=map.getDimensions();
  const auto valid=[&](glm::vec2 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=0&&p.y>=0&&p.x<dims.x&&p.y<dims.y;};
  if(!valid(start)||!valid(end)) {result.error="Pipe route is outside the map";return result;}
  if(kind<1 || kind>5) {result.error="Choose water, sewer, combined pipes, sewer lift or water booster";return result;}
  Coord current{int(start.x),int(start.y)},last{int(end.x),int(end.y)};result.tiles.push_back(current);
  const auto add=[&](Coord next) {if(next!=result.tiles.back()) result.tiles.push_back(next);};
  if(kind>=4) {
    const bool booster=kind==5;
    if(current!=last) {result.error="Click one pipe tile to place/remove a station";return result;}
    const auto& cell=map.utility(current);
    const bool present=booster?cell.waterBooster:cell.sewageLift;
    if(!remove && (!(booster?cell.waterPipe:cell.sewerPipe) || map.getTile(current).type==2)) {
      result.error=booster?"Booster requires an existing water pipe on land":"Lift requires an existing sewer pipe on land";return result;
    }
    if(present==!remove) {result.error=remove?"No selected station here":"Station already installed";return result;}
    result.changed=1;result.cost=remove?(booster?boosterRemovalCost:liftRemovalCost):(booster?boosterCost:liftCost);
    if(result.cost>funds) result.error="Insufficient funds for station";else result.valid=true;
    return result;
  }
  if(snapped) {
    while(current.x!=last.x) {current.x+=last.x>current.x?1:-1;add(current);}
    while(current.y!=last.y) {current.y+=last.y>current.y?1:-1;add(current);}
  } else {
    // Rasterize a continuous straight route, retaining cardinal connections
    // even where it crosses a grid corner. Surface roads/buildings are allowed.
    const int steps=std::max(1,int(std::ceil(std::hypot(end.x-start.x,end.y-start.y)*8)));
    for(int i=1;i<=steps;++i) {
      const auto point=start+(end-start)*(float(i)/steps);const Coord target{int(point.x),int(point.y)};
      while(current.x!=target.x) {current.x+=target.x>current.x?1:-1;add(current);}
      while(current.y!=target.y) {current.y+=target.y>current.y?1:-1;add(current);}
    }
  }
  for(Coord p:result.tiles) {
    const auto& c=map.utility(p);
    if(remove && (kind&1) && c.waterBooster) {result.error="Remove the water booster before removing its pipe";return result;}
    if(remove && (kind&2) && c.sewageLift) {result.error="Remove the sewer lift before removing its pipe";return result;}
    const bool water=(kind&1) && (remove?c.waterPipe:!c.waterPipe);
    const bool sewer=(kind&2) && (remove?c.sewerPipe:!c.sewerPipe);
    if(water||sewer) ++result.changed;
    result.cost+=remove ? ((water||sewer)?removalCost:0) : (water?waterCost:0)+(sewer?sewerCost:0);
  }
  if(!result.changed) result.error=remove?"No selected pipes on this route":"Route already has these pipes";
  else if(result.cost>funds) result.error="Insufficient funds for pipe route";
  else result.valid=true;
  return result;
}
bool PipeTool::apply(CityMap& map,const PipePlan& plan,int64_t& funds) {
  if(!plan.valid) return false;
  const auto current=PipeTool::plan(map,plan.start,plan.end,plan.kind,plan.snapped,plan.remove,funds);
  if(!current.valid || current.tiles!=plan.tiles || current.cost!=plan.cost) return false;
  if(!map.pipeNetworksEnabled()) {
    map.pipeNetworksEnabled()=true;
    // Preserve any legacy citywide sewage instead of discarding it on upgrade.
    map.utility(plan.tiles.front()).sewageBacklog+=map.environment().sewageBacklog;
  }
  for(Coord p:plan.tiles) {
    auto& c=map.utility(p);
    if(plan.kind==4) {c.sewageLift=!plan.remove;continue;}
    if(plan.kind==5) {c.waterBooster=!plan.remove;continue;}
    if(plan.kind&1) c.waterPipe=!plan.remove;
    if(plan.kind&2) c.sewerPipe=!plan.remove;
  }
  funds-=plan.cost;return true;
}
