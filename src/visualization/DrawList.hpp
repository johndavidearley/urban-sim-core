#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace visualizer {
struct Point {
  int x = 0, y = 0;
};
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};
struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
};
struct Vertex {
  float x, y, r, g, b, a;
  float u = -1.f, v = -1.f; // Negative UV selects solid geometry.
};

// Logical-pixel geometry in painter's order. The Vulkan backend scales to the
// framebuffer.
class DrawList {
public:
  std::vector<Vertex> vertices;
  Color color;
  Color background;
  uint32_t frameTimeMs = 0;
  void clear() {
    vertices.clear();
    background = color;
  }
  void triangle(float ax, float ay, float bx, float by, float cx, float cy) {
    const float r = color.r / 255.f, g = color.g / 255.f;
    const float b = color.b / 255.f, a = color.a / 255.f;
    vertices.insert(
        vertices.end(),
        {{ax, ay, r, g, b, a}, {bx, by, r, g, b, a}, {cx, cy, r, g, b, a}});
  }
  void fill(Rect rect) {
    if (rect.w <= 0 || rect.h <= 0)
      return;
    const float x = static_cast<float>(rect.x), y = static_cast<float>(rect.y);
    const float right = x + static_cast<float>(rect.w),
                bottom = y + static_cast<float>(rect.h);
    triangle(x, y, right, y, right, bottom);
    triangle(x, y, right, bottom, x, bottom);
  }
  void texturedQuad(float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1) {
    const float r = color.r / 255.f, g = color.g / 255.f;
    const float b = color.b / 255.f, a = color.a / 255.f;
    vertices.insert(vertices.end(), {
      {x,y,r,g,b,a,u0,v0}, {x+w,y,r,g,b,a,u1,v0}, {x+w,y+h,r,g,b,a,u1,v1},
      {x,y,r,g,b,a,u0,v0}, {x+w,y+h,r,g,b,a,u1,v1}, {x,y+h,r,g,b,a,u0,v1}});
  }
  void outline(Rect rect) {
    if (rect.w <= 0 || rect.h <= 0)
      return;
    fill({rect.x, rect.y, rect.w, 1});
    if (rect.h > 1)
      fill({rect.x, rect.y + rect.h - 1, rect.w, 1});
    if (rect.h > 2) {
      fill({rect.x, rect.y + 1, 1, rect.h - 2});
      if (rect.w > 1)
        fill({rect.x + rect.w - 1, rect.y + 1, 1, rect.h - 2});
    }
  }
  // Sweep a logical-pixel square along the segment. Continuous triangles keep
  // diagonal strokes consistent at fractional and high framebuffer scales.
  void line(int x0, int y0, int x1, int y1) {
    if (x0 == x1) {
      fill({x0, std::min(y0, y1), 1, std::abs(y1 - y0) + 1});
      return;
    }
    if (y0 == y1) {
      fill({std::min(x0, x1), y0, std::abs(x1 - x0) + 1, 1});
      return;
    }
    if (x0 > x1) {
      std::swap(x0, x1);
      std::swap(y0, y1);
    }
    const Point ascending[] = {{x0, y0},         {x0 + 1, y0}, {x1 + 1, y1},
                               {x1 + 1, y1 + 1}, {x1, y1 + 1}, {x0, y0 + 1}};
    const Point descending[] = {{x0, y0},         {x0, y0 + 1},
                                {x0 + 1, y0 + 1}, {x1 + 1, y1 + 1},
                                {x1 + 1, y1},     {x1, y1}};
    const Point *points = y1 > y0 ? ascending : descending;
    for (int i = 2; i < 6; ++i) {
      triangle(static_cast<float>(points[0].x), static_cast<float>(points[0].y),
               static_cast<float>(points[i - 1].x),
               static_cast<float>(points[i - 1].y),
               static_cast<float>(points[i].x),
               static_cast<float>(points[i].y));
    }
  }
  void lines(const Point *points, int count) {
    for (int i = 1; i < count; ++i)
      line(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y);
  }
  void polygon(const std::vector<Point> &points) {
    for (size_t i = 2; i < points.size(); ++i)
      triangle(static_cast<float>(points[0].x), static_cast<float>(points[0].y),
               static_cast<float>(points[i - 1].x),
               static_cast<float>(points[i - 1].y),
               static_cast<float>(points[i].x),
               static_cast<float>(points[i].y));
  }
};
inline void setDrawColor(DrawList *list, uint8_t r, uint8_t g, uint8_t b,
                         uint8_t a) {
  list->color = {r, g, b, a};
}
inline void fillRect(DrawList *list, const Rect *rect) { list->fill(*rect); }
inline void outlineRect(DrawList *list, const Rect *rect) {
  list->outline(*rect);
}
inline void drawLine(DrawList *list, int x0, int y0, int x1, int y1) {
  list->line(x0, y0, x1, y1);
}
inline void drawLines(DrawList *list, const Point *points, int count) {
  list->lines(points, count);
}
inline void clearFrame(DrawList *list) { list->clear(); }
} // namespace visualizer
