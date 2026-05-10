#include "EntityId.hpp"

namespace EntityIdUtils {
  static uint32_t nextId = 1;
  
  EntityId generateEntityId() {
    return nextId++;
  }
}
