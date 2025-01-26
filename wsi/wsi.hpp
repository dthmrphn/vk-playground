#pragma once

#include <memory>
#include <variant>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace wsi {

namespace event {

struct mouse {
    struct position {
        float x;
        float y;
    };

    struct button {
        bool lmb;
        bool rmb;
        bool mmb;
    };
};

struct keyboard {};

struct resize {
    int32_t w;
    int32_t h;
};

} // namespace event

using event_type = std::variant<std::monostate, event::mouse::button, event::mouse::position, event::keyboard, event::resize>;

struct platform;

class window {
  public:
    window(std::size_t width, std::size_t height, const std::string& name);
    ~window();

    VkSurfaceKHR create_surface(VkInstance instance) const;
    static std::vector<const char*> required_extensions();

    bool handle() const;
    event_type handle_event();

    void set_title(const std::string& name);

  private:
    std::unique_ptr<platform> _impl;
};

} // namespace wsi
