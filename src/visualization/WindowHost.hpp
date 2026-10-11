#pragma once
#include <cstdint>
#include <deque>
#include <string>
struct GLFWwindow;
namespace visualizer {
enum class EventType {
  Quit,
  KeyDown,
  MouseMotion,
  MouseDown,
  MouseUp,
  Scroll,
  FocusLost
};
enum class Key {
  Unknown,
  Escape,
  Enter,
  Space,
  Period,
  Equal,
  Plus,
  Minus,
  Left,
  Right,
  Up,
  Down,
  F1,
  F2,
  F3,
  F4,
  F5,
  F9,
  Num1,
  Num2,
  Num3,
  Num4,
  Num5,
  Num6,
  Num7,
  Num8,
  B,
  C,
  D,
  F,
  G,
  H,
  L,
  N,
  O,
  P,
  R,
  S,
  T,
  Z
};
Key translateGLFWKey(int code, int mods);
enum class MouseButton { Other, Left, Middle, Right };
struct InputEvent {
  EventType type = EventType::Quit;
  struct {
    int x = 0, y = 0;
  } motion;
  struct {
    int x = 0, y = 0;
    MouseButton button = MouseButton::Other;
  } button;
  struct {
    Key sym = Key::Unknown;
    bool repeat = false;
  } key;
  struct {
    double y = 0;
  } wheel;
};
class WindowHost {
public:
  WindowHost(int width, int height);
  ~WindowHost();
  WindowHost(const WindowHost &) = delete;
  WindowHost &operator=(const WindowHost &) = delete;
  GLFWwindow *window() const { return window_; }
  bool pollEvent(InputEvent &event);
  void dimensions(int &width, int &height) const;
  bool drawable() const;
  void waitEvents();
  void mousePosition(int *x, int *y) const;
  void title(const std::string &text);
  void resize(int width, int height);
  uint32_t timeMs() const;
  void queue(InputEvent event) { events_.push_back(event); }

private:
  GLFWwindow *window_ = nullptr;
  std::deque<InputEvent> events_;
  bool polling_ = false;
  double epoch_ = 0;
};
} // namespace visualizer
