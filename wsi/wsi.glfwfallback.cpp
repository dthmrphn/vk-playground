#include "wsi.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <fmt/format.h>
#include <queue>

namespace wsi {
class glfw final : public window {
    GLFWwindow* window;

    static glfw& self(GLFWwindow* win) {
        return *static_cast<glfw*>(glfwGetWindowUserPointer(win));
    }

    static void resize_handler(GLFWwindow* w, int width, int height) {
        self(w)._events.push(event::resize{width, height});
    }

    static void mouse_pos_handler(GLFWwindow* w, double x, double y) {
        const float fx = x;
        const float fy = y;
        self(w)._events.push(event::mouse::position{fx, fy});
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
        self(w)._events.push(ev);
    }

    static void keyboard_handler(GLFWwindow* w, int key, int scancode, int action, int mods) {}

    static void close_handler(GLFWwindow* w) {
        self(w)._events.push(event::exit{});
    }

  public:
    glfw(int32_t width, int32_t height, const char* name) {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(width, height, name, nullptr, nullptr);

        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, &glfw::resize_handler);
        glfwSetCursorPosCallback(window, &glfw::mouse_pos_handler);
        glfwSetMouseButtonCallback(window, &glfw::mouse_btn_handler);
        glfwSetKeyCallback(window, &glfw::keyboard_handler);
        glfwSetWindowCloseCallback(window, &glfw::close_handler);
    }

    ~glfw() {
        glfwTerminate();
    }

    VkSurfaceKHR create_surface(VkInstance instance) const override {
        VkSurfaceKHR surf{};
        const auto r = glfwCreateWindowSurface(instance, window, nullptr, &surf);
        if (r != VK_SUCCESS) {
            throw std::runtime_error(fmt::format("surface create error: {}", (int)r));
        }

        return surf;
    }

    void poll() override {
        auto rv = !glfwWindowShouldClose(window);
        glfwPollEvents();
    }

    void set_title(std::string_view name) override {
        glfwSetWindowTitle(window, name.data());
    }
};

std::vector<const char*> required_extensions() {
    uint32_t count{};
    const auto ex = glfwGetRequiredInstanceExtensions(&count);
    return {ex, ex + count};
}

std::unique_ptr<window> make_window(std::size_t width, std::size_t height, std::string_view name) {
    return std::make_unique<glfw>(width, height, name.data());
}

} // namespace wsi
