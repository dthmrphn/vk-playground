#include "wsi.hpp"

#include <fmt/format.h>

#include <linux/input-event-codes.h>
#include <vulkan/vulkan_wayland.h>

#include "xdg-shell.h"
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <cstring>
#include <queue>
#include <stdexcept>

template <>
void std::default_delete<wl_display>::operator()(wl_display* p) const {
    wl_display_disconnect(p);
}

template <>
void std::default_delete<wl_registry>::operator()(wl_registry* p) const {
    wl_registry_destroy(p);
}

template <>
void std::default_delete<wl_compositor>::operator()(wl_compositor* p) const {
    wl_compositor_destroy(p);
}

template <>
void std::default_delete<wl_surface>::operator()(wl_surface* p) const {
    wl_surface_destroy(p);
}

template <>
void std::default_delete<wl_keyboard>::operator()(wl_keyboard* p) const {
    wl_keyboard_destroy(p);
}

template <>
void std::default_delete<wl_pointer>::operator()(wl_pointer* p) const {
    wl_pointer_destroy(p);
}

template <>
void std::default_delete<wl_seat>::operator()(wl_seat* p) const {
    wl_seat_destroy(p);
}

template <>
void std::default_delete<xdg_wm_base>::operator()(xdg_wm_base* p) const {
    xdg_wm_base_destroy(p);
}

template <>
void std::default_delete<xdg_surface>::operator()(xdg_surface* p) const {
    xdg_surface_destroy(p);
}

template <>
void std::default_delete<xdg_toplevel>::operator()(xdg_toplevel* p) const {
    xdg_toplevel_destroy(p);
}

namespace wsi {
namespace wl {
template <typename T>
std::unique_ptr<T> make_unique(T* ptr) {
    if (!ptr) {
        throw std::runtime_error("ptr is nullptr");
    }
    return std::unique_ptr<T>(ptr);
}

template <typename T>
std::unique_ptr<T> registry_bind(wl_registry* registry, uint32_t name, const wl_interface* interface, uint32_t version) {
    auto p = static_cast<T*>(wl_registry_bind(registry, name, interface, version));
    return make_unique<T>(p);
}

namespace display {
using ptr = std::unique_ptr<wl_display>;
} // namespace display

namespace registry {
using ptr = std::unique_ptr<wl_registry>;
static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
static void remove(void* data, wl_registry* registry, uint32_t name);
static constexpr wl_registry_listener listener = {global, remove};
} // namespace registry

namespace compositor {
using ptr = std::unique_ptr<wl_compositor>;
} // namespace compositor

namespace surface {
using ptr = std::unique_ptr<wl_surface>;
} // namespace surface

namespace keyboard {
using ptr = std::unique_ptr<wl_keyboard>;
} // namespace keyboard

namespace pointer {
using ptr = std::unique_ptr<wl_pointer>;
static void enter(void* data, wl_pointer* wl_pointer, uint32_t serial, wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void leave(void* data, wl_pointer* wl_pointer, uint32_t serial, wl_surface* surface);
static void motion(void* data, wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void button(void* data, wl_pointer* wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void axis(void* data, wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
static void frame(void* data, wl_pointer* wl_pointer);
static constexpr wl_pointer_listener listener = {enter, leave, motion, button, axis, frame};

} // namespace pointer

namespace seat {
using ptr = std::unique_ptr<wl_seat>;
static void capabilities(void* data, wl_seat* seat, uint32_t caps);
static void name(void* data, wl_seat* seat, const char* name);
static constexpr wl_seat_listener listener = {capabilities, name};
} // namespace seat

} // namespace wl

namespace xdg {
namespace wm_base {
using ptr = std::unique_ptr<xdg_wm_base>;
static void ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial);
static constexpr xdg_wm_base_listener listener = {ping};
} // namespace wm_base

namespace surface {
using ptr = std::unique_ptr<xdg_surface>;
static void configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial);
static constexpr xdg_surface_listener listener = {configure};
} // namespace surface

namespace toplevel {
using ptr = std::unique_ptr<xdg_toplevel>;
static void configure(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states);
static void close(void* data, struct xdg_toplevel* xdg_toplevel);
static void configure_bounds(void* data, struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height);
static void wm_capabilities(void* data, struct xdg_toplevel* xdg_toplevel, struct wl_array* capabilities);
static constexpr xdg_toplevel_listener listener = {configure, close, configure_bounds, wm_capabilities};
} // namespace toplevel

} // namespace xdg

struct platform {
    wl::display::ptr display;
    wl::registry::ptr registry;
    wl::compositor::ptr compositor;
    wl::surface::ptr surface;
    wl::keyboard::ptr keyboard;
    wl::pointer::ptr pointer;
    wl::seat::ptr seat;

    xdg::wm_base::ptr xdg_wm_base;
    xdg::surface::ptr xdg_surface;
    xdg::toplevel::ptr xdg_toplevel;

    int32_t width;
    int32_t height;

    bool running;

    std::queue<event_type> events;

