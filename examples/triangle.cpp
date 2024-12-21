#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include <fmt/core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan_raii.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

constexpr static const char* enabled_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr static const char* enabled_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

constexpr std::uint32_t vert_shader_code[] = {
#include <triangle.vert.spv.hpp>
};
constexpr std::uint32_t frag_shader_code[] = {
#include <triangle.frag.spv.hpp>
};

static void check_layer(std::string_view layer) {
    const auto layers = vk::enumerateInstanceLayerProperties();
    for (const auto& l : layers) {
        if (l.layerName.data() == layer) {
            return;
        }
    }

    throw std::runtime_error(fmt::format("layer {} is not present or not installed", layer));
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                              VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                              VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
                                              void* /*pUserData*/) {
    fmt::print("{}\n", pCallbackData->pMessage);
    return false;
}

class glfw_window {
    GLFWwindow* _window;

  public:
    glfw_window(std::uint32_t width, std::uint32_t height, const char* name) {
        glfwInit();
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        _window = glfwCreateWindow(width, height, name, nullptr, nullptr);
    }

    vk::SurfaceKHR get_surface(const vk::raii::Instance& instance) const {
        VkSurfaceKHR surf{};
        glfwCreateWindowSurface(static_cast<VkInstance>(*instance), _window, nullptr, &surf);
        return surf;
    }

    std::vector<const char*> get_extensions() const {
        std::uint32_t count{};
        const auto exts = glfwGetRequiredInstanceExtensions(&count);
        return {exts, exts + count};
    }

    template <typename F>
    void loop_handler(F&& f) {
        while (!glfwWindowShouldClose(_window)) {
            glfwPollEvents();
            f();
        }
    }
};

struct vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static constexpr vk::VertexInputBindingDescription binding_desc() {
        return {0, sizeof(vertex), vk::VertexInputRate::eVertex};
    }

    static constexpr std::array<vk::VertexInputAttributeDescription, 2> attribute_desc() {
        return {{
            {0, 0, vk::Format::eR32G32Sfloat},
            {1, 0, vk::Format::eR32G32B32Sfloat},
        }};
    }
};

struct uniform {
    glm::mat4 m;
    glm::mat4 v;
    glm::mat4 p;

    static constexpr vk::DescriptorSetLayoutBinding layout_binding() {
        return {0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex};
    }
};

static std::uint32_t find_memory_type(const vk::PhysicalDeviceMemoryProperties& props, std::uint32_t filter, vk::MemoryPropertyFlags mask) {
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if (filter & (1 << i) && ((props.memoryTypes[i].propertyFlags & mask) == mask)) {
            return i;
        }
    }
    throw std::runtime_error("failed to find memory type");
}

struct buffer {
    vk::raii::Buffer buf{nullptr};
    vk::raii::DeviceMemory mem{nullptr};
    void* mapped{nullptr};

    buffer() = default;

    buffer(const vk::raii::Device& dev, const vk::PhysicalDeviceMemoryProperties& props, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags mask) {
        vk::BufferCreateInfo ci{{}, size, usage, vk::SharingMode::eExclusive};
        buf = {dev, ci};
        const auto req = buf.getMemoryRequirements();
        vk::MemoryAllocateInfo ai{req.size, find_memory_type(props, req.memoryTypeBits, mask)};
        mem = {dev, ai};
        buf.bindMemory(mem, 0);

        if (mask & vk::MemoryPropertyFlagBits::eHostVisible) {
            mapped = mem.mapMemory(0, size);
        }
    }

    void copy_from_host(void* data, vk::DeviceSize size) const {
        if (!mapped) {
            throw std::runtime_error("device memory isn't mapped for buffer");
        }
        std::memcpy(mapped, data, size);
    }
};

struct texture {
    vk::raii::Image img{nullptr};
    vk::raii::ImageView view{nullptr};
    vk::raii::DeviceMemory mem{nullptr};
    vk::raii::Sampler sampler{nullptr};

    texture() = default;

