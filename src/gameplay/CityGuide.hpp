#pragma once
#include "src/systems/ServiceSystem.hpp"
#include <algorithm>

namespace city_guide {
// Preserve the original IDs for existing diagnostics; new steps are additive.
enum Step { Road=0,Zones=1,Time=2,Power=3,Water=4,Budget=5,Complete=6,
  Pipes=7,Sanitation=8,Drainage=9,Save=10,WaterRepair=11,PowerRepair=12,Waste=13 };
struct Copy {const char* title;const char* detail;const char* hint;const char* action;};
inline Copy copy(int step) {
  switch(step) {
    case Road:return {"BUILD A ROAD","Roads connect homes, jobs and service sites.","Select Roads, then drag a short street.","SELECT ROADS"};
    case Zones:return {"ZONE HOMES AND JOBS","Zone homes and shops or industry beside roads.","Click Zoning again to choose a zone type.","SELECT ZONING"};
    case Power:return {"PLACE POWER","Choose a power plant beside a road.","Coal/generic power gives steady starter supply.","SELECT POWER"};
    case PowerRepair:return {"RESTORE ELECTRICITY","Check generation, funding and road reach.","Add a plant or extend coverage to dark parcels.","SHOW POWER"};
    case Water:return {"PLACE WATER SUPPLY","Place a water plant beside the road.","An inland well is a simple starter source.","SELECT WATER"};
    case Pipes:return {"CONNECT WATER PIPES","Drag pipes under streets from water to zones.","Combined trenches carry water and sewage.","SELECT PIPES"};
    case WaterRepair:return {"RESTORE WATER DELIVERY","Blue pipes must join a source and each parcel.","Check gaps, capacity, contamination and pressure.","SHOW WATER"};
    case Sanitation:return {"PLACE A SEWAGE OUTLET","Sanitation needs a connected sewer route.","Use a low site; inland sanitation exports sewage.","SELECT SANITATION"};
    case Drainage:return {"CONNECT SEWERS","Join orange sewers to a sanitation outlet.","Sewers drain downhill; powered lifts cross hills.","SHOW SEWERS"};
    case Waste:return {"ADD WASTE COLLECTION","Garbage accumulates as the city grows.","Place a landfill by a road; monitor its capacity.","SELECT GARBAGE"};
    case Time:return {"START THE CITY","Homes need roads, power, water and demand.","Start time; watch residents and jobs arrive.","START TIME"};
    case Budget:return {"REVIEW YOUR BUDGET","Compare tax revenue with operating expenses.","Avoid adding services faster than you can pay.","OPEN BUDGET"};
    case Save:return {"SAVE YOUR CITY","Save before expanding or changing utilities.","Load resumes the saved city and its simulation.","SAVE NOW"};
    default:return {"CITY GUIDE COMPLETE","Expand housing and jobs as demand grows.","Use the guide again when a service fails.","REVIEW GUIDE"};
  }
}
inline int evaluate(const CityMap& map,const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities,uint32_t tick,uint32_t population,bool budgetReviewed,bool saved) {
  bool homes=false,jobs=false,waterMissing=false,powerMissing=false,sewerMissing=false,pipes=false;
  const auto dims=map.getDimensions();
  for(int y=0;y<dims.y;++y) for(int x=0;x<dims.x;++x) {
    const auto& t=map.getTile({x,y});pipes|=map.utility({x,y}).waterPipe;
    homes|=t.zone==1;jobs|=t.zone==2||t.zone==3||t.zone==5;
    if(t.zone==1||t.zone==2||t.zone==3||t.zone==5||t.buildingId) {
      waterMissing|=!t.connectedToWater;powerMissing|=!t.connectedToPower;
      sewerMissing|=!t.connectedToSewer;
    }
  }
  const auto has=[&](ServiceType type) {return std::any_of(facilities.begin(),facilities.end(),[&](const auto& f){return f.type==type && f.quality>0;});};
  if(roads.getRoadCount()==0) return Road;
  if(!homes||!jobs) return Zones;
  if(!has(ServiceType::Power)) return Power;
  if(powerMissing) return PowerRepair;
  if(!has(ServiceType::Water)) return Water;
  if(map.pipeNetworksEnabled() && !pipes) return Pipes;
  if(waterMissing) return WaterRepair;
  if(tick==0 || population==0) return Time;
  if(!budgetReviewed) return Budget;
  if(!has(ServiceType::Sanitation)) return Sanitation;
  if(map.pipeNetworksEnabled() && sewerMissing) return Drainage;
  if(!has(ServiceType::Garbage)&&!has(ServiceType::Recycling)&&!has(ServiceType::Incinerator)) return Waste;
  if(!saved) return Save;
  return Complete;
}
} // namespace city_guide
