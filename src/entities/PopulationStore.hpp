#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "src/entities/PopulationGroup.hpp"

class PopulationStore {
private:
  std::unordered_map<EntityId, PopulationGroup> groups;

public:
  EntityId createGroup(IncomeBand band, uint32_t size, uint32_t employed);
  EntityId createGroup(uint32_t size, uint32_t employed);
  void clear();
  void upsertGroup(const PopulationGroup& group);

  PopulationGroup* getGroup(EntityId id);
  const PopulationGroup* getGroup(EntityId id) const;
  const std::unordered_map<EntityId, PopulationGroup>& getGroups() const;

  size_t getGroupCount() const;
  uint32_t getTotalPopulation() const;
  uint32_t getTotalEmployed() const;
  uint32_t applyDeaths(uint32_t deaths);
};
