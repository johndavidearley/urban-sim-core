#include "src/visualization/WindowHost.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <iostream>
#include <stdexcept>
namespace visualizer {
namespace {
WindowHost &host(GLFWwindow *w) {
  return *static_cast<WindowHost *>(glfwGetWindowUserPointer(w));
}
} // namespace
Key translateGLFWKey(int code, int mods) {
  switch (code) {
  case GLFW_KEY_ESCAPE:
    return Key::Escape;
  case GLFW_KEY_ENTER:
  case GLFW_KEY_KP_ENTER:
    return Key::Enter;
  case GLFW_KEY_SPACE:
    return Key::Space;
  case GLFW_KEY_PERIOD:
    return Key::Period;
  case GLFW_KEY_EQUAL:
    return (mods & GLFW_MOD_SHIFT) ? Key::Plus : Key::Equal;
  case GLFW_KEY_KP_ADD:
    return Key::Plus;
  case GLFW_KEY_MINUS:
  case GLFW_KEY_KP_SUBTRACT:
    return Key::Minus;
  case GLFW_KEY_LEFT:
    return Key::Left;
  case GLFW_KEY_RIGHT:
    return Key::Right;
  case GLFW_KEY_UP:
    return Key::Up;
  case GLFW_KEY_DOWN:
    return Key::Down;
  case GLFW_KEY_F1:
    return Key::F1;
  case GLFW_KEY_F2:
    return Key::F2;
  case GLFW_KEY_F3:
    return Key::F3;
  case GLFW_KEY_F4:
    return Key::F4;
  case GLFW_KEY_F6:
    return Key::F6;
  case GLFW_KEY_F5:
    return Key::F5;
  case GLFW_KEY_F9:
    return Key::F9;
  case GLFW_KEY_1:
    return Key::Num1;
  case GLFW_KEY_2:
    return Key::Num2;
  case GLFW_KEY_3:
    return Key::Num3;
  case GLFW_KEY_4:
    return Key::Num4;
  case GLFW_KEY_5:
    return Key::Num5;
  case GLFW_KEY_6:
    return Key::Num6;
  case GLFW_KEY_7:
    return Key::Num7;
  case GLFW_KEY_9:
    return Key::Num9;
  case GLFW_KEY_8:
    return Key::Num8;
  case GLFW_KEY_B:
    return Key::B;
  case GLFW_KEY_C:
    return Key::C;
  case GLFW_KEY_D:
    return Key::D;
  case GLFW_KEY_F:
    return Key::F;
  case GLFW_KEY_G:
    return Key::G;
  case GLFW_KEY_H:
    return Key::H;
  case GLFW_KEY_L:
    return Key::L;
  case GLFW_KEY_N:
    return Key::N;
  case GLFW_KEY_O:
    return Key::O;
  case GLFW_KEY_P:
    return Key::P;
  case GLFW_KEY_A:
    return Key::A;
  case GLFW_KEY_R:
    return Key::R;
  case GLFW_KEY_S:
    return Key::S;
  case GLFW_KEY_T:
    return Key::T;
  case GLFW_KEY_Z:
    return Key::Z;
  default:
    return Key::Unknown;
  }
}
WindowHost::WindowHost(int width, int height) {
#ifdef __APPLE__
  // Prefer the installed sibling runtime; respect explicit user driver
  // configuration.
  if (!std::getenv("VK_DRIVER_FILES") && !std::getenv("VK_ICD_FILENAMES")) {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string executable(size, '\0');
    if (_NSGetExecutablePath(executable.data(), &size) == 0) {
      const auto manifest = std::filesystem::weakly_canonical(
                                std::filesystem::path(executable.c_str()))
                                .parent_path() /
                            "../lib/urban-sim/vulkan/icd.d/MoltenVK_icd.json";
      if (std::filesystem::exists(manifest))
        setenv("VK_DRIVER_FILES", manifest.string().c_str(), 0);
    }
  }
#endif
  glfwSetErrorCallback([](int, const char *message) {
    std::cerr << "GLFW: " << message << '\n';
  });
  if (!glfwInit())
    throw std::runtime_error(
        "GLFW initialization failed: check your display session.");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(width, height, "UrbanSimCore Visualizer (Vulkan)",
                             nullptr, nullptr);
  if (!window_) {
    glfwTerminate();
    throw std::runtime_error("GLFW window creation failed.");
  }
  glfwSetWindowSizeLimits(window_, 800, 640, GLFW_DONT_CARE, GLFW_DONT_CARE);
  epoch_ = glfwGetTime();
  glfwSetWindowUserPointer(window_, this);
  glfwSetWindowCloseCallback(window_, [](GLFWwindow *w) {
    glfwSetWindowShouldClose(w, GLFW_FALSE);
    host(w).queue(InputEvent{});
  });
  glfwSetKeyCallback(
      window_, [](GLFWwindow *w, int key, int, int action, int mods) {
        if (action != GLFW_PRESS && action != GLFW_REPEAT)
          return;
        InputEvent event;
        event.type = EventType::KeyDown;
        event.key = {translateGLFWKey(key, mods), action == GLFW_REPEAT};
        host(w).queue(event);
      });
  glfwSetCursorPosCallback(window_, [](GLFWwindow *w, double x, double y) {
    InputEvent event;
    event.type = EventType::MouseMotion;
    event.motion = {static_cast<int>(std::floor(x)),
                    static_cast<int>(std::floor(y))};
    host(w).queue(event);
  });
  glfwSetMouseButtonCallback(
      window_, [](GLFWwindow *w, int button, int action, int) {
        InputEvent event;
        event.type =
            action == GLFW_PRESS ? EventType::MouseDown : EventType::MouseUp;
        host(w).mousePosition(&event.button.x, &event.button.y);
        event.button.button =
            button == GLFW_MOUSE_BUTTON_LEFT     ? MouseButton::Left
            : button == GLFW_MOUSE_BUTTON_RIGHT  ? MouseButton::Right
            : button == GLFW_MOUSE_BUTTON_MIDDLE ? MouseButton::Middle
                                                 : MouseButton::Other;
        host(w).queue(event);
      });
  glfwSetScrollCallback(window_, [](GLFWwindow *w, double, double y) {
    InputEvent event;
    event.type = EventType::Scroll;
    event.wheel.y = y;
    host(w).queue(event);
  });
  glfwSetWindowFocusCallback(window_, [](GLFWwindow *w, int focused) {
    if (!focused) {
      InputEvent event;
      event.type = EventType::FocusLost;
      host(w).queue(event);
    }
  });
}
WindowHost::~WindowHost() {
  if (window_)
    glfwDestroyWindow(window_);
  glfwTerminate();
}
bool WindowHost::pollEvent(InputEvent &event) {
  if (!polling_) {
    glfwPollEvents();
    polling_ = true;
  }
  if (events_.empty()) {
    polling_ = false;
    return false;
  }
  event = events_.front();
  events_.pop_front();
  if (playtestInput && event.type == EventType::MouseMotion) { injectedPointer_=true; injectedX_=event.motion.x; injectedY_=event.motion.y; }
  else if (playtestInput && (event.type == EventType::MouseDown || event.type == EventType::MouseUp)) { injectedPointer_=true; injectedX_=event.button.x; injectedY_=event.button.y; }
  return true;
}
void WindowHost::dimensions(int &width, int &height) const {
  glfwGetWindowSize(window_, &width, &height);
}
bool WindowHost::drawable() const {
  int w, h;
  glfwGetFramebufferSize(window_, &w, &h);
  return w > 0 && h > 0 && !glfwGetWindowAttrib(window_, GLFW_ICONIFIED);
}
void WindowHost::waitEvents() { glfwWaitEventsTimeout(0.05); }
void WindowHost::mousePosition(int *x, int *y) const {
  if (injectedPointer_) { if(x) *x=injectedX_; if(y) *y=injectedY_; return; }
  double px, py;
  glfwGetCursorPos(window_, &px, &py);
  *x = static_cast<int>(std::floor(px));
  *y = static_cast<int>(std::floor(py));
}
void WindowHost::title(const std::string &text) {
  glfwSetWindowTitle(window_, text.c_str());
}
void WindowHost::resize(int width, int height) {
  glfwSetWindowSize(window_, width, height);
}
uint32_t WindowHost::timeMs() const {
  if (playtestClock) return playtestClock();
  return static_cast<uint32_t>((glfwGetTime() - epoch_) * 1000.0);
}
} // namespace visualizer
