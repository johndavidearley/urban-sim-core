// Implementation is inline in header

#include <glm/glm.hpp>
#include <functional>

// Hash specialization for glm::ivec2
namespace std {
  template<>
  struct hash<glm::ivec2> {
    size_t operator()(const glm::ivec2& v) const {
      return hash<int>()(v.x) ^ (hash<int>()(v.y) << 1);
    }
  };
}


