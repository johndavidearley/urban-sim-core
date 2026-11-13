#include "src/systems/UtilityNetworkSystem.hpp"
#include "src/systems/EnvironmentSystem.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr Coord directions[]={{0,-1},{-1,0},{1,0},{0,1}};
constexpr double epsilon=1e-7;
struct Router {
  const CityMap& map;
  int width;
  bool sewer;
  std::vector<double> remaining,stationRemaining;
  bool hasStations=false;
  std::vector<Coord> stationSites;
  Router(const CityMap& m,bool s,bool gridPower=false):map(m),width(m.getDimensions().x),sewer(s),remaining(m.getTileCount(),100),stationRemaining(m.getTileCount(),s?UtilityNetworkSystem::liftCapacity:UtilityNetworkSystem::boosterCapacity) {
    if(gridPower) for(size_t i=0;i<map.getTileCount();++i) {
      const Coord p{int(i%width),int(i/width)};
      if((sewer?map.utility(p).sewageLift:map.utility(p).waterBooster) && map.getTile(p).connectedToPower) {hasStations=true;stationSites.push_back(p);}
    }
  }
  size_t index(Coord p) const {return static_cast<size_t>(p.y)*width+p.x;}
  Coord coord(size_t i) const {return {int(i%width),int(i/width)};}
  bool pipe(Coord p) const {return map.isValid(p) && (sewer?map.utility(p).sewerPipe:map.utility(p).waterPipe);}
  float height(Coord p) const {return map.environment().initialized?map.environment(p).terrainMeters:0;}
  bool downhill(Coord from,Coord to) const {return height(to)<=height(from)+.05f;}
  // A parcel lateral drops into a sewer buried 2m below its terrain. Once
  // inside the trunk, pipe-to-pipe gravity still follows the original grade.
  bool sewerEntry(Coord parcel,Coord pipeTile) const {return height(pipeTile)-2.f<=height(parcel)+.05f;}
  std::vector<Coord> connectors(Coord p) const {
    std::vector<Coord> result;if(pipe(p)) result.push_back(p);
    for(auto d:directions) {const Coord n{p.x+d.x,p.y+d.y};if(pipe(n)) result.push_back(n); }
    return result;
  }
  // Water searches outward from a pump. Sewer searches backward from an
  // outlet, so uphill neighbours are the valid contributing catchment.
  std::vector<int> search(Coord site,bool residual) const {
    std::vector<int> parent(map.getTileCount(),-1),distance(map.getTileCount(),0);
    std::queue<size_t> queue;const float head=height(site)+UtilityNetworkSystem::pumpHead;
    for(auto p:connectors(site)) {
      const auto i=index(p);
      if(residual && remaining[i]<=epsilon) continue;
      if(sewer ? !downhill(p,site) : height(p)>head) continue;
      parent[i]=int(i);queue.push(i);
    }
    while(!queue.empty()) {
      const auto i=queue.front();queue.pop();const Coord p=coord(i);
      for(auto d:directions) {
        const Coord next{p.x+d.x,p.y+d.y};if(!pipe(next)) continue;const auto n=index(next);
        if(parent[n]>=0 || (residual && remaining[n]<=epsilon)) continue;
        if(sewer ? !downhill(next,p) : height(next)>head-.05f*(distance[i]+1)) continue;
        parent[n]=int(i);distance[n]=distance[i]+1;queue.push(n);
      }
    }
    return parent;
  }
  std::vector<size_t> path(const std::vector<int>& parent,Coord target,Coord site) const {
    for(auto p:connectors(target)) {
      const auto i=index(p);if(parent[i]<0) continue;
      if(sewer ? !sewerEntry(target,p) : height(target)>height(site)+UtilityNetworkSystem::pumpHead) continue;
      std::vector<size_t> result;size_t n=i;
      while(true) {result.push_back(n);if(parent[n]==int(n)) break;n=static_cast<size_t>(parent[n]);}
      if(!sewer && height(target)>height(site)+UtilityNetworkSystem::pumpHead-.05f*result.size()) continue;
      return result;
    }
    return {};
  }
  // A pressure state is tied to a particular lift and distance travelled
  // since that lift. Keeping it separate from gravity prevents pressure from
  // appearing at unrelated branches or being renewed at every uphill tile.
  struct Route {std::vector<size_t> tiles,stations;float head=0;};
  Route pressurePath(Coord from,Coord site,bool residual) const {
    const auto starts=connectors(from),ends=connectors(site);
    if(starts.empty() || ends.empty()) return {};
    struct State {size_t tile;int pump,steps,parent;};
    std::vector<State> states;std::queue<size_t> queue;
    std::unordered_map<uint64_t,int> best;
    const auto push=[&](size_t tile,int pump,int steps,int parent) {
      if(residual && remaining[tile]<=epsilon) return;
      const auto key=(uint64_t(pump+2)<<32)|tile;
      const auto found=best.find(key);if(found!=best.end() && found->second<=steps) return;
      best[key]=steps;states.push_back({tile,pump,steps,parent});queue.push(states.size()-1);
    };
    for(Coord p:starts) {
      if(sewer ? sewerEntry(from,p) : height(p)<=height(from)+UtilityNetworkSystem::pumpHead-.05f)
        push(index(p),sewer?-1:-2,sewer?0:1,-1);
    }
    while(!queue.empty()) {
      const auto stateIndex=queue.front();queue.pop();const auto state=states[stateIndex];const Coord p=coord(state.tile);
      const bool pressurized=state.pump!=-1;
      const float origin=state.pump==-2?height(from):state.pump>=0?height(coord(size_t(state.pump))):height(p);
      const float head=pressurized?origin+UtilityNetworkSystem::pumpHead-.05f*state.steps:height(p);
      if(std::find(ends.begin(),ends.end(),p)!=ends.end() &&
         (pressurized ? height(site)<=head : downhill(p,site))) {
        Route route;route.head=head;std::unordered_set<size_t> nodes,pumps;
        for(int i=int(stateIndex);i>=0;i=states[size_t(i)].parent) {
          const auto& entry=states[size_t(i)];
          if(nodes.insert(entry.tile).second) route.tiles.push_back(entry.tile);
          if(entry.pump>=0 && pumps.insert(size_t(entry.pump)).second) route.stations.push_back(size_t(entry.pump));
        }
        return route;
      }
      if(sewer && state.pump>=0) push(state.tile,-1,0,int(stateIndex)); // Resume a gravity reach.
      if((sewer?map.utility(p).sewageLift:map.utility(p).waterBooster) && map.getTile(p).connectedToPower &&
         (!residual || stationRemaining[state.tile]>epsilon))
        push(state.tile,int(state.tile),0,int(stateIndex));
      for(Coord d:directions) {
        const Coord n{p.x+d.x,p.y+d.y};if(!pipe(n)) continue;
        if(pressurized ? height(n)<=head-.05f : downhill(p,n))
          push(index(n),state.pump,pressurized?state.steps+1:0,int(stateIndex));
      }
    }
    return {};
  }
  Route route(Coord from,Coord site,bool residual) const {
    if(hasStations) return sewer?pressurePath(from,site,residual):pressurePath(site,from,residual);
    const auto tiles=path(search(site,residual),from,site);
    return {tiles,{},sewer?0:height(site)+UtilityNetworkSystem::pumpHead-.05f*tiles.size()};
  }
  std::vector<int> reach(Coord site) const {
    auto parent=search(site,false);
    if(!sewer || !hasStations) return parent;
    // Every valid route is either gravity-only or reaches a first powered
    // lift by gravity. Qualify each lift once, then expand its gravity
    // catchment backward. Actual flow still uses the residual pressure search.
    std::queue<size_t> queue;
    for(Coord p:stationSites) {
      const auto i=index(p);if(parent[i]>=0) continue;
      if(!pressurePath(p,site,false).tiles.empty()) {parent[i]=int(i);queue.push(i);}
    }
    while(!queue.empty()) {
      const auto i=queue.front();queue.pop();const Coord p=coord(i);
      for(Coord d:directions) {
        const Coord n{p.x+d.x,p.y+d.y};if(!pipe(n)) continue;const auto j=index(n);
        if(parent[j]<0 && downhill(n,p)) {parent[j]=int(i);queue.push(j);}
      }
    }
    return parent;
  }
  double send(Coord from,Coord site,double amount,double& facilityRemaining) {
    double total=0;
    while(amount>epsilon && facilityRemaining>epsilon) {
      const auto selected=route(from,site,true);if(selected.tiles.empty()) break;
      double volume=std::min(amount,facilityRemaining);
      for(auto i:selected.tiles) volume=std::min(volume,remaining[i]);
      for(auto i:selected.stations) volume=std::min(volume,stationRemaining[i]);
      if(volume<=epsilon) break;
      for(auto i:selected.tiles) remaining[i]-=volume;
      for(auto i:selected.stations) stationRemaining[i]-=volume;
      facilityRemaining-=volume;amount-=volume;total+=volume;
    }
    return total;
  }
};
}
UtilityNetworkResult UtilityNetworkSystem::evaluate(const CityMap& map,const EntityStore& store,
    const std::vector<ServiceFacility>& facilities) {
  UtilityNetworkResult result;const auto count=map.getTileCount();
  result.liftUsed.resize(count);result.boosterUsed.resize(count);result.waterPressure.resize(count);
  result.sourceUsed.resize(facilities.size());result.outletUsed.resize(facilities.size());
  result.demand.resize(count);result.delivered.resize(count);result.handled.resize(count);
  result.waterConnected.resize(count);result.sewerConnected.resize(count);result.liftPowered.resize(count);result.boosterPowered.resize(count);result.backlog.resize(count);
  const bool gridPower=std::any_of(facilities.begin(),facilities.end(),[&](const auto& f) {
    return f.type==ServiceType::Power && f.powerCapacityMW>0 &&
      (f.powerSource!=PowerSourceType::Wind || EnvironmentSystem::windPowerFactor(map,f.position)>0);
  });
  Router water(map,false,gridPower),sewer(map,true,gridPower);
  const auto capacities=EnvironmentSystem::waterCapacities(map,facilities);
  std::vector<double> supply(capacities.begin(),capacities.end());
  std::vector<double> drainage(facilities.size());
  for(size_t f=0;f<facilities.size();++f) {
    const auto& facility=facilities[f];if(!map.isValid(facility.position)) continue;
    if(facility.type==ServiceType::Sanitation) drainage[f]=100*std::clamp(facility.quality,0.f,1.f)*
      (facility.surfaceWater?EnvironmentSystem::drainageFactor(map,facility.position):1.f);
    if((facility.type!=ServiceType::Water || supply[f]<=epsilon) && drainage[f]<=epsilon) continue;
    Router& network=facility.type==ServiceType::Water?water:sewer;
    const auto parents=network.reach(facility.position);
    for(size_t i=0;i<count;++i) {
      const Coord p=network.coord(i);
      Router::Route reachable;
      if(!network.sewer && network.hasStations) reachable=network.route(p,facility.position,false);
      else {reachable.tiles=network.path(parents,p,facility.position);reachable.head=network.height(facility.position)+pumpHead-.05f*reachable.tiles.size();}
      if(!reachable.tiles.empty()) {
        if(facility.type==ServiceType::Water) {
          result.waterConnected[i]=true;
          result.waterPressure[i]=std::max(result.waterPressure[i],std::max(0.f,reachable.head-network.height(p)));
        } else result.sewerConnected[i]=true;
      }
    }
  }
  std::vector<EntityId> ids;for(const auto& [id,b]:store.getBuildings()) {(void)b;ids.push_back(id);}
  std::sort(ids.begin(),ids.end());
  for(auto id:ids) {
    const auto& b=*store.getBuilding(id);if(!map.isValid(b.position)) continue;
    const auto i=water.index(b.position);const double demand=utilityDemandFor(b);result.demand[i]=float(demand);
    double remaining=demand;
    for(size_t f=0;f<facilities.size() && remaining>epsilon;++f) {
      if(facilities[f].type!=ServiceType::Water || supply[f]<=epsilon || !map.isValid(facilities[f].position)) continue;
      double available=supply[f];const double delivered=water.send(b.position,facilities[f].position,remaining,available);
      supply[f]=available;result.sourceUsed[f]+=delivered;remaining-=delivered;
    }
    result.delivered[i]=float(demand-remaining);
    if(demand>epsilon) result.waterConnected[i]=remaining<=epsilon;
  }
  // Backlogs survive building demolition and pipe removal at their parcel.
  // Generated sewage comes from actual water delivered on this evaluation.
  for(size_t i=0;i<count;++i) {
    const double generated=result.delivered[i]*.8;result.generated+=generated;
    double remaining=map.utility(water.coord(i)).sewageBacklog+generated;
    for(size_t f=0;f<facilities.size() && remaining>epsilon;++f) {
      if(drainage[f]<=epsilon || !map.isValid(facilities[f].position)) continue;
      const double handled=sewer.send(water.coord(i),facilities[f].position,remaining,drainage[f]);
      result.outletUsed[f]+=handled;result.handled[i]+=float(handled);remaining-=handled;result.discharged+=handled;
    }
    result.backlog[i]=std::max(0.,remaining);result.totalBacklog+=result.backlog[i];
    if(generated>epsilon || map.utility(water.coord(i)).sewageBacklog>epsilon)
      result.sewerConnected[i]=remaining<=epsilon;
  }
  for(size_t i=0;i<count;++i) {
    const Coord p=water.coord(i);result.liftUsed[i]=liftCapacity-sewer.stationRemaining[i];
    result.boosterUsed[i]=boosterCapacity-water.stationRemaining[i];
    result.boosterPowered[i]=gridPower && map.utility(p).waterBooster && map.getTile(p).connectedToPower;
    result.liftPowered[i]=gridPower && map.utility(p).sewageLift && map.getTile(p).connectedToPower;
  }
  return result;
}
void UtilityNetworkSystem::applyDiagnostics(CityMap& map,const UtilityNetworkResult& r) {
  const int width=map.getDimensions().x;
  for(size_t i=0;i<map.getTileCount();++i) {
    const Coord p{int(i%width),int(i/width)};auto& c=map.utility(p);auto& tile=map.getTile(p);
    tile.connectedToWater=r.waterConnected[i];tile.connectedToSewer=r.sewerConnected[i];
    c.waterDemand=r.demand[i];c.waterDelivered=r.delivered[i];c.sewageHandled=r.handled[i];c.liftFlow=float(r.liftUsed[i]);c.liftPowered=r.liftPowered[i];
    c.boosterFlow=float(r.boosterUsed[i]);c.boosterPowered=r.boosterPowered[i];c.waterPressure=r.waterPressure[i];
  }
}
void UtilityNetworkSystem::refresh(CityMap& map,const EntityStore& store,const std::vector<ServiceFacility>& facilities) {
  if(map.pipeNetworksEnabled()) applyDiagnostics(map,evaluate(map,store,facilities));
}

int64_t UtilityNetworkSystem::operatingCost(const CityMap& map) {
  int64_t cost=0;const auto dims=map.getDimensions();
  for(int y=0;y<dims.y;++y) for(int x=0;x<dims.x;++x) cost+=(map.utility({x,y}).sewageLift?liftUpkeep:0)+(map.utility({x,y}).waterBooster?boosterUpkeep:0);
  return cost;
}
