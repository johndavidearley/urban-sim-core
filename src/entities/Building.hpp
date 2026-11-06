#pragma once

#include "src/core/EntityId.hpp"
#include "src/world/Tile.hpp"

enum class BuildingType : int {
  Residential = 1,
  Commercial = 2,
  Industrial = 3,
  Office = 4
};

struct Building {
  EntityId id = EntityIdUtils::NullEntity;
  BuildingType type = BuildingType::Residential;
  Coord position{0, 0};
  int capacity = 0;
  int occupancy = 0;
  // Optional deliberate hotel specialization of a commercial building. Worker
  // occupancy remains employment; visitor beds/stays are a separate quantity.
  uint32_t hotelBeds = 0;
  bool hotelOpen = true; // Visitor operation only; commercial jobs/upkeep remain.
  uint32_t uniqueKind = 0; // Deliberately placed unzoned civic building; 0 is ordinary.
  uint32_t monumentKind = 0;
  bool isCivic() const {return uniqueKind!=0 || monumentKind!=0;}
  // Cached neighborhood read-outs; land value consumes them one tick later.
  float crimeRate = 0.0f;
  float illnessRate = 0.0f;
};
