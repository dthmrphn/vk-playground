#pragma once

#include <string_view>
#include <vulkan/vulkan.h>

namespace common {
class overlay {
  public:
    struct create_info {
        VkInstance instance;
        VkPhysicalDevice physical;
        VkDevice logical;
        uint32_t queue_index;
        VkQueue queue;
        VkDescriptorPool pool;
        VkRenderPass render_pass;
        uint32_t img_count_min;
        uint32_t img_count;
    };

    overlay() = default;
    overlay(const create_info& info, uint32_t w, uint32_t h);
    void release();

    void resize(uint32_t w, uint32_t h);
    void on_mouse_position(float x, float y);
    void on_mouse_buttons(bool right, bool left, bool middle);
    
    void begin() const;
    void draw(VkCommandBuffer cb) const;

    bool button(std::string_view name) const;
    void text(std::string_view text) const;
};

} // namespace common
