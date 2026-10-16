#include "HotelTool.hpp"

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/CityMap.hpp"

bool HotelTool::setOpen(EntityStore& store, uint32_t id, bool open) {
  auto* hotel=store.getBuilding(id);
  if (!hotel || hotel->type!=BuildingType::Commercial || !hotel->hotelBeds) return false;
  hotel->hotelOpen=open;
  return true;
}

HotelPlan HotelTool::plan(const CityMap& map, const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities, Coord position, int64_t funds) {
  HotelPlan result; result.position = position; result.cost = kConstructionCost;
  if (!map.isValid(position)) { result.error = "hotel site is outside the map"; return result; }
  const auto& tile = map.getTile(position);
  if (tile.type == 2 || tile.hasRoad || tile.buildingId || tile.zone || tile.regionalConnectionId) {
    result.error = "hotel requires empty unzoned land"; return result;
  }
  for (const auto& facility : facilities)
    if (facility.position == position) { result.error = "hotel site has a service"; return result; }
  Coord anchor;
  if (!roads.resolveRoadAnchor(position,anchor)) {
    result.error = "hotel requires road access"; return result;
  }
  if (!tile.connectedToPower || !tile.connectedToWater) {
    result.error = "hotel requires power and water"; return result;
  }
  if (funds < result.cost) { result.error = "insufficient funds"; return result; }
  result.valid = true;
  return result;
}

bool HotelTool::build(CityMap& map, const RoadNetwork& roads, EntityStore& store,
    const std::vector<ServiceFacility>& facilities, const HotelPlan& proposed, int64_t& funds) {
  if (!proposed.valid) return false;
  const auto current = plan(map,roads,facilities,proposed.position,funds);
  if (!current.valid || proposed.cost != current.cost) return false;
  const auto id = store.createBuilding(BuildingType::Commercial,current.position,kJobs);
  store.getBuilding(id)->hotelBeds = kBeds;
  map.getTile(current.position).buildingId = id;
  funds -= current.cost;
  return true;
}
