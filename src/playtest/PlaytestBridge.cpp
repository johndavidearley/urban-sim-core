#include "src/playtest/PlaytestBridge.hpp"
#include "src/visualization/VulkanRenderer.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace visualizer {
struct PlaytestBridge::Inbox {
  std::mutex mutex;
  std::deque<Json> requests;
  bool eof = false;
  bool stopping = false;
};
Json playtestCapabilities() {
  return {
      {"bridge_version", 1},
      {"window_minimize_restore", glfwGetPlatform() != GLFW_PLATFORM_WAYLAND},
      {"build", "UrbanSimCore-0.1.0 " __DATE__ " " __TIME__},
      {"commands",
       {"status", "input", "window", "city", "time", "tool", "view", "inspect",
        "coordinates", "screenshot", "diagnostics", "stop"}},
      {"limits",
       {{"message_bytes", 1048576},
        {"events", 256},
        {"ticks", 100},
        {"page", 512}}}};
}
PlaytestBridge::PlaytestBridge() : inbox_(std::make_shared<Inbox>()) {
  // Poll private stdin so teardown can join the reader even when the MCP
  // parent still has its pipe open. The reader never accesses GLFW/game state.
  reader_ = std::thread([inbox = inbox_] {
    std::string line;
    bool oversized = false;
    bool finished = false;
    while (!finished) {
      {
        std::lock_guard<std::mutex> lock(inbox->mutex);
        if (inbox->stopping)
          break;
      }
      char buffer[4096];
      size_t amount = 0;
#ifdef _WIN32
      const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
      DWORD available = 0, received = 0;
      if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
        break;
      if (available == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      if (!ReadFile(input, buffer,
                    std::min(available, static_cast<DWORD>(sizeof(buffer))),
                    &received, nullptr) ||
          received == 0)
        break;
      amount = received;
#else
      pollfd descriptor{STDIN_FILENO, POLLIN, 0};
      const int ready = poll(&descriptor, 1, 50);
      if (ready < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      if (ready == 0)
        continue;
      const auto received = read(STDIN_FILENO, buffer, sizeof(buffer));
      if (received < 0 && errno == EINTR)
        continue;
      if (received <= 0)
        break;
      amount = static_cast<size_t>(received);
#endif
      for (size_t i = 0; i < amount; ++i) {
        const char ch = buffer[i];
        if (ch != '\n') {
          if (line.size() < 1048576)
            line += ch;
          else
            oversized = true;
          continue;
        }
        Json request;
        try {
          if (oversized)
            throw std::runtime_error("Request exceeds 1 MiB");
          request = Json::parse(line);
        } catch (const std::exception &e) {
          request = {{"id", nullptr}, {"parse_error", e.what()}};
        }
        {
          std::lock_guard<std::mutex> lock(inbox->mutex);
          if (inbox->requests.size() < 64)
            inbox->requests.push_back(std::move(request));
          else {
            finished = true;
            break;
          }
        }
        line.clear();
        oversized = false;
      }
    }
    std::lock_guard<std::mutex> lock(inbox->mutex);
    inbox->eof = true;
  });
}
PlaytestBridge::~PlaytestBridge() {
  {
    std::lock_guard<std::mutex> lock(inbox_->mutex);
    inbox_->stopping = true;
  }
  if (reader_.joinable())
    reader_.join();
}
bool PlaytestBridge::disconnected() const {
  std::lock_guard<std::mutex> lock(inbox_->mutex);
  return inbox_->eof && inbox_->requests.empty();
}
void PlaytestBridge::begin(
    WindowHost &host,
    const std::function<Json(const std::string &, const Json &)> &handler) {
  if (active_)
    return;
  {
    std::lock_guard<std::mutex> lock(inbox_->mutex);
    if (inbox_->requests.empty())
      return;
    pending_ = std::move(inbox_->requests.front());
    inbox_->requests.pop_front();
  }
  active_ = true;
  result_ = Json::object();
  try {
    if (pending_.contains("parse_error"))
      throw std::runtime_error(pending_["parse_error"].get<std::string>());
    if (!pending_.is_object() || pending_.value("version", 0) != 1 ||
        !pending_.contains("id"))
      throw std::runtime_error("Expected version 1 request with id");
    const auto command = pending_.at("command").get<std::string>();
    const auto args = pending_.value("args", Json::object());
    if (!args.is_object())
      throw std::runtime_error("args must be an object");
    if (command == "input") {
      const auto &events = args.at("events");
      if (!events.is_array() || events.empty() || events.size() > 256)
        throw std::runtime_error("Expected 1..256 input events");
      std::vector<InputEvent> validated;
      for (const auto &item : events) {
        InputEvent event;
        const auto type = item.at("type").get<std::string>();
        const int x = item.value("x", 0), y = item.value("y", 0);
        if (x < 0 || y < 0 || x > 16384 || y > 16384)
          throw std::runtime_error("Pointer coordinates out of bounds");
        if (type == "key") {
          const auto key = item.at("key").get<std::string>();
          const std::vector<std::pair<std::string, Key>> keys = {
#define K(name) {#name, Key::name}
              K(Escape), K(Enter), K(Space), K(Period), K(Equal), K(Plus),
              K(Minus),  K(Left),  K(Right), K(Up),     K(Down),  K(F1),
              K(F2),     K(F3),    K(F4),    K(F5),     K(F9),    K(Num1),
              K(Num2),   K(Num3),  K(Num4),  K(Num5),   K(Num6),  K(Num7),
              K(Num8),   K(B),     K(C),     K(D),      K(F),     K(G),
              K(H),      K(L),     K(N),     K(O),      K(P),     K(R),
              K(S),      K(T),     K(Z)
#undef K
          };
          event.type = EventType::KeyDown;
          bool found = false;
          for (const auto &entry : keys)
            if (entry.first == key) {
              event.key = {entry.second, item.value("repeat", false)};
              found = true;
            }
          if (!found)
            throw std::runtime_error("Unknown key: " + key);
        } else if (type == "move") {
          event.type = EventType::MouseMotion;
          event.motion = {x, y};
        } else if (type == "down" || type == "up") {
          const auto button = item.value("button", "left");
          if (button != "left" && button != "middle" && button != "right")
            throw std::runtime_error("Unknown mouse button");
          event.type =
              type == "down" ? EventType::MouseDown : EventType::MouseUp;
          event.button = {x, y,
                          button == "left"     ? MouseButton::Left
                          : button == "middle" ? MouseButton::Middle
                                               : MouseButton::Right};
        } else if (type == "scroll") {
          InputEvent move;
          move.type = EventType::MouseMotion;
          move.motion = {x, y};
          validated.push_back(move);
          event.type = EventType::Scroll;
          event.wheel.y = item.at("delta").get<double>();
          if (!std::isfinite(event.wheel.y) || std::abs(event.wheel.y) > 100)
            throw std::runtime_error("Scroll delta out of bounds");
        } else if (type == "focus_lost")
          event.type = EventType::FocusLost;
        else if (type == "close")
          event.type = EventType::Quit;
        else
          throw std::runtime_error("Unknown input event type");
        validated.push_back(event);
      }
      for (const auto &event : validated)
        host.queue(event);
    } else if (command == "window") {
      const auto action = args.at("action").get<std::string>();
      if ((action == "minimize" || action == "restore") &&
          glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
        throw std::runtime_error(
            "Native minimize/restore inspection is unavailable on Wayland; use "
            "compositor controls");
      if (action == "resize") {
        const int w = args.at("width"), h = args.at("height");
        if (w < 800 || h < 640 || w > 4096 || h > 4096)
          throw std::runtime_error(
              "Window size must be 800..4096 by 640..4096");
        host.resize(w, h);
      } else if (action == "minimize")
        glfwIconifyWindow(host.window());
      else if (action == "restore")
        glfwRestoreWindow(host.window());
      else if (action == "focus_lost") {
        InputEvent e;
        e.type = EventType::FocusLost;
        host.queue(e);
      } else if (action == "close") {
        InputEvent e;
        e.type = EventType::Quit;
        host.queue(e);
      } else
        throw std::runtime_error("Unknown window action");
    } else if (command == "screenshot") {
      if (!host.drawable())
        throw std::runtime_error(
            "Window is minimized or framebuffer is unavailable");
      const auto name = args.at("name").get<std::string>();
      if (name.empty() || name.size() > 128 ||
          name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRS"
                                 "TUVWXYZ0123456789-_.") != std::string::npos ||
          name == "." || name == "..")
        throw std::runtime_error("Capture name must be a simple filename");
      std::filesystem::create_directories("captures");
      capturePath = "captures/" + name + ".ppm";
      result_["capture"] = capturePath;
    } else
      result_ = handler(command, args);
    if (command != "status" && command != "inspect" &&
        command != "coordinates" && command != "diagnostics" &&
        command != "screenshot")
      ++revision;
  } catch (const std::exception &e) {
    result_ = {{"error", {{"code", "command_failed"}, {"message", e.what()}}}};
  }
}
void PlaytestBridge::complete(const Json &state) {
  if (!active_)
    return;
  Json response = {{"version", 1},
                   {"id", pending_.is_object()
                              ? pending_.value("id", Json(nullptr))
                              : Json(nullptr)}};
  if (result_.contains("error"))
    response["error"] = result_["error"];
  else {
    response["result"] = result_;
    response["result"]["state"] = state;
  }
  std::cout << response.dump() << std::endl;
  active_ = false;
  capturePath.clear();
}
Json PlaytestBridge::metadata(WindowHost &host, VulkanRenderer &gpu,
                              const std::string &phase, uint32_t tick) const {
  int w, h, fw, fh;
  host.dimensions(w, h);
  glfwGetFramebufferSize(host.window(), &fw, &fh);
  return {{"phase", phase},
          {"tick", tick},
          {"revision", revision},
          {"frame", frame},
          {"deterministic", deterministic},
          {"dimensions",
           {{"width", w},
            {"height", h},
            {"framebuffer_width", fw},
            {"framebuffer_height", fh}}},
          {"validation_errors", gpu.validationErrors()},
          {"buffer_capacity", gpu.bufferCapacity()},
          {"terrain_seed", terrainSeed},
          {"simulation_seed", simulationSeed}};
}
} // namespace visualizer
