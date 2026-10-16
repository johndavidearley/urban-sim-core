#include "TerminalTool.hpp"
#include <algorithm>
#include "src/networks/RoadNetwork.hpp"
#include "src/networks/Pathfinding.hpp"

namespace {
bool fail(std::string* error,const char* message) {if (error) *error=message; return false;}
}
namespace regional {
const TerminalSpec* terminalSpec(Kind kind) {
  static constexpr TerminalSpec cargo{2,1,15000,500,20,0,200};
  static constexpr TerminalSpec passenger{2,1,10000,500,10,60,0};
  static constexpr TerminalSpec airport{4,2,25000,1000,30,120,0};
  switch (kind) {
    case Kind::CargoPort:return &cargo;
    case Kind::PassengerTerminal:return &passenger;
    case Kind::Airport:return &airport;
    default:return nullptr;
  }
}
}
TerminalPlan TerminalTool::plan(const CityMap& map,const RoadNetwork& roads,
  const std::vector<regional::Connection>& connections,Coord origin,regional::Kind kind,
  uint64_t nextId,const std::vector<Coord>& civicSites,int64_t funds) {
  TerminalPlan p; p.connection.origin=origin; p.connection.kind=kind;
  const auto* spec=regional::terminalSpec(kind);
  if (!spec) {p.error="choose a constructed terminal type";return p;}
  p.connection.width=spec->width; p.connection.height=spec->height;
  if (!map.isValid(origin)) {p.error="terminal outside map";return p;}
  for (int y=0;y<spec->height;++y) for (int x=0;x<spec->width;++x)
    p.tiles.push_back({origin.x+x,origin.y+y});
  p.cost=spec->constructionCost;
  if (!nextId || nextId>UINT32_MAX) {p.error="regional connection IDs exhausted";return p;}
  p.connection.id=static_cast<uint32_t>(nextId);
  p.connection.goodsPerTick=spec->goods; p.connection.passengersPerTick=spec->passengers;
  std::vector<Coord> entrances;
  for (int x=0;x<spec->width;++x) entrances.push_back({origin.x+x,origin.y+spec->height});
  for (int x=0;x<spec->width;++x) entrances.push_back({origin.x+x,origin.y-1});
  for (int y=0;y<spec->height;++y) {
    entrances.push_back({origin.x-1,origin.y+y}); entrances.push_back({origin.x+spec->width,origin.y+y});
  }
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

bool TerminalTool::build(CityMap& map,const RoadNetwork& roads,
  std::vector<regional::Connection>& connections,regional::Connection c,
  Coord destination,const std::vector<Coord>& civicSites,int64_t& funds,std::string* error) {
  if (error) error->clear();
  const auto* spec=regional::terminalSpec(c.kind);
  if (!spec || c.width!=spec->width || c.height!=spec->height)
    return fail(error,"invalid terminal footprint");
  c.constructed=false;
  c.passengersPerTick=spec->passengers; c.goodsPerTick=spec->goods;
  if (!RegionalConnectionTool::place(c,map,roads,destination,civicSites,spec->constructionCost,connections,funds,error)) return false;
  const auto stored=std::find_if(connections.begin(),connections.end(),[&](const auto& r){return r.id==c.id;});
  stored->constructed=true;
  for (int y=c.origin.y;y<c.origin.y+c.height;++y)
    for (int x=c.origin.x;x<c.origin.x+c.width;++x) map.getTile({x,y}).regionalConnectionId=c.id;
  return true;
}

bool TerminalTool::remove(CityMap& map,std::vector<regional::Connection>& connections,
  uint32_t id,regional::Kind expectedKind,int64_t& funds,std::string* error) {
  if (error) error->clear();
  const auto found=std::find_if(connections.begin(),connections.end(),[&](const auto& c){return c.id==id;});
  if (found==connections.end() || !found->constructed || found->kind!=expectedKind ||
      !regional::terminalSpec(found->kind)) return fail(error,"unknown constructed terminal");
  const auto c=*found;
  const auto cost=regional::terminalSpec(c.kind)->demolitionCost;
  if (c.width<=0 || c.height<=0 || !map.isValid(c.origin) ||
      static_cast<int64_t>(c.origin.x)+c.width>map.getDimensions().x ||
      static_cast<int64_t>(c.origin.y)+c.height>map.getDimensions().y)
    return fail(error,"invalid terminal footprint");
  for (int y=c.origin.y;y<c.origin.y+c.height;++y)
    for (int x=c.origin.x;x<c.origin.x+c.width;++x)
      if (map.getTile({x,y}).regionalConnectionId!=id) return fail(error,"terminal reservation ownership mismatch");
  if (funds<cost) return fail(error,"insufficient demolition funds");
  connections.erase(found);
  funds-=cost;
  for (int y=c.origin.y;y<c.origin.y+c.height;++y)
    for (int x=c.origin.x;x<c.origin.x+c.width;++x) map.getTile({x,y}).regionalConnectionId=0;
  return true;
}
