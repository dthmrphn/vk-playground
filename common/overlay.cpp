#include "overlay.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_vulkan.h>

namespace common {

overlay::overlay(const create_info& info, uint32_t w, uint32_t h) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = info.instance;
    init_info.PhysicalDevice = info.physical;
    init_info.Device = info.logical;
    init_info.QueueFamily = info.queue_index;
    init_info.Queue = info.queue;
    init_info.DescriptorPool = info.pool;
    init_info.RenderPass = info.render_pass;
    init_info.MinImageCount = info.img_count_min;
    init_info.ImageCount = info.img_count;
    ImGui_ImplVulkan_Init(&init_info);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

void overlay::release() {
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
}

void overlay::resize(uint32_t w, uint32_t h) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h);
}

void overlay::on_mouse_position(float x, float y) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
}

void overlay::on_mouse_buttons(bool right, bool left, bool middle) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(0, left);
    io.AddMouseButtonEvent(1, right);
    io.AddMouseButtonEvent(2, middle);
}

void overlay::begin() const {
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Overlay", nullptr);
}

void overlay::draw(VkCommandBuffer cb) const {
    ImGui::End();
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, cb);
}

bool overlay::button(std::string_view name) const {
    return ImGui::Button(name.data());
}

void overlay::text(std::string_view text) const {
    ImGui::Text("%s", text.data());
}

} // namespace common