    texture(const vk::raii::Device& device, const vk::PhysicalDeviceMemoryProperties& props, std::uint32_t width, std::uint32_t height) {
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

        img = {device, ici};

        const auto req = img.getMemoryRequirements();
        vk::MemoryAllocateInfo mai{req.size, find_memory_type(props, req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)};
        mem = {device, mai};
        img.bindMemory(mem, 0);

        vk::ImageViewCreateInfo ivci{
            {},
            img,
            vk::ImageViewType::e2D,
            vk::Format::eR8G8B8A8Srgb,
            {},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        view = {device, ivci};

        vk::SamplerCreateInfo sic{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            {},
        };
        sampler = {device, sic};
    }
};

class triangle {
    vk::raii::Context _context;
    vk::raii::Instance _instance{nullptr};
    vk::raii::DebugUtilsMessengerEXT _dbg_msgr{nullptr};
    vk::raii::PhysicalDevice _gpu{nullptr};
    vk::raii::Device _device{nullptr};

    vk::raii::SurfaceKHR _surface{nullptr};
    vk::SurfaceFormatKHR _surface_format;
    vk::Extent2D _surface_extent;

    vk::raii::SwapchainKHR _swapchain{nullptr};
    std::vector<vk::raii::ImageView> _image_views{};

    std::uint32_t _graphics_queue_index{};
    std::uint32_t _present_queue_index{};

    vk::raii::Queue _graphics_queue{nullptr};
    vk::raii::Queue _present_queue{nullptr};

    vk::raii::RenderPass _render_pass{nullptr};
    vk::raii::Pipeline _pipeline{nullptr};
    vk::raii::PipelineLayout _pipeline_layout{nullptr};

    std::vector<vk::raii::Framebuffer> _framebuffers{};

    vk::raii::CommandPool _command_pool{nullptr};
    vk::raii::CommandBuffer _command_buffer{nullptr};

    vk::raii::Semaphore _image_available_semaphore{nullptr};
    vk::raii::Semaphore _render_finished_semaphore{nullptr};
    vk::raii::Fence _fence{nullptr};

    buffer _verticies_buffer;
    buffer _indices_buffer;
    buffer _uniform_buffer;

    texture _texture;

    vk::raii::DescriptorSetLayout _descriptor_layout{nullptr};
    vk::raii::DescriptorPool _descriptor_pool{nullptr};
    vk::raii::DescriptorSet _descriptor_set{nullptr};

  public:
    triangle() = default;

