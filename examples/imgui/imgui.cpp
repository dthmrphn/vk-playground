#include "application.hpp"

#include <fmt/core.h>

#include <imgui.frag.hpp>
#include <imgui.vert.hpp>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_vulkan.h>

struct imgui : public common::application<imgui> {
    vk::raii::DescriptorPool _descriptor_pool{nullptr};

    float _scale{1.0f};

    imgui() : common::application<imgui>({"imgui", 1, "engine", 1, VK_API_VERSION_1_0}, 800, 600) {
        vk::DescriptorPoolSize sizes[] = {
            {vk::DescriptorType::eCombinedImageSampler, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE},
        };

        vk::DescriptorPoolCreateInfo dpci{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, sizes};
        _descriptor_pool = _device.make_descriptor_pool(dpci);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGui_ImplVulkan_InitInfo info = {};
        info.Instance = _device.instance();
        info.PhysicalDevice = _device.physical();
        info.Device = _device.logical();
        info.QueueFamily = _graphic_queue_index;
        info.Queue = _graphic_queue;
        info.DescriptorPool = *_descriptor_pool;
        info.RenderPass = *_render_pass;
        info.MinImageCount = _swapchain.image_views().size();
        info.ImageCount = info.MinImageCount + 1;
        ImGui_ImplVulkan_Init(&info);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1200, 600);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }

    void record(std::uint32_t i) {
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Hello, world!", nullptr);//, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("This is some useful text.");
        ImGui::Button("Some button");
        ImGui::End();

        const auto& cb = _frames[_current_frame].command_buffer;
        const auto [w, h] = _swapchain.extent();

        vk::ClearValue clear_values[] = {
            vk::ClearColorValue{0.5f, 0.5f, 0.5f, 1.0f},
            vk::ClearDepthStencilValue{1.0f, 0},
        };
        vk::RenderPassBeginInfo rpbi{_render_pass, _framebuffers[i], {{0, 0}, _swapchain.extent()}, clear_values};
        vk::Viewport viewport{0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();

        cb.reset();
        cb.begin({});
        cb.beginRenderPass(rpbi, vk::SubpassContents::eInline);

        ImGui_ImplVulkan_RenderDrawData(draw_data, *cb);

        cb.endRenderPass();
        cb.end();
    }

    void on_mouse_position(const wsi::event::mouse::position& e) {
		ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(e.x, e.y);
    }

    void on_mouse_button(const wsi::event::mouse::button& e) {
		ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(0, e.lmb);
        io.AddMouseButtonEvent(1, e.rmb);
        io.AddMouseButtonEvent(2, e.mmb);
    }

    ~imgui() {
        ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();
    }
};

int main() {
    try {
        imgui imgui{};

        imgui.run();

    } catch (const std::exception& ex) {
        fmt::print("error: {}\n", ex.what());
    }

    return 0;
}
