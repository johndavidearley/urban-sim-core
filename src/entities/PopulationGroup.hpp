#pragma once

#include <cstdint>

#include "src/core/EntityId.hpp"

struct PopulationGroup {
  EntityId id = EntityIdUtils::NullEntity;
  uint32_t size = 0;
  uint32_t employed = 0;
};
