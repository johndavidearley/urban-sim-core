#include "RegionalConnectionTool.hpp"
#include <algorithm>
#include <exception>

namespace {
bool fail(std::string* error,const std::string& message) {
  if (error) *error=message;
  return false;
}
bool contains(const regional::Connection& c,Coord p) {
  return p.x>=c.origin.x && p.y>=c.origin.y &&
    static_cast<int64_t>(p.x)<static_cast<int64_t>(c.origin.x)+c.width &&
    static_cast<int64_t>(p.y)<static_cast<int64_t>(c.origin.y)+c.height;
}
}

bool RegionalConnectionTool::canPlace(const regional::Connection& c,const CityMap& map,
  const RoadNetwork& roads,Coord destination,
  const std::vector<Coord>& civicSites,
  int64_t cost,const std::vector<regional::Connection>& registry,int64_t funds,std::string* error) {
  if (error) error->clear();
  if (error) error->clear();
  if (cost<0 || funds<0) return fail(error,"invalid construction funds/cost");
  if (c.constructed) return fail(error,"constructed terminals require their physical placement tool");
  if (registry.size()>=4096) return fail(error,"regional connection limit reached");
  if (c.kind==regional::Kind::RoadGateway)
    return fail(error,"road gateways are not constructed as terminals");
  if (std::any_of(registry.begin(),registry.end(),[&](const auto& r) {return r.id==c.id;}))
    return fail(error,"duplicate regional connection ID");
  try { regional::allocate(0,{},{{c,regional::Access::Disabled}}); }
  catch (const std::exception& e) { return fail(error,e.what()); }
  const regional::SeaAccess sea(map);
  const auto access=regional::diagnoseSite(c,map,roads,sea,destination,registry);
  if (access!=regional::Access::Ready && access!=regional::Access::Disabled)
    return fail(error,regional::accessName(access));
  for (const auto& site:civicSites)
    if (contains(c,site)) return fail(error,"site overlaps a civic facility");
  for (const auto& other:registry)
    if (contains(c,other.entrance) || contains(other,c.entrance))
      return fail(error,"site blocks a reserved road entrance");
  if (funds<cost) return fail(error,"insufficient construction funds");
  return true;
}

bool RegionalConnectionTool::place(const regional::Connection& c,const CityMap& map,
  const RoadNetwork& roads,Coord destination,const std::vector<Coord>& civicSites,
  int64_t cost,std::vector<regional::Connection>& registry,int64_t& funds,std::string* error) {
  if (!canPlace(c,map,roads,destination,civicSites,cost,registry,funds,error)) return false;
  // Push before charging: allocation failure cannot leave spent cash.
  registry.push_back(c);
  std::sort(registry.begin(),registry.end(),[](const auto& a,const auto& b){return a.id<b.id;});
  funds-=cost;
  return true;
}

bool RegionalConnectionTool::remove(uint32_t id,int64_t cost,
  std::vector<regional::Connection>& registry,int64_t& funds,std::string* error) {
  if (error) error->clear();
  if (cost<0 || funds<0) return fail(error,"invalid demolition funds/cost");
  const auto found=std::find_if(registry.begin(),registry.end(),[&](const auto& c){return c.id==id;});
  if (!id || found==registry.end()) return fail(error,"unknown regional connection ID");
  if (found->constructed) return fail(error,"constructed terminals require their physical removal tool");
  if (funds<cost) return fail(error,"insufficient demolition funds");
  registry.erase(found);
  funds-=cost;
  return true;
}
