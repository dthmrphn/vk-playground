#pragma once

#include <cstdint>

#include <vulkan/vulkan_raii.hpp>

class vulkan_device {
    vk::raii::Context _context;
    vk::raii::Instance _instance;
    vk::raii::DebugUtilsMessengerEXT _dbg_msgr;
    vk::raii::PhysicalDevice _physical_dev;
    vk::raii::Device _logical_dev;

  public:
    vulkan_device();
    vulkan_device(const vk::ApplicationInfo& app_info,
                  const std::vector<const char*>& layers,
                  const std::vector<const char*>& extensions,
                  vk::QueueFlags queues, bool debug);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                         VkDebugUtilsMessageTypeFlagsEXT,
                                                         const VkDebugUtilsMessengerCallbackDataEXT*,
                                                         void*);

    std::uint32_t queue_family_index(vk::QueueFlags flags) const;

    vk::Instance instance() const;
    vk::Device device() const;

    vk::raii::Queue make_graphic_queue() const;
    vk::raii::Queue make_present_queue() const;
    vk::raii::Queue make_compute_queue() const;
};
