#pragma once

#include <cstdint>
#include <string>

using EntityId = uint32_t;

namespace EntityIdUtils {
  // Generate a new unique entity ID
  EntityId generateEntityId();
  
  // Check for invalid/null ID
  constexpr bool isValid(EntityId id) { return id != 0; }
  
  // Null entity constant
  constexpr EntityId NullEntity = 0;
}
