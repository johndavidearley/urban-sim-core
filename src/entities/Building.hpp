#pragma once

#include "src/core/EntityId.hpp"
#include "src/world/Tile.hpp"

enum class BuildingType : int {
  Residential = 1,
  Commercial = 2,
  Industrial = 3
};

struct Building {
  EntityId id = EntityIdUtils::NullEntity;
  BuildingType type = BuildingType::Residential;
  Coord position{0, 0};
  int capacity = 0;
  int occupancy = 0;
};
