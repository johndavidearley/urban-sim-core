#include "src/systems/UtilityNetworkSystem.hpp"
#include "src/systems/EnvironmentSystem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace {
constexpr Coord directions[]={{-1,0},{1,0},{0,-1},{0,1}};
uint32_t hash(uint32_t value) {
  value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;return value^(value>>16);
}
std::vector<Coord> shore(const CityMap& map,Coord position) {
  std::vector<Coord> result;
  for(Coord d:directions) {
    const Coord p{position.x+d.x,position.y+d.y};
    if(map.isValid(p) && map.getTile(p).type==2) result.push_back(p);
  }
  return result;
}
double purity(const EnvironmentCell& cell) {
  if(cell.water<=1e-9) return 0.;
  return std::clamp(1.-20.*cell.contaminant/cell.water,0.,1.);
}
struct IntakeShare {size_t facility;Coord tile;double requested,raw=0.,clean=0.;};
std::vector<IntakeShare> shares(const CityMap& map,const std::vector<ServiceFacility>& facilities) {
  const auto dims=map.getDimensions();
  std::vector<double> requested(map.getTileCount(),0.);
  std::vector<IntakeShare> result;
  for(size_t i=0;i<facilities.size();++i) {
    const auto& f=facilities[i];if(f.type!=ServiceType::Water || !f.surfaceWater) continue;
    const auto cells=shore(map,f.position);if(cells.empty()) continue;
    for(Coord p:cells) {
      const double demand=std::max(0.f,f.waterSupplyUnits)/cells.size();
      result.push_back({i,p,demand});requested[static_cast<size_t>(p.y)*dims.x+p.x]+=demand;
    }
  }
  for(auto& s:result) {
    const auto& cell=map.environment(s.tile);
    const auto total=requested[static_cast<size_t>(s.tile.y)*dims.x+s.tile.x];
    s.raw=s.requested*(total>0 ? std::min(1.,cell.water/total) : 0.);
    s.clean=s.raw*purity(cell);
  }
  return result;
}
void removeWater(EnvironmentCell& cell,double volume,EnvironmentState& state,bool withdrawal) {
  volume=std::clamp(volume,0.,cell.water);
  const double mass=cell.water>0 ? cell.contaminant*volume/cell.water : 0.;
  cell.water-=volume;cell.contaminant-=mass;state.contaminantOut+=mass;
  if(withdrawal) state.withdrawal+=volume;else state.outflow+=volume;
}
void advectAir(CityMap& map) {
  const auto dims=map.getDimensions();std::vector<double> next(map.getTileCount(),0.);
  for(int z=0;z<dims.y;++z) for(int x=0;x<dims.x;++x) {
    const auto& cell=map.environment({x,z});
    const double ax=std::min(.18f,std::abs(cell.windX)*.008f),az=std::min(.18f,std::abs(cell.windZ)*.008f);
    const double mass=map.pollution({x,z});const auto i=static_cast<size_t>(z)*dims.x+x;
    next[i]+=mass*(1-ax-az);
    const auto send=[&](int nx,int nz,double amount) {
      if(map.isValid({nx,nz})) next[static_cast<size_t>(nz)*dims.x+nx]+=amount;
      else next[i]+=amount; // Retain attempted boundary exports in the finite domain.
    };
    send(x+(cell.windX>=0 ? 1 : -1),z,mass*ax);send(x,z+(cell.windZ>=0 ? 1 : -1),mass*az);
  }
  for(int z=0;z<dims.y;++z) for(int x=0;x<dims.x;++x)
    map.pollution({x,z})=static_cast<float>(std::clamp(next[static_cast<size_t>(z)*dims.x+x],0.,1.));
}
}

