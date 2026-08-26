#include "src/entities/EntityStore.hpp"

#include <algorithm>

#include "src/core/EntityId.hpp"

void EntityStore::insertIdSorted(std::vector<EntityId>& ids, EntityId id) {
  const auto it = std::lower_bound(ids.begin(), ids.end(), id);
  if (it == ids.end() || *it != id) {
    ids.insert(it, id);
  }
}

bool EntityStore::eraseId(std::vector<EntityId>& ids, EntityId id) {
  const auto it = std::lower_bound(ids.begin(), ids.end(), id);
  if (it == ids.end() || *it != id) {
    return false;
  }
  ids.erase(it);
  return true;
}

void EntityStore::indexInsert(const Building& building) {
  const int slot = buildingTypeSlot(building.type);
  if (slot < 0) {
    return;
  }
  insertIdSorted(idsByType[static_cast<size_t>(slot)], building.id);
  if (isJobBuildingType(building.type)) {
    insertIdSorted(jobIds_, building.id);
  }
  countByType_[static_cast<size_t>(slot)] += 1;
  capacityByType_[static_cast<size_t>(slot)] +=
    static_cast<uint32_t>(std::max(0, building.capacity));
}

void EntityStore::indexErase(const Building& building) {
  const int slot = buildingTypeSlot(building.type);
  if (slot < 0) {
    return;
  }
  eraseId(idsByType[static_cast<size_t>(slot)], building.id);
  if (isJobBuildingType(building.type)) {
    eraseId(jobIds_, building.id);
  }
  if (countByType_[static_cast<size_t>(slot)] > 0) {
    countByType_[static_cast<size_t>(slot)] -= 1;
  }
  const uint32_t cap = static_cast<uint32_t>(std::max(0, building.capacity));
  if (capacityByType_[static_cast<size_t>(slot)] >= cap) {
    capacityByType_[static_cast<size_t>(slot)] -= cap;
  } else {
    capacityByType_[static_cast<size_t>(slot)] = 0;
  }
}

EntityId EntityStore::createBuilding(BuildingType type, Coord position, int capacity) {
  Building building;
  building.id = EntityIdUtils::generateEntityId();
  building.type = type;
  building.position = position;
  building.capacity = capacity;
  building.occupancy = 0;

  const EntityId id = building.id;
  buildings[id] = building;
  indexInsert(buildings[id]);
  ++mutationVersion;
  return id;
}

bool EntityStore::removeBuilding(EntityId id) {
  const auto it = buildings.find(id);
  if (it == buildings.end()) {
    return false;
  }
  indexErase(it->second);
  buildings.erase(it);
  ++mutationVersion;
  return true;
}

void EntityStore::clear() {
  if (!buildings.empty()) {
    ++mutationVersion;
  }
  buildings.clear();
  for (auto& ids : idsByType) {
    ids.clear();
  }
  jobIds_.clear();
  countByType_.fill(0);
  capacityByType_.fill(0);
}

void EntityStore::upsertBuilding(const Building& building) {
  const auto it = buildings.find(building.id);
  if (it != buildings.end()) {
    indexErase(it->second);
    it->second = building;
    indexInsert(it->second);
  } else {
    buildings[building.id] = building;
    indexInsert(buildings[building.id]);
  }
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

const std::vector<EntityId>& EntityStore::idsByBuildingType(BuildingType type) const {
  static const std::vector<EntityId> kEmpty;
  const int slot = buildingTypeSlot(type);
  if (slot < 0) {
    return kEmpty;
  }
  return idsByType[static_cast<size_t>(slot)];
}

uint32_t EntityStore::countOfType(BuildingType type) const {
  const int slot = buildingTypeSlot(type);
  return slot < 0 ? 0u : countByType_[static_cast<size_t>(slot)];
}

uint32_t EntityStore::capacityOfType(BuildingType type) const {
  const int slot = buildingTypeSlot(type);
  return slot < 0 ? 0u : capacityByType_[static_cast<size_t>(slot)];
}
