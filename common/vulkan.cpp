#include "vulkan.hpp"

#include <set>

#include <fmt/core.h>

namespace vulkan {

VKAPI_ATTR VkBool32 VKAPI_CALL device::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT message_types,
                                                      VkDebugUtilsMessengerCallbackDataEXT const* callback_data,
                                                      void* user_data) {
    fmt::print("{}\n\n", callback_data->pMessage);
    return false;
}

device::device(const vk::ApplicationInfo& app_info,
               const vk::ArrayProxy<const char*>& layers,
               const vk::ArrayProxy<const char*>& device_extensions,
               const vk::ArrayProxy<const char*>& instance_extensions,
               vk::QueueFlags queues, bool debug) : device() {
    _instance = {
        _context,
        vk::InstanceCreateInfo{{}, &app_info, layers, instance_extensions},
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

    vk::DeviceCreateInfo device_ci{{}, queue_ci, layers, device_extensions};
    _logical_dev = {_physical_dev, device_ci};

    _graphic_queue = _logical_dev.getQueue(queue_family_index(vk::QueueFlagBits::eGraphics), 0);
    _compute_queue = _logical_dev.getQueue(queue_family_index(vk::QueueFlagBits::eCompute), 0);
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

const vk::PhysicalDevice& device::physical() const {
    return *_physical_dev;
}

const vk::Instance& device::instance() const {
    return *_instance;
}

const vk::Device& device::logical() const {
    return *_logical_dev;
}

const vk::Queue& device::graphic_queue() const {
    return *_graphic_queue;
}

const vk::Queue& device::compute_queue() const {
    return *_compute_queue;
}

const vk::Queue& device::present_queue() const {
    return *_present_queue;
}

vk::raii::Buffer device::make_buffer(const vk::BufferCreateInfo info) const {
    return _logical_dev.createBuffer(info);
}

vk::raii::DeviceMemory device::make_memory(const vk::MemoryAllocateInfo& info) const {
    return _logical_dev.allocateMemory(info);
}

vk::raii::Image device::make_image(const vk::ImageCreateInfo& info) const {
    return _logical_dev.createImage(info);
}

vk::raii::ImageView device::make_image_view(const vk::ImageViewCreateInfo& info) const {
    return _logical_dev.createImageView(info);
}

vk::raii::Sampler device::make_sampler(const vk::SamplerCreateInfo& info) const {
    return _logical_dev.createSampler(info);
}

vk::raii::SurfaceKHR device::make_surface(const vk::SurfaceKHR& surf) const {
    return {_instance, surf};
}

vk::raii::SwapchainKHR device::make_swapchain(const vk::SwapchainCreateInfoKHR& info) const {
    return {_logical_dev, info};
}

vk::raii::CommandPool device::make_command_pool(const vk::CommandPoolCreateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::CommandBuffers device::make_command_buffers(const vk::CommandBufferAllocateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::RenderPass device::make_render_pass(const vk::RenderPassCreateInfo& info) const {
    return _logical_dev.createRenderPass(info);
}

vk::raii::Framebuffer device::make_framebuffer(const vk::FramebufferCreateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::Fence device::make_fence(const vk::FenceCreateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::Semaphore device::make_semaphore(const vk::SemaphoreCreateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::DescriptorSetLayout device::make_descriptor_set_layout(const vk::DescriptorSetLayoutCreateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::DescriptorPool device::make_descriptor_pool(const vk::DescriptorPoolCreateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::DescriptorSets device::make_descriptor_sets(const vk::DescriptorSetAllocateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::ShaderModule device::make_shader_module(const vk::ShaderModuleCreateInfo& info) const {
    return {_logical_dev, info};
}

vk::raii::Pipeline device::make_pipeline(const vk::GraphicsPipelineCreateInfo& info) const {
    return {_logical_dev, nullptr, info};
}

vk::raii::Pipeline device::make_pipeline(const vk::ComputePipelineCreateInfo& info) const {
    return {_logical_dev, nullptr, info};
}

vk::raii::PipelineLayout device::make_pipeline_layout(const vk::PipelineLayoutCreateInfo& info) const {
    return {_logical_dev, info};
}

void device::copy_buffers(const vk::Buffer& src, const vk::Buffer& dst, vk::DeviceSize size) const {
    const auto i = queue_family_index(vk::QueueFlagBits::eTransfer);
    const auto q = _logical_dev.getQueue(i, 0);
    const auto pool = make_command_pool({
        vk::CommandPoolCreateFlagBits::eTransient,
        i,
    });

    const auto cb = std::move(make_command_buffers({pool, vk::CommandBufferLevel::ePrimary, 1}).front());
    cb.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    utils::copy_buffers(cb, src, dst, size);
    cb.end();

    q.submit(vk::SubmitInfo{{}, {}, *cb});
    q.waitIdle();
}

void device::copy_buffer_to_image(const vk::Buffer& buf, const vk::Image& img, vk::Extent3D extent, vk::ImageLayout new_layout) const {
    const auto i = queue_family_index(vk::QueueFlagBits::eTransfer);
    const auto q = _logical_dev.getQueue(i, 0);
    const auto pool = make_command_pool({
        vk::CommandPoolCreateFlagBits::eTransient,
        i,
    });

    const auto cb = std::move(make_command_buffers({pool, vk::CommandBufferLevel::ePrimary, 1}).front());

    cb.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    utils::copy_buffer_to_image(cb, buf, img, extent, new_layout);
    cb.end();

    q.submit(vk::SubmitInfo{{}, {}, *cb});
    q.waitIdle();
}

void device::image_transition(const vk::Image& img, vk::ImageLayout old_layout, vk::ImageLayout new_layout) const {
    const auto i = queue_family_index(vk::QueueFlagBits::eTransfer);
    const auto q = _logical_dev.getQueue(i, 0);
    const auto pool = make_command_pool({
        vk::CommandPoolCreateFlagBits::eTransient,
        i,
    });

    const auto cb = std::move(make_command_buffers({pool, vk::CommandBufferLevel::ePrimary, 1}).front());

    cb.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    utils::image_transition(cb, img, old_layout, new_layout);
    cb.end();

    q.submit(vk::SubmitInfo{{}, {}, *cb});
    q.waitIdle();
}

buffer::buffer(const device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags mask) : _size(size) {
    vk::BufferCreateInfo ci{{}, size, usage, vk::SharingMode::eExclusive};
    _buf = device.make_buffer(ci);

    const auto req = _buf.getMemoryRequirements();
    const auto index = device.memory_type_index(req.memoryTypeBits, mask);

    vk::MemoryAllocateInfo ai{req.size, index};
    _mem = device.make_memory(ai);
    _buf.bindMemory(_mem, 0);
}

const vk::Buffer& buffer::buf() const {
    return *_buf;
}

const vk::DeviceMemory& buffer::mem() const {
    return *_mem;
}

const vk::DeviceSize buffer::size() const {
    return _size;
}

device_buffer::device_buffer(const device& device, vk::DeviceSize size, vk::BufferUsageFlags usage)
    : buffer(device, size, usage, vk::MemoryPropertyFlagBits::eDeviceLocal) {}

host_buffer::host_buffer(const device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, const void* data)
    : buffer(device, size, usage, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached) {
    _mapped = _mem.mapMemory(0, size);

    if (data) {
        copy(data, size);
    }
}

void host_buffer::copy(const void* data, vk::DeviceSize size) const {
    std::memcpy(_mapped, data, size);
}

void host_buffer::copy_to(void* data, vk::DeviceSize size) const {
    std::memcpy(data, _mapped, size);
}

texture::texture(const device& device, std::uint32_t width, std::uint32_t height)
    : texture(device, width, height, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst) {}

texture::texture(const device& device, std::uint32_t width, std::uint32_t height, vk::ImageUsageFlags usage)
    : _extent(width, height, 1), _width(width), _height(height) {
    vk::ImageCreateInfo ici{
        {},
        vk::ImageType::e2D,
        vk::Format::eR8G8B8A8Unorm,
        {width, height, 1},
        1,
        1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        usage,
        vk::SharingMode::eExclusive,
    };

    _img = device.make_image(ici);

    const auto req = _img.getMemoryRequirements();
    const auto index = device.memory_type_index(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::MemoryAllocateInfo mai{req.size, index};
    _mem = device.make_memory(mai);
    _img.bindMemory(_mem, 0);

    vk::ImageViewCreateInfo ivci{
        {},
        _img,
        vk::ImageViewType::e2D,
        vk::Format::eR8G8B8A8Unorm,
        {},
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    _view = device.make_image_view(ivci);

    vk::SamplerCreateInfo sic{
        {},
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        {},
    };

    _sampler = device.make_sampler(sic);
}

const vk::Image& texture::image() const {
    return *_img;
}
const vk::ImageView& texture::view() const {
    return *_view;
}

const vk::Sampler& texture::sampler() const {
    return *_sampler;
}

const vk::Extent3D texture::extent() const {
    return _extent;
}

std::uint32_t texture::width() const {
    return _width;
}

std::uint32_t texture::height() const {
    return _height;
}

swapchain::swapchain(const device& device, const vk::SurfaceKHR& surf, std::uint32_t w, std::uint32_t h) {
    _surface = device.make_surface(surf);

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

    _swapchain = device.make_swapchain(sci);

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
        _image_views.emplace_back(device.make_image_view(ci));
    }
}

const vk::SwapchainKHR& swapchain::get() const {
    return *_swapchain;
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

std::pair<vk::Result, std::uint32_t> swapchain::acquire_next(std::uint64_t timeout, const vk::Semaphore& semaphore, const vk::Fence& fence) const {
    return _swapchain.acquireNextImage(timeout, semaphore, fence);
}

namespace utils {
void copy_buffers(const vk::CommandBuffer& cb, const vk::Buffer& src, const vk::Buffer& dst, vk::DeviceSize size) {
    cb.copyBuffer(src, dst, {{0, 0, size}});
}

void image_transition(const vk::CommandBuffer& cb, const vk::Image& img, vk::ImageLayout old_layout, vk::ImageLayout new_layout) {
    const auto src_stage{vk::PipelineStageFlagBits::eAllCommands};
    const auto dst_stage{vk::PipelineStageFlagBits::eAllCommands};

    vk::ImageMemoryBarrier barrier{
        {},
        {},
        old_layout,
        new_layout,
        {},
        {},
        img,
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    switch (old_layout) {
    case vk::ImageLayout::eUndefined:
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        break;
    case vk::ImageLayout::eTransferSrcOptimal:
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        break;
    case vk::ImageLayout::eTransferDstOptimal:
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        break;
    default:
        break;
    }

    switch (new_layout) {
    case vk::ImageLayout::eTransferSrcOptimal:
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        break;
    case vk::ImageLayout::eTransferDstOptimal:
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        break;
    default:
        break;
    }

    cb.pipelineBarrier(src_stage, dst_stage, {}, {}, {}, barrier);
}

void copy_buffer_to_image(const vk::CommandBuffer& cb, const vk::Buffer& buf, const vk::Image& img, vk::Extent3D extent, vk::ImageLayout new_layout) {
    image_transition(cb, img, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

    cb.copyBufferToImage(buf,
                         img,
                         vk::ImageLayout::eTransferDstOptimal,
                         vk::BufferImageCopy{
                             0,
                             0,
                             0,
                             {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                             {0, 0, 0},
                             extent,
                         });

    image_transition(cb, img, vk::ImageLayout::eTransferDstOptimal, new_layout);
}

} // namespace utils

} // namespace vulkan