    void make_instance(const std::vector<const char*>& extensions);
    const vk::raii::Instance& get_instance() const;
    void make_debug_messenger();
    void make_physical_device();
    void make_surface(const vk::SurfaceKHR& surf, std::uint32_t width, std::uint32_t height);
    void make_logical_device();
    void make_swapchain(std::uint32_t width, std::uint32_t height);
    void make_renderpass();
    void make_texture_image();
    void make_vertex_buffer();
    void make_indices_buffer();
    void make_uniform_buffer();
    void make_descriptor_set();
    void make_pipeline();
    void make_framebuffers();
    void make_command_buffer();
    void make_synchronization();
    void render();
    void wait_finished();
};

void triangle::make_instance(const std::vector<const char*>& extensions) {
    vk::ApplicationInfo ai{"triangle", 1, "engine", 1, VK_API_VERSION_1_0};
    _instance = {
        _context,
        vk::InstanceCreateInfo{{}, &ai, 1, enabled_layers, (std::uint32_t)extensions.size(), extensions.data()},
    };
}

const vk::raii::Instance& triangle::get_instance() const {
    return _instance;
}

void triangle::make_debug_messenger() {
    vk::DebugUtilsMessageSeverityFlagsEXT severity_flags{vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError};
    vk::DebugUtilsMessageTypeFlagsEXT message_flags{vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation};
    vk::DebugUtilsMessengerCreateInfoEXT debug_ci({}, severity_flags, message_flags, &debug_callback);
    _dbg_msgr = {_instance, debug_ci};
}

void triangle::make_physical_device() {
    vk::raii::PhysicalDevices devs{_instance};
    _gpu = devs.front();
}

void triangle::make_surface(const vk::SurfaceKHR& surf, std::uint32_t width, std::uint32_t height) {
    const auto props = _gpu.getQueueFamilyProperties();
    const auto iter = std::find_if(props.begin(), props.end(), [](auto p) {
        return p.queueFlags & vk::QueueFlagBits::eGraphics;
    });

    _graphics_queue_index = std::distance(props.begin(), iter);
    _surface = {_instance, surf};

    if (_gpu.getSurfaceSupportKHR(_graphics_queue_index, _surface)) {
        _present_queue_index = _graphics_queue_index;
    }
}

void triangle::make_logical_device() {
    float priority{1.0f};
    vk::DeviceQueueCreateInfo qci{{}, _graphics_queue_index, 1, &priority};
    vk::DeviceCreateInfo dci{{}, qci, enabled_layers, enabled_extensions};
    _device = {_gpu, dci};

    _graphics_queue = _device.getQueue(_graphics_queue_index, 0);
    _present_queue = _device.getQueue(_present_queue_index, 0);
}

void triangle::make_swapchain(std::uint32_t width, std::uint32_t height) {
    const auto formats = _gpu.getSurfaceFormatsKHR(_surface);
    _surface_format = formats[0];
    for (const auto& f : formats) {
        if (f.format == vk::Format::eB8G8R8A8Unorm && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            _surface_format = f;
        }
    }

    const auto capabilities = _gpu.getSurfaceCapabilitiesKHR(_surface);
    _surface_extent = vk::Extent2D{
        std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };

    const auto pre_transform = capabilities.currentTransform;
    const auto composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    const auto present_mode = vk::PresentModeKHR::eMailbox;

    const std::uint32_t qfi[] = {_graphics_queue_index, _present_queue_index};
    vk::SwapchainCreateInfoKHR sci{
        {},
        _surface,
        capabilities.minImageCount + 1,
        _surface_format.format,
        _surface_format.colorSpace,
        _surface_extent,
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        qfi,
        pre_transform,
        composite_alpha,
        present_mode,
        true,
    };

    _swapchain = {_device, sci};
    const auto images = _swapchain.getImages();

    vk::ImageViewCreateInfo ci{
        {},
        {},
        vk::ImageViewType::e2D,
        _surface_format.format,
        {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity},
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    for (const auto& image : images) {
        ci.image = image;
        _image_views.emplace_back(_device, ci);
    }
}

void triangle::make_renderpass() {
    vk::AttachmentDescription attachment_desc{
        {},
        _surface_format.format,
        vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::ePresentSrcKHR,
    };
    vk::AttachmentReference attachment_ref{0, vk::ImageLayout::eColorAttachmentOptimal};
    vk::SubpassDescription subpass_desc{{}, vk::PipelineBindPoint::eGraphics, {}, attachment_ref};
    vk::SubpassDependency subpass_dep{
        VK_SUBPASS_EXTERNAL,
        0,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eColorAttachmentWrite,
    };
    vk::RenderPassCreateInfo rpci{
        {},
        attachment_desc,
        subpass_desc,
        subpass_dep,
    };

    _render_pass = {_device, rpci};
}

void triangle::make_texture_image() {
    int w{}, h{}, c{}, wc{4};
    auto data = stbi_load("textures/vulkan.png", &w, &h, &c, wc);
    if (!data) {
        throw std::runtime_error("failed to load image");
    }

    std::uint32_t width = w;
    std::uint32_t height = h;

    const vk::DeviceSize size = w * h * wc;
    const auto props = _gpu.getMemoryProperties();
    buffer staging{_device,
                   props,
                   size,
                   vk::BufferUsageFlagBits::eTransferSrc,
                   vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};
    staging.copy_from_host(data, size);

    _texture = {_device, props, width, height};

    vk::CommandBufferAllocateInfo ai{_command_pool, vk::CommandBufferLevel::ePrimary, 1};
    vk::raii::CommandBuffer buffer{std::move(vk::raii::CommandBuffers{_device, ai}.front())};

    buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    {
        vk::ImageMemoryBarrier barrier{
            {},
            vk::AccessFlagBits::eTransferWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            {},
            {},
            _texture.img,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };

        vk::PipelineStageFlags src_stage{vk::PipelineStageFlagBits::eTopOfPipe};
        vk::PipelineStageFlags dst_stage{vk::PipelineStageFlagBits::eTransfer};

        buffer.pipelineBarrier(src_stage, dst_stage, {}, nullptr, nullptr, barrier);
    }

    vk::BufferImageCopy bic{
        0,
        0,
        0,
        {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        {0, 0, 0},
        {width, height, 1},
    };
    buffer.copyBufferToImage(staging.buf, _texture.img, vk::ImageLayout::eTransferDstOptimal, bic);

    {
        vk::ImageMemoryBarrier barrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            {},
            {},
            _texture.img,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };

        vk::PipelineStageFlags src_stage{vk::PipelineStageFlagBits::eTransfer};
        vk::PipelineStageFlags dst_stage{vk::PipelineStageFlagBits::eFragmentShader};

        buffer.pipelineBarrier(src_stage, dst_stage, {}, nullptr, nullptr, barrier);
    }

    buffer.end();

    vk::SubmitInfo si{{}, {}, *buffer};
    _graphics_queue.submit(si);
    _graphics_queue.waitIdle();
}

void triangle::make_vertex_buffer() {
    std::array<vertex, 4> verticies = {{
        {{-0.5, +0.5}, {1.0, 0.0, 0.0}},
        {{+0.5, +0.5}, {0.0, 1.0, 0.0}},
        {{+0.5, -0.5}, {0.0, 0.0, 1.0}},
        {{-0.5, -0.5}, {1.0, 1.0, 1.0}},
    }};

    constexpr auto size = sizeof(vertex) * verticies.size();
    const auto props = _gpu.getMemoryProperties();
    buffer staging{_device,
                   props,
                   size,
                   vk::BufferUsageFlagBits::eTransferSrc,
                   vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};
    staging.copy_from_host(verticies.data(), size);

    _verticies_buffer = {_device,
                         props,
                         size,
                         vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                         vk::MemoryPropertyFlagBits::eDeviceLocal};

    vk::CommandBufferAllocateInfo ai{_command_pool, vk::CommandBufferLevel::ePrimary, 1};
    vk::raii::CommandBuffer buffer{std::move(vk::raii::CommandBuffers{_device, ai}.front())};

    buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    vk::BufferCopy bc{0, 0, size};
    buffer.copyBuffer(staging.buf, _verticies_buffer.buf, bc);
    buffer.end();

    vk::SubmitInfo si{{}, {}, *buffer};
    _graphics_queue.submit(si);
    _graphics_queue.waitIdle();
}

void triangle::make_indices_buffer() {
    std::array<std::uint32_t, 6> indicies = {
        0, 1, 2, 2, 3, 0};

    constexpr auto size = sizeof(std::uint32_t) * indicies.size();
    const auto props = _gpu.getMemoryProperties();
    buffer staging{_device,
                   props,
                   size,
                   vk::BufferUsageFlagBits::eTransferSrc,
                   vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};
    staging.copy_from_host(indicies.data(), size);

    _indices_buffer = {_device,
                       props,
                       size,
                       vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                       vk::MemoryPropertyFlagBits::eDeviceLocal};

    vk::CommandBufferAllocateInfo ai{_command_pool, vk::CommandBufferLevel::ePrimary, 1};
    vk::raii::CommandBuffer buffer{std::move(vk::raii::CommandBuffers{_device, ai}.front())};

    buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    vk::BufferCopy bc{0, 0, size};
    buffer.copyBuffer(staging.buf, _indices_buffer.buf, bc);
    buffer.end();

    vk::SubmitInfo si{{}, {}, *buffer};
    _graphics_queue.submit(si);
    _graphics_queue.waitIdle();
}

void triangle::make_uniform_buffer() {
    constexpr auto size = sizeof(uniform);
    const auto props = _gpu.getMemoryProperties();

    _uniform_buffer = {_device,
                       props,
                       size,
                       vk::BufferUsageFlagBits::eUniformBuffer,
                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};
}

void triangle::make_descriptor_set() {
    constexpr auto layout_binding = uniform::layout_binding();
    vk::DescriptorSetLayoutCreateInfo dslci{{}, layout_binding};
    _descriptor_layout = {_device, dslci};

    vk::DescriptorPoolSize size{vk::DescriptorType::eUniformBuffer, 1};
    vk::DescriptorPoolCreateInfo dpci{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, size};
    _descriptor_pool = {_device, dpci};

    vk::DescriptorSetAllocateInfo dsai{_descriptor_pool, *_descriptor_layout};
    _descriptor_set = std::move(_device.allocateDescriptorSets(dsai).front());

    vk::DescriptorBufferInfo dbi{_uniform_buffer.buf, 0, sizeof(uniform)};
    vk::WriteDescriptorSet wds{_descriptor_set, 0, 0, vk::DescriptorType::eUniformBuffer, {}, dbi};
    _device.updateDescriptorSets(wds, nullptr);
}

void triangle::make_pipeline() {
    const auto vert_shader = _device.createShaderModule({{}, sizeof(vert_shader_code), vert_shader_code});
    const auto frag_shader = _device.createShaderModule({{}, sizeof(frag_shader_code), frag_shader_code});

    vk::PipelineShaderStageCreateInfo shader_stages[] = {
        vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, vert_shader, "main"},
        vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, frag_shader, "main"},
    };

