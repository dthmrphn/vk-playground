#include "application.hpp"
#include <fmt/base.h>

struct simple : public common::application<simple> {
    simple() : common::application<simple>({"simple", 1, "engine", 1, VK_API_VERSION_1_0}, 800, 600) {}

    void record(std::uint32_t i) const {
        const auto& cb = _frames[_current_frame].command_buffer;

        vk::ClearValue clear_value{vk::ClearColorValue{0.5f, 0.5f, 0.5f, 1.0f}};
        vk::RenderPassBeginInfo rpbi{_render_pass, _framebuffers[i], {{0, 0}, _swapchain.extent()}, clear_value};
        vk::Viewport viewport{0.0f, 0.0f, (float)_swapchain.extent().width, (float)_swapchain.extent().height, 0.0f, 1.0f};

        cb.reset();
        cb.begin({});
        cb.beginRenderPass(rpbi, vk::SubpassContents::eInline);
        cb.setViewport(0, viewport);
        cb.setScissor(0, vk::Rect2D{{0, 0}, _swapchain.extent()});
        cb.endRenderPass();
        cb.end();
    }
};

int main() {
    simple app{};
    app.run();
}
