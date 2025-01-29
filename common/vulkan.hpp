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

    vk::raii::Queue _graphic_queue{nullptr};
    vk::raii::Queue _present_queue{nullptr};
    vk::raii::Queue _compute_queue{nullptr};

  public:
    device() = default;
    device(const vk::ApplicationInfo& app_info,
           const vk::ArrayProxy<const char*>& layers,
           const vk::ArrayProxy<const char*>& device_extensions,
           const vk::ArrayProxy<const char*>& instance_extensions,
           vk::QueueFlags queues, bool debug);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                         VkDebugUtilsMessageTypeFlagsEXT,
                                                         const VkDebugUtilsMessengerCallbackDataEXT*,
                                                         void*);

    std::uint32_t queue_family_index(vk::QueueFlags flags) const;
    std::uint32_t memory_type_index(std::uint32_t filter, vk::MemoryPropertyFlags mask) const;

    const vk::PhysicalDevice& physical() const;
    const vk::Instance& instance() const;
    const vk::Device& logical() const;

    const vk::Queue& graphic_queue() const;
    const vk::Queue& present_queue() const;
    const vk::Queue& compute_queue() const;

    vk::raii::Buffer make_buffer(const vk::BufferCreateInfo info) const;
    vk::raii::DeviceMemory make_memory(const vk::MemoryAllocateInfo& info) const;
    vk::raii::Image make_image(const vk::ImageCreateInfo& info) const;
    vk::raii::ImageView make_image_view(const vk::ImageViewCreateInfo& info) const;
    vk::raii::Sampler make_sampler(const vk::SamplerCreateInfo& info) const;

    vk::raii::SurfaceKHR make_surface(const vk::SurfaceKHR& surf) const;
    vk::raii::SwapchainKHR make_swapchain(const vk::SwapchainCreateInfoKHR& info) const;

    vk::raii::CommandPool make_command_pool(const vk::CommandPoolCreateInfo& info) const;
    vk::raii::CommandBuffers make_command_buffers(const vk::CommandBufferAllocateInfo& info) const;

    vk::raii::RenderPass make_render_pass(const vk::RenderPassCreateInfo& info) const;
    vk::raii::Framebuffer make_framebuffer(const vk::FramebufferCreateInfo& info) const;

    vk::raii::Fence make_fence(const vk::FenceCreateInfo& info) const;
    vk::raii::Semaphore make_semaphore(const vk::SemaphoreCreateInfo& info) const;

    vk::raii::DescriptorSetLayout make_descriptor_set_layout(const vk::DescriptorSetLayoutCreateInfo& info) const;
    vk::raii::DescriptorPool make_descriptor_pool(const vk::DescriptorPoolCreateInfo& info) const;
    vk::raii::DescriptorSets make_descriptor_sets(const vk::DescriptorSetAllocateInfo& info) const;

    vk::raii::ShaderModule make_shader_module(const vk::ShaderModuleCreateInfo& info) const;
    vk::raii::Pipeline make_pipeline(const vk::GraphicsPipelineCreateInfo& info) const;
    vk::raii::Pipeline make_pipeline(const vk::ComputePipelineCreateInfo& info) const;
    vk::raii::PipelineLayout make_pipeline_layout(const vk::PipelineLayoutCreateInfo& info) const;

    void copy_buffers(const vk::Buffer& src, const vk::Buffer& dst, vk::DeviceSize size) const;
    void copy_buffer_to_image(const vk::Buffer& buf, const vk::Image& img, vk::Extent3D extent, vk::ImageLayout new_layout) const;
    void image_transition(const vk::Image& img, vk::ImageLayout old_layout, vk::ImageLayout new_layout) const;
};

namespace utils {
void copy_buffers(const vk::CommandBuffer& cb, const vk::Buffer& src, const vk::Buffer& dst, vk::DeviceSize size);
void copy_buffer_to_image(const vk::CommandBuffer& cb, const vk::Buffer& buf, const vk::Image& img, vk::Extent3D extent, vk::ImageLayout new_layout);
void image_transition(const vk::CommandBuffer& cb, const vk::Image& img, vk::ImageLayout old_layout, vk::ImageLayout new_layout);
} // namespace utils

class buffer {
  protected:
    vk::raii::Buffer _buf{nullptr};
    vk::raii::DeviceMemory _mem{nullptr};
    vk::DeviceSize _size;

  public:
    buffer() = default;
    buffer(const device& dev, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags mask);

    const vk::Buffer& buf() const;
    const vk::DeviceMemory& mem() const;
    const vk::DeviceSize size() const;
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
    host_buffer(const device& dev, vk::DeviceSize size, vk::BufferUsageFlags usage, const void* data = nullptr);

    void copy(const void* data, vk::DeviceSize size) const;
    void copy_to(void* data, vk::DeviceSize size) const;
};

class texture {
    vk::raii::Image _img{nullptr};
    vk::raii::ImageView _view{nullptr};
    vk::raii::DeviceMemory _mem{nullptr};
    vk::raii::Sampler _sampler{nullptr};
    vk::Extent3D _extent{};
    std::uint32_t _width;
    std::uint32_t _height;

  public:
    texture() = default;
    texture(const device& device, std::uint32_t width, std::uint32_t height);
    texture(const device& device, std::uint32_t width, std::uint32_t height, vk::ImageUsageFlags usage);

    static constexpr vk::DescriptorSetLayoutBinding layout_binding(std::uint32_t binding) {
        return {binding, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eAll};
    }

    const vk::Image& image() const;
    const vk::ImageView& view() const;
    const vk::Sampler& sampler() const;
    const vk::Extent3D extent() const;
    std::uint32_t width() const;
    std::uint32_t height() const;
};

class swapchain {
    vk::raii::SurfaceKHR _surface{nullptr};
    vk::raii::SwapchainKHR _swapchain{nullptr};
    vk::SurfaceFormatKHR _format{};
    vk::Extent2D _extent{};
    std::vector<vk::raii::ImageView> _image_views{};

  public:
    swapchain() = default;
    swapchain(const device& device, const vk::SurfaceKHR& surf, std::uint32_t w, std::uint32_t h);
    void resize(const device& device, std::uint32_t w, std::uint32_t h);

    const vk::SwapchainKHR& get() const;

    vk::SurfaceFormatKHR format() const;
    vk::Extent2D extent() const;
    std::vector<vk::ImageView> image_views() const;

    std::pair<vk::Result, std::uint32_t> acquire_next(std::uint64_t timeout, const vk::Semaphore& semaphore = {}, const vk::Fence& fence = {}) const;
};

} // namespace vulkan
