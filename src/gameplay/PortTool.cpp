#include "PortTool.hpp"
#include <algorithm>
#include "src/networks/RoadNetwork.hpp"
#include "src/networks/Pathfinding.hpp"

namespace {
bool fail(std::string* error,const char* message) {if (error) *error=message; return false;}
}
PortPlan PortTool::plan(const CityMap& map,const RoadNetwork& roads,
  const std::vector<regional::Connection>& connections,Coord origin,regional::Kind kind,
  uint64_t nextId,const std::vector<Coord>& civicSites,int64_t funds) {
  PortPlan p; p.connection.origin=origin; p.connection.kind=kind;
  p.connection.width=2; p.connection.height=1;
  if (kind!=regional::Kind::CargoPort && kind!=regional::Kind::PassengerTerminal) {
    p.error="choose cargo or passenger port"; return p;
  }
  if (!map.isValid(origin)) {p.error="port outside map";return p;}
  p.tiles={origin,{origin.x+1,origin.y}};
  p.cost=kind==regional::Kind::CargoPort ? kCargoCost : kPassengerCost;
  if (!nextId || nextId>UINT32_MAX) {p.error="regional connection IDs exhausted";return p;}
  p.connection.id=static_cast<uint32_t>(nextId);
  p.connection.goodsPerTick=kind==regional::Kind::CargoPort ? 200u : 0u;
  p.connection.passengersPerTick=kind==regional::Kind::PassengerTerminal ? 60u : 0u;
  const Coord entrances[]={{origin.x,origin.y+1},{origin.x+1,origin.y+1},
    {origin.x,origin.y-1},{origin.x+1,origin.y-1},{origin.x-1,origin.y},{origin.x+2,origin.y}};
  p.error="missing road entrance";
  for (const auto entrance:entrances) {
    if (!map.isValid(entrance) || !roads.hasRoadAdjacency(entrance)) continue;
    p.connection.entrance=entrance; p.destination={-1,-1};
    for (const auto site:civicSites) {
      Coord anchor;
      if (roads.resolveRoadAnchor(site,anchor) &&
          Pathfinding::findShortestPath(roads,entrance,anchor).found) {
        p.destination=anchor; break;
      }
    }
    if (RegionalConnectionTool::canPlace(p.connection,map,roads,p.destination,civicSites,
          p.cost,connections,funds,&p.error)) {p.valid=true;return p;}
  }
  return p;
}

bool PortTool::build(CityMap& map,const RoadNetwork& roads,
  std::vector<regional::Connection>& connections,regional::Connection c,
  Coord destination,const std::vector<Coord>& civicSites,int64_t& funds,std::string* error) {
  if (error) error->clear();
  if ((c.kind!=regional::Kind::CargoPort && c.kind!=regional::Kind::PassengerTerminal) ||
      c.width!=2 || c.height!=1) return fail(error,"port requires a 2x1 maritime footprint");
  c.constructed=false; // Generic registration cannot create physical ownership.
  c.passengersPerTick=c.kind==regional::Kind::PassengerTerminal ? 60u : 0u;
  c.goodsPerTick=c.kind==regional::Kind::CargoPort ? 200u : 0u;
  const auto cost=c.kind==regional::Kind::CargoPort ? kCargoCost : kPassengerCost;
  if (!RegionalConnectionTool::place(c,map,roads,destination,civicSites,cost,connections,funds,error)) return false;
  const auto stored=std::find_if(connections.begin(),connections.end(),[&](const auto& r){return r.id==c.id;});
  stored->constructed=true;
  for (int x=c.origin.x;x<c.origin.x+c.width;++x)
    map.getTile({x,c.origin.y}).regionalConnectionId=c.id;
  return true;
}

bool PortTool::remove(CityMap& map,std::vector<regional::Connection>& connections,
  uint32_t id,int64_t& funds,std::string* error) {
  if (error) error->clear();
  const auto found=std::find_if(connections.begin(),connections.end(),[&](const auto& c){return c.id==id;});
  if (found==connections.end() || !found->constructed ||
      (found->kind!=regional::Kind::CargoPort && found->kind!=regional::Kind::PassengerTerminal))
    return fail(error,"unknown constructed port");
  const auto c=*found;
  if (c.width<=0 || c.height<=0 || !map.isValid(c.origin) ||
      static_cast<int64_t>(c.origin.x)+c.width>map.getDimensions().x ||
      static_cast<int64_t>(c.origin.y)+c.height>map.getDimensions().y)
    return fail(error,"invalid port footprint");
  for (int y=c.origin.y;y<c.origin.y+c.height;++y)
    for (int x=c.origin.x;x<c.origin.x+c.width;++x)
      if (map.getTile({x,y}).regionalConnectionId!=id) return fail(error,"port reservation ownership mismatch");
  if (funds<kDemolitionCost) return fail(error,"insufficient demolition funds");
  connections.erase(found);
  funds-=kDemolitionCost;
  for (int y=c.origin.y;y<c.origin.y+c.height;++y)
    for (int x=c.origin.x;x<c.origin.x+c.width;++x) map.getTile({x,y}).regionalConnectionId=0;
  return true;
}
