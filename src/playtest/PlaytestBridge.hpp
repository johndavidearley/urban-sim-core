#pragma once
#include "src/visualization/WindowHost.hpp"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
namespace visualizer {
class VulkanRenderer;
using Json = nlohmann::json;
class PlaytestBridge {
public:
  PlaytestBridge();
  ~PlaytestBridge();
  bool deterministic = true;
  uint32_t terrainSeed = 42, simulationSeed = 98765;
  uint64_t revision = 0, frame = 0;
  void
  begin(WindowHost &host,
        const std::function<Json(const std::string &, const Json &)> &handler);
  void complete(const Json &state);
  Json metadata(WindowHost &host, VulkanRenderer &gpu, const std::string &phase,
                uint32_t tick) const;
  std::string capturePath;
  uint32_t animationTime() const {
    return static_cast<uint32_t>(revision * 50);
  }
  bool disconnected() const;

private:
  struct Inbox;
  std::shared_ptr<Inbox> inbox_;
  std::thread reader_;
  Json pending_, result_;
  bool active_ = false;
};
Json playtestCapabilities();
} // namespace visualizer
