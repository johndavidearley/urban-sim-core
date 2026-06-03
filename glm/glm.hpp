#pragma once

namespace glm {

struct ivec2 {
  int x;
  int y;

  constexpr ivec2() : x(0), y(0) {}
  constexpr ivec2(int xValue, int yValue) : x(xValue), y(yValue) {}

  constexpr bool operator==(const ivec2& other) const {
    return x == other.x && y == other.y;
  }

  constexpr bool operator!=(const ivec2& other) const {
    return !(*this == other);
  }
};

struct ivec4 {
  int x;
  int y;
  int z;
  int w;

  constexpr ivec4() : x(0), y(0), z(0), w(0) {}
  constexpr ivec4(int xValue, int yValue, int zValue, int wValue)
      : x(xValue), y(yValue), z(zValue), w(wValue) {}

  constexpr bool operator==(const ivec4& other) const {
    return x == other.x && y == other.y && z == other.z && w == other.w;
  }

  constexpr bool operator!=(const ivec4& other) const {
    return !(*this == other);
  }
};

} // namespace glm
