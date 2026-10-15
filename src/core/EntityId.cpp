#include "EntityId.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace EntityIdUtils {
  static uint64_t nextId = 1;

  uint64_t nextEntityId() { return nextId; }
  void restoreNextEntityId(uint64_t next) {
    if (next == 0 || next > static_cast<uint64_t>(std::numeric_limits<EntityId>::max()) + 1)
      throw std::invalid_argument("invalid next entity ID");
    nextId = next;
  }
  
  void reserveThrough(EntityId id) {
    nextId = std::max(nextId, static_cast<uint64_t>(id) + 1);
  }

  EntityId generateEntityId() {
    if (nextId > std::numeric_limits<EntityId>::max())
      throw std::overflow_error("entity IDs exhausted");
    return static_cast<EntityId>(nextId++);
  }
}
