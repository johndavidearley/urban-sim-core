#include "PortTool.hpp"
#include <algorithm>
namespace {
bool maritime(regional::Kind k) {return k==regional::Kind::CargoPort || k==regional::Kind::PassengerTerminal;}
}
PortPlan PortTool::plan(const CityMap& map,const RoadNetwork& roads,
  const std::vector<regional::Connection>& connections,Coord origin,regional::Kind kind,
  uint64_t nextId,const std::vector<Coord>& civicSites,int64_t funds) {
  if (!maritime(kind)) {PortPlan p;p.error="choose cargo or passenger port";return p;}
  return TerminalTool::plan(map,roads,connections,origin,kind,nextId,civicSites,funds);
}
bool PortTool::build(CityMap& map,const RoadNetwork& roads,
  std::vector<regional::Connection>& connections,regional::Connection c,
  Coord destination,const std::vector<Coord>& civicSites,int64_t& funds,std::string* error) {
  if (!maritime(c.kind)) {if(error)*error="choose cargo or passenger port";return false;}
  return TerminalTool::build(map,roads,connections,c,destination,civicSites,funds,error);
}
bool PortTool::remove(CityMap& map,std::vector<regional::Connection>& connections,
  uint32_t id,int64_t& funds,std::string* error) {
  const auto found=std::find_if(connections.begin(),connections.end(),[&](const auto& c){return c.id==id;});
  if (found==connections.end() || !maritime(found->kind)) {if(error)*error="unknown constructed port";return false;}
  return TerminalTool::remove(map,connections,id,found->kind,funds,error);
}
