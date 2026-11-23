#pragma once
#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace visualizer {
// Bounded visual representatives of aggregate flows, never simulation agents.
class CityActivity {
public:
  enum class Kind { Car, Truck, Pedestrian };
  struct Waypoint {float x,z,y,distance;};
  struct Trip { Kind kind; std::vector<Coord> path; float phase,speed; uint32_t color;
    std::vector<Waypoint> geometry{}; };
  struct Actor { Kind kind; float x,z,dx,dz; uint32_t color; float y=0; };
  static constexpr size_t maxCars=192,maxTrucks=64,maxPedestrians=96;
  const std::vector<Trip>& trips() const {return trips_;}
  double seconds() const {return seconds_;}
  void invalidate() {valid_=false;}
  void advance(uint32_t now,bool paused,float speed=1.f) {
    if(clockValid_ && !paused) seconds_+=std::min(now-lastMs_,250u)*.001*speed;
    lastMs_=now;clockValid_=true;
  }
  void refresh(const RoadNetwork& roads,const EntityStore& store,uint32_t tick) {
    if(valid_ && tick==tick_ && topology_==roads.getTopologyVersion() &&
       routing_==roads.getRoutingVersion() && buildings_==store.getMutationVersion()) return;
    const bool sameNetwork=valid_ && topology_==roads.getTopologyVersion() && routing_==roads.getRoutingVersion();
    auto previous=std::move(trips_);
    tick_=tick;topology_=roads.getTopologyVersion();routing_=roads.getRoutingVersion();
    buildings_=store.getMutationVersion();valid_=true;trips_.clear();
    auto edges=roads.getAllEdgeTraffic();
    const auto edgeSeed=[](const auto& e) {
      return uint32_t(e.from.x)*73856093u ^ uint32_t(e.from.y)*19349663u ^
             uint32_t(e.to.x)*83492791u ^ uint32_t(e.to.y)*2654435761u;
    };
    // Stable spatial sampling spreads the bounded population over the city,
    // instead of exhausting the budget on its northernmost streets.
    std::sort(edges.begin(),edges.end(),[&](const auto& a,const auto& b) {
      const auto x=edgeSeed(a),y=edgeSeed(b);
      if(x!=y) return x<y;
      return std::tie(a.from.y,a.from.x,a.to.y,a.to.x)<std::tie(b.from.y,b.from.x,b.to.y,b.to.x);
    });
    std::unordered_set<Coord,Vec2Hash> occupied;
    for(const auto& [id,b]:store.getBuildings()) {
      (void)id;
      Coord anchor;
      if(b.occupancy>0 && roads.resolveRoadAnchor(b.position,anchor)) occupied.insert(anchor);
    }
    std::unordered_map<Coord,std::vector<Coord>,Vec2Hash> driving,hauling,walking;
    const auto supported=[&](Coord a,Coord b) {
      // Curved alignments and separated decks are handled with their own lane geometry.
      return a.y>=0 && b.y>=0 && std::abs(a.x-b.x)+std::abs(a.y-b.y)==1 &&
        !roads.alignmentOwnsEdge(a,b);
    };
    for(const auto& e:edges) if(supported(e.from,e.to)) {
      if(e.roadClass!=RoadClass::Highway && e.roadClass!=RoadClass::Ramp) {
        walking[e.from].push_back(e.to);walking[e.to].push_back(e.from);
      }
      for(auto* graph:{&driving,&hauling}) {
        const bool loaded=graph==&hauling ? e.freightLoad>0 : e.currentLoad>e.freightLoad;
        if(!loaded) continue;
        if(roads.canTravel(e.from,e.to)) (*graph)[e.from].push_back(e.to);
        if(roads.canTravel(e.to,e.from)) (*graph)[e.to].push_back(e.from);
      }
    }
    for(auto* graph:{&driving,&hauling,&walking}) for(auto& [p,next]:*graph) {
      (void)p;
      std::sort(next.begin(),next.end(),[](Coord a,Coord b){return std::tie(a.y,a.x)<std::tie(b.y,b.x);});
      next.erase(std::unique(next.begin(),next.end()),next.end());
    }
    const auto route=[&](Coord a,Coord b,const auto& graph,uint32_t seed) {
      std::vector<Coord> path{a,b};
      for(size_t hop=0;hop<10;++hop) {
        const auto found=graph.find(path.back());if(found==graph.end()) break;
        size_t count=0;
        for(auto p:found->second) if(p!=path[path.size()-2]) ++count;
        if(!count) break;
        size_t selected=(seed+uint32_t(hop)*17u)%count;
        const auto previous=path[path.size()-2];
        for(auto p:found->second) if(p!=previous && selected--==0) {path.push_back(p);break;}
      }
      return path;
    };
    size_t cars=0,trucks=0,people=0;
    // Special roads use the same Bezier centerlines and height profile as rendering.
    std::unordered_map<RoadNetwork::EdgeKey,RoadNetwork::EdgeTrafficInfo,RoadNetwork::EdgeKeyHash> loads;
    for(const auto& e:edges) loads.emplace(RoadNetwork::EdgeKey{e.from,e.to},e);
    for(const auto& alignment:roads.alignments()) {
      if(alignment.curves.empty() || alignment.path.size()<2) continue;
      const auto nodes=alignment.routingPath();
      float carLoad=0,truckLoad=0,congestion=0;
      bool inhabited=false;
      for(auto p:nodes) inhabited|=occupied.count(p)>0;
      for(size_t i=1;i<nodes.size();++i) {
        const auto found=loads.find({nodes[i-1],nodes[i]});if(found==loads.end()) continue;
        carLoad=std::max(carLoad,found->second.currentLoad-found->second.freightLoad);
        truckLoad=std::max(truckLoad,found->second.freightLoad);
        congestion=std::max(congestion,found->second.congestion);
      }
      const uint32_t seed=uint32_t(alignment.path.front().x)*73856093u ^ uint32_t(alignment.path.front().y)*19349663u;
      const auto add=[&](Kind kind,size_t& count,size_t limit,float speed) {
        if(count>=limit) return;
        Trip trip{kind,nodes,float(seed%4093u)/4093.f,speed,seed};
        float distance=0;
        for(size_t ci=0;ci<alignment.curves.size();++ci) {
          const auto& curve=alignment.curves[ci];
          const int steps=std::min(64,curve.subdivisions());
          for(int i=ci?1:0;i<=steps;++i) {
            const float t=float(i)/steps;
            const auto point=curve.point(t);
            if(!trip.geometry.empty()) {
              const auto& prev=trip.geometry.back();const float dx=point.x-prev.x,dz=point.y-prev.z;
              distance+=std::sqrt(dx*dx+dz*dz);
            }
            trip.geometry.push_back({point.x,point.y,alignment.height((float(ci)+t)/alignment.curves.size()),distance});
          }
        }
        if(distance>.001f) {trips_.push_back(std::move(trip));++count;}
      };
      if(carLoad>0) add(Kind::Car,cars,maxCars/3,.75f/(1+congestion));
      if(truckLoad>0) add(Kind::Truck,trucks,maxTrucks/3,.55f/(1+congestion));
      if(inhabited && alignment.elevation==0 && alignment.roadClass!=RoadClass::Highway && alignment.roadClass!=RoadClass::Ramp)
        add(Kind::Pedestrian,people,maxPedestrians/3,.17f);
    }
    for(const auto& e:edges) {
      if(!supported(e.from,e.to)) continue;
      const uint32_t seed=edgeSeed(e);
      Coord a=e.from,b=e.to;
      if(!roads.canTravel(a,b) || (!e.oneWay && (seed&1u))) std::swap(a,b);
      if(e.currentLoad>0 && roads.canTravel(a,b)) {
        const auto add=[&](Kind kind,size_t& count,size_t limit,int wanted,float speed) {
          if(count>=limit) return;
          const auto path=route(a,b,kind==Kind::Truck ? hauling : driving,seed);
          for(int i=0;i<wanted && count<limit;++i,++count)
            trips_.push_back({kind,path,float((seed+uint32_t(i)*997u)%4093u)/4093.f,speed,seed+uint32_t(i)});
        };
        if(e.currentLoad>e.freightLoad)
          add(Kind::Car,cars,maxCars,std::min(3,1+int((e.currentLoad-e.freightLoad)/32.f)),.75f/(1.f+e.congestion));
        if(e.freightLoad>0) add(Kind::Truck,trucks,maxTrucks,1,.55f/(1.f+e.congestion));
      }
      if(people<maxPedestrians && e.roadClass!=RoadClass::Highway && e.roadClass!=RoadClass::Ramp &&
         (occupied.count(a)||occupied.count(b))) {
        const auto path=route(a,b,walking,seed);
        trips_.push_back({Kind::Pedestrian,path,float(seed%4093u)/4093.f,.17f,seed});++people;
      }
    }
    // A new traffic measurement may change speed, but must not teleport an
    // existing representative backward/forward along its unchanged route.
    if(sameNetwork) {
      std::unordered_multimap<uint64_t,const Trip*> old;
      for(const auto& trip:previous) old.emplace((uint64_t(trip.kind)<<32)|trip.color,&trip);
      for(auto& trip:trips_) {
        const auto range=old.equal_range((uint64_t(trip.kind)<<32)|trip.color);
        for(auto it=range.first;it!=range.second;++it) if(it->second->path==trip.path) {
          const auto& before=*it->second;
          const float length=trip.geometry.empty() ? float(trip.path.size()-1) : trip.geometry.back().distance;
          double phase=before.phase+seconds_*(before.speed-trip.speed)/length;
          phase-=std::floor(phase);trip.phase=float(phase);break;
        }
      }
    }
  }
  std::vector<Actor> actors() const {
    std::vector<Actor> result;result.reserve(trips_.size());
    for(const auto& trip:trips_) {
      if(!trip.geometry.empty()) {
        const float length=trip.geometry.back().distance;
        const float distance=float(std::fmod(seconds_*trip.speed+trip.phase*length,length));
        const auto next=std::upper_bound(trip.geometry.begin(),trip.geometry.end(),distance,
          [](float value,const Waypoint& p){return value<p.distance;});
        if(next==trip.geometry.end() || next==trip.geometry.begin()) continue;
        const auto& a=*(next-1);const auto& b=*next;
        const float span=std::max(.00001f,b.distance-a.distance),t=(distance-a.distance)/span;
        const float dx=(b.x-a.x)/span,dz=(b.z-a.z)/span;
        const float side=trip.kind==Kind::Pedestrian ? .39f : .14f;
        result.push_back({trip.kind,a.x+(b.x-a.x)*t-dz*side,a.z+(b.z-a.z)*t+dx*side,
          dx,dz,trip.color,a.y+(b.y-a.y)*t});
        continue;
      }
      const float length=float(trip.path.size()-1);
      const float progress=float(std::fmod(seconds_*trip.speed+trip.phase*length,length));
      const auto segment=std::min(size_t(progress),trip.path.size()-2);
      const float t=progress-float(segment);
      const auto a=trip.path[segment],b=trip.path[segment+1];
      const float dx=float(b.x-a.x),dz=float(b.y-a.y);
      auto normal=[&](size_t i) {
        const auto u=trip.path[i],v=trip.path[i+1];return glm::vec2{float(-(v.y-u.y)),float(v.x-u.x)};
      };
      const auto n=normal(segment);
      const auto start=segment ? (normal(segment-1)+n)*.5f : n;
      const auto end=segment+2<trip.path.size() ? (n+normal(segment+1))*.5f : n;
      const float side=trip.kind==Kind::Pedestrian ? .39f : .14f;
      result.push_back({trip.kind,a.x+.5f+dx*t+side*(start.x+(end.x-start.x)*t),
        a.y+.5f+dz*t+side*(start.y+(end.y-start.y)*t),dx,dz,trip.color});
    }
    return result;
  }
private:
  bool valid_=false,clockValid_=false;
  uint32_t tick_=0,lastMs_=0;
  uint64_t topology_=0,routing_=0,buildings_=0;
  double seconds_=0;
  std::vector<Trip> trips_;
};
}
