#pragma once

#include <cstdint>
#include <string>

using EntityId = uint32_t;

namespace EntityIdUtils {
  // Generate a new unique entity ID
  EntityId generateEntityId();
  // Imported IDs must never be reused by later construction/population groups.
  void reserveThrough(EntityId id);
  uint64_t nextEntityId();
  // Full-world snapshot restore only; permits the exhausted UINT32_MAX+1 state.
  void restoreNextEntityId(uint64_t next);
  
  // Check for invalid/null ID
  constexpr bool isValid(EntityId id) { return id != 0; }
  
  // Null entity constant
  constexpr EntityId NullEntity = 0;
}
