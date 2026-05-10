#pragma once

#include <unordered_map>

#include "src/entities/Building.hpp"

class EntityStore {
private:
  std::unordered_map<EntityId, Building> buildings;

public:
  EntityId createBuilding(BuildingType type, Coord position, int capacity);

  Building* getBuilding(EntityId id);
  const Building* getBuilding(EntityId id) const;
  const std::unordered_map<EntityId, Building>& getBuildings() const;

  size_t getBuildingCount() const;
};
