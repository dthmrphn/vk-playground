#include "device.hpp"

#include <set>

#include <fmt/core.h>

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_device::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                             VkDebugUtilsMessageTypeFlagsEXT message_types,
                                                             VkDebugUtilsMessengerCallbackDataEXT const* callback_data,
                                                             void* user_data) {
    fmt::print("{}\n", callback_data->pMessage);
    return false;
}

vulkan_device::vulkan_device()
    : _instance(nullptr), _dbg_msgr(nullptr), _physical_dev(nullptr), _logical_dev(nullptr) {}

vulkan_device::vulkan_device(const vk::ApplicationInfo& app_info,
                             const std::vector<const char*>& layers,
                             const std::vector<const char*>& extensions,
                             vk::QueueFlags queues, bool debug) : vulkan_device() {
    _instance = {
        _context,
        vk::InstanceCreateInfo{{}, &app_info, layers, extensions},
    };

    _physical_dev = vk::raii::PhysicalDevices{_instance}.front();

    if (debug) {
        using severity = vk::DebugUtilsMessageSeverityFlagBitsEXT;
        using type = vk::DebugUtilsMessageTypeFlagBitsEXT;
        constexpr auto sev_flags{severity::eWarning | severity::eError};
        constexpr auto msg_flags{type::eGeneral | type::ePerformance | type::eValidation};
        vk::DebugUtilsMessengerCreateInfoEXT debug_ci({}, sev_flags, msg_flags, &vulkan_device::debug_callback);

        _dbg_msgr = {_instance, debug_ci};
    }

    std::set<std::uint32_t> indices{};
    
    if (queues & vk::QueueFlagBits::eGraphics) {
        const auto index = queue_family_index(vk::QueueFlagBits::eGraphics);
        indices.insert(index);
    }

    if (queues & vk::QueueFlagBits::eCompute) {
        const auto index = queue_family_index(vk::QueueFlagBits::eCompute);
        indices.insert(index);
    }

    if (queues & vk::QueueFlagBits::eTransfer) {
        const auto index = queue_family_index(vk::QueueFlagBits::eTransfer);
        indices.insert(index);
    }

    const auto priority{1.0f};
    std::vector<vk::DeviceQueueCreateInfo> queue_ci;
    for (const auto i : indices) {
        queue_ci.emplace_back(vk::DeviceQueueCreateFlags(), i, 1, &priority);
    }

    vk::DeviceCreateInfo device_ci{{}, queue_ci, layers, extensions};
    _logical_dev = {_physical_dev, device_ci};
}

std::uint32_t vulkan_device::queue_family_index(vk::QueueFlags flags) const {
    const auto props = _physical_dev.getQueueFamilyProperties();
    const auto iter = std::find_if(props.begin(), props.end(), [flags](auto p) {
        return p.queueFlags & flags;
    });

    if (iter == props.end()) {
        throw std::runtime_error(fmt::format("failed to get {} queue index", vk::to_string(flags)));
    }

    return std::distance(props.begin(), iter);
}

vk::raii::Queue vulkan_device::make_graphic_queue() const {
    const auto i = queue_family_index(vk::QueueFlagBits::eGraphics);
    return _logical_dev.getQueue(i, 0);
}

vk::raii::Queue vulkan_device::make_compute_queue() const {
    const auto i = queue_family_index(vk::QueueFlagBits::eCompute);
    return _logical_dev.getQueue(i, 0);
}

vk::raii::Queue vulkan_device::make_present_queue() const {
    return {nullptr};
}

vk::Instance vulkan_device::instance() const {
    return _instance;
}

vk::Device vulkan_device::device() const {
    return _logical_dev;
}
