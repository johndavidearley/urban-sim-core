#pragma once

#include <cstdint>

#include "src/core/EntityId.hpp"

enum class IncomeBand : int {
  Low = 0,
  Middle = 1,
  High = 2
};

struct PopulationGroup {
  EntityId id = EntityIdUtils::NullEntity;
  IncomeBand band = IncomeBand::Middle;
  uint32_t size = 0;
  uint32_t employed = 0;
};
