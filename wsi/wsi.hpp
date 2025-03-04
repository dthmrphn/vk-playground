#pragma once

#include <memory>
#include <queue>
#include <string_view>
#include <variant>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace wsi {

namespace event {

namespace mouse {

struct position {
    float x;
    float y;
};

struct button {
    bool lmb;
    bool rmb;
    bool mmb;
};

} // namespace mouse

struct keyboard {};

struct resize {
    int32_t w;
    int32_t h;
};

struct exit {};

using variant = std::variant<std::monostate, mouse::button, mouse::position, keyboard, resize, exit>;

} // namespace event

class window {
  public:
    virtual ~window() = default;

    virtual VkSurfaceKHR create_surface(VkInstance instance) const = 0;
    virtual void set_title(std::string_view name) = 0;

    virtual event::variant poll_event() {
        poll();
        if (_events.size()) {
            const auto e = _events.front();
            _events.pop();
            return e;
        }

        return {};
    }

  protected:
    virtual void poll() = 0;

    std::queue<event::variant> _events;
};

std::vector<const char*> required_extensions();
std::unique_ptr<window> make_window(std::size_t width, std::size_t height, std::string_view name);

} // namespace wsi