    platform(int32_t width, int32_t height, const char* name) : width(width), height(height), running(true) {
        display = wl::make_unique(wl_display_connect(NULL));
        registry = wl::make_unique(wl_display_get_registry(display.get()));
        wl_registry_add_listener(registry.get(), &wl::registry::listener, this);
        wl_display_roundtrip(display.get());

        surface = wl::make_unique(wl_compositor_create_surface(compositor.get()));
        xdg_surface = wl::make_unique(xdg_wm_base_get_xdg_surface(xdg_wm_base.get(), surface.get()));
        xdg_surface_add_listener(xdg_surface.get(), &xdg::surface::listener, this);

        xdg_toplevel = wl::make_unique(xdg_surface_get_toplevel(xdg_surface.get()));
        xdg_toplevel_set_title(xdg_toplevel.get(), name);
        xdg_toplevel_add_listener(xdg_toplevel.get(), &xdg::toplevel::listener, this);
        wl_surface_commit(surface.get());
        wl_display_roundtrip(display.get());

        running = true;
    }
};

namespace wl {
namespace registry {
void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto self = static_cast<platform*>(data);
    if (!strcmp(interface, wl_compositor_interface.name)) {
        self->compositor = wl::registry_bind<wl_compositor>(registry, name, &wl_compositor_interface, version);
    }

    if (!strcmp(interface, xdg_wm_base_interface.name)) {
        self->xdg_wm_base = wl::registry_bind<struct xdg_wm_base>(registry, name, &xdg_wm_base_interface, version);
        xdg_wm_base_add_listener(self->xdg_wm_base.get(), &xdg::wm_base::listener, nullptr);
    }

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat = wl::registry_bind<wl_seat>(registry, name, &wl_seat_interface, version);
        wl_seat_add_listener(self->seat.get(), &wl::seat::listener, self);
    }
}

void remove(void* data, struct wl_registry* registry, uint32_t name) {}
} // namespace registry

namespace seat {
void capabilities(void* data, wl_seat* seat, uint32_t caps) {
    auto self = static_cast<platform*>(data);

    bool keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
    // if (keyboard && !self->keyboard) {
    //     self->keyboard = wl::make_unique(wl_seat_get_keyboard(self->seat.get()));
    //     wl_keyboard_add_listener(self->keyboard.get(), &wl::keyboard::listener, self);
    // }

    bool pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (pointer && !self->pointer) {
        self->pointer = wl::make_unique(wl_seat_get_pointer(self->seat.get()));
        wl_pointer_add_listener(self->pointer.get(), &wl::pointer::listener, self);
    }
}

void name(void* data, wl_seat* seat, const char* name) {}

} // namespace seat

namespace pointer {
void enter(void* data, wl_pointer* wl_pointer, uint32_t serial, wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {}

void leave(void* data, wl_pointer* wl_pointer, uint32_t serial, wl_surface* surface) {}

void motion(void* data, wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    auto self = static_cast<platform*>(data);
    const float x = wl_fixed_to_double(surface_x);
    const float y = wl_fixed_to_double(surface_y);
    self->events.push(event::mouse::position{x, y});
}

void button(void* data, wl_pointer* wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    auto self = static_cast<platform*>(data);
    event::mouse::button ev{};
    switch (button) {
    case BTN_LEFT:
        ev.lmb = (bool)state;
        break;
    case BTN_RIGHT:
        ev.rmb = (bool)state;
        break;
    case BTN_MIDDLE:
        ev.mmb = (bool)state;
        break;
    }

    self->events.push(ev);
}

void axis(void* data, wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {}

void frame(void* data, wl_pointer* wl_pointer) {}

} // namespace pointer

} // namespace wl

namespace xdg {
namespace wm_base {
void ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}
} // namespace wm_base
namespace surface {
void configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
}
} // namespace surface

namespace toplevel {
void configure(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states) {
    auto self = static_cast<platform*>(data);

    if (!width && !height) {
        return;
    }

    if (self->width != width || self->height != height) {
        self->width = width;
        self->height = height;
        self->events.push(event::resize{width, height});
        wl_surface_commit(self->surface.get());
    }
}

void close(void* data, struct xdg_toplevel* xdg_toplevel) {
    auto self = static_cast<platform*>(data);
    self->running = false;
}

void configure_bounds(void* data, struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height) {}
void wm_capabilities(void* data, struct xdg_toplevel* xdg_toplevel, struct wl_array* capabilities) {}
} // namespace toplevel
} // namespace xdg

window::window(std::size_t width, std::size_t height, const std::string& name)
    : _impl(std::make_unique<platform>(width, height, name.data())) {}

window::~window() = default;

VkSurfaceKHR window::create_surface(VkInstance instance) const {
    VkWaylandSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    info.surface = _impl->surface.get();
    info.display = _impl->display.get();

    VkSurfaceKHR surf{};
    const auto r = vkCreateWaylandSurfaceKHR(instance, &info, nullptr, &surf);
    if (r != VK_SUCCESS) {
        throw std::runtime_error(fmt::format("surface create error: {}", (int)r));
    }

    return surf;
}

std::vector<const char*> window::required_extensions() {
    return {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };
}

bool window::handle() const {
    wl_display_dispatch_pending(_impl->display.get());
    return _impl->running;
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
    xdg_toplevel_set_title(_impl->xdg_toplevel.get(), name.c_str());
}

} // namespace wsi
