#include "wsi.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <fmt/format.h>
#include <queue>

namespace wsi {
struct platform {
    GLFWwindow* window;

    std::queue<event_type> events;

    platform(int32_t width, int32_t height, const char* name) {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(width, height, name, nullptr, nullptr);

        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, &platform::resize_handler);
        glfwSetCursorPosCallback(window, &platform::mouse_pos_handler);
        glfwSetMouseButtonCallback(window, &platform::mouse_btn_handler);
        glfwSetKeyCallback(window, &platform::keyboard_handler);
    }

    static platform& self(GLFWwindow* win) {
        return *static_cast<platform*>(glfwGetWindowUserPointer(win));
    }

    static void resize_handler(GLFWwindow* w, int width, int height) {
        self(w).events.push(event::resize{width, height});
    }

    static void mouse_pos_handler(GLFWwindow* w, double x, double y) {
        const float fx = x;
        const float fy = y;
        self(w).events.push(event::mouse::position{fx, fy});
    }

    static void mouse_btn_handler(GLFWwindow* w, int button, int action, int mods) {
        event::mouse::button ev;
        switch (button) {
        case GLFW_MOUSE_BUTTON_RIGHT:
            ev.rmb = action;
            break;
        case GLFW_MOUSE_BUTTON_LEFT:
            ev.lmb = action;
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            ev.mmb = action;
            break;
        }
        self(w).events.push(ev);
    }

    static void keyboard_handler(GLFWwindow* w, int key, int scancode, int action, int mods) {}
};

window::window(std::size_t width, std::size_t height, const std::string& name)
    : _impl(std::make_unique<platform>(width, height, name.data())) {}

window::~window() = default;

VkSurfaceKHR window::create_surface(VkInstance instance) const {
    VkSurfaceKHR surf{};
    glfwCreateWindowSurface(instance, _impl->window, nullptr, &surf);
    return surf;
}

std::vector<const char*> window::required_extensions() {
    std::uint32_t c{};
    const auto e = glfwGetRequiredInstanceExtensions(&c);
    return {e, e + c};
}

bool window::handle() const {
    auto rv = !glfwWindowShouldClose(_impl->window);
    glfwPollEvents();
    return rv;
}

event_type window::handle_event() {
    if (_impl->events.size()) {
        const auto e = _impl->events.front();
        _impl->events.pop();
        return e;
    }

    return {};
}

void window::set_title(const std::string& name) {
    glfwSetWindowTitle(_impl->window, name.c_str());
}

} // namespace wsi
