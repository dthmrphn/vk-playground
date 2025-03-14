#include "wsi.hpp"

#include <fmt/core.h>
#include <xcb/xcb.h>

#include <vulkan/vulkan_xcb.h>

namespace wsi {

class xcb final : public window {
    xcb_window_t window;
    xcb_connection_t* connection{nullptr};
    xcb_screen_t* screen{nullptr};
    xcb_intern_atom_reply_t* reply{nullptr};

    int32_t width;
    int32_t height;

  public:
    xcb(int32_t width, int32_t height, std::string_view name) : width(width), height(height) {
        int screenp{0};
        connection = xcb_connect(nullptr, &screenp);

        const auto setup = xcb_get_setup(connection);
        auto iter = xcb_setup_roots_iterator(setup);
        while (screenp-- > 0) {
            xcb_screen_next(&iter);
        }
        screen = iter.data;

        window = xcb_generate_id(connection);

        uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
        uint32_t values[3];
        values[0] = screen->white_pixel;
        values[1] = XCB_EVENT_MASK_KEY_RELEASE |
                    XCB_EVENT_MASK_KEY_PRESS |
                    XCB_EVENT_MASK_EXPOSURE |
                    XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                    XCB_EVENT_MASK_POINTER_MOTION |
                    XCB_EVENT_MASK_BUTTON_PRESS |
                    XCB_EVENT_MASK_BUTTON_RELEASE;

        xcb_create_window(connection,
                          XCB_COPY_FROM_PARENT,
                          window, screen->root,
                          0, 0, width, height, 0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          screen->root_visual,
                          mask, values);

        using namespace std::literals;
        auto reply_tmp = atom_helper(true, "WM_PROTOCOLS"sv);
        reply = atom_helper(false, "WM_DELETE_WINDOW"sv);

        xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
                            window, (*reply_tmp).atom, 4, 32, 1,
                            &(*reply).atom);

        set_title(name);

        free(reply_tmp);

        xcb_map_window(connection, window);
    }

    ~xcb() {
        free(reply);
        xcb_destroy_window(connection, window);
        xcb_disconnect(connection);
    }

    xcb_intern_atom_reply_t* atom_helper(bool only_if_exists, std::string_view sv) {
        const auto cookie = xcb_intern_atom(connection, true, sv.size(), sv.data());
        return xcb_intern_atom_reply(connection, cookie, nullptr);
    }

    void mouse_pos_event(const xcb_motion_notify_event_t* e) {
        float x = e->event_x;
        float y = e->event_y;
        _events.push(event::mouse::position{x, y});
    }

    void mouse_btn_event(const xcb_button_press_event_t* e, bool press) {
        event::mouse::button ev;

        switch (e->detail) {
        case XCB_BUTTON_INDEX_1:
            ev.lmb = press;
            break;
        case XCB_BUTTON_INDEX_2:
            ev.mmb = press;
            break;
        case XCB_BUTTON_INDEX_3:
            ev.rmb = press;
            break;
        }

        _events.push(ev);
    }

    void window_resize(const xcb_configure_notify_event_t* e) {
        if (e->width != width || e->height != height) {
            width = e->width;
            height = e->height;
            _events.push(event::resize{width, height});
        }
    }

    VkSurfaceKHR create_surface(VkInstance instance) const override {
        VkXcbSurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        info.window = window;
        info.connection = connection;

        VkSurfaceKHR surf{};
        const auto r = vkCreateXcbSurfaceKHR(instance, &info, nullptr, &surf);
        if (r != VK_SUCCESS) {
            throw std::runtime_error(fmt::format("surface create error: {}", (int)r));
        }

        return surf;
    }

    void poll() override {
        auto e = xcb_poll_for_event(connection);
        if (e) {
            switch (e->response_type & ~0x80) {
            case XCB_CLIENT_MESSAGE:
                if ((*(xcb_client_message_event_t*)e).data.data32[0] == (*reply).atom) {
                    _events.push(event::exit{});
                }
                break;
            case XCB_MOTION_NOTIFY:
                mouse_pos_event((xcb_motion_notify_event_t*)e);
                break;
            case XCB_BUTTON_PRESS:
                mouse_btn_event((xcb_button_press_event_t*)e, true);
                break;
            case XCB_BUTTON_RELEASE:
                mouse_btn_event((xcb_button_press_event_t*)e, false);
                break;
            case XCB_CONFIGURE_NOTIFY:
                window_resize((xcb_configure_notify_event_t*)e);
                break;
            }

            free(e);
        }
    }

    void set_title(std::string_view name) override {
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
                            window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                            name.size(), name.data());
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
                            window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
                            name.size(), name.data());
    }
};

std::vector<const char*> required_extensions() {
    return {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
    };
}

std::unique_ptr<window> make_window(std::size_t width, std::size_t height, std::string_view name) {
    return std::make_unique<xcb>(width, height, name.data());
}

} // namespace wsi
