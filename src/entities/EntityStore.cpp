#include "src/entities/EntityStore.hpp"

#include "src/core/EntityId.hpp"

EntityId EntityStore::createBuilding(BuildingType type, Coord position, int capacity) {
  Building building;
  building.id = EntityIdUtils::generateEntityId();
  building.type = type;
  building.position = position;
  building.capacity = capacity;
  building.occupancy = 0;

  const EntityId id = building.id;
  buildings[id] = building;
  ++mutationVersion;
  return id;
}

bool EntityStore::removeBuilding(EntityId id) {
  if (buildings.erase(id) == 0) {
    return false;
  }
  ++mutationVersion;
  return true;
}

void EntityStore::clear() {
  if (!buildings.empty()) {
    ++mutationVersion;
  }
  buildings.clear();
}

void EntityStore::upsertBuilding(const Building& building) {
  buildings[building.id] = building;
  ++mutationVersion;
}

Building* EntityStore::getBuilding(EntityId id) {
  auto it = buildings.find(id);
  if (it == buildings.end()) {
    return nullptr;
  }
  return &it->second;
}

const Building* EntityStore::getBuilding(EntityId id) const {
  auto it = buildings.find(id);
  if (it == buildings.end()) {
    return nullptr;
  }
  return &it->second;
}

const std::unordered_map<EntityId, Building>& EntityStore::getBuildings() const {
  return buildings;
}

size_t EntityStore::getBuildingCount() const {
  return buildings.size();
}

uint64_t EntityStore::getMutationVersion() const {
  return mutationVersion;
}
