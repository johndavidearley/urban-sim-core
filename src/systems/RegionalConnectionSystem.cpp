#include "RegionalConnectionSystem.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include "src/networks/RoadNetwork.hpp"
#include "src/networks/Pathfinding.hpp"

namespace regional {
namespace {
constexpr Coord directions[] = {{1,0},{-1,0},{0,1},{0,-1}};
bool validBounds(const Connection& c, const CityMap& map) {
  const auto size = map.getDimensions();
  return c.width > 0 && c.height > 0 && c.origin.x >= 0 && c.origin.y >= 0 &&
    static_cast<int64_t>(c.origin.x)+c.width <= size.x &&
    static_cast<int64_t>(c.origin.y)+c.height <= size.y;
}
bool contains(const Connection& c, Coord p) {
  return p.x >= c.origin.x && p.y >= c.origin.y &&
    static_cast<int64_t>(p.x) < static_cast<int64_t>(c.origin.x)+c.width &&
    static_cast<int64_t>(p.y) < static_cast<int64_t>(c.origin.y)+c.height;
}
bool seaKind(Kind k) { return k == Kind::CargoPort || k == Kind::PassengerTerminal; }
}

SeaAccess::SeaAccess(const CityMap& map) {
  const auto size = map.getDimensions(); width_=size.x; height_=size.y;
  reachable_.assign(map.getTileCount(),false);
  std::queue<Coord> pending;
  auto visit = [&](Coord p) {
    if (!map.isValid(p) || map.getTile(p).type != 2) return;
    const auto index = static_cast<size_t>(p.y)*width_+p.x;
    if (!reachable_[index]) { reachable_[index]=true; pending.push(p); }
  };
  for (int x=0;x<width_;++x) { visit({x,0}); visit({x,height_-1}); }
  for (int y=0;y<height_;++y) { visit({0,y}); visit({width_-1,y}); }
  while (!pending.empty()) {
    const auto p=pending.front(); pending.pop();
    for (auto d:directions) visit({p.x+d.x,p.y+d.y});
  }
}
bool SeaAccess::reachesBoundary(Coord p) const {
  return p.x>=0 && p.y>=0 && p.x<width_ && p.y<height_ &&
    reachable_[static_cast<size_t>(p.y)*width_+p.x];
}

namespace {
Access diagnose(const Connection& c, const CityMap& map,
                const RoadNetwork& roads, const SeaAccess& sea,
                Coord destination, const std::vector<Connection>& reservations,
                bool operational) {
  if (operational && !c.constructed) return Access::NotConstructed;
  if (!validBounds(c,map) || (operational && !c.id)) return Access::InvalidFootprint;
  bool shore=false, power=true, water=true;
  for (int y=c.origin.y;y<c.origin.y+c.height;++y)
    for (int x=c.origin.x;x<c.origin.x+c.width;++x) {
      Coord p{x,y}; const auto& tile=map.getTile(p);
      const bool wrongOwner=operational ? tile.regionalConnectionId!=c.id : tile.regionalConnectionId!=0;
      if (tile.type==2 || tile.buildingId || wrongOwner || tile.zone ||
          (tile.hasRoad && c.kind!=Kind::RoadGateway)) return Access::Occupied;
      for (const auto& other:reservations)
        if (contains(other,p)) return Access::Occupied;
      power=power && tile.connectedToPower; water=water && tile.connectedToWater;
      for (auto d:directions) shore=shore || sea.reachesBoundary({p.x+d.x,p.y+d.y});
    }
  bool entrance=false;
  if (!map.isValid(c.entrance)) return Access::MissingEntrance;
  if (c.kind==Kind::RoadGateway) entrance=contains(c,c.entrance);
  else if (!contains(c,c.entrance))
    for (auto d:directions) entrance=entrance || contains(c,{c.entrance.x+d.x,c.entrance.y+d.y});
  if (!entrance || !roads.hasRoadAdjacency(c.entrance)) return Access::MissingEntrance;
  if (!roads.hasRoadAdjacency(destination) ||
      !Pathfinding::findShortestPath(roads,c.entrance,destination).found) return Access::NoRoadPath;
  if (!power) return Access::MissingPower;
  if (!water) return Access::MissingWater;
  if (seaKind(c.kind) && !shore) return Access::NoSeaAccess;
  return c.enabled ? Access::Ready : Access::Disabled;
}
}

Access diagnoseSite(const Connection& c, const CityMap& map,
                    const RoadNetwork& roads, const SeaAccess& sea,
                    Coord destination, const std::vector<Connection>& reservations) {
  return diagnose(c,map,roads,sea,destination,reservations,false);
}

Access diagnoseOperation(const Connection& c, const CityMap& map,
                         const RoadNetwork& roads, const SeaAccess& sea,
                         Coord destination) {
  return diagnose(c,map,roads,sea,destination,{},true);
}

const char* accessName(Access a) {
  switch (a) {
    case Access::Ready: return "ready";
    case Access::Disabled: return "disabled";
    case Access::InvalidFootprint: return "invalid footprint";
    case Access::Occupied: return "occupied site";
    case Access::MissingEntrance: return "missing road entrance";
    case Access::NoRoadPath: return "no road path to destination";
    case Access::MissingPower: return "missing power";
    case Access::MissingWater: return "missing water";
    case Access::NoSeaAccess: return "no boundary-connected water";
    case Access::NotConstructed: return "not constructed";
  }
  return "unknown";
}

Report allocate(uint32_t tick, Demand demand, std::vector<Offer> offers) {
  if (offers.size()>4096) throw std::invalid_argument("too many regional connections");
  std::sort(offers.begin(),offers.end(),[](const Offer& a,const Offer& b) {
    return a.connection.id<b.connection.id;
  });
  Report r; r.tick=tick; r.requested=demand; r.unmet=demand;
  uint32_t previous=0;
  for (const auto& offer:offers) {
    const auto& c=offer.connection;
    if (c.id==0 || c.id==previous) throw std::invalid_argument("invalid regional connection ID");
    previous=c.id;
    if (static_cast<int>(offer.access)<0 || static_cast<int>(offer.access)>static_cast<int>(Access::NotConstructed))
      throw std::invalid_argument("invalid regional access status");
    const bool passengers=c.kind!=Kind::CargoPort;
    const bool cargo=c.kind==Kind::CargoPort || c.kind==Kind::RoadGateway;
    if ((!passengers && c.passengersPerTick) || (!cargo && c.goodsPerTick) ||
        static_cast<int>(c.kind)<0 || static_cast<int>(c.kind)>3)
      throw std::invalid_argument("invalid regional connection capacity/mode");
    Usage u; u.id=c.id; u.access=c.enabled ? offer.access : Access::Disabled;
    u.capacityMeasured=true;
    if (u.access==Access::Ready) {
      u.availablePassengers=c.passengersPerTick;u.availableGoods=c.goodsPerTick;
      u.passengers=std::min(r.unmet.passengers,c.passengersPerTick);
      u.imports=std::min(r.unmet.imports,c.goodsPerTick);
      u.exports=std::min(r.unmet.exports,c.goodsPerTick-u.imports);
      r.unmet.passengers-=u.passengers; r.unmet.imports-=u.imports; r.unmet.exports-=u.exports;
    }
    r.connections.push_back(u);
  }
  r.admitted={demand.passengers-r.unmet.passengers,demand.imports-r.unmet.imports,
              demand.exports-r.unmet.exports};
  return r;
}
} // namespace regional
