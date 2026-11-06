#include "UniqueBuildings.hpp"
#include <cmath>
#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/CityMap.hpp"

namespace uniques {
bool built(const EntityStore& store, uint32_t kind) {
  for (const auto& entry : store.getBuildings()) if (entry.second.uniqueKind == kind) return true;
  return false;
}
int64_t upkeep(const EntityStore& store) {
  int64_t result = 0;
  for (const auto& entry : store.getBuildings())
    if (const auto* item = spec(entry.second.uniqueKind)) result += item->upkeep;
  return result;
}
std::vector<Effect> activeEffects(const CityMap& map, const RoadNetwork& roads, const EntityStore& store) {
  std::vector<Effect> result;
  // Stable catalog order avoids dependence on entity hash iteration order.
  for (const auto& item : catalog) for (const auto& entry : store.getBuildings()) {
    const auto& building = entry.second;
    if (building.uniqueKind != item.id || !map.isValid(building.position)) continue;
    const auto& tile = map.getTile(building.position);
    Coord anchor;
    if (tile.buildingId == building.id && tile.connectedToPower && tile.connectedToWater &&
        roads.resolveRoadAnchor(building.position, anchor))
      result.push_back({building.position, item.radius, item.landValue});
  }
  return result;
}
float landValueBonus(const std::vector<Effect>& effects, Coord tile) {
  float result = 0.f;
  for (const auto& effect : effects) {
    const int distance = std::abs(tile.x - effect.position.x) + std::abs(tile.y - effect.position.y);
    result = std::max(result, effect.bonus * std::max(0.f, 1.f - static_cast<float>(distance) / effect.radius));
  }
  return result; // Strongest nearby benefit; no stacking exploit.
}
}

UniqueBuildingPlan UniqueBuildingTool::plan(const CityMap& map, const RoadNetwork& roads,
    const EntityStore& store, const std::vector<ServiceFacility>& facilities,
    const uniques::Progress& progress, uint32_t kind, Coord position, int64_t funds) {
  UniqueBuildingPlan result; result.kind = kind; result.position = position;
  const auto* item = uniques::spec(kind);
  if (!item) { result.error = "Unknown unique building"; return result; }
  result.cost = item->cost;
  if (!progress.unlocked(kind)) {
    result.error = std::string("LOCKED: ") + item->requirement + " " +
      std::to_string(progress.values[kind - 1]) + "/" + std::to_string(item->threshold);
    return result;
  }
  if (uniques::built(store, kind)) { result.error = "Unique building already built"; return result; }
  if (!map.isValid(position)) { result.error = "Unique site outside map"; return result; }
  const auto& tile = map.getTile(position);
  if (tile.type == 2 || tile.hasRoad || tile.buildingId || tile.zone || tile.regionalConnectionId) {
    result.error = "Unique requires empty unzoned land"; return result;
  }
  for (const auto& facility : facilities) if (facilityCoversTile(facility, position)) {
    result.error = "Unique site has a service"; return result;
  }
  Coord anchor;
  if (!roads.resolveRoadAnchor(position, anchor)) { result.error = "Unique requires road access"; return result; }
  if (!tile.connectedToPower || !tile.connectedToWater) {
    result.error = "Unique requires power and water"; return result;
  }
  if (funds < result.cost) { result.error = "Insufficient funds"; return result; }
  result.valid = true;
  return result;
}
bool UniqueBuildingTool::build(CityMap& map, const RoadNetwork& roads, EntityStore& store,
    const std::vector<ServiceFacility>& facilities, const uniques::Progress& progress,
    const UniqueBuildingPlan& proposed, int64_t& funds) {
  if (!proposed.valid) return false;
  const auto current = plan(map, roads, store, facilities, progress, proposed.kind, proposed.position, funds);
  if (!current.valid || current.cost != proposed.cost) return false;
  // Deliberate, unzoned civic entity: no jobs, residents or ordinary tax/upkeep.
  Building building;
  building.id = EntityIdUtils::generateEntityId();
  building.type = BuildingType::Office;
  building.position = current.position;
  building.uniqueKind = current.kind;
  store.upsertBuilding(building);
  map.getTile(current.position).buildingId = building.id;
  funds -= current.cost;
  return true;
}
