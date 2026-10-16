#include "src/visualization/WindowHost.hpp"
#include "gtest/gtest.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
using namespace visualizer;
TEST(VisualizerInput, ShiftPlusAndKeypadUseTheSameZoomAction) {
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_EQUAL, 0), Key::Equal);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_EQUAL, GLFW_MOD_SHIFT), Key::Plus);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_KP_ADD, 0), Key::Plus);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_KP_SUBTRACT, 0), Key::Minus);
}
TEST(VisualizerInput, ShortcutsAreIndependentOfShiftAndUnknownKeysAreIgnored) {
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_R, 0), Key::R);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_R, GLFW_MOD_SHIFT), Key::R);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_ENTER, 0), Key::Enter);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_KP_ENTER, 0), Key::Enter);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_F9, 0), Key::F9);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_V, GLFW_MOD_SHIFT), Key::V);
  EXPECT_EQ(translateGLFWKey(GLFW_KEY_UNKNOWN, 0), Key::Unknown);
}
