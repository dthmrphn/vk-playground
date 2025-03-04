#include "wsi.hpp"

#include <cstdlib>
#include <fmt/format.h>

#include <linux/input-event-codes.h>
#include <vulkan/vulkan_wayland.h>

#include "xdg-shell.h"
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-cursor.h>

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
void std::default_delete<wl_shm>::operator()(wl_shm* p) const {
    wl_shm_destroy(p);
}

template <>
void std::default_delete<wl_cursor_theme>::operator()(wl_cursor_theme* p) const {
    wl_cursor_theme_destroy(p);
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

using wl_display_ptr = std::unique_ptr<wl_display>;
using wl_registry_ptr = std::unique_ptr<wl_registry>;
using wl_compositor_ptr = std::unique_ptr<wl_compositor>;
using wl_surface_ptr = std::unique_ptr<wl_surface>;
using wl_keyboard_ptr = std::unique_ptr<wl_keyboard>;
using wl_pointer_ptr = std::unique_ptr<wl_pointer>;
using wl_seat_ptr = std::unique_ptr<wl_seat>;
using wl_shm_ptr = std::unique_ptr<wl_shm>;
using wl_cursor_theme_ptr = std::unique_ptr<wl_cursor_theme>;

using xdg_wm_base_ptr = std::unique_ptr<xdg_wm_base>;
using xdg_surface_ptr = std::unique_ptr<xdg_surface>;
using xdg_toplevel_ptr = std::unique_ptr<xdg_toplevel>;

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

class wayland final : public window {
    wl_display_ptr display;
    wl_registry_ptr registry;
    wl_compositor_ptr compositor;
    wl_surface_ptr surface;
    wl_keyboard_ptr keyboard;
    wl_pointer_ptr pointer;
    wl_seat_ptr seat;
    wl_shm_ptr shm;
    wl_surface_ptr cursor_surf;
    wl_cursor_theme_ptr cursor_theme;

    xdg_wm_base_ptr xdg_wmbase;
    xdg_surface_ptr xdg_surface;
    xdg_toplevel_ptr xdg_toplevel;

    int32_t width;
    int32_t height;

    static wayland& self(void* data) {
        return *static_cast<wayland*>(data);
    }

  public:
    wayland(int32_t width, int32_t height, std::string_view name) : width(width), height(height) {
        display = make_unique(wl_display_connect(NULL));
        registry = make_unique(wl_display_get_registry(display.get()));
        {
            static constexpr wl_registry_listener listener = {
                registry_global,
                registry_remove,
            };
            wl_registry_add_listener(registry.get(), &listener, this);
        }
        wl_display_roundtrip(display.get());

        surface = make_unique(wl_compositor_create_surface(compositor.get()));
        xdg_surface = make_unique(xdg_wm_base_get_xdg_surface(xdg_wmbase.get(), surface.get()));
        {
            static constexpr xdg_surface_listener listener = {surface_configure};
            xdg_surface_add_listener(xdg_surface.get(), &listener, this);
        }

        xdg_toplevel = make_unique(xdg_surface_get_toplevel(xdg_surface.get()));
        {
            static constexpr xdg_toplevel_listener listener = {
                toplevel_configure,
                toplevel_close,
                toplevel_configure_bounds,
                toplevel_wm_capabilities,
            };
            xdg_toplevel_add_listener(xdg_toplevel.get(), &listener, this);
        }
        wl_surface_commit(surface.get());
        wl_display_roundtrip(display.get());

        int32_t size = 20;

        if (std::getenv("XCURSOR_SIZE")) {
            size = std::stoi(std::getenv("XCURSOR_SIZE"));
        }

        cursor_theme = make_unique(wl_cursor_theme_load(std::getenv("XCURSOR_THEME"), size, shm.get()));
        cursor_surf = make_unique(wl_compositor_create_surface(compositor.get()));

        set_title(name);
    }

    static void registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        if (!strcmp(interface, wl_compositor_interface.name)) {
            self(data).compositor = registry_bind<wl_compositor>(registry, name, &wl_compositor_interface, version);
        }

        if (!strcmp(interface, xdg_wm_base_interface.name)) {
            self(data).xdg_wmbase = registry_bind<xdg_wm_base>(registry, name, &xdg_wm_base_interface, version);
            static constexpr xdg_wm_base_listener listener = {xdg_wm_ping};
            xdg_wm_base_add_listener(self(data).xdg_wmbase.get(), &listener, nullptr);
        }

        if (!strcmp(interface, wl_seat_interface.name)) {
            self(data).seat = registry_bind<wl_seat>(registry, name, &wl_seat_interface, 4);
            static constexpr wl_seat_listener listener = {seat_capabilities, seat_name};
            wl_seat_add_listener(self(data).seat.get(), &listener, data);
        }

        if (!strcmp(interface, wl_shm_interface.name)) {
            self(data).shm = registry_bind<wl_shm>(registry, name, &wl_shm_interface, version);
        }
    }

    static void registry_remove(void* data, wl_registry* registry, uint32_t name) {}

    static void seat_capabilities(void* data, wl_seat* seat, uint32_t caps) {
        bool keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
        // if (keyboard && !self(data).keyboard) {
        //     self(data).keyboard = make_unique(wl_seat_get_keyboard(self(data).seat.get()));
        //     wl_keyboard_add_listener(self(data).keyboard.get(), &keyboard::listener, self);
        // }

        bool pointer = caps & WL_SEAT_CAPABILITY_POINTER;
        if (pointer && !self(data).pointer) {
            self(data).pointer = make_unique(wl_seat_get_pointer(self(data).seat.get()));
            static constexpr wl_pointer_listener listener = {
                pointer_enter,
                pointer_leave,
                pointer_motion,
                pointer_button,
                pointer_axis,
            };

            wl_pointer_add_listener(self(data).pointer.get(), &listener, data);
        }
    }

    static void seat_name(void* data, wl_seat* seat, const char* name) {}

    static void pointer_enter(void* data, wl_pointer* wl_pointer, uint32_t serial, wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
        const auto cursor = wl_cursor_theme_get_cursor(self(data).cursor_theme.get(), "left_ptr");
        if (cursor) {
            const auto img = cursor->images[0];
            const auto buf = wl_cursor_image_get_buffer(img);

            wl_pointer_set_cursor(wl_pointer, serial, self(data).cursor_surf.get(), img->hotspot_x, img->hotspot_y);

            wl_surface_set_buffer_scale(self(data).cursor_surf.get(), 1);
            wl_surface_attach(self(data).cursor_surf.get(), buf, 0, 0);
            wl_surface_damage(self(data).cursor_surf.get(), 0, 0, img->width, img->height);
            wl_surface_commit(self(data).cursor_surf.get());
        }
    }

    static void pointer_leave(void* data, wl_pointer* wl_pointer, uint32_t serial, wl_surface* surface) {}

    static void pointer_motion(void* data, wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
        const float x = wl_fixed_to_double(surface_x);
        const float y = wl_fixed_to_double(surface_y);
        self(data)._events.push(event::mouse::position{x, y});
    }

    static void pointer_button(void* data, wl_pointer* wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
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

        self(data)._events.push(ev);
    }

    static void pointer_axis(void* data, wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {}

    static void capabilities(void* data, wl_seat* seat, uint32_t caps) {}
    static void name(void* data, wl_seat* seat, const char* name) {}

    static void xdg_wm_ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
        xdg_wm_base_pong(xdg_wm_base, serial);
    }

    static void surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
        xdg_surface_ack_configure(xdg_surface, serial);
    }

    static void toplevel_configure(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states) {
        if (!width && !height) {
            return;
        }

        if (self(data).width != width || self(data).height != height) {
            self(data).width = width;
            self(data).height = height;
            self(data)._events.push(event::resize{width, height});
            wl_surface_commit(self(data).surface.get());
        }
    }

    static void toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel) {
        self(data)._events.push(event::exit{});
    }

    static void toplevel_configure_bounds(void* data, struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height) {}
    static void toplevel_wm_capabilities(void* data, struct xdg_toplevel* xdg_toplevel, struct wl_array* capabilities) {}

    VkSurfaceKHR create_surface(VkInstance instance) const override {
        VkWaylandSurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        info.surface = surface.get();
        info.display = display.get();

        VkSurfaceKHR surf{};
        const auto r = vkCreateWaylandSurfaceKHR(instance, &info, nullptr, &surf);
        if (r != VK_SUCCESS) {
            throw std::runtime_error(fmt::format("surface create error: {}", (int)r));
        }

        return surf;
    }

    void poll() override {
        wl_display_dispatch_pending(display.get());
    }

    void set_title(std::string_view name) override {
        xdg_toplevel_set_title(xdg_toplevel.get(), name.data());
        xdg_toplevel_set_app_id(xdg_toplevel.get(), name.data());
    }
};

std::vector<const char*> required_extensions() {
    return {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };
}

std::unique_ptr<window> make_window(std::size_t width, std::size_t height, std::string_view name) {
    return std::make_unique<wayland>(width, height, name.data());
}

} // namespace wsi
