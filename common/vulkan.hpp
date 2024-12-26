#pragma once

#include <cstdint>

#include <vulkan/vulkan_raii.hpp>

namespace vulkan {

class device {
    vk::raii::Context _context;
    vk::raii::Instance _instance{nullptr};
    vk::raii::DebugUtilsMessengerEXT _dbg_msgr{nullptr};
    vk::raii::PhysicalDevice _physical_dev{nullptr};
    vk::raii::Device _logical_dev{nullptr};

  public:
    device() = default;
    device(const vk::ApplicationInfo& app_info,
           const std::vector<const char*>& layers,
           const std::vector<const char*>& extensions,
           vk::QueueFlags queues, bool debug);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                         VkDebugUtilsMessageTypeFlagsEXT,
                                                         const VkDebugUtilsMessengerCallbackDataEXT*,
                                                         void*);

    std::uint32_t queue_family_index(vk::QueueFlags flags) const;
    std::uint32_t memory_type_index(std::uint32_t filter, vk::MemoryPropertyFlags mask) const;

    const vk::raii::PhysicalDevice& physical() const;
    const vk::raii::Instance& instance() const;
    const vk::raii::Device& logical() const;

    vk::raii::Queue make_graphic_queue() const;
    vk::raii::Queue make_present_queue() const;
    vk::raii::Queue make_compute_queue() const;
};

class buffer {
  protected:
    vk::raii::Buffer _buf{nullptr};
    vk::raii::DeviceMemory _mem{nullptr};

  public:
    buffer() = default;
    buffer(const device& dev, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags mask);

    const vk::raii::Buffer& buf() const;
    const vk::raii::DeviceMemory& mem() const;
};

class device_buffer : public buffer {
  public:
    device_buffer() = default;
    device_buffer(const device& dev, vk::DeviceSize size, vk::BufferUsageFlags usage);
};

class host_buffer : public buffer {
    void* _mapped{nullptr};

  public:
    host_buffer() = default;
    host_buffer(const device& dev, vk::DeviceSize size, vk::BufferUsageFlags usage);

    void copy(void* data, vk::DeviceSize size) const;
};

class texture {
    vk::raii::Image _img{nullptr};
    vk::raii::ImageView _view{nullptr};
    vk::raii::DeviceMemory _mem{nullptr};
    vk::raii::Sampler _sampler{nullptr};

  public:
    texture() = default;
    texture(const device& device, std::uint32_t width, std::uint32_t height);

    static constexpr vk::DescriptorSetLayoutBinding layout_binding(std::uint32_t binding) {
        return {binding, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
    }
};

class swapchain {
    vk::raii::SurfaceKHR _surface{nullptr};
    vk::raii::SwapchainKHR _swapchain{nullptr};
    vk::SurfaceFormatKHR _format{};
    vk::Extent2D _extent{};
    std::vector<vk::raii::ImageView> _image_views{};

  public:
    swapchain() = default;
    swapchain(const device& dev, const vk::SurfaceKHR& surf, std::uint32_t w, std::uint32_t h);
    
    const vk::raii::SwapchainKHR& get() const;

    vk::SurfaceFormatKHR format() const;
    vk::Extent2D extent() const;
    std::vector<vk::ImageView> image_views() const;
};

} // namespace vulkan