    constexpr auto binding_desc = vertex::binding_desc();
    constexpr auto attribute_desc = vertex::attribute_desc();
    vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, binding_desc, attribute_desc};
    vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, vk::False};
    vk::PipelineViewportStateCreateInfo viewport_state{{}, 1, nullptr, 1, nullptr};
    vk::PipelineRasterizationStateCreateInfo rasterization_state{
        {},
        vk::False,
        vk::False,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack,
        vk::FrontFace::eCounterClockwise,
        vk::False,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, vk::False};
    vk::ColorComponentFlags color_flags(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendAttachmentState colorblend_attachment{
        vk::True,
        vk::BlendFactor::eSrcAlpha,
        vk::BlendFactor::eOneMinusSrcAlpha,
        vk::BlendOp::eAdd,
        vk::BlendFactor::eZero,
        vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        color_flags,
    };

    vk::PipelineColorBlendStateCreateInfo colorblend_state{
        {},
        vk::False,
        vk::LogicOp::eCopy,
        colorblend_attachment,
        {0.0f, 0.0f, 0.0f, 0.0f},
    };

    vk::DynamicState dynamic_states[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic_state{{}, dynamic_states};

    vk::PipelineLayoutCreateInfo plci{{}, *_descriptor_layout};
    _pipeline_layout = {_device, plci};

    vk::GraphicsPipelineCreateInfo pci{
        {},
        shader_stages,
        &vertex_input_state,
        &input_assembly_state,
        nullptr,
        &viewport_state,
        &rasterization_state,
        &multisample_state,
        nullptr,
        &colorblend_state,
        &dynamic_state,
        _pipeline_layout,
        _render_pass,
    };

    _pipeline = {_device, nullptr, pci};
}

void triangle::make_framebuffers() {
    for (const auto& iv : _image_views) {
        vk::FramebufferCreateInfo fbci{{}, _render_pass, *iv, _surface_extent.width, _surface_extent.height, 1};
        _framebuffers.emplace_back(_device, fbci);
    }
}

void triangle::make_command_buffer() {
    // creating command pool
    vk::CommandPoolCreateFlags flags{vk::CommandPoolCreateFlagBits::eResetCommandBuffer};
    vk::CommandPoolCreateInfo ci{flags, _graphics_queue_index};
    _command_pool = {_device, ci};

    // creating command buffer
    vk::CommandBufferAllocateInfo ai{_command_pool, vk::CommandBufferLevel::ePrimary, 1};
    _command_buffer = {std::move(vk::raii::CommandBuffers{_device, ai}.front())};
}

void triangle::make_synchronization() {
    vk::SemaphoreCreateInfo sci{};
    _image_available_semaphore = {_device, sci};
    _render_finished_semaphore = {_device, sci};

    vk::FenceCreateInfo fci{vk::FenceCreateFlagBits::eSignaled};
    _fence = {_device, fci};
}

void triangle::render() {
    while (vk::Result::eTimeout == _device.waitForFences(*_fence, vk::True, -1)) {
    }
    _device.resetFences(*_fence);

    const auto [_, index] = _swapchain.acquireNextImage(-1, _image_available_semaphore);

    _command_buffer.reset();
    _command_buffer.begin({});

    const float time = glfwGetTime();
    uniform ubo{
        glm::rotate(glm::mat4(1.0f), time, glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::mat4(1.0f),
        glm::mat4(1.0f),
    };
    _uniform_buffer.copy_from_host(&ubo, sizeof(ubo));

    vk::ClearValue clear_value{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}};
    vk::RenderPassBeginInfo rpbi{_render_pass, _framebuffers[index], {{0, 0}, _surface_extent}, clear_value};
    _command_buffer.beginRenderPass(rpbi, vk::SubpassContents::eInline);
    _command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline);
    _command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, _pipeline_layout, 0, {_descriptor_set}, nullptr);
    _command_buffer.bindVertexBuffers(0, *_verticies_buffer.buf, {0});
    _command_buffer.bindIndexBuffer(_indices_buffer.buf, 0, vk::IndexType::eUint32);
    vk::Viewport viewport{0.0f, 0.0f, (float)_surface_extent.width, (float)_surface_extent.height, 0.0f, 1.0f};
    _command_buffer.setViewport(0, viewport);
    _command_buffer.setScissor(0, vk::Rect2D{{0, 0}, _surface_extent});
    _command_buffer.drawIndexed(6, 1, 0, 0, 0);
    _command_buffer.endRenderPass();
    _command_buffer.end();

    const auto wait_stage_flags = vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submit{*_image_available_semaphore, wait_stage_flags, *_command_buffer, *_render_finished_semaphore};
    _graphics_queue.submit(submit, _fence);

    vk::PresentInfoKHR present_info{*_render_finished_semaphore, *_swapchain, index};
    auto rv = _present_queue.presentKHR(present_info);
    (void)rv;
}

void triangle::wait_finished() {
    _device.waitIdle();
}

int main() {
    try {
        // init glfw
        std::uint32_t width = 800;
        std::uint32_t height = 600;

        for (const auto& l : enabled_layers) {
            check_layer(l);
        }

        glfw_window window{width, height, "triangle"};
        triangle triangle{};

        auto extensions = window.get_extensions();
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        triangle.make_instance(extensions);
        triangle.make_debug_messenger();
        triangle.make_physical_device();

        const auto& instance = triangle.get_instance();
        const auto surface = window.get_surface(instance);

        triangle.make_surface(surface, width, height);
        triangle.make_logical_device();
        triangle.make_swapchain(width, height);
        triangle.make_command_buffer();
        triangle.make_renderpass();
        triangle.make_texture_image();
        triangle.make_vertex_buffer();
        triangle.make_indices_buffer();
        triangle.make_uniform_buffer();
        triangle.make_descriptor_set();
        triangle.make_pipeline();
        triangle.make_framebuffers();
        triangle.make_synchronization();

        window.loop_handler([&triangle]() {
            triangle.render();
        });

        triangle.wait_finished();

    } catch (const std::exception& ex) {
        fmt::print("error: {}\n", ex.what());
    }

    return 0;
}