void EnvironmentSystem::initialize(CityMap& map,uint32_t seed) {
  auto& state=map.environment();if(state.initialized) return;
  state={};state.initialized=true;state.seed=seed;
  const auto dims=map.getDimensions();
  for(int z=0;z<dims.y;++z) for(int x=0;x<dims.x;++x) {
    auto& cell=map.environment({x,z});const auto type=map.getTile({x,z}).type;
    const float slope=8.f*(1.f-float(z)/std::max(1,dims.y-1));
    // Top-to-bottom hydraulic datum gives generated rivers a downstream
    // direction. Coherent land relief is stamped by TerrainGenerator.
    if(type==2) {cell.terrainMeters=slope;cell.bedMeters=slope-2.f;cell.water=2*cellArea;}
    else {cell.terrainMeters=slope+(type==1 ? 12.f : 1.f);cell.bedMeters=cell.terrainMeters;cell.water=0;}
    cell.contaminant=0;cell.currentX=cell.currentZ=0;
  }
  EntityStore empty;updateWind(map,empty);
}
void EnvironmentSystem::updateWind(CityMap& map,const EntityStore& store) {
  auto& state=map.environment();const uint32_t epoch=static_cast<uint32_t>(state.tick/48);
  const auto weather=[&](uint32_t e) {
    const auto value=hash(state.seed+e*0x9e3779b9u);
    float speed=2.f+float(value%1400u)*.01f;
    if((value>>20)%37u==0u) speed=26.f+float(value%600u)*.01f;
    const float angle=float((value>>12)%6283u)*.001f;
    return glm::vec2{speed*std::cos(angle),speed*std::sin(angle)};
  };
  float blend=float(state.tick%48)/48;blend=blend*blend*(3-2*blend);
  const auto a=weather(epoch),b=weather(epoch+1);const auto wind=a+(b-a)*blend;
  state.windX=wind.x;state.windZ=wind.y;
  const float speed=std::sqrt(wind.x*wind.x+wind.y*wind.y);
  const float ux=speed>.001f ? wind.x/speed : 0,uz=speed>.001f ? wind.y/speed : 0;
  const auto dims=map.getDimensions();
  for(int z=0;z<dims.y;++z) for(int x=0;x<dims.x;++x) {
    auto& cell=map.environment({x,z});float shelter=0;
    for(int distance=1;distance<=4;++distance) {
      const Coord p{x-int(std::lround(ux*distance)),z-int(std::lround(uz*distance))};
      if(!map.isValid(p)) continue;
      shelter+=std::max(0.f,map.environment(p).terrainMeters-cell.terrainMeters)*.015f/distance;
      if(const auto* building=store.getBuilding(map.getTile(p).buildingId))
        shelter+=std::min(.35f,.035f*std::log2(float(std::max(1,building->capacity))))/distance;
    }
    const float exposure=std::clamp(1.f+std::max(0.f,cell.terrainMeters)*.004f-shelter,.2f,1.35f);
    cell.windX=wind.x*exposure;cell.windZ=wind.y*exposure;
  }
}
float EnvironmentSystem::windPowerFactor(float speed) {
  if(!std::isfinite(speed) || speed<3.f || speed>=25.f) return 0;
  if(speed>=12.f) return 1;
  return (speed*speed*speed-27.f)/(1728.f-27.f);
}
float EnvironmentSystem::windPowerFactor(const CityMap& map,Coord p) {
  if(!map.environment().initialized) return 1;
  if(!map.isValid(p)) return 0;
  const auto& cell=map.environment(p);return windPowerFactor(std::sqrt(cell.windX*cell.windX+cell.windZ*cell.windZ));
}
float EnvironmentSystem::cleanFraction(const CityMap& map,Coord p) {
  const auto cells=shore(map,p);double water=0,mass=0;
  for(Coord s:cells) {water+=map.environment(s).water;mass+=map.environment(s).contaminant;}
  return water>1e-9 ? float(std::clamp(1.-20.*mass/water,0.,1.)) : 0;
}
float EnvironmentSystem::drainageFactor(const CityMap& map,Coord p) {
  if(!map.environment().initialized) return 1;
  if(!map.isValid(p)) return 0;
  float best=0;
  for(Coord s:shore(map,p)) {
    const auto& cell=map.environment(s);
    const double head=cell.bedMeters+cell.water/cellArea;
    best=std::max(best,float(std::clamp((map.environment(p).terrainMeters-head)/.5,0.,1.)));
  }
  return best;
}
std::vector<float> EnvironmentSystem::waterCapacities(const CityMap& map,const std::vector<ServiceFacility>& facilities) {
  std::vector<float> result(facilities.size(),0);
  for(size_t i=0;i<facilities.size();++i) if(facilities[i].type==ServiceType::Water && !facilities[i].surfaceWater)
    result[i]=std::max(0.f,facilities[i].waterSupplyUnits);
  for(const auto& s:shares(map,facilities)) result[s.facility]+=float(s.clean);
  return result;
}

