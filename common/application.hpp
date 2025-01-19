#pragma once

#include "vulkan.hpp"

#include <GLFW/glfw3.h>
#include <chrono>

namespace common {

class fps_counter {
    std::chrono::system_clock::time_point _tp;
    std::uint32_t _counter;
    std::uint32_t _fps;

  public:
    fps_counter();
    void count();
    void reset();
    std::uint32_t value() const;
};

class application_base {
    fps_counter _counter;
    std::string _name;

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

    struct depth {
        vk::raii::Image image{nullptr};
        vk::raii::ImageView view{nullptr};
        vk::raii::DeviceMemory memory{nullptr};
    } _depth;

    GLFWwindow* _window;

    std::uint32_t acquire();
    void present(std::uint32_t i);

    bool loop_handler();

    void on_resize(std::uint32_t w, std::uint32_t h);

    struct default_pipeline_info {
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state;
        vk::PipelineDepthStencilStateCreateInfo depth_state;
        vk::PipelineViewportStateCreateInfo viewport_state;
        vk::PipelineRasterizationStateCreateInfo rasterization_state;
        vk::PipelineMultisampleStateCreateInfo multisample_state;
        vk::ColorComponentFlags color_flags;
        vk::PipelineColorBlendAttachmentState colorblend_attachment;
        vk::PipelineColorBlendStateCreateInfo colorblend_state;
        vk::DynamicState dynamic_states[2];
        vk::PipelineDynamicStateCreateInfo dynamic_state;

        default_pipeline_info();
        operator vk::GraphicsPipelineCreateInfo() const;
    };

  private:
    void update_swapchain(std::uint32_t w, std::uint32_t h);
    void make_framebuffers();
    void make_depth_image();

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

    static application& self(GLFWwindow* win) {
        return *static_cast<application*>(glfwGetWindowUserPointer(win));
    }

    static void resize_handler(GLFWwindow* w, int width, int height) {
        self(w).on_resize(width, height);
    }

    static void mouse_handler(GLFWwindow* w, double x, double y) {
        self(w).impl().on_mouse(x, y);
    }

    static void keyboard_handler(GLFWwindow* w, int key, int scancode, int action, int mods) {
        self(w).impl().on_keyboard(key, scancode, action, mods);
    }

    void record(std::uint32_t i) {
        static_assert(false, "T must implement record(std::uint32_t) method");
    }

    void on_mouse(double x, double y) {}

    void on_keyboard(int key, int scancode, int action, int mods) {}

    std::uint32_t acquire_impl() {
        return impl().acquire();
    }

    void record_impl(std::uint32_t i) {
        impl().record(i);
    }

    void present_impl(std::uint32_t i) {
        impl().present(i);
    }

  public:
    application(const vk::ApplicationInfo& app_info, std::uint32_t w, std::uint32_t h)
        : application_base(app_info, w, h) {
        glfwSetWindowUserPointer(_window, this);
        glfwSetFramebufferSizeCallback(_window, &application::resize_handler);
        glfwSetCursorPosCallback(_window, &application::mouse_handler);
        glfwSetKeyCallback(_window, &application::keyboard_handler);
    }

    void run() {
        while (loop_handler()) {
            const auto i = acquire_impl();
            record_impl(i);
            present_impl(i);
        }

        _device.logical().waitIdle();
    }
};

} // namespace common
