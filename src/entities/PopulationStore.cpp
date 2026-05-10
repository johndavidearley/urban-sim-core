#include "src/entities/PopulationStore.hpp"

#include "src/core/EntityId.hpp"

EntityId PopulationStore::createGroup(uint32_t size, uint32_t employed) {
  PopulationGroup group;
  group.id = EntityIdUtils::generateEntityId();
  group.size = size;
  group.employed = employed;

  const EntityId id = group.id;
  groups[id] = group;
  return id;
}

void PopulationStore::clear() {
  groups.clear();
}

PopulationGroup* PopulationStore::getGroup(EntityId id) {
  auto it = groups.find(id);
  if (it == groups.end()) {
    return nullptr;
  }
  return &it->second;
}

const PopulationGroup* PopulationStore::getGroup(EntityId id) const {
  auto it = groups.find(id);
  if (it == groups.end()) {
    return nullptr;
  }
  return &it->second;
}

const std::unordered_map<EntityId, PopulationGroup>& PopulationStore::getGroups() const {
  return groups;
}

size_t PopulationStore::getGroupCount() const {
  return groups.size();
}

uint32_t PopulationStore::getTotalPopulation() const {
  uint32_t total = 0;
  for (const auto& [id, group] : groups) {
    (void)id;
    total += group.size;
  }
  return total;
}

uint32_t PopulationStore::getTotalEmployed() const {
  uint32_t total = 0;
  for (const auto& [id, group] : groups) {
    (void)id;
    total += group.employed;
  }
  return total;
}
