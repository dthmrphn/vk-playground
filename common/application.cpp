#define GLFW_INCLUDE_VULKAN
#include "application.hpp"

#include <fmt/core.h>

constexpr static const char* enabled_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr static const char* device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

namespace common {
application_base::application_base(const vk::ApplicationInfo& app_info, std::uint32_t w, std::uint32_t h) {
    // glfw initialization
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    _window = glfwCreateWindow(w, h, app_info.pApplicationName, nullptr, nullptr);

    // device creation
    std::uint32_t count{};
    const auto exts = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> extensions{exts, exts + count};
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    _device = vulkan::device{
        app_info,
        enabled_layers,
        device_extensions,
        extensions,
        vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute,
        true,
    };

    // queue creation
    _graphic_queue_index = _device.queue_family_index(vk::QueueFlagBits::eGraphics);
    _graphic_queue = _device.graphic_queue();
    _present_queue_index = _device.queue_family_index(vk::QueueFlagBits::eGraphics);
    _present_queue = _device.graphic_queue();

    // swapchain creation
    VkSurfaceKHR surf{};
    glfwCreateWindowSurface(_device.instance(), _window, nullptr, &surf);
    _swapchain = vulkan::swapchain{_device, surf, w, h};

    // command pool and buffer creation
    vk::CommandPoolCreateFlags flags{vk::CommandPoolCreateFlagBits::eResetCommandBuffer};
    vk::CommandPoolCreateInfo ci{flags, _graphic_queue_index};
    _command_pool = _device.make_command_pool(ci);

    vk::CommandBufferAllocateInfo ai{_command_pool, vk::CommandBufferLevel::ePrimary, frames_in_flight};
    auto buffers = _device.make_command_buffers(ai);
    for (std::size_t i = 0; i < frames_in_flight; ++i) {
        _frames[i].command_buffer = std::move(buffers[i]);
    }

    // depth image creation
    make_depth_image();

    // render pass creation
    vk::AttachmentDescription attachments[] = {
        vk::AttachmentDescription{
            {},
            _swapchain.format().format,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::ePresentSrcKHR,
        },
        vk::AttachmentDescription{
            {},
            vk::Format::eD32Sfloat,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
        },
    };
    vk::AttachmentReference color_attachment_ref{0, vk::ImageLayout::eColorAttachmentOptimal};
    vk::AttachmentReference depth_attachment_ref{1, vk::ImageLayout::eDepthStencilAttachmentOptimal};
    vk::SubpassDescription subpass_desc{
        {},
        vk::PipelineBindPoint::eGraphics,
        {},
        color_attachment_ref,
        nullptr,
        &depth_attachment_ref,
    };
    vk::SubpassDependency subpass_dep{
        VK_SUBPASS_EXTERNAL,
        0,
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
    };
    vk::RenderPassCreateInfo rpci{
        {},
        attachments,
        subpass_desc,
        subpass_dep,
    };

    _render_pass = _device.make_render_pass(rpci);

    // framebuffer creation
    make_framebuffers();

    // synchronization creation
    vk::SemaphoreCreateInfo sci{};
    vk::FenceCreateInfo fci{vk::FenceCreateFlagBits::eSignaled};
    for (std::size_t i = 0; i < frames_in_flight; ++i) {
        _frames[i].image_available_semaphore = _device.make_semaphore(sci);
        _frames[i].render_finished_semaphore = _device.make_semaphore(sci);
        _frames[i].fence = _device.make_fence(fci);
    }
}

std::uint32_t application_base::acquire() {
    const auto& fence = _frames[_current_frame].fence;
    const auto& semaphore = _frames[_current_frame].image_available_semaphore;
    while (vk::Result::eTimeout == _device.logical().waitForFences(*fence, vk::True, -1)) {
    }
    _device.logical().resetFences(*fence);

    auto [rv, index] = _swapchain.get().acquireNextImage(-1, semaphore);
    if (rv != vk::Result::eSuccess) {
        fmt::print("acquire err: {}\n", vk::to_string(rv));
    }

    return index;
}

void application_base::present(std::uint32_t index) {
    const auto& [cb, image_available_semaphore, render_finished_semaphore, fence] = _frames[_current_frame];
    vk::PipelineStageFlags wait_flags{vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submit{
        *image_available_semaphore,
        wait_flags,
        *cb,
        *render_finished_semaphore,
    };
    _graphic_queue.submit(submit, fence);

    vk::PresentInfoKHR present_info{*render_finished_semaphore, *_swapchain.get(), index};
    auto rv = _present_queue.presentKHR(present_info);
    if (rv != vk::Result::eSuccess) {
        fmt::print("present err: {}\n", vk::to_string(rv));
    }

    _current_frame = (_current_frame + 1) % frames_in_flight;
}

void application_base::on_resize(std::uint32_t w, std::uint32_t h) {
    update_swapchain(w, h);
}

void application_base::update_swapchain(std::uint32_t w, std::uint32_t h) {
    _device.logical().waitIdle();

    _swapchain.resize(_device, w, h);

    make_depth_image();
    make_framebuffers();
}

void application_base::make_framebuffers() {
    _framebuffers.clear();
    for (const auto& iv : _swapchain.image_views()) {
        vk::ImageView views[] = {iv, _depth.view};
        vk::FramebufferCreateInfo fbci{
            {},
            _render_pass,
            views,
            _swapchain.extent().width,
            _swapchain.extent().height,
            1,
        };
        _framebuffers.emplace_back(_device.make_framebuffer(fbci));
    }
}

void application_base::make_depth_image() {
    vk::ImageCreateInfo ici{
        {},
        vk::ImageType::e2D,
        vk::Format::eD32Sfloat,
        vk::Extent3D{_swapchain.extent(), 1},
        1,
        1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
    };
    _depth.image = _device.make_image(ici);

    const auto req = _depth.image.getMemoryRequirements();
    const auto index = _device.memory_type_index(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::MemoryAllocateInfo mai{req.size, index};
    _depth.memory = _device.make_memory(mai);
    _depth.image.bindMemory(_depth.memory, 0);

    vk::ImageViewCreateInfo ivci{
        {},
        _depth.image,
        vk::ImageViewType::e2D,
        vk::Format::eD32Sfloat,
        {},
        {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
    };
    _depth.view = _device.make_image_view(ivci);
}

bool application_base::loop_handler() const {
    auto rv = !glfwWindowShouldClose(_window);
    glfwPollEvents();
    return rv;
}

application_base::default_pipeline_info::default_pipeline_info() {
    input_assembly_state = {{}, vk::PrimitiveTopology::eTriangleList, vk::False};
    depth_state = {{}, true, true, vk::CompareOp::eLess, false, false};
    viewport_state = {{}, 1, nullptr, 1, nullptr};
    rasterization_state = {
        {},
        vk::False,
        vk::False,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eNone,
        vk::FrontFace::eCounterClockwise,
        vk::False,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    multisample_state = {{}, vk::SampleCountFlagBits::e1, vk::False};
    color_flags = {
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };

    colorblend_attachment = {
        vk::True,
        vk::BlendFactor::eSrcAlpha,
        vk::BlendFactor::eOneMinusSrcAlpha,
        vk::BlendOp::eAdd,
        vk::BlendFactor::eZero,
        vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        color_flags,
    };

    colorblend_state = {
        {},
        vk::False,
        vk::LogicOp::eCopy,
        colorblend_attachment,
        {0.0f, 0.0f, 0.0f, 0.0f},
    };

    dynamic_states[0] = vk::DynamicState::eViewport;
    dynamic_states[1] = vk::DynamicState::eScissor;
    dynamic_state = vk::PipelineDynamicStateCreateInfo{{}, dynamic_states};
}

application_base::default_pipeline_info::operator vk::GraphicsPipelineCreateInfo () const {
    return vk::GraphicsPipelineCreateInfo{
        {},
        {},
        nullptr,
        &input_assembly_state,
        nullptr,
        &viewport_state,
        &rasterization_state,
        &multisample_state,
        &depth_state,
        &colorblend_state,
        &dynamic_state,
    };
}

} // namespace common
