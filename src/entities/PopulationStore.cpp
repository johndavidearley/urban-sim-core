#include "src/entities/PopulationStore.hpp"

#include <algorithm>
#include <vector>

#include "src/core/EntityId.hpp"

EntityId PopulationStore::createGroup(IncomeBand band, uint32_t size, uint32_t employed) {
  PopulationGroup group;
  group.id = EntityIdUtils::generateEntityId();
  group.band = band;
  group.size = size;
  group.employed = std::min(employed, size);

  const EntityId id = group.id;
  groups[id] = group;
  return id;
}

EntityId PopulationStore::createGroup(uint32_t size, uint32_t employed) {
  return createGroup(IncomeBand::Middle, size, employed);
}

void PopulationStore::clear() {
  groups.clear();
}

void PopulationStore::upsertGroup(const PopulationGroup& group) {
  groups[group.id] = group;
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

uint32_t PopulationStore::applyDeaths(uint32_t deaths) {
  const uint32_t total = getTotalPopulation();
  const uint32_t applied = std::min(deaths, total);
  if (applied == 0) return 0;
  std::vector<EntityId> ids;
  ids.reserve(groups.size());
  for (const auto& [id, group] : groups) {
    (void)group;
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  uint32_t remaining = applied;
  uint32_t populationRemaining = total;
  for (const EntityId id : ids) {
    PopulationGroup& group = groups[id];
    const uint32_t groupDeaths = populationRemaining > 0
      ? std::min(group.size, static_cast<uint32_t>(
          (static_cast<uint64_t>(remaining) * group.size) / populationRemaining))
      : 0;
    const uint32_t actual = (id == ids.back()) ? std::min(group.size, remaining) : groupDeaths;
    const uint32_t employedDeaths = group.size > 0
      ? static_cast<uint32_t>((static_cast<uint64_t>(group.employed) * actual) / group.size) : 0;
    group.size -= actual;
    group.employed -= std::min(group.employed, employedDeaths);
    remaining -= actual;
    populationRemaining -= (group.size + actual);
  }
  return applied - remaining;
}
