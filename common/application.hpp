#pragma once

#include "overlay.hpp"
#include "vulkan.hpp"

#include <chrono>
#include <wsi.hpp>

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

    std::chrono::system_clock::time_point _tp;

  protected:
    static constexpr auto frames_in_flight{2};

    std::size_t _current_frame{};

    wsi::window _window;

    vulkan::device _device;
    vulkan::swapchain _swapchain;

    vk::raii::DescriptorPool _overlay_desc_pool{nullptr};
    overlay _overlay;

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

    std::uint32_t acquire();
    void present(std::uint32_t i);

    bool loop_handler();

    void on_resize(const wsi::event::resize& e);
    void on_mouse_position(const wsi::event::mouse::position& e);
    void on_mouse_button(const wsi::event::mouse::button& e);

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

    float current_time() const;

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

    void on_mouse_position(const wsi::event::mouse::position& e) {}

    void on_mouse_button(const wsi::event::mouse::button& e) {}

    std::uint32_t acquire_impl() {
        return impl().acquire();
    }

    void record_impl(std::uint32_t i) {
        impl().record(i);
    }

    void present_impl(std::uint32_t i) {
        impl().present(i);
    }

    template <class... Ts>
    struct overloaded : Ts... {
        using Ts::operator()...;
    };

    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

  public:
    application(const vk::ApplicationInfo& app_info, std::uint32_t w, std::uint32_t h)
        : application_base(app_info, w, h) {
    }

    void run() {
        const auto visitor = overloaded{
            [this](wsi::event::resize e) { application_base::on_resize(e); },
            [this](wsi::event::mouse::position e) {
                application_base::on_mouse_position(e);
                impl().on_mouse_position(e);
            },
            [this](wsi::event::mouse::button e) {
                application_base::on_mouse_button(e);
                impl().on_mouse_button(e);
            },
            [this](auto&& e) {},
        };

        while (loop_handler()) {
            std::visit(visitor, _window.handle_event());

            const auto i = acquire_impl();
            record_impl(i);
            present_impl(i);
        }

        _device.logical().waitIdle();
        _overlay.release();
    }
};

} // namespace common
