#pragma once
#include <string>
namespace visualizer {
class WindowHost;
class VulkanRenderer;
int runGraphicsSmoke(WindowHost &host, VulkanRenderer &gpu,
                     const std::string &outputDirectory,
                     bool benchmark = false);
} // namespace visualizer
