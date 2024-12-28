#include "vulkan.hpp"

#include <set>

#include <fmt/core.h>

namespace vulkan {

constexpr static const char* enabled_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr static const char* enabled_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

VKAPI_ATTR VkBool32 VKAPI_CALL device::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT message_types,
                                                      VkDebugUtilsMessengerCallbackDataEXT const* callback_data,
                                                      void* user_data) {
    fmt::print("{}\n", callback_data->pMessage);
    return false;
}

device::device(const vk::ApplicationInfo& app_info,
               const std::vector<const char*>& layers,
               const std::vector<const char*>& extensions,
               vk::QueueFlags queues, bool debug) : device() {
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
        vk::DebugUtilsMessengerCreateInfoEXT debug_ci({}, sev_flags, msg_flags, &device::debug_callback);

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

    vk::DeviceCreateInfo device_ci{{}, queue_ci, enabled_layers, enabled_extensions};
    _logical_dev = {_physical_dev, device_ci};
}

std::uint32_t device::queue_family_index(vk::QueueFlags flags) const {
    const auto props = _physical_dev.getQueueFamilyProperties();
    const auto iter = std::find_if(props.begin(), props.end(), [flags](auto p) {
        return p.queueFlags & flags;
    });

    if (iter == props.end()) {
        throw std::runtime_error(fmt::format("failed to get {} queue index", vk::to_string(flags)));
    }

    return std::distance(props.begin(), iter);
}

std::uint32_t device::memory_type_index(std::uint32_t filter, vk::MemoryPropertyFlags mask) const {
    const auto props = _physical_dev.getMemoryProperties();
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if (filter & (1 << i) && ((props.memoryTypes[i].propertyFlags & mask) == mask)) {
            return i;
        }
    }
    throw std::runtime_error("failed to find memory type");
}

vk::raii::Queue device::make_graphic_queue() const {
    const auto i = queue_family_index(vk::QueueFlagBits::eGraphics);
    return _logical_dev.getQueue(i, 0);
}

vk::raii::Queue device::make_compute_queue() const {
    const auto i = queue_family_index(vk::QueueFlagBits::eCompute);
    return _logical_dev.getQueue(i, 0);
}

vk::raii::Queue device::make_present_queue() const {
    return {nullptr};
}

const vk::raii::PhysicalDevice& device::physical() const {
    return _physical_dev;
}

const vk::raii::Instance& device::instance() const {
    return _instance;
}

const vk::raii::Device& device::logical() const {
    return _logical_dev;
}

buffer::buffer(const device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags mask) {
    vk::BufferCreateInfo ci{{}, size, usage, vk::SharingMode::eExclusive};
    _buf = {device.logical(), ci};

    const auto req = _buf.getMemoryRequirements();
    const auto index = device.memory_type_index(req.memoryTypeBits, mask);

    vk::MemoryAllocateInfo ai{req.size, index};
    _mem = {device.logical(), ai};
    _buf.bindMemory(_mem, 0);
}

const vk::raii::Buffer& buffer::buf() const {
    return _buf;
}

const vk::raii::DeviceMemory& buffer::mem() const {
    return _mem;
}

device_buffer::device_buffer(const device& device, vk::DeviceSize size, vk::BufferUsageFlags usage)
    : buffer(device, size, usage, vk::MemoryPropertyFlagBits::eDeviceLocal) {}

host_buffer::host_buffer(const device& device, vk::DeviceSize size, vk::BufferUsageFlags usage)
    : buffer(device, size, usage, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent) {
    _mapped = _mem.mapMemory(0, size);
}

void host_buffer::copy(void* data, vk::DeviceSize size) const {
    std::memcpy(_mapped, data, size);
}

texture::texture(const device& device, std::uint32_t width, std::uint32_t height) {
    vk::ImageCreateInfo ici{
        {},
        vk::ImageType::e2D,
        vk::Format::eR8G8B8A8Srgb,
        {width, height, 1},
        1,
        1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive,
    };

    _img = {device.logical(), ici};

    const auto req = _img.getMemoryRequirements();
    const auto index = device.memory_type_index(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::MemoryAllocateInfo mai{req.size, index};
    _mem = {device.logical(), mai};
    _img.bindMemory(_mem, 0);

    vk::ImageViewCreateInfo ivci{
        {},
        _img,
        vk::ImageViewType::e2D,
        vk::Format::eR8G8B8A8Srgb,
        {},
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    _view = {device.logical(), ivci};

    vk::SamplerCreateInfo sic{
        {},
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        {},
    };

    _sampler = {device.logical(), sic};
}

swapchain::swapchain(const device& device, const vk::SurfaceKHR& surf, std::uint32_t w, std::uint32_t h) {
    _surface = {device.instance(), surf};
    
    resize(device, w, h);
}

void swapchain::resize(const device& device, std::uint32_t w, std::uint32_t h) {
    const auto formats = device.physical().getSurfaceFormatsKHR(_surface);
    _format = formats[0];
    for (const auto& f : formats) {
        if (f.format == vk::Format::eB8G8R8A8Unorm && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            _format = f;
        }
    }

    const auto capabilities = device.physical().getSurfaceCapabilitiesKHR(_surface);
    _extent = vk::Extent2D{
        std::clamp(w, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(h, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };

    const auto pre_transform = capabilities.currentTransform;
    const auto composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    const auto present_mode = vk::PresentModeKHR::eMailbox;

    vk::SwapchainCreateInfoKHR sci{
        {},
        _surface,
        capabilities.minImageCount + 1,
        _format.format,
        _format.colorSpace,
        _extent,
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        nullptr,
        pre_transform,
        composite_alpha,
        present_mode,
        true,
        _swapchain,
    };

    _swapchain = {device.logical(), sci};

    vk::ImageViewCreateInfo ci{
        {},
        {},
        vk::ImageViewType::e2D,
        _format.format,
        {
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
        },
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    const auto images = _swapchain.getImages();
    _image_views.clear();
    for (const auto& image : images) {
        ci.image = image;
        _image_views.emplace_back(device.logical(), ci);
    }
}

const vk::raii::SwapchainKHR& swapchain::get() const {
    return _swapchain;
}

vk::SurfaceFormatKHR swapchain::format() const {
    return _format;
}

vk::Extent2D swapchain::extent() const {
    return _extent;
}

std::vector<vk::ImageView> swapchain::image_views() const {
    std::vector<vk::ImageView> views{};
    for (const auto& iv : _image_views) {
        views.push_back(*iv);
    }
    return views;
}

} // namespace vulkan
