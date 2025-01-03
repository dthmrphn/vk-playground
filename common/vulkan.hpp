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

    const vk::PhysicalDevice& physical() const;
    const vk::Instance& instance() const;
    const vk::Device& logical() const;

    vk::raii::Queue make_graphic_queue() const;
    vk::raii::Queue make_present_queue() const;
    vk::raii::Queue make_compute_queue() const;

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
    vk::raii::PipelineLayout make_pipeline_layout(const vk::PipelineLayoutCreateInfo& info) const;
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
    swapchain(const device& device, const vk::SurfaceKHR& surf, std::uint32_t w, std::uint32_t h);
    void resize(const device& device, std::uint32_t w, std::uint32_t h);

    const vk::raii::SwapchainKHR& get() const;

    vk::SurfaceFormatKHR format() const;
    vk::Extent2D extent() const;
    std::vector<vk::ImageView> image_views() const;
};

} // namespace vulkan
