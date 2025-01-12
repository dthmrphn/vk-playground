#pragma once

#include "vulkan.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace common {

class application_base {
  protected:
    static constexpr auto frames_in_flight{2};

    std::size_t _current_frame{};

    vulkan::device _device;
    vulkan::swapchain _swapchain;

    std::uint32_t _graphic_queue_index{};
    std::uint32_t _present_queue_index{};

    vk::Queue _graphic_queue{nullptr};
    vk::Queue _present_queue{nullptr};

    vk::raii::RenderPass _render_pass{nullptr};

    std::vector<vk::raii::Framebuffer> _framebuffers{};

    vk::raii::CommandPool _command_pool{nullptr};

    struct frame_data {
        vk::raii::CommandBuffer command_buffer{nullptr};
        vk::raii::Semaphore image_available_semaphore{nullptr};
        vk::raii::Semaphore render_finished_semaphore{nullptr};
        vk::raii::Fence fence{nullptr};
    };

    std::array<frame_data, frames_in_flight> _frames;

    GLFWwindow* _window;

    std::uint32_t acquire();
    // void record(std::uint32_t i);
    void present(std::uint32_t i);

    void render();
    void update_swapchain(std::uint32_t w, std::uint32_t h);

    static void resize_handler(GLFWwindow*, int, int);

    bool loop_handler() const;

  public:
    application_base(const vk::ApplicationInfo& app_info, std::uint32_t w, std::uint32_t h);
};

template <typename T>
class application : public application_base {
    application() = default;
    friend T;

    T& impl() {
        return static_cast<T&>(*this);
    }

  public:
    application(const vk::ApplicationInfo& app_info, std::uint32_t w, std::uint32_t h)
        : application_base(app_info, w, h) {}

    void run() {
        while (loop_handler()) {
            const auto i = acquire();
            impl().record(i);
            present(i);
        }

        _device.logical().waitIdle();
    }
};

} // namespace common
