#pragma once
#include "src/visualization/DrawList.hpp"
#include "src/systems/RegionalAccounting.hpp"
#include "src/systems/TourismSystem.hpp"
#include <algorithm>
#include "src/entities/EntityStore.hpp"
#include "src/gameplay/RegionalDiagnostics.hpp"

namespace visualizer {
inline Rect regionalPanelRect(int width,int height) {
  return {(width-720)/2,(height-540)/2,720,540};
}
// Close, previous, next, operation, capacity, tourism, terminal/hotel tab, locate.
inline Rect regionalControlRect(int action,int width,int height) {
  const auto p=regionalPanelRect(width,height);
  if (action>=8) return {p.x+540,p.y+394+(action-8)*28,160,22};
  if (action>=6) return {p.x+20+(action-6)*520,p.y+268,160,22};
  return {p.x+20+action*114,p.y+490,108,28};
}
inline int regionalHitTest(int x,int y,int width,int height) {
  for (int action=0;action<10;++action) {
    const auto r=regionalControlRect(action,width,height);
    if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) return action;
  }
  return -1;
}
// Stable IDs make selection independent of the source registry's ordering.
inline std::vector<regional::Connection> managedTerminals(const std::vector<regional::Connection>& connections) {
  std::vector<regional::Connection> result;
  for (const auto& connection:connections)
    if (connection.constructed && connection.kind!=regional::Kind::RoadGateway) result.push_back(connection);
  std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.id<b.id;});
  return result;
}
struct ManagedHotel {
  Building building;
  uint64_t guests=0;
};
inline std::vector<ManagedHotel> managedHotels(const EntityStore& store,const tourism::State& state) {
  std::vector<ManagedHotel> result;
  for (const auto& entry:store.getBuildings())
    if (entry.second.hotelBeds) result.push_back({entry.second,0});
  std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.building.id<b.building.id;});
  for (const auto& stay:state.stays) {
    const auto found=std::lower_bound(result.begin(),result.end(),stay.hotelId,
      [](const auto& hotel,uint32_t id){return hotel.building.id<id;});
    if (found!=result.end() && found->building.id==stay.hotelId) found->guests+=stay.guests;
  }
  return result;
}
inline bool regionalControlEnabled(int action,bool hotels,size_t count,size_t alerts=0) {
  if (action<0 || action>=10) return false;
  if (action>=8) return alerts>0;
  if (action==0 || action==5 || action==6) return true;
  return count>0 && (action!=4 || !hotels);
}
void drawRegionalPanel(DrawList* renderer,int width,int height,
  const std::vector<regional::Connection>& connections,size_t selected,
  const regional::Settlement& cargo,const tourism::Runtime& tourism,
  uint64_t openBeds,int64_t configuredUpkeep,bool finiteCargo,int focus,
  const std::vector<ManagedHotel>& hotels,bool hotelTab,size_t selectedHotel,size_t selectedAlert);
}
