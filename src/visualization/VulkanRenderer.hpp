#pragma once
#include "src/visualization/DrawList.hpp"
#include <memory>
#include <string>
struct GLFWwindow;
namespace visualizer {
class VulkanRenderer {
public:
  explicit VulkanRenderer(GLFWwindow *window);
  ~VulkanRenderer();
  VulkanRenderer(const VulkanRenderer &) = delete;
  VulkanRenderer &operator=(const VulkanRenderer &) = delete;
  // Optional PPM capture of the submitted framebuffer, for bounded smoke tests.
  bool present(const DrawList &list, const std::string &capturePath = {});
  unsigned validationErrors() const;
  size_t bufferCapacity() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace visualizer
