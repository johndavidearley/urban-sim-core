#include "src/visualization/DrawList.hpp"
#include "src/visualization/VisualizerHud.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/WindowHost.hpp"
#include "gtest/gtest.h"
#include <algorithm>
using namespace visualizer;
namespace {
float area(const DrawList &list) {
  float total = 0;
  for (size_t i = 0; i < list.vertices.size(); i += 3) {
    const auto &a = list.vertices[i];
    const auto &b = list.vertices[i + 1];
    const auto &c = list.vertices[i + 2];
    total +=
        std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2.f;
  }
  return total;
}
} // namespace
TEST(VisualizerGeometry, RectangleOrderAndAlphaSurviveBatching) {
  DrawList list;
  list.color = {255, 0, 0, 255};
  list.fill({3, 5, 7, 11});
  list.color = {0, 0, 255, 128};
  list.fill({3, 5, 7, 11});
  ASSERT_EQ(list.vertices.size(), 12u);
  EXPECT_FLOAT_EQ(area(list), 154.f);
  EXPECT_FLOAT_EQ(list.vertices[0].r, 1.f);
  EXPECT_FLOAT_EQ(list.vertices[0].a, 1.f);
  EXPECT_FLOAT_EQ(list.vertices[6].b, 1.f);
  EXPECT_FLOAT_EQ(list.vertices[6].a, 128.f / 255.f);
  list.clear();
  EXPECT_TRUE(list.vertices.empty());
  EXPECT_EQ(list.background.b, 255);
}
TEST(VisualizerGeometry, OutlineDoesNotBlendCornersTwice) {
  DrawList list;
  list.outline({0, 0, 7, 11});
  EXPECT_FLOAT_EQ(area(list), 32.f);
  list.clear();
  list.outline({0, 0, 1, 1});
  EXPECT_FLOAT_EQ(area(list), 1.f);
  list.clear();
  list.fill({0, 0, 0, 8});
  list.outline({0, 0, -1, 8});
  EXPECT_TRUE(list.vertices.empty());
}
TEST(VisualizerGeometry, LinesIncludeEndpointsInBothDirections) {
  DrawList list;
  list.line(2, 3, 6, 7);
  EXPECT_FLOAT_EQ(area(list), 9.f);
  ASSERT_EQ(list.vertices.size(), 12u);
  EXPECT_FLOAT_EQ(list.vertices[0].x, 2.f);
  EXPECT_FLOAT_EQ(list.vertices[2].x, 7.f);
  list.clear();
  list.line(6, 7, 2, 3);
  EXPECT_FLOAT_EQ(area(list), 9.f);
  EXPECT_FLOAT_EQ(list.vertices[0].x, 2.f);
  list.clear();
  list.line(2, 3, 2, 3);
  EXPECT_FLOAT_EQ(area(list), 1.f);
}
TEST(VisualizerGeometry, GlyphCaseSpacingAndLargeTextArePreserved) {
  DrawList upper, lower;
  drawGlyph(&upper, 0, 0, 'A', {255, 255, 255}, 2);
  drawGlyph(&lower, 0, 0, 'a', {255, 255, 255}, 2);
  ASSERT_EQ(upper.vertices.size(), lower.vertices.size());
  for (size_t i = 0; i < upper.vertices.size(); ++i) {
    EXPECT_EQ(upper.vertices[i].x, lower.vertices[i].x);
    EXPECT_EQ(upper.vertices[i].y, lower.vertices[i].y);
  }
  const bool saved = gReadableUiText;
  gReadableUiText = false;
  DrawList small;
  drawText(&small, 0, 0, "A A", {255, 255, 255}, 1);
  gReadableUiText = true;
  DrawList large;
  drawText(&large, 0, 0, "A A", {255, 255, 255}, 1);
  gReadableUiText = saved;
  EXPECT_FLOAT_EQ(area(large), area(small) * 4.f);
  EXPECT_FLOAT_EQ(large.vertices[upper.vertices.size()].x - large.vertices[0].x,
                  12.f);
}
TEST(VisualizerGeometry, LogicalHitTestsIgnoreFramebufferScale) {
  const auto button = paletteButtonRect(0, 1280, 800);
  EXPECT_EQ(paletteHitTest(button.x, button.y, 1280, 800), PaletteTool::Road);
  EXPECT_EQ(paletteHitTest(button.x + button.w, button.y, 1280, 800),
            PaletteTool::None);
  const auto resized = paletteButtonRect(0, 1100, 720);
  EXPECT_EQ(button.x - resized.x, 90);
  EXPECT_EQ(button.y - resized.y, 80);
}
