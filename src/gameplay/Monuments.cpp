#include "Monuments.hpp"
#include "src/world/CityMap.hpp"

namespace monuments {
bool built(const EntityStore& store,uint32_t kind) {
  for(const auto& entry:store.getBuildings()) if(entry.second.monumentKind==kind) return true;
  return false;
}
bool active(const EntityStore& store,const RoadNetwork& roads,uint32_t kind) {
  for(const auto& entry:store.getBuildings()) if(entry.second.monumentKind==kind) {
    Coord anchor;
    if(roads.resolveRoadAnchor(entry.second.position,anchor)) return true;
  }
  return false;
}
bool eligible(const EntityStore& store,uint32_t kind) {
  const auto* item=spec(kind);if(!item) return false;
  for(auto required:item->prerequisites) if(!uniques::built(store,required)) return false;
  return true;
}
std::string requirements(const EntityStore& store,uint32_t kind,bool missingOnly) {
  std::string result;
  if(const auto* item=spec(kind)) for(auto required:item->prerequisites) {
    if(missingOnly && uniques::built(store,required)) continue;
    if(!result.empty()) result+=" + ";
    result+=uniques::spec(required)->name;
  }
  return result;
}
int64_t upkeep(const EntityStore& store) {
  int64_t result=0;
  for(const auto& entry:store.getBuildings()) if(const auto* item=spec(entry.second.monumentKind)) result+=item->upkeep;
  return result;
}
void appendPower(std::vector<ServiceFacility>& facilities,const EntityStore& store,const RoadNetwork& roads) {
  for(const auto& entry:store.getBuildings()) if(entry.second.monumentKind==3) {
    Coord anchor;if(!roads.resolveRoadAnchor(entry.second.position,anchor)) continue;
    ServiceFacility source;source.type=ServiceType::Power;source.position=entry.second.position;
    source.maxTravelDistance=30;source.powerCapacityMW=powerMW;source.emissionsKgPerMWh=0.f;
    facilities.push_back(source);
  }
}
void configureCoverage(ServiceCoverageCache& cache,const EntityStore& store,const RoadNetwork& roads) {
  const bool health=active(store,roads,1),education=active(store,roads,2);
  if(cache.citywideHealth!=health || cache.citywideEducation!=education)
    cache.cachedStoreMutationVersion=static_cast<uint64_t>(-1);
  cache.citywideHealth=health;cache.citywideEducation=education;
}
bool cleanPollution(CityMap& map,const EntityStore& store,const RoadNetwork& roads) {
  if(!active(store,roads,4)) return false;
  const auto dims=map.getDimensions();
  for(int y=0;y<dims.y;++y) for(int x=0;x<dims.x;++x) map.pollution({x,y})*=cleanupFactor;
  return true;
}
}

MonumentPlan MonumentTool::plan(const CityMap& map,const RoadNetwork& roads,const EntityStore& store,
    const std::vector<ServiceFacility>& facilities,uint32_t kind,Coord position,int64_t funds) {
  MonumentPlan result;result.kind=kind;result.position=position;
  const auto* item=monuments::spec(kind);
  if(!item) {result.error="Unknown monument";return result;}
  result.cost=item->cost;
  if(monuments::built(store,kind)) {result.error="Monument already built";return result;}
  if(!monuments::eligible(store,kind)) {
    result.error="REQUIRES BUILT: "+monuments::requirements(store,kind,true);return result;
  }
  if(!map.isValid(position)) {result.error="Monument site outside map";return result;}
  const auto& tile=map.getTile(position);
  if(tile.type==2 || tile.hasRoad || tile.buildingId || tile.zone || tile.regionalConnectionId) {
    result.error="Monument requires empty unzoned land";return result;
  }
  for(const auto& facility:facilities) if(facilityCoversTile(facility,position)) {
    result.error="Monument site has a service";return result;
  }
  Coord anchor;
  if(!roads.resolveRoadAnchor(position,anchor)) {result.error="Monument requires road access";return result;}
  if(!tile.connectedToPower || !tile.connectedToWater) {result.error="Monument requires power and water to build";return result;}
  if(funds<result.cost) {result.error="Insufficient funds";return result;}
  result.valid=true;return result;
}
bool MonumentTool::build(CityMap& map,const RoadNetwork& roads,EntityStore& store,
    const std::vector<ServiceFacility>& facilities,const MonumentPlan& proposed,int64_t& funds) {
  if(!proposed.valid) return false;
  const auto current=plan(map,roads,store,facilities,proposed.kind,proposed.position,funds);
  if(!current.valid || current.cost!=proposed.cost) return false;
  Building building;building.id=EntityIdUtils::generateEntityId();building.type=BuildingType::Office;
  building.position=current.position;building.monumentKind=current.kind;
  store.upsertBuilding(building);map.getTile(current.position).buildingId=building.id;
  funds-=current.cost;return true;
}
