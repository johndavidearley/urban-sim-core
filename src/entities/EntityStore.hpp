#pragma once

#include <unordered_map>
#include <cstdint>

#include "src/entities/Building.hpp"

class EntityStore {
private:
  std::unordered_map<EntityId, Building> buildings;
  uint64_t mutationVersion = 0;

public:
  EntityId createBuilding(BuildingType type, Coord position, int capacity);
  bool removeBuilding(EntityId id);
  void clear();
  void upsertBuilding(const Building& building);

  Building* getBuilding(EntityId id);
  const Building* getBuilding(EntityId id) const;
  const std::unordered_map<EntityId, Building>& getBuildings() const;

  size_t getBuildingCount() const;
  uint64_t getMutationVersion() const;
};