void EnvironmentSystem::step(CityMap& map,const RoadNetwork& roads,const EntityStore& store,
    const std::vector<ServiceFacility>& facilities,uint32_t seed,bool transportAir) {
  initialize(map,seed);auto& state=map.environment();++state.tick;
  updateWind(map,store);
  state.windPhase=std::fmod(state.windPhase+std::sqrt(state.windX*state.windX+state.windZ*state.windZ)*.09f,6.2831853f);
  state.inflow=state.outflow=state.withdrawal=state.discharge=0;
  state.contaminantIn=state.contaminantOut=state.contaminantDecay=0;
  state.sewageGenerated=state.sewageHandled=0;
  const auto dims=map.getDimensions();const auto count=map.getTileCount();
  const auto index=[&](Coord p) {return static_cast<size_t>(p.y)*dims.x+p.x;};
  // Apply boundary reservoirs before computing simultaneous conservative fluxes.
  for(int x=0;x<dims.x;++x) {
    if(map.getTile({x,0}).type==2) {
      auto& cell=map.environment({x,0});const double incoming=std::max(0.,2*cellArea*state.inflowScale-cell.water);
      cell.water+=incoming;state.inflow+=incoming;
    }
    if(dims.y>1 && map.getTile({x,dims.y-1}).type==2) {
      auto& cell=map.environment({x,dims.y-1});removeWater(cell,std::max(0.,cell.water-2*cellArea),state,false);
    }
  }
  struct Flux {Coord from,to;double volume;};std::vector<Flux> fluxes;
  std::vector<double> outgoing(count,0),waterDelta(count,0),massDelta(count,0),flowX(count,0),flowZ(count,0);
  for(int z=0;z<dims.y;++z) for(int x=0;x<dims.x;++x) if(map.getTile({x,z}).type==2) {
    const Coord p{x,z};const auto& a=map.environment(p);
    for(Coord d:{Coord{1,0},Coord{0,1}}) {
      const Coord n{x+d.x,z+d.y};if(!map.isValid(n)||map.getTile(n).type!=2) continue;
      const auto& b=map.environment(n);
      const double difference=a.bedMeters+a.water/cellArea-b.bedMeters-b.water/cellArea;
      const Coord from=difference>=0 ? p : n,to=difference>=0 ? n : p;
      const double amount=std::abs(difference)*600.;
      fluxes.push_back({from,to,amount});outgoing[index(from)]+=amount;
      // Small conservative mixing, much weaker than downstream advection.
      const double ca=a.water>1e-9 ? a.contaminant/a.water : 0,cb=b.water>1e-9 ? b.contaminant/b.water : 0;
      const double mixed=.002*std::min(a.water,b.water)*(ca-cb);
      massDelta[index(p)]-=mixed;massDelta[index(n)]+=mixed;
    }
  }
  for(const auto& f:fluxes) {
    const auto& donor=map.environment(f.from);
    const double scale=outgoing[index(f.from)]>0 ? std::min(1.,donor.water*.35/outgoing[index(f.from)]) : 0;
    const double volume=f.volume*scale,mass=donor.water>1e-9 ? donor.contaminant*volume/donor.water : 0;
    waterDelta[index(f.from)]-=volume;waterDelta[index(f.to)]+=volume;
    massDelta[index(f.from)]-=mass;massDelta[index(f.to)]+=mass;
    const double dx=(f.to.x-f.from.x)*volume*.5,dz=(f.to.y-f.from.y)*volume*.5;
    for(Coord p:{f.from,f.to}) {flowX[index(p)]+=dx;flowZ[index(p)]+=dz;}
  }
  for(int z=0;z<dims.y;++z) for(int x=0;x<dims.x;++x) {
    auto& cell=map.environment({x,z});const auto i=index({x,z});
    if(map.getTile({x,z}).type!=2) {cell.water=cell.contaminant=0;cell.currentX=cell.currentZ=0;continue;}
    cell.water=std::max(0.,cell.water+waterDelta[i]);cell.contaminant=std::max(0.,cell.contaminant+massDelta[i]);
    const double decay=cell.contaminant*.002;cell.contaminant-=decay;state.contaminantDecay+=decay;
    cell.currentX=cell.water>1e-9 ? float(flowX[i]/cell.water) : 0;cell.currentZ=cell.water>1e-9 ? float(flowZ[i]/cell.water) : 0;
  }
  // All intakes share a single per-cell availability budget, independent of
  // facility order. Reads/previews use exactly the same allocation.
  auto intakeShares=shares(map,facilities);const auto capacities=waterCapacities(map,facilities);
  std::vector<EntityId> buildingIds;buildingIds.reserve(store.getBuildingCount());
  for(const auto& [id,b]:store.getBuildings()) {(void)b;buildingIds.push_back(id);}
  std::sort(buildingIds.begin(),buildingIds.end());
  double demand=0;for(auto id:buildingIds) {
    const auto& b=*store.getBuilding(id);
    if(map.isValid(b.position) && map.getTile(b.position).connectedToWater) demand+=utilityDemandFor(b);
  }
  const double supply=std::accumulate(capacities.begin(),capacities.end(),0.);
  UtilityNetworkResult network;
  if(map.pipeNetworksEnabled()) {
    network=UtilityNetworkSystem::evaluate(map,store,facilities);
    UtilityNetworkSystem::applyDiagnostics(map,network);
  }
  const double utilization=supply>0 ? std::min(1.,demand/supply) : 0;
  // Remove donor mass at a fixed concentration; sequential users cannot
  // overdraw or change the concentration seen by the next intake.
  for(const auto& s:intakeShares) {
    const double used=map.pipeNetworksEnabled() ?
      (capacities[s.facility]>0 ? network.sourceUsed[s.facility]/capacities[s.facility] : 0) : utilization;
    removeWater(map.environment(s.tile),s.raw*used,state,true);
  }
  state.sewageGenerated=map.pipeNetworksEnabled()?network.generated:std::min(demand,supply)*.8;
  if(map.pipeNetworksEnabled()) {
    state.sewageBacklog=network.totalBacklog;
    const int width=dims.x;
    for(size_t i=0;i<count;++i) map.utility({int(i%width),int(i/width)}).sewageBacklog=network.backlog[i];
  } else state.sewageBacklog+=state.sewageGenerated;
  struct Outlet {const ServiceFacility* facility;double capacity;size_t index;};std::vector<Outlet> outlets;
  double drainage=0;
  for(size_t i=0;i<facilities.size();++i) {
    const auto& f=facilities[i];if(f.type!=ServiceType::Sanitation || f.quality<=0) continue;
    Coord anchor;if(!map.pipeNetworksEnabled() && !roads.resolveRoadAnchor(f.position,anchor)) continue;
    const double capacity=100.*std::clamp(f.quality,0.f,1.f)*(f.surfaceWater ? drainageFactor(map,f.position) : 1.f);
    outlets.push_back({&f,capacity,i});drainage+=capacity;
  }
  const double handled=map.pipeNetworksEnabled()?network.discharged:std::min(state.sewageBacklog,drainage);
  state.sewageHandled=handled;if(!map.pipeNetworksEnabled()) state.sewageBacklog-=handled;
  for(const auto& outlet:outlets) {
    const auto& f=*outlet.facility;const double volume=map.pipeNetworksEnabled()?network.outletUsed[outlet.index]:(drainage>0 ? handled*outlet.capacity/drainage : 0);
    if(!f.surfaceWater || volume<=0) continue;
    const auto cells=shore(map,f.position);if(cells.empty()) continue;
    for(Coord p:cells) {
      auto& cell=map.environment(p);const double added=volume/cells.size(),mass=added*(f.sewageTreatment ? .02 : 1.);
      cell.water+=added;cell.contaminant+=mass;state.discharge+=added;state.contaminantIn+=mass;
    }
  }
  if(transportAir) advectAir(map);
}

void EnvironmentSystem::transportAir(CityMap& map) { advectAir(map); }
