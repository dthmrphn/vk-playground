#include "application.hpp"

#include <GLFW/glfw3.h>
#include <fmt/core.h>

constexpr static const char* enabled_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

constexpr static const char* enabled_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

namespace common {
application::application(const vk::ApplicationInfo& app_info, std::uint32_t w, std::uint32_t h) {
    // glfw initialization
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    _window = glfwCreateWindow(w, h, app_info.pApplicationName, nullptr, nullptr);
    glfwSetFramebufferSizeCallback(_window, &application::resize_handler);
    glfwSetWindowUserPointer(_window, this);

    // device creation
    std::uint32_t count{};
    const auto exts = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> extensions{exts, exts + count};
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers{enabled_layers, enabled_layers + 1};
    _device = vulkan::device{app_info, layers, extensions, vk::QueueFlagBits::eGraphics, true};

    // queue creation
    _graphic_queue_index = _device.queue_family_index(vk::QueueFlagBits::eGraphics);
    _graphic_queue = {_device.logical(), _graphic_queue_index, 0};
    _present_queue_index = _device.queue_family_index(vk::QueueFlagBits::eGraphics);
    _present_queue = {_device.logical(), _graphic_queue_index, 0};

    // swapchain creation
    VkSurfaceKHR surf{};
    glfwCreateWindowSurface(*_device.instance(), _window, nullptr, &surf);
    _swapchain = vulkan::swapchain{_device, surf, w, h};

    // command pool and buffer creation
    vk::CommandPoolCreateFlags flags{vk::CommandPoolCreateFlagBits::eResetCommandBuffer};
    vk::CommandPoolCreateInfo ci{flags, _graphic_queue_index};
    _command_pool = {_device.logical(), ci};

    vk::CommandBufferAllocateInfo ai{_command_pool, vk::CommandBufferLevel::ePrimary, frames_in_flight};
    auto buffers = vk::raii::CommandBuffers{_device.logical(), ai};
    for (std::size_t i = 0; i < frames_in_flight; ++i) {
        _frames[i].command_buffer = std::move(buffers[i]);
    }

    // render pass creation
    vk::AttachmentDescription attachment_desc{
        {},
        _swapchain.format().format,
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

    _render_pass = {_device.logical(), rpci};

    // framebuffer creation
    for (const auto& iv : _swapchain.image_views()) {
        vk::FramebufferCreateInfo fbci{
            {},
            _render_pass,
            iv,
            _swapchain.extent().width,
            _swapchain.extent().height,
            1,
        };
        _framebuffers.emplace_back(_device.logical(), fbci);
    }

    // synchronization creation
    vk::SemaphoreCreateInfo sci{};
    vk::FenceCreateInfo fci{vk::FenceCreateFlagBits::eSignaled};
    for (std::size_t i = 0; i < frames_in_flight; ++i) {
        _frames[i].image_available_semaphore = {_device.logical(), sci};
        _frames[i].render_finished_semaphore = {_device.logical(), sci};
        _frames[i].fence = {_device.logical(), fci};
    }
}

std::uint32_t application::acquire() {
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

void application::record(std::uint32_t index) {
    const auto& cb = _frames[_current_frame].command_buffer;

    vk::ClearValue clear_value{vk::ClearColorValue{0.5f, 0.5f, 0.5f, 1.0f}};
    vk::RenderPassBeginInfo rpbi{_render_pass, _framebuffers[index], {{0, 0}, _swapchain.extent()}, clear_value};
    vk::Viewport viewport{0.0f, 0.0f, (float)_swapchain.extent().width, (float)_swapchain.extent().height, 0.0f, 1.0f};

    cb.reset();
    cb.begin({});
    cb.beginRenderPass(rpbi, vk::SubpassContents::eInline);
    cb.setViewport(0, viewport);
    cb.setScissor(0, vk::Rect2D{{0, 0}, _swapchain.extent()});
    cb.endRenderPass();
    cb.end();
}

void application::present(std::uint32_t index) {
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
}

void application::render() {
    const auto i = acquire();
    record(i);
    present(i);

    _current_frame = (_current_frame + 1) % frames_in_flight;
}

void application::resize_handler(GLFWwindow* win, int w, int h) {
    auto app = static_cast<application*>(glfwGetWindowUserPointer(win));
    app->update_swapchain(w, h);
}

void application::update_swapchain(std::uint32_t w, std::uint32_t h) {
    _device.logical().waitIdle();

    _swapchain.resize(_device, w, h);

    _framebuffers.clear();
    for (const auto& iv : _swapchain.image_views()) {
        vk::FramebufferCreateInfo fbci{
            {},
            _render_pass,
            iv,
            _swapchain.extent().width,
            _swapchain.extent().height,
            1,
        };
        _framebuffers.emplace_back(_device.logical(), fbci);
    }
}

void application::run() {
    while (!glfwWindowShouldClose(_window)) {
        glfwPollEvents();
        render();
    }

    _device.logical().waitIdle();
}

} // namespace common
