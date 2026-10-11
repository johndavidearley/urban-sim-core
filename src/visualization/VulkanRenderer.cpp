#include "src/visualization/VulkanRenderer.hpp"
#define GLFW_INCLUDE_VULKAN
#include "colored_frag.hpp"
#include "colored_vert.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace visualizer {
namespace {
template <class T> T info(VkStructureType type) {
  T result{};
  result.sType = type;
  return result;
}
void check(VkResult result, const char *operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(
        std::string(operation) + " failed (Vulkan " + std::to_string(result) +
        "). Check Vulkan drivers/MoltenVK and loader configuration.");
}
bool has(const std::vector<VkExtensionProperties> &extensions,
         const char *name) {
  for (const auto &extension : extensions)
    if (std::strcmp(extension.extensionName, name) == 0)
      return true;
  return false;
}
float linearize(float c) {
  return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
}
} // namespace
struct VulkanRenderer::Impl {
  GLFWwindow *window;
  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphics = VK_NULL_HANDLE, presentation = VK_NULL_HANDLE;
  uint32_t graphicsFamily = 0, presentFamily = 0;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  bool srgb = false, canCapture = false;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  std::vector<VkSemaphore> finished;
  struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    VkDeviceSize capacity = 0;
    bool coherent = false;
  };
  struct Frame {
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore available = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    Buffer vertices;
  };
  std::array<Frame, 2> frames{};
  size_t frameIndex = 0;
  std::atomic<unsigned> errors{0};
  bool recreate = true;

  explicit Impl(GLFWwindow *target) : window(target) {
    try {
      initialize();
    } catch (...) {
      cleanup();
      throw;
    }
  }
  ~Impl() { cleanup(); }
  static VKAPI_ATTR VkBool32 VKAPI_CALL
  message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
          VkDebugUtilsMessageTypeFlagsEXT,
          const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
      ++static_cast<Impl *>(user)->errors;
    std::cerr << "Vulkan validation: " << data->pMessage << '\n';
    return VK_FALSE;
  }
  void initialize() {
    if (!glfwVulkanSupported())
      throw std::runtime_error(
          "Vulkan loader unavailable. Install a Vulkan driver (Windows/Linux) "
          "or Vulkan loader and MoltenVK (macOS).");
    uint32_t count = 0;
    const char **required = glfwGetRequiredInstanceExtensions(&count);
    if (!required)
      throw std::runtime_error(
          "GLFW cannot create a Vulkan surface on this display.");
    std::vector<const char *> enabled(required, required + count);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
          "Enumerate instance extensions");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count,
                                                 extensions.data()),
          "Enumerate instance extensions");
    auto instanceInfo =
        info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    if (has(extensions, "VK_KHR_portability_enumeration")) {
      enabled.push_back("VK_KHR_portability_enumeration");
      instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    if (has(extensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
      enabled.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    std::vector<const char *> layers;
    auto debugInfo = info<VkDebugUtilsMessengerCreateInfoEXT>(
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    debugInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = message;
    debugInfo.pUserData = this;
    auto validation = info<VkValidationFeaturesEXT>(
        VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
    const VkValidationFeatureEnableEXT sync =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
#ifndef NDEBUG
    check(vkEnumerateInstanceLayerProperties(&count, nullptr),
          "Enumerate validation layers");
    std::vector<VkLayerProperties> availableLayers(count);
    check(vkEnumerateInstanceLayerProperties(&count, availableLayers.data()),
          "Enumerate validation layers");
    for (const auto &layer : availableLayers) {
      if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") != 0)
        continue;
      layers.push_back("VK_LAYER_KHRONOS_validation");
      uint32_t layerCount = 0;
      check(vkEnumerateInstanceExtensionProperties(layer.layerName, &layerCount,
                                                   nullptr),
            "Enumerate validation extensions");
      std::vector<VkExtensionProperties> layerExtensions(layerCount);
      check(vkEnumerateInstanceExtensionProperties(layer.layerName, &layerCount,
                                                   layerExtensions.data()),
            "Enumerate validation extensions");
      if (has(layerExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
        enabled.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        validation.enabledValidationFeatureCount = 1;
        validation.pEnabledValidationFeatures = &sync;
        instanceInfo.pNext = &validation;
      }
      if (has(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        enabled.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        debugInfo.pNext = instanceInfo.pNext;
        instanceInfo.pNext = &debugInfo;
      }
      break;
    }
#else
    (void)validation;
    (void)sync;
#endif
    auto app = info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "UrbanSimCore";
    app.apiVersion = VK_API_VERSION_1_0;
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    instanceInfo.ppEnabledExtensionNames = enabled.data();
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    check(vkCreateInstance(&instanceInfo, nullptr, &instance),
          "Create Vulkan instance");
    if (!layers.empty() && has(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      debugInfo.pNext = nullptr;
      auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
      if (create)
        check(create(instance, &debugInfo, nullptr, &debug),
              "Create validation messenger");
    }
    check(glfwCreateWindowSurface(instance, window, nullptr, &surface),
          "Create window surface");
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr),
          "Enumerate GPUs");
    std::vector<VkPhysicalDevice> candidates(count);
    check(vkEnumeratePhysicalDevices(instance, &count, candidates.data()),
          "Enumerate GPUs");
    std::vector<const char *> deviceExtensions;
    for (auto candidate : candidates) {
      check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count,
                                                 nullptr),
            "Enumerate device extensions");
      std::vector<VkExtensionProperties> supported(count);
      check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count,
                                                 supported.data()),
            "Enumerate device extensions");
      if (!has(supported, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        continue;
      uint32_t formatCount = 0, modeCount = 0;
      check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface,
                                                 &formatCount, nullptr),
            "Query surface formats");
      check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface,
                                                      &modeCount, nullptr),
            "Query presentation modes");
      if (!formatCount || !modeCount)
        continue;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
      std::vector<VkQueueFamilyProperties> queues(count);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count,
                                               queues.data());
      int gf = -1, pf = -1;
      for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = VK_FALSE;
        check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface,
                                                   &present),
              "Query presentation support");
        if (queues[i].queueCount &&
            (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
          gf = static_cast<int>(i);
        if (queues[i].queueCount && present)
          pf = static_cast<int>(i);
        if (queues[i].queueCount && present &&
            (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
          gf = pf = static_cast<int>(i);
          break;
        }
      }
      if (gf < 0 || pf < 0)
        continue;
      physical = candidate;
      graphicsFamily = static_cast<uint32_t>(gf);
      presentFamily = static_cast<uint32_t>(pf);
      deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
      if (has(supported, "VK_KHR_portability_subset"))
        deviceExtensions.push_back("VK_KHR_portability_subset");
      break;
    }
    if (!physical)
      throw std::runtime_error(
          "No Vulkan GPU supports graphics and presentation on this display.");
    const float priority = 1.f;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (auto family : {graphicsFamily, presentFamily}) {
      if (!queues.empty() && family == graphicsFamily)
        continue;
      auto queue = info<VkDeviceQueueCreateInfo>(
          VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
      queue.queueFamilyIndex = family;
      queue.queueCount = 1;
      queue.pQueuePriorities = &priority;
      queues.push_back(queue);
    }
    auto deviceInfo =
        info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
    deviceInfo.pQueueCreateInfos = queues.data();
    deviceInfo.enabledExtensionCount =
        static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device),
          "Create Vulkan device");
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphics);
    vkGetDeviceQueue(device, presentFamily, 0, &presentation);
    auto poolInfo = info<VkCommandPoolCreateInfo>(
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily;
    check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool),
          "Create command pool");
    for (auto &frame : frames) {
      auto allocate = info<VkCommandBufferAllocateInfo>(
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
      allocate.commandPool = pool;
      allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocate.commandBufferCount = 1;
      check(vkAllocateCommandBuffers(device, &allocate, &frame.command),
            "Allocate command buffer");
      auto semaphore =
          info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
      check(vkCreateSemaphore(device, &semaphore, nullptr, &frame.available),
            "Create acquire semaphore");
      auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
      fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      check(vkCreateFence(device, &fence, nullptr, &frame.fence),
            "Create frame fence");
    }
    makeSwapchain();
  }
  void destroyBuffer(Buffer &buffer) {
    if (buffer.mapped)
      vkUnmapMemory(device, buffer.memory);
    if (buffer.handle)
      vkDestroyBuffer(device, buffer.handle, nullptr);
    if (buffer.memory)
      vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
  }
  void allocateBuffer(Buffer &buffer, VkDeviceSize capacity,
                      VkBufferUsageFlags usage) {
    destroyBuffer(buffer);
    auto create =
        info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    create.size = capacity;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &create, nullptr, &buffer.handle),
          "Create geometry/readback buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t index = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
      const auto flags = memory.memoryTypes[i].propertyFlags;
      if (!(requirements.memoryTypeBits & (1u << i)) ||
          !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
        continue;
      index = i;
      if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        break;
    }
    if (index == UINT32_MAX)
      throw std::runtime_error(
          "Vulkan device has no suitable host-visible memory.");
    buffer.coherent = (memory.memoryTypes[index].propertyFlags &
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    auto allocate =
        info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = index;
    check(vkAllocateMemory(device, &allocate, nullptr, &buffer.memory),
          "Allocate buffer memory");
    check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0),
          "Bind buffer memory");
    check(
        vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped),
        "Map buffer memory");
    buffer.capacity = capacity;
  }
  void destroySwapchain() {
    for (auto framebuffer : framebuffers)
      vkDestroyFramebuffer(device, framebuffer, nullptr);
    framebuffers.clear();
    if (pipeline)
      vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
    if (layout)
      vkDestroyPipelineLayout(device, layout, nullptr);
    layout = VK_NULL_HANDLE;
    if (renderPass)
      vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
    for (auto view : views)
      vkDestroyImageView(device, view, nullptr);
    views.clear();
    for (auto semaphore : finished)
      vkDestroySemaphore(device, semaphore, nullptr);
    finished.clear();
    images.clear();
    if (swapchain)
      vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
  }
  void makeSwapchain() {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
      recreate = true;
      return;
    }
    check(vkDeviceWaitIdle(device), "Wait before swapchain recreation");
    destroySwapchain();
    VkSurfaceCapabilitiesKHR caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
          "Query surface capabilities");
    uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count,
                                               nullptr),
          "Query formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count,
                                               formats.data()),
          "Query formats");
    if (formats.empty())
      throw std::runtime_error(
          "The current display has no Vulkan surface formats.");
    VkSurfaceFormatKHR chosen = formats.front();
    if (formats.size() == 1 && chosen.format == VK_FORMAT_UNDEFINED)
      chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
    for (const auto &candidate : formats) {
      if ((candidate.format == VK_FORMAT_B8G8R8A8_UNORM ||
           candidate.format == VK_FORMAT_R8G8B8A8_UNORM) &&
          candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosen = candidate;
        break;
      }
    }
    format = chosen.format;
    srgb =
        format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
    extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
      extent.width = std::max(
          caps.minImageExtent.width,
          std::min(caps.maxImageExtent.width, static_cast<uint32_t>(width)));
      extent.height = std::max(
          caps.minImageExtent.height,
          std::min(caps.maxImageExtent.height, static_cast<uint32_t>(height)));
    }
    if (!extent.width || !extent.height) {
      recreate = true;
      return;
    }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
      throw std::runtime_error(
          "Surface does not support color attachment rendering.");
    auto create = info<VkSwapchainCreateInfoKHR>(
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    create.surface = surface;
    create.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && create.minImageCount > caps.maxImageCount)
      create.minImageCount = caps.maxImageCount;
    create.imageFormat = format;
    create.imageColorSpace = chosen.colorSpace;
    create.imageExtent = extent;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    canCapture =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (canCapture)
      create.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const uint32_t families[] = {graphicsFamily, presentFamily};
    create.imageSharingMode = graphicsFamily == presentFamily
                                  ? VK_SHARING_MODE_EXCLUSIVE
                                  : VK_SHARING_MODE_CONCURRENT;
    if (graphicsFamily != presentFamily) {
      create.queueFamilyIndexCount = 2;
      create.pQueueFamilyIndices = families;
    }
    create.preTransform = caps.currentTransform;
    for (auto mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
      if (caps.supportedCompositeAlpha & mode) {
        create.compositeAlpha = mode;
        break;
      }
    create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    create.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device, &create, nullptr, &swapchain),
          "Create swapchain");
    check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr),
          "Get swapchain images");
    images.resize(count);
    check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()),
          "Get swapchain images");
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{0,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    auto pass =
        info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    check(vkCreateRenderPass(device, &pass, nullptr, &renderPass),
          "Create render pass");
    makePipeline();
    for (auto image : images) {
      auto viewInfo =
          info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
      viewInfo.image = image;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = format;
      viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VkImageView view;
      check(vkCreateImageView(device, &viewInfo, nullptr, &view),
            "Create image view");
      views.push_back(view);
      auto framebuffer = info<VkFramebufferCreateInfo>(
          VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
      framebuffer.renderPass = renderPass;
      framebuffer.attachmentCount = 1;
      framebuffer.pAttachments = &views.back();
      framebuffer.width = extent.width;
      framebuffer.height = extent.height;
      framebuffer.layers = 1;
      VkFramebuffer target;
      check(vkCreateFramebuffer(device, &framebuffer, nullptr, &target),
            "Create framebuffer");
      framebuffers.push_back(target);
      auto semaphore =
          info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
      VkSemaphore complete;
      check(vkCreateSemaphore(device, &semaphore, nullptr, &complete),
            "Create present semaphore");
      finished.push_back(complete);
    }
    recreate = false;
  }
  void makePipeline() {
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    try {
      auto shader = info<VkShaderModuleCreateInfo>(
          VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
      shader.codeSize = sizeof(colored_vert);
      shader.pCode = colored_vert;
      check(vkCreateShaderModule(device, &shader, nullptr, &vert),
            "Create vertex shader");
      shader.codeSize = sizeof(colored_frag);
      shader.pCode = colored_frag;
      check(vkCreateShaderModule(device, &shader, nullptr, &frag),
            "Create fragment shader");
      std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
      for (auto &stage : stages) {
        stage = info<VkPipelineShaderStageCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stage.pName = "main";
      }
      stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
      stages[0].module = vert;
      stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      stages[1].module = frag;
      VkVertexInputBindingDescription binding{
          0, static_cast<uint32_t>(sizeof(Vertex)),
          VK_VERTEX_INPUT_RATE_VERTEX};
      const VkVertexInputAttributeDescription attributes[] = {
          {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
          {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
           static_cast<uint32_t>(offsetof(Vertex, r))}};
      auto input = info<VkPipelineVertexInputStateCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
      input.vertexBindingDescriptionCount = 1;
      input.pVertexBindingDescriptions = &binding;
      input.vertexAttributeDescriptionCount = 2;
      input.pVertexAttributeDescriptions = attributes;
      auto assembly = info<VkPipelineInputAssemblyStateCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
      assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      auto viewport = info<VkPipelineViewportStateCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
      viewport.viewportCount = viewport.scissorCount = 1;
      auto raster = info<VkPipelineRasterizationStateCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
      raster.polygonMode = VK_POLYGON_MODE_FILL;
      raster.cullMode = VK_CULL_MODE_NONE;
      raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
      raster.lineWidth = 1.f;
      auto samples = info<VkPipelineMultisampleStateCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
      samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
      VkPipelineColorBlendAttachmentState blend{};
      blend.blendEnable = VK_TRUE;
      blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      blend.colorBlendOp = VK_BLEND_OP_ADD;
      blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      blend.alphaBlendOp = VK_BLEND_OP_ADD;
      blend.colorWriteMask =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      auto blending = info<VkPipelineColorBlendStateCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
      blending.attachmentCount = 1;
      blending.pAttachments = &blend;
      const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                              VK_DYNAMIC_STATE_SCISSOR};
      auto dynamic = info<VkPipelineDynamicStateCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
      dynamic.dynamicStateCount = 2;
      dynamic.pDynamicStates = dynamicStates;
      VkPushConstantRange range{
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 12};
      auto layoutInfo = info<VkPipelineLayoutCreateInfo>(
          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
      layoutInfo.pushConstantRangeCount = 1;
      layoutInfo.pPushConstantRanges = &range;
      check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout),
            "Create pipeline layout");
      auto create = info<VkGraphicsPipelineCreateInfo>(
          VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
      create.stageCount = 2;
      create.pStages = stages.data();
      create.pVertexInputState = &input;
      create.pInputAssemblyState = &assembly;
      create.pViewportState = &viewport;
      create.pRasterizationState = &raster;
      create.pMultisampleState = &samples;
      create.pColorBlendState = &blending;
      create.pDynamicState = &dynamic;
      create.layout = layout;
      create.renderPass = renderPass;
      check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create,
                                      nullptr, &pipeline),
            "Create graphics pipeline");
    } catch (...) {
      if (vert)
        vkDestroyShaderModule(device, vert, nullptr);
      if (frag)
        vkDestroyShaderModule(device, frag, nullptr);
      throw;
    }
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
  }
  bool present(const DrawList &list, const std::string &path) {
    int width, height, logicalWidth, logicalHeight;
    glfwGetFramebufferSize(window, &width, &height);
    glfwGetWindowSize(window, &logicalWidth, &logicalHeight);
    if (width <= 0 || height <= 0 || logicalWidth <= 0 || logicalHeight <= 0)
      return false;
    VkSurfaceCapabilitiesKHR caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
          "Query current surface size");
    const auto target = caps.currentExtent.width == UINT32_MAX
                            ? VkExtent2D{static_cast<uint32_t>(width),
                                         static_cast<uint32_t>(height)}
                            : caps.currentExtent;
    if (recreate || target.width != extent.width ||
        target.height != extent.height)
      makeSwapchain();
    if (!swapchain || !extent.width || !extent.height)
      return false;
    auto &frame = frames[frameIndex];
    check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX),
          "Wait for frame");
    uint32_t imageIndex;
    auto result =
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame.available,
                              VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      recreate = true;
      return false;
    }
    if (result != VK_SUBOPTIMAL_KHR)
      check(result, "Acquire swapchain image");
    if (result == VK_SUBOPTIMAL_KHR)
      recreate = true;
    const VkDeviceSize bytes = list.vertices.size() * sizeof(Vertex);
    if (bytes > frame.vertices.capacity) {
      VkDeviceSize capacity =
          std::max<VkDeviceSize>(65536, frame.vertices.capacity);
      while (capacity < bytes)
        capacity *= 2;
      allocateBuffer(frame.vertices, capacity,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (bytes) {
      std::memcpy(frame.vertices.mapped, list.vertices.data(),
                  static_cast<size_t>(bytes));
      if (!frame.vertices.coherent) {
        auto range =
            info<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
        range.memory = frame.vertices.memory;
        range.size = VK_WHOLE_SIZE;
        check(vkFlushMappedMemoryRanges(device, 1, &range),
              "Flush geometry memory");
      }
    }
    Buffer readback;
    try {
      if (!path.empty()) {
        if (!canCapture || (format != VK_FORMAT_B8G8R8A8_UNORM &&
                            format != VK_FORMAT_R8G8B8A8_UNORM && !srgb))
          throw std::runtime_error(
              "Surface format/usage does not support PPM capture.");
        allocateBuffer(readback,
                       static_cast<VkDeviceSize>(extent.width) * extent.height *
                           4,
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT);
      }
      check(vkResetCommandBuffer(frame.command, 0), "Reset command buffer");
      auto begin = info<VkCommandBufferBeginInfo>(
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
      check(vkBeginCommandBuffer(frame.command, &begin),
            "Begin frame commands");
      VkClearValue clear{};
      clear.color.float32[0] = list.background.r / 255.f;
      clear.color.float32[1] = list.background.g / 255.f;
      clear.color.float32[2] = list.background.b / 255.f;
      clear.color.float32[3] = list.background.a / 255.f;
      if (srgb)
        for (int i = 0; i < 3; ++i)
          clear.color.float32[i] = linearize(clear.color.float32[i]);
      auto pass =
          info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
      pass.renderPass = renderPass;
      pass.framebuffer = framebuffers[imageIndex];
      pass.renderArea.extent = extent;
      pass.clearValueCount = 1;
      pass.pClearValues = &clear;
      vkCmdBeginRenderPass(frame.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline);
      VkViewport viewport{0,
                          0,
                          static_cast<float>(extent.width),
                          static_cast<float>(extent.height),
                          0,
                          1};
      VkRect2D scissor{{0, 0}, extent};
      vkCmdSetViewport(frame.command, 0, 1, &viewport);
      vkCmdSetScissor(frame.command, 0, 1, &scissor);
      const struct {
        float width, height;
        int32_t srgb;
      } push{static_cast<float>(logicalWidth),
             static_cast<float>(logicalHeight), srgb ? 1 : 0};
      vkCmdPushConstants(frame.command, layout,
                         VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, static_cast<uint32_t>(sizeof(push)), &push);
      if (bytes) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(frame.command, 0, 1, &frame.vertices.handle,
                               &offset);
        vkCmdDraw(frame.command, static_cast<uint32_t>(list.vertices.size()), 1,
                  0, 0);
      }
      vkCmdEndRenderPass(frame.command);
      if (readback.handle) {
        auto barrier =
            info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[imageIndex];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(frame.command,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(frame.command, images[imageIndex],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.handle, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
        auto hostBarrier = info<VkBufferMemoryBarrier>(
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = hostBarrier.dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = readback.handle;
        hostBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &hostBarrier, 0, nullptr);
      }
      check(vkEndCommandBuffer(frame.command), "End frame commands");
      check(vkResetFences(device, 1, &frame.fence), "Reset frame fence");
      const VkPipelineStageFlags wait =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
      submit.waitSemaphoreCount = 1;
      submit.pWaitSemaphores = &frame.available;
      submit.pWaitDstStageMask = &wait;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &frame.command;
      submit.signalSemaphoreCount = 1;
      submit.pSignalSemaphores = &finished[imageIndex];
      check(vkQueueSubmit(graphics, 1, &submit, frame.fence), "Submit frame");
      auto presentInfo =
          info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = &finished[imageIndex];
      presentInfo.swapchainCount = 1;
      presentInfo.pSwapchains = &swapchain;
      presentInfo.pImageIndices = &imageIndex;
      result = vkQueuePresentKHR(presentation, &presentInfo);
      if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        recreate = true;
      else
        check(result, "Present frame");
      if (readback.handle) {
        check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX),
              "Wait for capture");
        if (!readback.coherent) {
          auto range =
              info<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
          range.memory = readback.memory;
          range.size = VK_WHOLE_SIZE;
          check(vkInvalidateMappedMemoryRanges(device, 1, &range),
                "Invalidate capture memory");
        }
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
        const auto *pixels = static_cast<const uint8_t *>(readback.mapped);
        const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM ||
                          format == VK_FORMAT_B8G8R8A8_SRGB;
        for (size_t i = 0;
             i < static_cast<size_t>(extent.width) * extent.height; ++i) {
          const char rgb[] = {
              static_cast<char>(pixels[4 * i + (bgra ? 2 : 0)]),
              static_cast<char>(pixels[4 * i + 1]),
              static_cast<char>(pixels[4 * i + (bgra ? 0 : 2)])};
          file.write(rgb, 3);
        }
        if (!file)
          throw std::runtime_error("Cannot write framebuffer capture: " + path);
      }
    } catch (...) {
      if (readback.handle) {
        vkDeviceWaitIdle(device);
        destroyBuffer(readback);
      }
      throw;
    }
    destroyBuffer(readback);
    frameIndex = (frameIndex + 1) % frames.size();
    return true;
  }
  void cleanup() {
    if (device) {
      vkDeviceWaitIdle(device);
      destroySwapchain();
      for (auto &frame : frames) {
        destroyBuffer(frame.vertices);
        if (frame.available)
          vkDestroySemaphore(device, frame.available, nullptr);
        if (frame.fence)
          vkDestroyFence(device, frame.fence, nullptr);
      }
      if (pool)
        vkDestroyCommandPool(device, pool, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (surface)
      vkDestroySurfaceKHR(instance, surface, nullptr);
    if (debug) {
      auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
      if (destroy)
        destroy(instance, debug, nullptr);
    }
    if (instance)
      vkDestroyInstance(instance, nullptr);
  }
};
VulkanRenderer::VulkanRenderer(GLFWwindow *window)
    : impl_(std::make_unique<Impl>(window)) {}
VulkanRenderer::~VulkanRenderer() = default;
bool VulkanRenderer::present(const DrawList &list,
                             const std::string &capturePath) {
  return impl_->present(list, capturePath);
}
unsigned VulkanRenderer::validationErrors() const {
  return impl_->errors.load();
}
size_t VulkanRenderer::bufferCapacity() const {
  size_t result = 0;
  for (const auto &frame : impl_->frames)
    result += static_cast<size_t>(frame.vertices.capacity);
  return result;
}
} // namespace visualizer
