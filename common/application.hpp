#pragma once

#include "vulkan.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace common {

class application {
    vulkan::device _device;
    vulkan::swapchain _swapchain;

    std::uint32_t _graphic_queue_index{};
    std::uint32_t _present_queue_index{};

    vk::raii::Queue _graphic_queue{nullptr};
    vk::raii::Queue _present_queue{nullptr};

    vk::raii::RenderPass _render_pass{nullptr};

    std::vector<vk::raii::Framebuffer> _framebuffers{};

    vk::raii::CommandPool _command_pool{nullptr};
    vk::raii::CommandBuffer _command_buffer{nullptr};

    vk::raii::Semaphore _image_available_semaphore{nullptr};
    vk::raii::Semaphore _render_finished_semaphore{nullptr};
    vk::raii::Fence _fence{nullptr};

    GLFWwindow* _window;

    void render();

  public:
    application(const vk::ApplicationInfo& app_info, std::uint32_t w, std::uint32_t h);

    void run();
};

} // namespace common
