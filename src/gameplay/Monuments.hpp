#pragma once
#include "src/gameplay/UniqueBuildings.hpp"
#include "src/systems/ServiceSystem.hpp"

namespace monuments {
constexpr size_t count = 4;
constexpr float powerMW = 2000.f;
constexpr float cleanupFactor = 0.25f;
constexpr float landValueBonus = 25.f;
struct Spec {
  uint32_t id;
  const char* key;
  const char* name;
  std::array<uint32_t,3> prerequisites;
  int64_t cost, upkeep;
  const char* effect;
};
inline constexpr std::array<Spec,count> catalog{{
  {1,"medical_citadel","MEDICAL CITADEL",{{1,5,6}},6000000,120,"CITYWIDE HEALTH / UNLIMITED CARE"},
  {2,"universal_campus","UNIVERSAL CAMPUS",{{2,5,6}},6500000,140,"CITYWIDE EDUCATION / UNLIMITED SEATS"},
  {3,"clean_energy_spire","CLEAN ENERGY SPIRE",{{1,3,6}},7000000,180,"2000 MW / ZERO EMISSIONS / ROAD REACH 30"},
  {4,"ecology_dome","ECOLOGY DOME",{{3,4,6}},6000000,100,"75% POLLUTION CLEANUP / CITYWIDE VALUE +25"}
}};
inline const Spec* spec(uint32_t id) {return id && id<=count ? &catalog[id-1] : nullptr;}
inline const Spec* spec(const std::string& key) {
  for(const auto& item:catalog) if(key==item.key) return &item;
  return nullptr;
}
bool built(const EntityStore& store,uint32_t kind);
bool active(const EntityStore& store,const RoadNetwork& roads,uint32_t kind);
bool eligible(const EntityStore& store,uint32_t kind);
std::string requirements(const EntityStore& store,uint32_t kind,bool missingOnly=false);
int64_t upkeep(const EntityStore& store);
void appendPower(std::vector<ServiceFacility>& facilities,const EntityStore& store,const RoadNetwork& roads);
void configureCoverage(ServiceCoverageCache& cache,const EntityStore& store,const RoadNetwork& roads);
bool cleanPollution(CityMap& map,const EntityStore& store,const RoadNetwork& roads);
}

struct MonumentPlan {
  uint32_t kind=0;
  Coord position{0,0};
  int64_t cost=0;
  bool valid=false;
  std::string error;
};
class MonumentTool {
public:
  static MonumentPlan plan(const CityMap& map,const RoadNetwork& roads,const EntityStore& store,
    const std::vector<ServiceFacility>& facilities,uint32_t kind,Coord position,int64_t funds);
  static bool build(CityMap& map,const RoadNetwork& roads,EntityStore& store,
    const std::vector<ServiceFacility>& facilities,const MonumentPlan& proposed,int64_t& funds);
};
