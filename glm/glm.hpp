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

// Float 2-component vector, added for TrafficMicroSim::vehicleWorldPosition
// (a literal 2D vehicle position needs float precision - progress along an
// edge and lane offsets are both fractional). Deliberately minimal, like
// ivec2 above: just the arithmetic this project's own vector math needs,
// not a general-purpose vec2.
struct vec2 {
  float x;
  float y;

  constexpr vec2() : x(0.0f), y(0.0f) {}
  constexpr vec2(float xValue, float yValue) : x(xValue), y(yValue) {}
  constexpr explicit vec2(float scalar) : x(scalar), y(scalar) {}
  constexpr explicit vec2(const ivec2& v) : x(static_cast<float>(v.x)), y(static_cast<float>(v.y)) {}

  constexpr vec2 operator+(const vec2& other) const { return vec2(x + other.x, y + other.y); }
  constexpr vec2 operator-(const vec2& other) const { return vec2(x - other.x, y - other.y); }
  constexpr vec2 operator*(float scalar) const { return vec2(x * scalar, y * scalar); }

  constexpr bool operator==(const vec2& other) const {
    return x == other.x && y == other.y;
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
